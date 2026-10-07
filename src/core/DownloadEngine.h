// Flux Downloader - Core download engine.
//
// Implements IDM/FDM-style segmented (multi-connection) downloading with:
//  - pause / resume (HTTP Range requests, resumable state saved to a .fluxstate file)
//  - per-download & global bandwidth throttling
//  - automatic retry with backoff
//  - checksum verification
//  - progress / speed signals for the GUI
#pragma once

#include "core/DownloadItem.h"
#include "core/SpeedLimiter.h"

#include <QMutex>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>
#include <vector>

QString guessFilename(const QString& url);
QString sanitizeFilename(const QString& name);
// "name.ext" -> "name (1).ext" if a finished file of that name already exists in dir.
QString uniqueFilename(const QString& dir, const QString& name);
QString stateFilePath(const QString& saveDir, const QString& filename);
QString partFilePath(const QString& finalPath, int index);
// Deletes the .partN files and .fluxstate left behind by an unfinished download.
void removePartialFiles(const QString& saveDir, const QString& filename);

struct SegmentSnapshot {
    qint64 start = 0;
    qint64 end = -1;  // -1 = open ended (single connection, unknown size)
    qint64 done = 0;
    QString info;
};

class DownloadWorker : public QThread {
    Q_OBJECT
public:
    enum class StopReason { None = 0, Pause, Cancel, Preempt };

    struct Options {
        int maxConnections = 8;
        QString proxy;
        bool filenameIsGuess = false;  // allow Content-Disposition to rename
    };

    DownloadWorker(const DownloadItem& item, const Options& opts, SpeedLimiter* globalLimiter,
                   QObject* parent = nullptr);
    ~DownloadWorker() override;

    const QString& itemId() const { return m_id; }

    void requestStop(StopReason reason);
    StopReason stopReason() const { return static_cast<StopReason>(m_stop.load()); }
    void setSpeedLimitKbps(int kbps) { m_limiter.setLimit(kbps); }

    QVector<SegmentSnapshot> segments() const;

signals:
    void progressUpdated(const QString& id, qint64 downloaded, qint64 total, double speedBps);
    void statusChanged(const QString& id, const QString& status);
    void infoResolved(const QString& id, qint64 total, int connections, bool resumable);
    void filenameResolved(const QString& id, const QString& filename);
    void errorOccurred(const QString& id, const QString& message, bool retryable);
    void finishedOk(const QString& id);
    void stopped(const QString& id);

protected:
    void run() override;

public:
    // Internal per-connection state. Public only so the curl callbacks can reach it.
    struct Segment {
        int index = 0;
        qint64 start = 0;
        qint64 end = -1;
        QString partPath;
        std::atomic<qint64> done{0};
        std::atomic<int> state{0};
        QString error;           // written by the segment thread, read after join
        bool rangeRefused = false;
        bool permanent = false;  // e.g. HTTP 404: don't retry
    };
    bool shouldStop() const { return m_stop.load() != 0; }
    void throttle(qint64 n);
    std::atomic<qint64> m_total{0};

private:
    struct Probe {
        qint64 total = 0;
        bool supportsRange = false;
        QString dispositionName;
        QString effectiveUrl;
        QString error;
    };
    Probe probe();
    void runSegment(Segment* seg, bool requireRange);
    bool transfer(const QString& finalPath, int numConns, bool segmented, QString* error);
    bool mergeParts(const QString& finalPath, QString* error);
    bool verifyChecksum(const QString& path, QString* error) const;
    void sleepInterruptible(int ms) const;

    QString m_id;
    QString m_url;
    QString m_filename;
    QString m_saveDir;
    QString m_referrer;
    QString m_checksum;
    QString m_checksumAlgo;
    Options m_opts;

    SpeedLimiter m_limiter;
    SpeedLimiter* m_global;
    std::atomic<int> m_stop{0};
    bool m_permanentError = false;

    mutable QMutex m_segMutex;
    std::vector<std::unique_ptr<Segment>> m_segments;
};
