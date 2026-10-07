#include "core/YouTubeEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QStringList searchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {appDir, QDir(appDir).filePath("tools"), QDir(appDir).filePath("ffmpeg")};
}

QString findTool(const QString& name)
{
    const QString found = QStandardPaths::findExecutable(name, searchDirs());
    return found.isEmpty() ? QStandardPaths::findExecutable(name) : found;
}

// The progress template yt-dlp fills in for every progress tick.
const char* kProgressTemplate =
    "download:FLUXPROG %(progress.downloaded_bytes)s %(progress.total_bytes)s "
    "%(progress.total_bytes_estimate)s %(progress.speed)s %(progress.status)s";

QString lastErrorLine(const QByteArray& stderrData)
{
    const QStringList lines = QString::fromUtf8(stderrData).split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it)
        if (it->startsWith(QLatin1String("ERROR:")))
            return it->mid(6).trimmed();
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

// A bare channel URL resolves to its tabs (Videos/Shorts/Live) rather than
// videos, so point it at the Videos tab.
QString normalizeChannelUrl(const QString& url)
{
    static const QRegularExpression channelRoot(
        QStringLiteral(R"(^(https?://(www\.|m\.)?youtube\.com/(@[^/?#]+|channel/[^/?#]+|c/[^/?#]+|user/[^/?#]+))/?$)"));
    const auto m = channelRoot.match(url.trimmed());
    return m.hasMatch() ? m.captured(1) + QStringLiteral("/videos") : url;
}

}  // namespace

QString YouTube::ytDlpPath()
{
    return findTool(QStringLiteral("yt-dlp"));
}

QStringList YouTube::ffmpegArgs()
{
    // Prefer an ffmpeg bundled next to the app; otherwise yt-dlp searches PATH.
    const QString bundled = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"), searchDirs());
    if (!bundled.isEmpty())
        return {QStringLiteral("--ffmpeg-location"), QFileInfo(bundled).absolutePath()};
    return {};
}

QString YouTube::missingMessage()
{
    return QStringLiteral(
        "yt-dlp was not found.\n\nPut yt-dlp%1 next to FluxDownloader%1 (or in a 'tools' folder beside it), "
        "or install it on your PATH (https://github.com/yt-dlp/yt-dlp/releases).")
#ifdef Q_OS_WIN
        .arg(QStringLiteral(".exe"));
#else
        .arg(QString());
#endif
}

// ---- YouTubeProbe -------------------------------------------------------------

YouTubeProbe::YouTubeProbe(QObject* parent) : QObject(parent) {}

YouTubeProbe::~YouTubeProbe()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

void YouTubeProbe::start(const QString& url)
{
    const QString exe = YouTube::ytDlpPath();
    if (exe.isEmpty()) {
        emit failed(YouTube::missingMessage());
        return;
    }
    m_url = url;
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray out = m_proc->readAllStandardOutput();
        const QByteArray err = m_proc->readAllStandardError();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (status != QProcess::NormalExit || code != 0) {
            const QString msg = lastErrorLine(err);
            emit failed(msg.isEmpty() ? QStringLiteral("yt-dlp exited with code %1").arg(code) : msg);
            return;
        }
        QJsonParseError perr{};
        const QJsonObject info = QJsonDocument::fromJson(out, &perr).object();
        if (perr.error != QJsonParseError::NoError) {
            emit failed(QStringLiteral("Could not parse yt-dlp output: %1").arg(perr.errorString()));
            return;
        }

        QVector<YouTubeEntry> entries;
        QString kind = QStringLiteral("youtube_video");
        bool isLivestream = false;
        const QString type = info.value("_type").toString();
        if (type == QLatin1String("playlist") || type == QLatin1String("multi_video") || info.contains("entries")) {
            const bool channel = info.value("webpage_url_basename").toString() == QLatin1String("channel") ||
                                 (!info.value("channel_id").toString().isEmpty() &&
                                  info.value("id").toString() == info.value("channel_id").toString());
            kind = channel ? QStringLiteral("youtube_channel") : QStringLiteral("youtube_playlist");
            for (const QJsonValue& v : info.value("entries").toArray()) {
                const QJsonObject e = v.toObject();
                if (e.isEmpty())
                    continue;
                YouTubeEntry entry;
                entry.id = e.value("id").toString();
                entry.title = e.value("title").toString(entry.id);
                entry.url = e.value("url").toString(e.value("webpage_url").toString());
                if (entry.url.isEmpty())
                    entry.url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(entry.id);
                entry.duration = e.value("duration").toDouble();
                entry.isLive = e.value("live_status").toString() == QLatin1String("is_live");
                entries.push_back(entry);
            }
        } else {
            isLivestream = info.value("is_live").toBool();
            YouTubeEntry entry;
            entry.id = info.value("id").toString();
            entry.title = info.value("title").toString(entry.id);
            entry.url = info.value("webpage_url").toString(m_url);
            entry.duration = info.value("duration").toDouble();
            entry.isLive = isLivestream;
            entries.push_back(entry);
        }
        emit resolved(entries, kind, isLivestream);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit failed(QStringLiteral("Could not start yt-dlp: %1").arg(m_proc->errorString()));
            m_proc->deleteLater();
            m_proc = nullptr;
        }
    });
    m_proc->start(exe, {"-J", "--flat-playlist", "--no-warnings", "--encoding", "utf-8", normalizeChannelUrl(url)});
}

// ---- YouTubeDownloadWorker ---------------------------------------------------------

YouTubeDownloadWorker::YouTubeDownloadWorker(const QString& itemId, const QString& url, const QString& saveDir,
                                             bool audioOnly, const QString& quality, bool subtitles,
                                             bool isLivestream, QObject* parent)
    : QObject(parent),
      m_id(itemId),
      m_url(url),
      m_saveDir(saveDir),
      m_audioOnly(audioOnly),
      m_quality(quality),
      m_subtitles(subtitles),
      m_isLivestream(isLivestream)
{
    m_decisionTimeout.setSingleShot(true);
    m_decisionTimeout.setInterval(300 * 1000);  // 5 minutes, like the original
    connect(&m_decisionTimeout, &QTimer::timeout, this, [this] {
        if (m_awaitingDecision)
            stopLivestream();
    });
}

YouTubeDownloadWorker::~YouTubeDownloadWorker()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString YouTubeDownloadWorker::formatSpec() const
{
    if (m_audioOnly)
        return QStringLiteral("bestaudio/best");
    if (m_quality == QLatin1String("best") || m_quality.isEmpty())
        return QStringLiteral("bestvideo+bestaudio/best");
    return QStringLiteral("bestvideo[height<=%1]+bestaudio/best[height<=%1]").arg(m_quality);
}

QStringList YouTubeDownloadWorker::commonArgs() const
{
    QStringList a{"--newline", "--no-color", "--no-playlist", "--encoding", "utf-8", "--progress",
                  "--progress-template", QString::fromLatin1(kProgressTemplate),
                  "--print", "before_dl:FLUXTITLE %(title)s", "--print", "after_move:FLUXFILE %(filepath)s",
                  "-f", formatSpec()};
    if (m_audioOnly)
        a << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "192K";
    else
        a << "--merge-output-format" << "mp4";  // lossless remux into .mp4
    if (m_subtitles)
        a << "--write-subs" << "--write-auto-subs";
    a << YouTube::ffmpegArgs();
    return a;
}

void YouTubeDownloadWorker::start()
{
    const QString exe = YouTube::ytDlpPath();
    if (exe.isEmpty()) {
        emit errorOccurred(m_id, YouTube::missingMessage());
        return;
    }
    QDir().mkpath(m_saveDir);
    m_running = true;
    if (!m_isLivestream) {
        emit statusChanged(m_id, QStringLiteral("Downloading"));
        runDownload(QDir(m_saveDir).filePath("%(title)s.%(ext)s"), false, [this](bool ok) {
            if (ok)
                finishOk();
        });
        return;
    }
    liveStep();
}

void YouTubeDownloadWorker::cancel()
{
    m_canceled = true;
    m_awaitingDecision = false;
    m_decisionTimeout.stop();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();  // finished handler reports the cancel
    } else if (m_running) {
        m_running = false;
        emit statusChanged(m_id, QStringLiteral("Canceled"));
        emit canceled(m_id);
    }
}

void YouTubeDownloadWorker::continueLivestream()
{
    if (!m_awaitingDecision)
        return;
    m_awaitingDecision = false;
    m_decisionTimeout.stop();
    ++m_segment;
    // Small delay before next segment
    QTimer::singleShot(2000, this, [this] {
        if (!m_canceled)
            liveStep();
    });
}

void YouTubeDownloadWorker::stopLivestream()
{
    if (m_awaitingDecision) {
        m_awaitingDecision = false;
        m_decisionTimeout.stop();
        emit statusChanged(m_id, QStringLiteral("Livestream download stopped by user"));
        finishOk();
    } else {
        cancel();
    }
}

void YouTubeDownloadWorker::handleOutputLine(const QString& line)
{
    if (line.startsWith(QLatin1String("FLUXPROG "))) {
        const QStringList f = line.mid(9).split(' ');
        if (f.size() < 5)
            return;
        const qint64 downloaded = static_cast<qint64>(f[0].toDouble());
        qint64 total = static_cast<qint64>(f[1].toDouble());
        if (total <= 0)
            total = static_cast<qint64>(f[2].toDouble());
        const double speed = f[3].toDouble();
        if (f[4] == QLatin1String("finished"))
            emit statusChanged(m_id, QStringLiteral("Merging/Processing"));
        else
            emit progressUpdated(m_id, downloaded, total, speed);
    } else if (line.startsWith(QLatin1String("FLUXTITLE "))) {
        emit titleResolved(m_id, line.mid(10).trimmed());
    } else if (line.startsWith(QLatin1String("FLUXFILE "))) {
        emit fileResolved(m_id, line.mid(9).trimmed());
    }
}

void YouTubeDownloadWorker::runDownload(const QString& outTemplate, bool liveFromStart,
                                        std::function<void(bool ok)> done)
{
    QStringList args = commonArgs();
    if (liveFromStart)
        args << "--live-from-start" << "--wait-for-video" << "1-5";
    args << "-o" << outTemplate << m_url;

    m_lastError.clear();
    m_stdoutBuf.clear();
    auto* proc = new QProcess(this);
    m_proc = proc;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_stdoutBuf += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_stdoutBuf.left(nl)).trimmed();
            m_stdoutBuf.remove(0, nl + 1);
            handleOutputLine(line);
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        const QString err = lastErrorLine(proc->readAllStandardError());
        if (!err.isEmpty())
            m_lastError = err;
    });
    connect(proc, &QProcess::finished, this, [this, proc, done](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        if (m_canceled) {
            m_running = false;
            emit statusChanged(m_id, QStringLiteral("Canceled"));
            emit canceled(m_id);
            return;
        }
        if (status == QProcess::NormalExit && code == 0) {
            done(true);
            return;
        }
        const QString msg = m_lastError.isEmpty() ? QStringLiteral("yt-dlp exited with code %1").arg(code) : m_lastError;
        if (m_isLivestream) {
            // Check if it's because the stream ended.
            checkLive([this, msg](bool known, bool live) {
                if (known && !live) {
                    emit statusChanged(m_id, QStringLiteral("Livestream ended"));
                    finishOk();
                } else {
                    fail(msg);
                }
            });
            return;
        }
        fail(msg);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            proc->deleteLater();
            fail(QStringLiteral("Could not start yt-dlp: %1").arg(proc->errorString()));
        }
    });
    proc->start(YouTube::ytDlpPath(), args);
}

void YouTubeDownloadWorker::checkLive(std::function<void(bool known, bool isLive)> done)
{
    auto* proc = new QProcess(this);
    m_proc = proc;
    connect(proc, &QProcess::finished, this, [this, proc, done](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        if (m_canceled) {
            m_running = false;
            emit statusChanged(m_id, QStringLiteral("Canceled"));
            emit canceled(m_id);
            return;
        }
        if (status != QProcess::NormalExit || code != 0) {
            done(false, false);
            return;
        }
        const QJsonObject info = QJsonDocument::fromJson(proc->readAllStandardOutput()).object();
        done(!info.isEmpty(), info.value("is_live").toBool());
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            proc->deleteLater();
            done(false, false);
        }
    });
    proc->start(YouTube::ytDlpPath(), {"-J", "--no-playlist", "--no-warnings", "--encoding", "utf-8", m_url});
}

// Download a livestream from the beginning to the current position, then ask
// the user whether to keep recording.
void YouTubeDownloadWorker::liveStep()
{
    emit statusChanged(m_id, QStringLiteral("Downloading livestream (segment %1)").arg(m_segment));
    checkLive([this](bool known, bool live) {
        if (known && !live) {
            if (m_segment == 1) {
                // Stream has ended, download normally
                emit statusChanged(m_id, QStringLiteral("Stream ended - downloading full video"));
                runDownload(QDir(m_saveDir).filePath("%(title)s.%(ext)s"), false, [this](bool ok) {
                    if (ok)
                        finishOk();
                });
            } else {
                emit statusChanged(m_id, QStringLiteral("Livestream ended - download complete"));
                finishOk();
            }
            return;
        }
        const QString tmpl = QDir(m_saveDir).filePath(QStringLiteral("%(title)s_part%1.%(ext)s").arg(m_segment));
        runDownload(tmpl, true, [this](bool) {
            // Check if stream is still live before prompting
            checkLive([this](bool known2, bool live2) {
                if (!known2 || !live2) {
                    emit statusChanged(m_id, QStringLiteral("Livestream ended - download complete"));
                    finishOk();
                    return;
                }
                emit statusChanged(m_id, QStringLiteral("Waiting for user decision..."));
                m_awaitingDecision = true;
                m_decisionTimeout.start();
                emit livestreamPrompt(m_id, QStringLiteral("Downloaded livestream segment %1.\n\nThe stream is still "
                                                           "live.\n\nDo you want to continue downloading?")
                                                .arg(m_segment));
            });
        });
    });
}

void YouTubeDownloadWorker::finishOk()
{
    m_running = false;
    emit statusChanged(m_id, QStringLiteral("Completed"));
    emit finishedOk(m_id);
}

void YouTubeDownloadWorker::fail(const QString& message)
{
    m_running = false;
    emit errorOccurred(m_id, message);
}
