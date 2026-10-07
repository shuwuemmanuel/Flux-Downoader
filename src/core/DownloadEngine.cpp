#include "core/DownloadEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace {

constexpr long kBufferSize = 64 * 1024;
constexpr int kMaxSegmentAttempts = 5;
constexpr int kMaxConnections = 32;
const char* kUserAgent = "FluxDownloader/1.0";

enum SegState { SegConnecting = 0, SegReceiving, SegRetrying, SegDone, SegError, SegStopped };

QString segStateText(int s)
{
    switch (s) {
    case SegConnecting: return QStringLiteral("Connecting...");
    case SegReceiving: return QStringLiteral("Receiving data...");
    case SegRetrying: return QStringLiteral("Reconnecting...");
    case SegDone: return QStringLiteral("Completed");
    case SegError: return QStringLiteral("Error");
    case SegStopped: return QStringLiteral("Stopped");
    }
    return {};
}

// ---- libcurl helpers ---------------------------------------------------

struct CurlHandle {
    CURL* h = curl_easy_init();
    curl_slist* headers = nullptr;
    char errbuf[CURL_ERROR_SIZE] = {};
    ~CurlHandle()
    {
        if (headers)
            curl_slist_free_all(headers);
        if (h)
            curl_easy_cleanup(h);
    }
};

int xferInfoCb(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    // Abort promptly on pause / cancel even when no data is flowing.
    return static_cast<DownloadWorker*>(ud)->shouldStop() ? 1 : 0;
}

void setupCurl(CurlHandle& c, DownloadWorker* worker, const QString& url, const QString& proxy,
               const QString& referrer, const QByteArray& range)
{
    CURL* h = c.h;
    curl_easy_setopt(h, CURLOPT_URL, url.toUtf8().constData());
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);  // ~ a 30 s read timeout
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(h, CURLOPT_BUFFERSIZE, kBufferSize);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, c.errbuf);
    // Every segment gets its own connection; HTTP/2 multiplexing would funnel
    // them all into one TCP stream and defeat segmented downloading.
    curl_easy_setopt(h, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
#ifdef _WIN32
    curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
    if (!proxy.isEmpty())
        curl_easy_setopt(h, CURLOPT_PROXY, proxy.toUtf8().constData());
    if (!referrer.isEmpty())
        curl_easy_setopt(h, CURLOPT_REFERER, referrer.toUtf8().constData());

    // Identity encoding keeps byte-range math and size reporting exact.
    c.headers = curl_slist_append(c.headers, "Accept-Encoding: identity");
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, c.headers);
    if (!range.isEmpty())
        curl_easy_setopt(h, CURLOPT_RANGE, range.constData());

    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferInfoCb);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, worker);
}

QString curlError(const CurlHandle& c, CURLcode code)
{
    const QString detail = QString::fromUtf8(c.errbuf).trimmed();
    return detail.isEmpty() ? QString::fromUtf8(curl_easy_strerror(code)) : detail;
}

// ---- probe ----

struct ProbeCtx {
    QList<QByteArray> headers;
};

size_t probeHeaderCb(char* buf, size_t size, size_t n, void* ud)
{
    auto* ctx = static_cast<ProbeCtx*>(ud);
    const QByteArray line = QByteArray(buf, static_cast<qsizetype>(size * n)).trimmed();
    if (line.startsWith("HTTP/"))
        ctx->headers.clear();  // new response (redirect hop)
    else if (!line.isEmpty())
        ctx->headers << line;
    return size * n;
}

size_t probeWriteCb(char*, size_t, size_t, void*)
{
    return 0;  // headers are all we need; abort as soon as the body starts
}

QByteArray headerValue(const QList<QByteArray>& headers, const QByteArray& name)
{
    for (const QByteArray& h : headers) {
        const int colon = h.indexOf(':');
        if (colon > 0 && h.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return h.mid(colon + 1).trimmed();
    }
    return {};
}

QString parseContentDisposition(const QByteArray& value)
{
    if (value.isEmpty())
        return {};
    static const QRegularExpression extRe(QStringLiteral(R"(filename\*\s*=\s*([^']*)'[^']*'([^;]+))"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression plainRe(QStringLiteral(R"re(filename\s*=\s*"?([^";]+)"?)re"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QString v = QString::fromLatin1(value);
    auto m = extRe.match(v);
    if (m.hasMatch())
        return QUrl::fromPercentEncoding(m.captured(2).trimmed().toLatin1());
    m = plainRe.match(v);
    if (m.hasMatch())
        return QString::fromUtf8(m.captured(1).trimmed().toLatin1());
    return {};
}

// ---- segment transfer ----

struct WriteCtx {
    DownloadWorker* worker = nullptr;
    DownloadWorker::Segment* seg = nullptr;
    QFile* file = nullptr;
    CURL* curl = nullptr;
    bool requireRange = false;
    bool checked = false;
    bool writeFailed = false;
};

size_t segmentWriteCb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    auto* c = static_cast<WriteCtx*>(ud);
    auto* seg = c->seg;
    const qint64 n = static_cast<qint64>(size * nmemb);
    if (c->worker->shouldStop())
        return 0;

    if (!c->checked) {
        c->checked = true;
        long code = 0;
        curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &code);
        if (c->requireRange) {
            if (code != 206) {
                seg->rangeRefused = true;
                seg->error = QStringLiteral("Server did not honor Range request (got %1); "
                                            "cannot safely use multiple connections for this file")
                                 .arg(code);
                return 0;
            }
        } else {
            if (code == 200 && seg->done.load() > 0) {
                // Server ignored our Range header; restart from scratch.
                c->file->resize(0);
                c->file->seek(0);
                seg->done = 0;
            }
            if (c->worker->m_total.load() <= 0) {
                curl_off_t cl = -1;
                curl_easy_getinfo(c->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
                if (cl > 0)
                    c->worker->m_total = static_cast<qint64>(cl) + seg->done.load();
            }
        }
        seg->state = SegReceiving;
    }

    qint64 toWrite = n;
    if (seg->end >= 0) {
        const qint64 remaining = seg->end - seg->start + 1 - seg->done.load();
        toWrite = std::min(toWrite, std::max<qint64>(remaining, 0));
    }
    if (toWrite > 0) {
        if (c->file->write(ptr, toWrite) != toWrite) {
            c->writeFailed = true;
            seg->error = QStringLiteral("Could not write to %1: %2").arg(seg->partPath, c->file->errorString());
            return 0;
        }
        seg->done += toWrite;
        c->worker->throttle(toWrite);
    }
    // Returning less than n aborts the transfer, which is what we want once the
    // segment's range is complete.
    return toWrite == n ? static_cast<size_t>(n) : 0;
}

}  // namespace

// ---- free helpers -----------------------------------------------------------

QString sanitizeFilename(const QString& name)
{
    QString out = QFileInfo(name).fileName();
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    out.replace(bad, QStringLiteral("_"));
    out = out.trimmed();
    while (out.endsWith('.'))
        out.chop(1);
    return out;
}

QString guessFilename(const QString& url)
{
    const QString name = sanitizeFilename(QUrl(url).fileName(QUrl::FullyDecoded));
    return name.isEmpty() ? QStringLiteral("download") : name;
}

QString stateFilePath(const QString& saveDir, const QString& filename)
{
    return QDir(saveDir).filePath(QStringLiteral(".%1.fluxstate").arg(filename));
}

QString partFilePath(const QString& finalPath, int index)
{
    return QStringLiteral("%1.part%2").arg(finalPath).arg(index);
}

void removePartialFiles(const QString& saveDir, const QString& filename)
{
    const QString finalPath = QDir(saveDir).filePath(filename);
    for (int i = 0; i < kMaxConnections; ++i)
        QFile::remove(partFilePath(finalPath, i));
    QFile::remove(stateFilePath(saveDir, filename));
}

QString uniqueFilename(const QString& dir, const QString& name)
{
    const QDir d(dir);
    auto taken = [&](const QString& n) {
        return QFile::exists(d.filePath(n)) || QFile::exists(stateFilePath(dir, n));
    };
    if (!taken(name))
        return name;
    const QFileInfo fi(name);
    QString base = fi.completeBaseName();
    QString ext = fi.suffix();
    // keep double extensions like .tar.gz together
    if (base.endsWith(QLatin1String(".tar"))) {
        base.chop(4);
        ext = QStringLiteral("tar.") + ext;
    }
    for (int i = 1; i < 10000; ++i) {
        const QString candidate =
            ext.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(i) : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(ext);
        if (!taken(candidate))
            return candidate;
    }
    return name;
}

// ---- DownloadWorker -------------------------------------------------------------

DownloadWorker::DownloadWorker(const DownloadItem& item, const Options& opts, SpeedLimiter* globalLimiter,
                               QObject* parent)
    : QThread(parent),
      m_id(item.id),
      m_url(item.url),
      m_filename(item.filename),
      m_saveDir(item.saveDir),
      m_referrer(item.referrer),
      m_checksum(item.checksumExpected.trimmed()),
      m_checksumAlgo(item.checksumAlgo),
      m_opts(opts),
      m_limiter(item.speedLimitKbps),
      m_global(globalLimiter)
{
}

DownloadWorker::~DownloadWorker()
{
    requestStop(StopReason::Cancel);
    wait();
}

void DownloadWorker::requestStop(StopReason reason)
{
    int expected = 0;
    // First reason wins, except that an explicit cancel overrides pause/preempt.
    if (!m_stop.compare_exchange_strong(expected, static_cast<int>(reason)) && reason == StopReason::Cancel)
        m_stop = static_cast<int>(StopReason::Cancel);
}

void DownloadWorker::throttle(qint64 n)
{
    m_limiter.throttle(n);
    m_global->throttle(n);
}

QVector<SegmentSnapshot> DownloadWorker::segments() const
{
    QMutexLocker lock(&m_segMutex);
    QVector<SegmentSnapshot> out;
    out.reserve(static_cast<int>(m_segments.size()));
    for (const auto& s : m_segments)
        out.push_back({s->start, s->end, s->done.load(), segStateText(s->state.load())});
    return out;
}

void DownloadWorker::sleepInterruptible(int ms) const
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms && !shouldStop())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

DownloadWorker::Probe DownloadWorker::probe()
{
    // Some servers/CDNs advertise "Accept-Ranges: bytes" but don't actually
    // honor a Range request, so probe with a real 1-byte ranged GET rather
    // than trusting a HEAD response.
    Probe p;
    CurlHandle c;
    ProbeCtx ctx;
    setupCurl(c, this, m_url, m_opts.proxy, m_referrer, "0-0");
    curl_easy_setopt(c.h, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c.h, CURLOPT_HEADERFUNCTION, probeHeaderCb);
    curl_easy_setopt(c.h, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, probeWriteCb);
    const CURLcode rc = curl_easy_perform(c.h);
    if (rc != CURLE_OK && rc != CURLE_WRITE_ERROR) {
        p.error = curlError(c, rc);
        return p;
    }
    long code = 0;
    curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
    char* eff = nullptr;
    curl_easy_getinfo(c.h, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff)
        p.effectiveUrl = QString::fromUtf8(eff);

    const QByteArray contentRange = headerValue(ctx.headers, "Content-Range");
    if (code == 206 && contentRange.contains('/')) {
        p.total = contentRange.mid(contentRange.lastIndexOf('/') + 1).trimmed().toLongLong();
        p.supportsRange = p.total > 0;
    } else if (code == 200) {
        p.total = headerValue(ctx.headers, "Content-Length").toLongLong();
    }
    p.dispositionName = sanitizeFilename(parseContentDisposition(headerValue(ctx.headers, "Content-Disposition")));
    return p;
}

void DownloadWorker::runSegment(Segment* seg, bool requireRange)
{
    int attempts = 0;
    while (attempts < kMaxSegmentAttempts) {
        if (shouldStop()) {
            seg->state = SegStopped;
            return;
        }
        const qint64 from = seg->start + seg->done.load();
        if (seg->end >= 0 && from > seg->end) {
            seg->state = SegDone;
            seg->error.clear();
            return;
        }

        QFile file(seg->partPath);
        if (!file.open(seg->done.load() > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly)) {
            seg->error = QStringLiteral("Cannot open %1: %2").arg(seg->partPath, file.errorString());
            seg->state = SegError;
            return;
        }

        QByteArray range;
        if (seg->end >= 0)
            range = QByteArray::number(from) + '-' + QByteArray::number(seg->end);
        else if (from > 0)
            range = QByteArray::number(from) + '-';

        CurlHandle c;
        WriteCtx ctx;
        ctx.worker = this;
        ctx.seg = seg;
        ctx.file = &file;
        ctx.curl = c.h;
        ctx.requireRange = requireRange;
        setupCurl(c, this, m_url, m_opts.proxy, m_referrer, range);
        curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, segmentWriteCb);
        curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &ctx);
        seg->state = attempts == 0 ? SegConnecting : SegRetrying;

        const CURLcode rc = curl_easy_perform(c.h);
        file.close();

        if (shouldStop()) {
            seg->state = SegStopped;
            return;
        }
        if (seg->rangeRefused || ctx.writeFailed) {
            seg->state = SegError;
            return;  // not something a retry can fix
        }
        const bool complete = seg->end >= 0 ? seg->done.load() >= seg->end - seg->start + 1
                                            : (rc == CURLE_OK && (m_total.load() <= 0 || seg->done.load() >= m_total.load()));
        if (complete) {
            seg->state = SegDone;
            seg->error.clear();
            return;
        }
        seg->error = rc == CURLE_OK ? QStringLiteral("Connection closed before the download finished") : curlError(c, rc);
        if (rc == CURLE_HTTP_RETURNED_ERROR) {
            long code = 0;
            curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &code);
            if (code >= 400 && code < 500 && code != 408 && code != 429) {
                seg->permanent = true;  // 404, 403, ... - retrying won't help
                seg->state = SegError;
                return;
            }
        }
        if (++attempts >= kMaxSegmentAttempts)
            break;
        seg->state = SegRetrying;
        sleepInterruptible(std::min(1 << attempts, 10) * 1000);
    }
    seg->state = SegError;
}

bool DownloadWorker::transfer(const QString& finalPath, int numConns, bool segmented, QString* error)
{
    // Build segments, picking up any bytes already on disk from a previous run.
    {
        QMutexLocker lock(&m_segMutex);
        m_segments.clear();
        const qint64 total = m_total.load();
        const qint64 segSize = segmented ? total / numConns : 0;
        qint64 start = 0;
        for (int i = 0; i < numConns; ++i) {
            auto s = std::make_unique<Segment>();
            s->index = i;
            s->partPath = partFilePath(finalPath, i);
            if (segmented) {
                s->start = start;
                s->end = (i == numConns - 1) ? total - 1 : start + segSize - 1;
                start = s->end + 1;
            }
            const qint64 existing = QFileInfo(s->partPath).exists() ? QFileInfo(s->partPath).size() : 0;
            const qint64 length = s->end >= 0 ? s->end - s->start + 1 : -1;
            if (length >= 0 && existing > length) {
                QFile::remove(s->partPath);  // corrupt / from another layout
                s->done = 0;
            } else {
                s->done = existing;
            }
            m_segments.push_back(std::move(s));
        }
    }

    std::atomic<int> running{numConns};
    std::vector<std::thread> threads;
    threads.reserve(numConns);
    for (auto& s : m_segments) {
        Segment* seg = s.get();
        threads.emplace_back([this, seg, segmented, &running] {
            runSegment(seg, segmented);
            --running;
        });
    }

    auto downloadedNow = [this] {
        qint64 sum = 0;
        for (const auto& s : m_segments)
            sum += s->done.load();
        return sum;
    };

    QElapsedTimer tick;
    tick.start();
    qint64 last = downloadedNow();
    double speed = 0;
    while (running.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (tick.elapsed() < 500)
            continue;
        const double dt = tick.restart() / 1000.0;
        const qint64 now = downloadedNow();
        const double inst = std::max<qint64>(0, now - last) / dt;
        speed = speed <= 0 ? inst : 0.6 * inst + 0.4 * speed;
        last = now;
        emit progressUpdated(m_id, now, m_total.load(), speed);
    }
    for (auto& t : threads)
        t.join();
    emit progressUpdated(m_id, downloadedNow(), m_total.load(), 0);

    if (shouldStop())
        return false;

    QStringList errors;
    bool rangeRefused = false;
    for (const auto& s : m_segments) {
        if (s->state.load() != SegDone) {
            rangeRefused = rangeRefused || s->rangeRefused;
            m_permanentError = m_permanentError || s->permanent;
            errors << (s->error.isEmpty() ? QStringLiteral("segment %1 incomplete").arg(s->index + 1) : s->error);
        }
    }
    if (!errors.isEmpty()) {
        errors.removeDuplicates();
        *error = (rangeRefused ? QStringLiteral("RANGE:") : QString()) + errors.join(QStringLiteral("; "));
        return false;
    }
    return mergeParts(finalPath, error);
}

bool DownloadWorker::mergeParts(const QString& finalPath, QString* error)
{
    emit statusChanged(m_id, QStringLiteral("Merging"));
    QFile::remove(finalPath);
    const QString first = partFilePath(finalPath, 0);
    if (!QFile::rename(first, finalPath)) {
        *error = QStringLiteral("Could not create %1").arg(finalPath);
        return false;
    }
    const int count = static_cast<int>(m_segments.size());
    if (count <= 1)
        return true;

    QFile out(finalPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
        *error = QStringLiteral("Could not open %1: %2").arg(finalPath, out.errorString());
        return false;
    }
    QByteArray buf;
    for (int i = 1; i < count; ++i) {
        QFile part(partFilePath(finalPath, i));
        if (!part.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Missing segment file %1").arg(part.fileName());
            return false;
        }
        while (!part.atEnd()) {
            buf = part.read(4 * 1024 * 1024);
            if (out.write(buf) != buf.size()) {
                *error = QStringLiteral("Write failed: %1").arg(out.errorString());
                return false;
            }
        }
        part.close();
        part.remove();
    }
    return true;
}

bool DownloadWorker::verifyChecksum(const QString& path, QString* error) const
{
    if (m_checksum.isEmpty())
        return true;

    QString algo = m_checksumAlgo.toLower();
    QString expected = m_checksum.toLower();
    const int colon = expected.indexOf(':');
    if (colon > 0) {  // "sha256:abcd..." form
        algo = expected.left(colon);
        expected = expected.mid(colon + 1).trimmed();
    }
    if (algo.isEmpty()) {
        switch (expected.size()) {
        case 32: algo = "md5"; break;
        case 40: algo = "sha1"; break;
        case 128: algo = "sha512"; break;
        default: algo = "sha256"; break;
        }
    }
    QCryptographicHash::Algorithm a = QCryptographicHash::Sha256;
    if (algo == "md5")
        a = QCryptographicHash::Md5;
    else if (algo == "sha1")
        a = QCryptographicHash::Sha1;
    else if (algo == "sha512")
        a = QCryptographicHash::Sha512;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    QCryptographicHash h(a);
    if (!h.addData(&f)) {
        *error = QStringLiteral("Could not read %1 for checksum").arg(path);
        return false;
    }
    const QString digest = QString::fromLatin1(h.result().toHex());
    if (digest != expected) {
        *error = QStringLiteral("Checksum mismatch (expected %1, got %2)").arg(m_checksum, digest);
        return false;
    }
    return true;
}

void DownloadWorker::run()
{
    QDir().mkpath(m_saveDir);
    emit statusChanged(m_id, Status::Connecting);

    const Probe p = probe();
    if (shouldStop()) {
        emit stopped(m_id);
        return;
    }

    // Let the server name the file when we only had a guess from the URL
    // (e.g. ".../download?id=42"), as long as nothing was written yet.
    if (m_opts.filenameIsGuess && !QFile::exists(stateFilePath(m_saveDir, m_filename))) {
        QString better = p.dispositionName;
        if (better.isEmpty() && !m_filename.contains('.') && !p.effectiveUrl.isEmpty())
            better = guessFilename(p.effectiveUrl);
        if (!better.isEmpty() && better != QLatin1String("download") && better != m_filename) {
            m_filename = uniqueFilename(m_saveDir, better);
            emit filenameResolved(m_id, m_filename);
        }
    }

    const QString finalPath = QDir(m_saveDir).filePath(m_filename);
    const qint64 total = p.total;
    const bool supportsRange = p.supportsRange;
    m_total = total;

    int numConns = 1;
    if (supportsRange) {
        int adaptiveCap;
        if (total < 2LL * 1024 * 1024)
            adaptiveCap = 2;
        else if (total < 20LL * 1024 * 1024)
            adaptiveCap = 4;
        else if (total < 100LL * 1024 * 1024)
            adaptiveCap = 8;
        else if (total < 500LL * 1024 * 1024)
            adaptiveCap = 16;
        else
            adaptiveCap = 32;
        numConns = std::max(1, std::min(m_opts.maxConnections, adaptiveCap));
    }
    // Keep segments at least 256 KB so tiny files aren't wastefully over-split.
    if (supportsRange)
        numConns = static_cast<int>(std::min<qint64>(numConns, total / (256 * 1024)));
    numConns = std::clamp(numConns, 1, kMaxConnections);

    // Resume: reuse the segment layout from the previous run so the part files
    // on disk still line up with their byte ranges.
    const QString statePath = stateFilePath(m_saveDir, m_filename);
    {
        QFile sf(statePath);
        if (sf.open(QIODevice::ReadOnly)) {
            const QJsonObject st = QJsonDocument::fromJson(sf.readAll()).object();
            sf.close();
            const qint64 prevTotal = st.value("total_size").toInteger();
            const int prevConns = st.value("num_conns").toInt(1);
            if (supportsRange && prevTotal == total && prevConns >= 1 && prevConns <= kMaxConnections) {
                numConns = prevConns;
            } else if (prevTotal != total || prevConns != numConns) {
                removePartialFiles(m_saveDir, m_filename);  // file changed on the server
            }
        }
    }
    auto saveState = [&](int conns) {
        QFile sf(statePath);
        if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            sf.write(QJsonDocument(QJsonObject{{"total_size", total}, {"num_conns", conns}, {"url", m_url}})
                         .toJson(QJsonDocument::Compact));
    };
    saveState(numConns);

    emit infoResolved(m_id, total, numConns, supportsRange);
    emit statusChanged(m_id, Status::Downloading);

    QString err;
    bool ok;
    if (supportsRange && numConns > 1) {
        ok = transfer(finalPath, numConns, true, &err);
        if (!ok && !shouldStop() && err.startsWith(QLatin1String("RANGE:"))) {
            // Server advertised range support but didn't honor it mid-stream;
            // clean up partial segments and fall back to a single connection.
            removePartialFiles(m_saveDir, m_filename);
            saveState(1);
            emit infoResolved(m_id, total, 1, false);
            err.clear();
            ok = transfer(finalPath, 1, false, &err);
        }
    } else {
        ok = transfer(finalPath, 1, false, &err);
    }

    if (shouldStop()) {
        emit stopped(m_id);
        return;
    }
    if (!ok) {
        if (err.startsWith(QLatin1String("RANGE:")))
            err = err.mid(6);
        emit errorOccurred(m_id, err.isEmpty() ? (p.error.isEmpty() ? QStringLiteral("Download failed") : p.error) : err,
                           !m_permanentError);
        return;
    }

    QFile::remove(statePath);
    if (!m_checksum.isEmpty()) {
        emit statusChanged(m_id, QStringLiteral("Verifying"));
        QString cerr;
        if (!verifyChecksum(finalPath, &cerr)) {
            emit errorOccurred(m_id, cerr, false);
            return;
        }
    }
    emit finishedOk(m_id);
}
