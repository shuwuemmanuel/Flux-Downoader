#include "core/QueueManager.h"

#include "core/SettingsManager.h"
#include "core/YouTubeEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

QueueManager::QueueManager(SettingsManager* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_globalLimiter(settings->getInt("global_speed_limit_kbps"))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    connect(settings, &SettingsManager::changed, this, [this](const QString& key) {
        if (key == QLatin1String("max_concurrent_downloads"))
            maybeStartNext();  // a raised limit starts queued downloads right away
        else if (key == QLatin1String("global_speed_limit_kbps"))
            m_globalLimiter.setLimit(m_settings->getInt("global_speed_limit_kbps"));
    });
}

QueueManager::~QueueManager()
{
    shutdown();
}

bool QueueManager::isYouTube(const DownloadItem& item) const
{
    return item.kind.startsWith(QLatin1String("youtube"));
}

// ---- persistence -------------------------------------------------------------

void QueueManager::loadSaved()
{
    QFile f(SettingsManager::queuePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        DownloadItem it = DownloadItem::fromJson(v.toObject());
        if (it.url.isEmpty() || m_items.contains(it.id))
            continue;
        // Anything that was running or waiting picks up where it left off.
        if (it.status != Status::Completed && it.status != Status::Paused && it.status != Status::Error &&
            it.status != Status::Canceled)
            it.status = Status::Queued;
        it.speedBps = 0;
        m_items.insert(it.id, it);
        m_order << it.id;
        emit itemAdded(it.id);
    }
    maybeStartNext();
}

void QueueManager::scheduleSave()
{
    if (!m_shuttingDown)
        m_saveTimer.start();
}

void QueueManager::saveNow() const
{
    QJsonArray arr;
    for (const QString& id : m_order)
        arr << m_items[id].toJson();
    QSaveFile f(SettingsManager::queuePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void QueueManager::shutdown()
{
    if (m_shuttingDown)
        return;
    saveNow();  // statuses as they are now, so running downloads resume next launch
    m_shuttingDown = true;
    m_saveTimer.stop();
    for (auto& w : m_httpWorkers) {
        if (w) {
            w->disconnect(this);
            w->requestStop(DownloadWorker::StopReason::Pause);
        }
    }
    for (auto& w : m_httpWorkers) {
        if (w) {
            w->wait();
            delete w.data();
        }
    }
    m_httpWorkers.clear();
    for (auto& w : m_ytWorkers) {
        if (w) {
            w->disconnect(this);
            delete w.data();
        }
    }
    m_ytWorkers.clear();
}

// ---- adding / lookup ---------------------------------------------------------------

QString QueueManager::add(DownloadItem item, bool autoStart)
{
    if (!autoStart && item.status == Status::Queued)
        item.status = Status::Paused;
    const QString id = item.id;
    m_items.insert(id, item);
    m_order << id;
    emit itemAdded(id);
    scheduleSave();
    if (autoStart)
        maybeStartNext();
    return id;
}

const DownloadItem* QueueManager::item(const QString& id) const
{
    auto it = m_items.constFind(id);
    return it == m_items.cend() ? nullptr : &it.value();
}

bool QueueManager::isRunning(const QString& id) const
{
    return (m_httpWorkers.contains(id) && m_httpWorkers.value(id)) ||
           (m_ytWorkers.contains(id) && m_ytWorkers.value(id));
}

int QueueManager::activeCount() const
{
    int n = 0;
    for (const auto& w : m_httpWorkers)
        if (w)
            ++n;
    for (const auto& w : m_ytWorkers)
        if (w)
            ++n;
    return n;
}

QVector<SegmentSnapshot> QueueManager::segments(const QString& id) const
{
    const auto w = m_httpWorkers.value(id);
    return w ? w->segments() : QVector<SegmentSnapshot>{};
}

// ---- scheduling ---------------------------------------------------------------------

void QueueManager::maybeStartNext()
{
    if (m_shuttingDown)
        return;
    const int maxConc = std::max(1, m_settings->getInt("max_concurrent_downloads"));
    const double now = nowSeconds();

    QList<DownloadItem*> queued;
    for (const QString& id : m_order) {
        DownloadItem& it = m_items[id];
        if (it.status == Status::Queued && !isRunning(id) && m_retryAt.value(id, 0) <= now)
            queued << &it;
    }
    std::stable_sort(queued.begin(), queued.end(), [](const DownloadItem* a, const DownloadItem* b) {
        const int pa = Priority::order(a->priority), pb = Priority::order(b->priority);
        return pa != pb ? pa < pb : a->addedTime < b->addedTime;
    });

    while (!queued.isEmpty() && activeCount() < maxConc) {
        DownloadItem* it = queued.takeFirst();
        if (isYouTube(*it))
            startYouTube(*it);
        else
            startHttp(*it);
    }

    // Preemption: if a queued item outranks the lowest-priority active HTTP
    // download, stop that one (progress is preserved on disk via partial
    // segment files) and re-queue it, freeing a slot for the higher-priority
    // item - mirrors FDM's priority scheduling.
    if (queued.isEmpty() || activeCount() < maxConc)
        return;
    for (const auto& w : m_httpWorkers)
        if (w && w->stopReason() == DownloadWorker::StopReason::Preempt)
            return;  // one preemption at a time
    DownloadWorker* victim = nullptr;
    const DownloadItem* victimItem = nullptr;
    for (auto it = m_httpWorkers.begin(); it != m_httpWorkers.end(); ++it) {
        if (!it.value() || it.value()->stopReason() != DownloadWorker::StopReason::None)
            continue;
        const DownloadItem& cand = m_items[it.key()];
        if (!victimItem || Priority::order(cand.priority) > Priority::order(victimItem->priority) ||
            (Priority::order(cand.priority) == Priority::order(victimItem->priority) &&
             cand.addedTime > victimItem->addedTime)) {
            victim = it.value();
            victimItem = &cand;
        }
    }
    if (victim && Priority::order(queued.first()->priority) < Priority::order(victimItem->priority))
        victim->requestStop(DownloadWorker::StopReason::Preempt);
}

void QueueManager::startHttp(DownloadItem& item)
{
    DownloadWorker::Options opts;
    opts.maxConnections = std::clamp(m_settings->getInt("max_connections_per_file"), 1, 32);
    opts.proxy = m_settings->getString("proxy");
    opts.filenameIsGuess = item.autoName;

    item.errorMessage.clear();
    auto* w = new DownloadWorker(item, opts, &m_globalLimiter, this);
    const QString id = item.id;
    m_httpWorkers.insert(id, w);
    m_retryAt.remove(id);

    connect(w, &DownloadWorker::progressUpdated, this,
            [this](const QString& id, qint64 downloaded, qint64 total, double speed) {
                auto it = m_items.find(id);
                if (it == m_items.end())
                    return;
                it->downloaded = downloaded;
                it->totalSize = total;
                it->speedBps = speed;
                emit itemProgress(id, downloaded, total, speed);
            });
    connect(w, &DownloadWorker::statusChanged, this, [this, w](const QString& id, const QString& status) {
        if (w->stopReason() == DownloadWorker::StopReason::None)
            setStatus(id, status);
    });
    connect(w, &DownloadWorker::infoResolved, this,
            [this](const QString& id, qint64 total, int connections, bool resumable) {
                auto it = m_items.find(id);
                if (it == m_items.end())
                    return;
                it->totalSize = total;
                it->connections = connections;
                it->resumable = resumable;
                emit itemChanged(id);
                scheduleSave();
            });
    connect(w, &DownloadWorker::filenameResolved, this, [this](const QString& id, const QString& name) {
        auto it = m_items.find(id);
        if (it == m_items.end())
            return;
        it->filename = name;
        it->autoName = false;
        const QString cat = m_settings->categoryForExt(QStringLiteral(".") + QFileInfo(name).suffix());
        if (it->category == QLatin1String("Other") && cat != QLatin1String("Other") &&
            m_settings->getBool("auto_categorize"))
            it->category = cat;  // keep the folder; just fix the sidebar category
        emit itemChanged(id);
        scheduleSave();
    });
    connect(w, &DownloadWorker::errorOccurred, this,
            [this](const QString& id, const QString& msg, bool retryable) { handleError(id, msg, retryable); });
    connect(w, &DownloadWorker::finishedOk, this, [this](const QString& id) { handleFinished(id); });
    connect(w, &DownloadWorker::stopped, this, [this, w](const QString& id) {
        if (w->stopReason() == DownloadWorker::StopReason::Preempt && m_items.contains(id))
            setStatus(id, Status::Queued);
        // Pause / cancel already set the status when they were requested.
    });
    connect(w, &QThread::finished, this, [this, w, id] {
        if (m_httpWorkers.value(id) == w)
            m_httpWorkers.remove(id);
        w->deleteLater();
        workerGone(id);
    });

    setStatus(id, Status::Connecting);
    w->start();
}

void QueueManager::startYouTube(DownloadItem& item)
{
    QString quality = item.ytQuality;
    auto* w = new YouTubeDownloadWorker(item.id, item.url, item.saveDir, item.ytAudioOnly, quality, item.ytSubtitles,
                                        item.ytLivestream, this);
    const QString id = item.id;
    item.errorMessage.clear();
    m_ytWorkers.insert(id, w);

    connect(w, &YouTubeDownloadWorker::progressUpdated, this,
            [this](const QString& id, qint64 downloaded, qint64 total, double speed) {
                auto it = m_items.find(id);
                if (it == m_items.end())
                    return;
                it->downloaded = downloaded;
                it->totalSize = total;
                it->speedBps = speed;
                emit itemProgress(id, downloaded, total, speed);
            });
    connect(w, &YouTubeDownloadWorker::statusChanged, this, [this](const QString& id, const QString& status) {
        if (status != QLatin1String("Completed") && status != QLatin1String("Canceled"))
            setStatus(id, status);
    });
    connect(w, &YouTubeDownloadWorker::titleResolved, this, [this](const QString& id, const QString& title) {
        auto it = m_items.find(id);
        if (it == m_items.end() || title.isEmpty())
            return;
        if (it->filename.isEmpty() || it->filename == it->url) {
            it->filename = title;
            emit itemChanged(id);
            scheduleSave();
        }
    });
    connect(w, &YouTubeDownloadWorker::fileResolved, this, [this](const QString& id, const QString& path) {
        auto it = m_items.find(id);
        if (it == m_items.end() || path.isEmpty())
            return;
        const QFileInfo fi(path);
        it->filename = fi.fileName();
        it->saveDir = fi.absolutePath();
        if (fi.exists())
            it->totalSize = it->downloaded = fi.size();
        emit itemChanged(id);
        scheduleSave();
    });
    connect(w, &YouTubeDownloadWorker::livestreamPrompt, this, &QueueManager::livestreamPrompt);
    connect(w, &YouTubeDownloadWorker::errorOccurred, this, [this, w](const QString& id, const QString& msg) {
        handleError(id, msg, false);  // yt-dlp errors are rarely transient
        if (m_ytWorkers.value(id) == w)
            m_ytWorkers.remove(id);
        w->deleteLater();
        workerGone(id);
    });
    connect(w, &YouTubeDownloadWorker::finishedOk, this, [this, w](const QString& id) {
        if (m_ytWorkers.value(id) == w)
            m_ytWorkers.remove(id);
        w->deleteLater();
        handleFinished(id);
        workerGone(id);
    });
    connect(w, &YouTubeDownloadWorker::canceled, this, [this, w](const QString& id) {
        if (m_ytWorkers.value(id) == w)
            m_ytWorkers.remove(id);
        m_pausing.remove(id);
        w->deleteLater();
        workerGone(id);
    });

    setStatus(id, Status::Connecting);
    w->start();
}

void QueueManager::workerGone(const QString& id)
{
    Q_UNUSED(id);
    scheduleSave();
    maybeStartNext();
    if (activeCount() == 0) {
        bool waiting = false;
        bool any = false;
        for (const DownloadItem& it : std::as_const(m_items)) {
            any = true;
            if (it.status == Status::Queued)
                waiting = true;
        }
        if (any && !waiting && m_retryAt.isEmpty())
            emit queueEmptied();
    }
}

void QueueManager::setStatus(const QString& id, const QString& status)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    if (it->status == status)
        return;
    it->status = status;
    if (status != Status::Downloading)
        it->speedBps = 0;
    emit itemStatus(id, status);
    scheduleSave();
}

void QueueManager::handleError(const QString& id, const QString& message, bool retryable)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    const int retries = m_retryCounts.value(id, 0);
    const int maxRetries = m_settings->getInt("auto_retry_count");
    if (retryable && retries < maxRetries) {
        m_retryCounts[id] = retries + 1;
        const int delaySec = std::max(0, m_settings->getInt("auto_retry_delay_sec")) * (retries + 1);
        m_retryAt[id] = nowSeconds() + delaySec;
        it->errorMessage = message;
        setStatus(id, Status::Queued);
        QTimer::singleShot(delaySec * 1000 + 50, this, [this, id] {
            m_retryAt.remove(id);
            maybeStartNext();
        });
        return;
    }
    m_retryCounts.remove(id);
    it->errorMessage = message;
    setStatus(id, Status::Error);
    emit itemError(id, message);
}

void QueueManager::handleFinished(const QString& id)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    m_retryCounts.remove(id);
    it->completedTime = nowSeconds();
    if (it->totalSize > 0)
        it->downloaded = it->totalSize;
    it->speedBps = 0;
    it->errorMessage.clear();
    it->status = Status::Completed;
    emit itemStatus(id, Status::Completed);
    appendHistory(it->toHistoryJson());
    emit itemCompleted(id);
    scheduleSave();
}

// ---- controls ------------------------------------------------------------------------

void QueueManager::pause(const QString& id)
{
    auto it = m_items.find(id);
    if (it == m_items.end() || it->status == Status::Completed)
        return;
    if (auto w = m_httpWorkers.value(id))
        w->requestStop(DownloadWorker::StopReason::Pause);
    if (auto y = m_ytWorkers.value(id)) {
        m_pausing.insert(id);
        y->cancel();  // yt-dlp resumes its .part file when restarted
    }
    m_retryAt.remove(id);
    setStatus(id, Status::Paused);
}

void QueueManager::resume(const QString& id)
{
    auto it = m_items.find(id);
    if (it == m_items.end() || it->status == Status::Completed)
        return;
    if (isRunning(id) && it->status != Status::Paused && it->status != Status::Canceled)
        return;  // already going
    m_retryCounts.remove(id);
    setStatus(id, Status::Queued);
    maybeStartNext();
}

void QueueManager::pauseAll()
{
    for (const QString& id : m_order) {
        const QString& s = m_items[id].status;
        if (s != Status::Completed && s != Status::Error && s != Status::Canceled && s != Status::Paused)
            pause(id);
    }
}

void QueueManager::resumeAll()
{
    for (const QString& id : m_order)
        if (m_items[id].status == Status::Paused || m_items[id].status == Status::Error)
            resume(id);
    maybeStartNext();
}

void QueueManager::cancel(const QString& id)
{
    auto it = m_items.find(id);
    if (it == m_items.end() || it->status == Status::Completed)
        return;
    if (auto w = m_httpWorkers.value(id))
        w->requestStop(DownloadWorker::StopReason::Cancel);
    if (auto y = m_ytWorkers.value(id))
        y->cancel();
    m_retryAt.remove(id);
    setStatus(id, Status::Canceled);
}

void QueueManager::remove(const QString& id, bool deleteFile)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    const DownloadItem copy = it.value();
    cancel(id);
    if (auto w = m_httpWorkers.value(id)) {
        // Let the thread wind down before touching its files.
        w->disconnect(this);
        connect(w, &QThread::finished, w, &QObject::deleteLater);
        m_httpWorkers.remove(id);
        if (deleteFile)
            w->wait(5000);
        if (w->isFinished())
            w->deleteLater();
    }
    if (auto y = m_ytWorkers.value(id)) {
        y->disconnect(this);
        y->deleteLater();
        m_ytWorkers.remove(id);
    }
    m_items.remove(id);
    m_order.removeAll(id);
    m_retryCounts.remove(id);
    m_retryAt.remove(id);

    if (deleteFile && !copy.filename.isEmpty() && !copy.saveDir.isEmpty()) {
        const QString path = copy.finalPath();
        if (QFileInfo(path).isFile())
            QFile::remove(path);
        if (!isYouTube(copy))
            removePartialFiles(copy.saveDir, copy.filename);
    }
    scheduleSave();
    maybeStartNext();
}

void QueueManager::setPriority(const QString& id, const QString& priority)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    it->priority = priority;
    scheduleSave();
    maybeStartNext();
}

void QueueManager::setItemSpeedLimit(const QString& id, int kbps)
{
    auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    it->speedLimitKbps = std::max(0, kbps);
    if (auto w = m_httpWorkers.value(id))
        w->setSpeedLimitKbps(it->speedLimitKbps);
    scheduleSave();
}

void QueueManager::setGlobalSpeedLimit(int kbps)
{
    m_settings->set("global_speed_limit_kbps", kbps);
    m_globalLimiter.setLimit(kbps);
}

void QueueManager::answerLivestream(const QString& id, bool keepGoing)
{
    if (auto y = m_ytWorkers.value(id)) {
        if (keepGoing)
            y->continueLivestream();
        else
            y->stopLivestream();
    }
}
