// Flux Downloader - Download Item model
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Status {
inline const QString Queued = QStringLiteral("Queued");
inline const QString Connecting = QStringLiteral("Connecting");
inline const QString Downloading = QStringLiteral("Downloading");
inline const QString Paused = QStringLiteral("Paused");
inline const QString Completed = QStringLiteral("Completed");
inline const QString Error = QStringLiteral("Error");
inline const QString Canceled = QStringLiteral("Canceled");
}  // namespace Status

namespace Priority {
// "Highest" | "High" | "Normal" | "Low" | "Lowest"
const QStringList& levels();
// Lower number = more important (Highest == 0).
int order(const QString& level);
}  // namespace Priority

QString newId();
double nowSeconds();

struct DownloadItem {
    QString url;
    QString filename;
    QString saveDir;
    QString category = QStringLiteral("Other");
    QString id = newId();
    qint64 totalSize = 0;
    qint64 downloaded = 0;
    QString status = Status::Queued;
    double speedBps = 0.0;
    QString errorMessage;
    int connections = 1;
    double addedTime = nowSeconds();
    double completedTime = 0.0;
    QString kind = QStringLiteral("generic");  // "generic" | "youtube_video" | ...
    QString checksumExpected;
    QString checksumAlgo;                       // empty = detect from digest length
    QString priority = QStringLiteral("Normal");
    QString referrer;
    int speedLimitKbps = 0;                     // per-download limit, 0 = unlimited
    bool resumable = false;
    bool autoName = false;                      // filename was guessed; server may rename

    // YouTube options (only meaningful for youtube_* kinds)
    bool ytAudioOnly = false;
    QString ytQuality = QStringLiteral("best");
    bool ytSubtitles = false;
    bool ytLivestream = false;

    int progressPercent() const;
    double etaSeconds() const;  // < 0 when unknown
    QString finalPath() const;

    QJsonObject toJson() const;                 // full record (queue persistence)
    QJsonObject toHistoryJson() const;          // same shape the Python app wrote
    static DownloadItem fromJson(const QJsonObject& o);
};
