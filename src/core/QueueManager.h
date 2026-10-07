// Flux Downloader - Queue Manager
// Owns every HTTP and YouTube download, runs them through one queue with a
// max-concurrency limit, FDM-style priority scheduling and preemption,
// auto-retry, history logging, and persists the list across restarts.
#pragma once

#include "core/DownloadEngine.h"
#include "core/DownloadItem.h"
#include "core/SpeedLimiter.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

class SettingsManager;
class YouTubeDownloadWorker;

class QueueManager : public QObject {
    Q_OBJECT
public:
    explicit QueueManager(SettingsManager* settings, QObject* parent = nullptr);
    ~QueueManager() override;

    // Restores the list saved by the previous session. Call once the UI is connected.
    void loadSaved();
    void shutdown();

    QString add(DownloadItem item, bool autoStart = true);
    const DownloadItem* item(const QString& id) const;
    const QStringList& order() const { return m_order; }
    bool isRunning(const QString& id) const;
    int activeCount() const;

    void pause(const QString& id);
    void resume(const QString& id);
    void pauseAll();
    void resumeAll();
    void cancel(const QString& id);
    void remove(const QString& id, bool deleteFile = false);
    void setPriority(const QString& id, const QString& priority);
    void setItemSpeedLimit(const QString& id, int kbps);
    void setGlobalSpeedLimit(int kbps);
    void answerLivestream(const QString& id, bool keepGoing);

    QVector<SegmentSnapshot> segments(const QString& id) const;

signals:
    void itemAdded(const QString& id);
    void itemChanged(const QString& id);  // name / size / connections changed
    void itemProgress(const QString& id, qint64 downloaded, qint64 total, double speed);
    void itemStatus(const QString& id, const QString& status);
    void itemError(const QString& id, const QString& message);
    void itemCompleted(const QString& id);
    void queueEmptied();
    void livestreamPrompt(const QString& id, const QString& message);

private:
    void maybeStartNext();
    void startHttp(DownloadItem& item);
    void startYouTube(DownloadItem& item);
    void setStatus(const QString& id, const QString& status);
    void handleError(const QString& id, const QString& message, bool retryable);
    void handleFinished(const QString& id);
    void workerGone(const QString& id);
    void scheduleSave();
    void saveNow() const;
    bool isYouTube(const DownloadItem& item) const;

    SettingsManager* m_settings;
    QHash<QString, DownloadItem> m_items;
    QStringList m_order;
    QHash<QString, QPointer<DownloadWorker>> m_httpWorkers;
    QHash<QString, QPointer<YouTubeDownloadWorker>> m_ytWorkers;
    QHash<QString, int> m_retryCounts;
    QHash<QString, double> m_retryAt;
    QSet<QString> m_pausing;  // YouTube workers being stopped for a pause
    SpeedLimiter m_globalLimiter;
    QTimer m_saveTimer;
    bool m_shuttingDown = false;
};
