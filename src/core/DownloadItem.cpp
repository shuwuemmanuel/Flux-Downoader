#include "core/DownloadItem.h"

#include <QDateTime>
#include <QDir>
#include <QUuid>

const QStringList& Priority::levels()
{
    static const QStringList kLevels = {"Highest", "High", "Normal", "Low", "Lowest"};
    return kLevels;
}

int Priority::order(const QString& level)
{
    const int idx = levels().indexOf(level);
    return idx < 0 ? 2 : idx;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

double nowSeconds()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

int DownloadItem::progressPercent() const
{
    if (totalSize <= 0)
        return 0;
    return static_cast<int>(qMin<qint64>(100, downloaded * 100 / totalSize));
}

double DownloadItem::etaSeconds() const
{
    if (speedBps <= 0 || totalSize <= 0)
        return -1;
    return (totalSize - downloaded) / speedBps;
}

QString DownloadItem::finalPath() const
{
    return QDir(saveDir).filePath(filename);
}

QJsonObject DownloadItem::toHistoryJson() const
{
    return QJsonObject{
        {"id", id},
        {"url", url},
        {"filename", filename},
        {"save_dir", saveDir},
        {"category", category},
        {"total_size", totalSize},
        {"downloaded", downloaded},
        {"status", status},
        {"added_time", addedTime},
        {"completed_time", completedTime},
        {"kind", kind},
    };
}

QJsonObject DownloadItem::toJson() const
{
    QJsonObject o = toHistoryJson();
    o["error_message"] = errorMessage;
    o["connections"] = connections;
    o["checksum_expected"] = checksumExpected;
    o["checksum_algo"] = checksumAlgo;
    o["priority"] = priority;
    o["referrer"] = referrer;
    o["speed_limit_kbps"] = speedLimitKbps;
    o["resumable"] = resumable;
    o["auto_name"] = autoName;
    o["yt_audio_only"] = ytAudioOnly;
    o["yt_quality"] = ytQuality;
    o["yt_subtitles"] = ytSubtitles;
    o["yt_livestream"] = ytLivestream;
    return o;
}

DownloadItem DownloadItem::fromJson(const QJsonObject& o)
{
    DownloadItem it;
    it.id = o.value("id").toString(it.id);
    it.url = o.value("url").toString();
    it.filename = o.value("filename").toString();
    it.saveDir = o.value("save_dir").toString();
    it.category = o.value("category").toString("Other");
    it.totalSize = o.value("total_size").toInteger();
    it.downloaded = o.value("downloaded").toInteger();
    it.status = o.value("status").toString(Status::Queued);
    it.addedTime = o.value("added_time").toDouble(nowSeconds());
    it.completedTime = o.value("completed_time").toDouble();
    it.kind = o.value("kind").toString("generic");
    it.errorMessage = o.value("error_message").toString();
    it.connections = o.value("connections").toInt(1);
    it.checksumExpected = o.value("checksum_expected").toString();
    it.checksumAlgo = o.value("checksum_algo").toString();
    it.priority = o.value("priority").toString("Normal");
    it.referrer = o.value("referrer").toString();
    it.speedLimitKbps = o.value("speed_limit_kbps").toInt();
    it.resumable = o.value("resumable").toBool();
    it.autoName = o.value("auto_name").toBool();
    it.ytAudioOnly = o.value("yt_audio_only").toBool();
    it.ytQuality = o.value("yt_quality").toString("best");
    it.ytSubtitles = o.value("yt_subtitles").toBool();
    it.ytLivestream = o.value("yt_livestream").toBool();
    return it;
}
