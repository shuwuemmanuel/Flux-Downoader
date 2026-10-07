// Flux Downloader - Torrent engine (BitTorrent support)
//
// Built on libtorrent-rasterbar (the same engine that powers qBittorrent / Deluge):
//  - DHT, Local Service Discovery, and PEX for peer finding without a tracker
//  - Per-torrent pause/resume/remove (with optional data deletion)
//  - Per-file priority selection within a torrent (Skip / Low / Normal / High)
//  - Torrent queue priority (Highest -> Lowest) that reorders libtorrent's
//    internal download queue, so important torrents get bandwidth first
//  - Global up/down bandwidth limits shared with the settings dialog
//  - Torrents are remembered across restarts (~/.flux_downloader/torrents.json)
//
// When built without libtorrent, `available()` is false and the UI disables
// torrent features - everything else keeps working.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>

class SettingsManager;

namespace TorrentPriority {
constexpr int Skip = 0;
constexpr int Low = 1;
constexpr int Normal = 4;
constexpr int High = 7;
}  // namespace TorrentPriority

const QStringList& torrentQueuePriorities();

struct TorrentFileInfo {
    int index = 0;
    QString path;
    qint64 size = 0;
    int priority = TorrentPriority::Normal;
};

struct TorrentStatus {
    double progress = 0;  // 0..100
    qint64 downloadRate = 0;
    qint64 uploadRate = 0;
    int numPeers = 0;
    int numSeeds = 0;
    qint64 totalWanted = 0;
    qint64 totalWantedDone = 0;
    QString state;
    QString name;
};

class TorrentEngine : public QObject {
    Q_OBJECT
public:
    explicit TorrentEngine(SettingsManager* settings, QObject* parent = nullptr);
    ~TorrentEngine() override;

    static bool available();

    // Re-reads port / DHT / bandwidth limits from the settings.
    void applySettings();
    // Both throw std::runtime_error with a readable message on failure.
    QString addMagnet(const QString& uri, const QString& savePath);
    QString addTorrentFile(const QString& torrentPath, const QString& savePath);
    // Re-adds the torrents saved by a previous session. Call once the UI is connected.
    void restoreSession();

    void pause(const QString& tid);
    void resume(const QString& tid);
    void remove(const QString& tid, bool deleteFiles = false);
    void setFilePriority(const QString& tid, int fileIndex, int priority);
    void setQueuePriority(const QString& tid, const QString& level);

    bool contains(const QString& tid) const;
    QString savePath(const QString& tid) const;
    QString name(const QString& tid) const;
    QStringList ids() const;
    void shutdown();

signals:
    void torrentAdded(const QString& id, const QString& name);
    void torrentRemoved(const QString& id);
    void metadataReady(const QString& id, const QVector<TorrentFileInfo>& files);
    void progressUpdated(const QString& id, const TorrentStatus& status);
    void statusChanged(const QString& id, const QString& state);
    void torrentError(const QString& id, const QString& message);
    void torrentFinished(const QString& id);

private:
    struct Impl;
    void poll();
    void persist() const;

    SettingsManager* m_settings;
    std::unique_ptr<Impl> d;
    QTimer m_pollTimer;
};
