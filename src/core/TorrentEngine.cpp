#include "core/TorrentEngine.h"

#include "core/DownloadItem.h"
#include "core/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <functional>
#include <stdexcept>

#ifdef FLUX_HAVE_LIBTORRENT
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>
#endif

const QStringList& torrentQueuePriorities()
{
    static const QStringList kLevels = {"Highest", "High", "Normal", "Low", "Lowest"};
    return kLevels;
}

#ifdef FLUX_HAVE_LIBTORRENT

namespace {
QString torrentStoreDir()
{
    const QString dir = QDir(SettingsManager::appDir()).filePath("torrents");
    QDir().mkpath(dir);
    return dir;
}
}  // namespace

namespace lt = libtorrent;

struct TorrentWrapper {
    QString id;
    lt::torrent_handle handle;
    QString savePath;
    QString name;
    QString source;  // magnet URI, or our private copy of the .torrent file
    QVector<TorrentFileInfo> files;
    double addedTime = nowSeconds();
    QString queuePriority = QStringLiteral("Normal");
    bool metadataEmitted = false;
    bool wasFinished = false;
    bool firstPoll = true;
    bool userPaused = false;
    QVector<int> pendingFilePriorities;  // restored priorities waiting for metadata
};

struct TorrentStore {
    std::unique_ptr<lt::session> session;
    QHash<QString, TorrentWrapper> torrents;
    QStringList order;  // insertion order, for persistence

    TorrentWrapper* byHandle(const lt::torrent_handle& h)
    {
        for (auto it = torrents.begin(); it != torrents.end(); ++it)
            if (it->handle == h)
                return &it.value();
        return nullptr;
    }
};

struct TorrentEngine::Impl : TorrentStore {};

namespace {

int kbpsToBps(int kbps)
{
    return kbps > 0 ? kbps * 1024 : 0;
}

lt::settings_pack makePack(SettingsManager* s)
{
    lt::settings_pack pack;
    const int port = s->getInt("torrent_port") > 0 ? s->getInt("torrent_port") : 6881;
    pack.set_str(lt::settings_pack::user_agent, std::string("FluxDownloader/1.0 libtorrent/") + lt::version());
    pack.set_str(lt::settings_pack::listen_interfaces,
                 QStringLiteral("0.0.0.0:%1,[::]:%1").arg(port).toStdString());
    pack.set_bool(lt::settings_pack::enable_dht, s->getBool("torrent_dht_enabled"));
    pack.set_bool(lt::settings_pack::enable_lsd, true);
    pack.set_bool(lt::settings_pack::enable_upnp, true);
    pack.set_bool(lt::settings_pack::enable_natpmp, true);
    pack.set_int(lt::settings_pack::download_rate_limit, kbpsToBps(s->getInt("torrent_download_limit_kbps")));
    pack.set_int(lt::settings_pack::upload_rate_limit, kbpsToBps(s->getInt("torrent_upload_limit_kbps")));
    pack.set_int(lt::settings_pack::active_downloads, 8);
    pack.set_int(lt::settings_pack::active_seeds, 8);
    pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::storage);
    return pack;
}

// load_torrent_file() exists in both libtorrent 2.0 and 2.1 (2.1 dropped the
// torrent_info(path, error_code) constructor).
lt::add_torrent_params loadTorrentFile(const QString& path)
{
    try {
        return lt::load_torrent_file(path.toStdString());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Could not read .torrent file: ") + e.what());
    }
}

QVector<TorrentFileInfo> extractFiles(const lt::torrent_handle& h, const lt::torrent_info& info)
{
    QVector<TorrentFileInfo> files;
    // libtorrent 2.1 replaced files() (now deprecated) with layout().
#if LIBTORRENT_VERSION_MAJOR > 2 || (LIBTORRENT_VERSION_MAJOR == 2 && LIBTORRENT_VERSION_MINOR >= 1)
    const lt::file_storage& fs = info.layout();
#else
    const lt::file_storage& fs = info.files();
#endif
    std::vector<lt::download_priority_t> prios;
    if (h.is_valid())
        prios = h.get_file_priorities();
    for (int i = 0; i < fs.num_files(); ++i) {
        TorrentFileInfo f;
        f.index = i;
        f.path = QString::fromStdString(fs.file_path(lt::file_index_t(i)));
        f.size = fs.file_size(lt::file_index_t(i));
        f.priority = i < static_cast<int>(prios.size()) ? static_cast<int>(static_cast<std::uint8_t>(prios[i]))
                                                       : TorrentPriority::Normal;
        files.push_back(f);
    }
    return files;
}

QString stateName(lt::torrent_status::state_t s)
{
    switch (s) {
    case lt::torrent_status::checking_files: return QStringLiteral("Checking files");
    case lt::torrent_status::downloading_metadata: return QStringLiteral("Fetching metadata");
    case lt::torrent_status::downloading: return QStringLiteral("Downloading");
    case lt::torrent_status::finished: return QStringLiteral("Finished");
    case lt::torrent_status::seeding: return QStringLiteral("Seeding");
    case lt::torrent_status::checking_resume_data: return QStringLiteral("Checking resume data");
    default: return QStringLiteral("Queued (checking)");
    }
}

}  // namespace

TorrentEngine::TorrentEngine(SettingsManager* settings, QObject* parent)
    : QObject(parent), m_settings(settings), d(std::make_unique<Impl>())
{
    try {
        d->session = std::make_unique<lt::session>(lt::session_params(makePack(settings)));
    } catch (const std::exception&) {
        d->session.reset();
        return;
    }
    connect(&m_pollTimer, &QTimer::timeout, this, &TorrentEngine::poll);
    m_pollTimer.start(1000);
}

TorrentEngine::~TorrentEngine()
{
    shutdown();
}

bool TorrentEngine::available()
{
    return true;
}

void TorrentEngine::applySettings()
{
    if (d->session)
        d->session->apply_settings(makePack(m_settings));
}

static QString addParams(TorrentStore* d, lt::add_torrent_params params,
                         const QString& savePath, const QString& source, const QString& forcedId,
                         std::function<void(TorrentWrapper&)> beforeEmit)
{
    QDir().mkpath(savePath);
    params.save_path = savePath.toStdString();
    params.storage_mode = lt::storage_mode_sparse;
    lt::error_code ec;
    lt::torrent_handle h = d->session->add_torrent(std::move(params), ec);
    if (ec)
        throw std::runtime_error(ec.message());
    if (d->byHandle(h))
        throw std::runtime_error("This torrent is already in the list.");

    TorrentWrapper w;
    w.id = forcedId.isEmpty() ? newId() : forcedId;
    w.handle = h;
    w.savePath = savePath;
    w.source = source;
    const lt::torrent_status st = h.status(lt::torrent_handle::query_name);
    w.name = st.name.empty() ? QStringLiteral("Fetching metadata…") : QString::fromStdString(st.name);
    if (beforeEmit)
        beforeEmit(w);
    d->torrents.insert(w.id, w);
    d->order << w.id;
    return w.id;
}

QString TorrentEngine::addMagnet(const QString& uri, const QString& savePath)
{
    if (!d->session)
        throw std::runtime_error("Torrent engine unavailable.");
    lt::error_code ec;
    lt::add_torrent_params params = lt::parse_magnet_uri(uri.trimmed().toStdString(), ec);
    if (ec)
        throw std::runtime_error("Invalid magnet link: " + ec.message());
    const QString tid = addParams(d.get(), std::move(params), savePath, uri.trimmed(), {}, nullptr);
    emit torrentAdded(tid, d->torrents[tid].name);
    persist();
    return tid;
}

QString TorrentEngine::addTorrentFile(const QString& torrentPath, const QString& savePath)
{
    if (!d->session)
        throw std::runtime_error("Torrent engine unavailable.");
    lt::add_torrent_params params = loadTorrentFile(torrentPath);
    const auto info = params.ti;  // shared_ptr<const torrent_info> on 2.1

    // Keep a private copy so the torrent can be restored after a restart even
    // if the original file is moved or deleted.
    const QString tid = newId();
    const QString copy = QDir(torrentStoreDir()).filePath(tid + ".torrent");
    QFile::copy(torrentPath, copy);

    try {
        addParams(d.get(), std::move(params), savePath, copy, tid, [&](TorrentWrapper& w) {
            w.name = QString::fromStdString(info->name());
            w.files = extractFiles(w.handle, *info);
            w.metadataEmitted = true;
        });
    } catch (...) {
        QFile::remove(copy);
        throw;
    }
    emit torrentAdded(tid, d->torrents[tid].name);
    emit metadataReady(tid, d->torrents[tid].files);
    persist();
    return tid;
}

void TorrentEngine::restoreSession()
{
    if (!d->session)
        return;
    QFile f(SettingsManager::torrentsPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        const QString source = o.value("source").toString();
        const QString savePath = o.value("save_path").toString();
        if (id.isEmpty() || source.isEmpty())
            continue;
        QVector<int> prios;
        for (const QJsonValue& p : o.value("file_priorities").toArray())
            prios << p.toInt(TorrentPriority::Normal);
        try {
            lt::error_code ec;
            lt::add_torrent_params params;
            if (source.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
                params = lt::parse_magnet_uri(source.toStdString(), ec);
            else
                params = loadTorrentFile(source);  // throws -> skipped below
            if (ec)
                continue;
            addParams(d.get(), std::move(params), savePath, source, id, [&](TorrentWrapper& w) {
                const QString savedName = o.value("name").toString();
                if (!savedName.isEmpty())
                    w.name = savedName;
                w.queuePriority = o.value("queue_priority").toString("Normal");
                w.addedTime = o.value("added_time").toDouble(w.addedTime);
                w.pendingFilePriorities = prios;
                if (o.value("paused").toBool()) {
                    w.userPaused = true;
                    w.handle.unset_flags(lt::torrent_flags::auto_managed);
                    w.handle.pause();
                }
            });
        } catch (const std::exception&) {
            continue;
        }
        emit torrentAdded(id, d->torrents[id].name);
        if (d->torrents[id].userPaused)
            emit statusChanged(id, QStringLiteral("Paused"));
    }
}

void TorrentEngine::pause(const QString& tid)
{
    auto it = d->torrents.find(tid);
    if (it == d->torrents.end())
        return;
    // Take it out of the automatic queue, otherwise libtorrent would resume it.
    it->handle.unset_flags(lt::torrent_flags::auto_managed);
    it->handle.pause();
    it->userPaused = true;
    emit statusChanged(tid, QStringLiteral("Paused"));
    persist();
}

void TorrentEngine::resume(const QString& tid)
{
    auto it = d->torrents.find(tid);
    if (it == d->torrents.end())
        return;
    it->handle.set_flags(lt::torrent_flags::auto_managed);
    it->handle.resume();
    it->userPaused = false;
    emit statusChanged(tid, QStringLiteral("Downloading"));
    persist();
}

void TorrentEngine::remove(const QString& tid, bool deleteFiles)
{
    auto it = d->torrents.find(tid);
    if (it == d->torrents.end())
        return;
    if (d->session)
        d->session->remove_torrent(it->handle, deleteFiles ? lt::session::delete_files : lt::remove_flags_t{});
    if (it->source.startsWith(torrentStoreDir()))
        QFile::remove(it->source);
    d->torrents.erase(it);
    d->order.removeAll(tid);
    persist();
    emit torrentRemoved(tid);
}

void TorrentEngine::setFilePriority(const QString& tid, int fileIndex, int priority)
{
    auto it = d->torrents.find(tid);
    if (it == d->torrents.end())
        return;
    it->handle.file_priority(lt::file_index_t(fileIndex), lt::download_priority_t(static_cast<std::uint8_t>(priority)));
    for (TorrentFileInfo& f : it->files)
        if (f.index == fileIndex)
            f.priority = priority;
    persist();
}

void TorrentEngine::setQueuePriority(const QString& tid, const QString& level)
{
    auto it = d->torrents.find(tid);
    if (it == d->torrents.end())
        return;
    it->queuePriority = level;
    lt::torrent_handle& h = it->handle;
    if (level == QLatin1String("Highest"))
        h.queue_position_top();
    else if (level == QLatin1String("Lowest"))
        h.queue_position_bottom();
    else if (level == QLatin1String("High"))
        h.queue_position_up();
    else if (level == QLatin1String("Low"))
        h.queue_position_down();
    // "Normal" leaves queue position as-is
    persist();
}

bool TorrentEngine::contains(const QString& tid) const
{
    return d->torrents.contains(tid);
}

QString TorrentEngine::savePath(const QString& tid) const
{
    return d->torrents.value(tid).savePath;
}

QString TorrentEngine::name(const QString& tid) const
{
    return d->torrents.value(tid).name;
}

QStringList TorrentEngine::ids() const
{
    return d->order;
}

void TorrentEngine::poll()
{
    if (!d->session)
        return;

    std::vector<lt::alert*> alerts;
    d->session->pop_alerts(&alerts);
    for (lt::alert* a : alerts) {
        lt::torrent_handle h;
        if (auto* te = lt::alert_cast<lt::torrent_error_alert>(a))
            h = te->handle;
        else if (auto* fe = lt::alert_cast<lt::file_error_alert>(a))
            h = fe->handle;
        else
            continue;
        if (TorrentWrapper* w = d->byHandle(h))
            emit torrentError(w->id, QString::fromStdString(a->message()));
    }

    bool changed = false;
    const QStringList ids = d->order;
    for (const QString& tid : ids) {
        auto it = d->torrents.find(tid);
        if (it == d->torrents.end())
            continue;
        TorrentWrapper& w = it.value();
        if (!w.handle.is_valid())
            continue;
        const lt::torrent_status st = w.handle.status();

        if (!w.metadataEmitted && st.has_metadata) {
            if (auto info = w.handle.torrent_file()) {
                w.name = QString::fromStdString(info->name());
                w.metadataEmitted = true;
                for (int i = 0; i < w.pendingFilePriorities.size(); ++i)
                    w.handle.file_priority(lt::file_index_t(i),
                                           lt::download_priority_t(static_cast<std::uint8_t>(w.pendingFilePriorities[i])));
                w.pendingFilePriorities.clear();
                w.files = extractFiles(w.handle, *info);
                changed = true;
                emit metadataReady(tid, w.files);
            }
        } else if (w.metadataEmitted && !w.pendingFilePriorities.isEmpty()) {
            for (int i = 0; i < w.pendingFilePriorities.size(); ++i)
                w.handle.file_priority(lt::file_index_t(i),
                                       lt::download_priority_t(static_cast<std::uint8_t>(w.pendingFilePriorities[i])));
            w.pendingFilePriorities.clear();
        }

        QString state = stateName(st.state);
        const bool paused = static_cast<bool>(st.flags & lt::torrent_flags::paused);
        if (w.userPaused)
            state = QStringLiteral("Paused");
        else if (paused)
            state = QStringLiteral("Queued");
        if (!st.errc.message().empty() && st.errc)
            state = QStringLiteral("Error: ") + QString::fromStdString(st.errc.message());

        TorrentStatus s;
        s.progress = st.progress * 100.0;
        s.downloadRate = st.download_rate;
        s.uploadRate = st.upload_rate;
        s.numPeers = st.num_peers;
        s.numSeeds = st.num_seeds;
        s.totalWanted = st.total_wanted;
        s.totalWantedDone = st.total_wanted_done;
        s.state = state;
        s.name = w.name;
        emit progressUpdated(tid, s);

        const bool done = st.has_metadata && st.progress >= 1.0f;
        if (done && !w.wasFinished) {
            w.wasFinished = true;
            // Torrents restored already-complete shouldn't notify again.
            if (!w.firstPoll)
                emit torrentFinished(tid);
        }
        w.firstPoll = false;
    }
    if (changed)
        persist();
}

void TorrentEngine::persist() const
{
    QJsonArray arr;
    for (const QString& tid : d->order) {
        const TorrentWrapper& w = d->torrents[tid];
        QJsonArray prios;
        for (const TorrentFileInfo& f : w.files)
            prios << f.priority;
        if (w.files.isEmpty())
            for (int p : w.pendingFilePriorities)
                prios << p;
        arr << QJsonObject{{"id", w.id},
                           {"source", w.source},
                           {"save_path", w.savePath},
                           {"name", w.name},
                           {"queue_priority", w.queuePriority},
                           {"paused", w.userPaused},
                           {"added_time", w.addedTime},
                           {"file_priorities", prios}};
    }
    QSaveFile f(SettingsManager::torrentsPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void TorrentEngine::shutdown()
{
    m_pollTimer.stop();
    if (d && d->session) {
        persist();
        d->session.reset();
    }
}

#else  // !FLUX_HAVE_LIBTORRENT --------------------------------------------------------

struct TorrentEngine::Impl {};

TorrentEngine::TorrentEngine(SettingsManager* settings, QObject* parent)
    : QObject(parent), m_settings(settings), d(std::make_unique<Impl>())
{
}
TorrentEngine::~TorrentEngine() = default;
bool TorrentEngine::available() { return false; }
void TorrentEngine::applySettings() {}
QString TorrentEngine::addMagnet(const QString&, const QString&)
{
    throw std::runtime_error("This build of Flux Downloader has no BitTorrent support (libtorrent not found).");
}
QString TorrentEngine::addTorrentFile(const QString&, const QString&)
{
    throw std::runtime_error("This build of Flux Downloader has no BitTorrent support (libtorrent not found).");
}
void TorrentEngine::restoreSession() {}
void TorrentEngine::pause(const QString&) {}
void TorrentEngine::resume(const QString&) {}
void TorrentEngine::remove(const QString&, bool) {}
void TorrentEngine::setFilePriority(const QString&, int, int) {}
void TorrentEngine::setQueuePriority(const QString&, const QString&) {}
bool TorrentEngine::contains(const QString&) const { return false; }
QString TorrentEngine::savePath(const QString&) const { return {}; }
QString TorrentEngine::name(const QString&) const { return {}; }
QStringList TorrentEngine::ids() const { return {}; }
void TorrentEngine::poll() {}
void TorrentEngine::persist() const {}
void TorrentEngine::shutdown() {}

#endif
