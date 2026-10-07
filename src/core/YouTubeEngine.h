// Flux Downloader - YouTube engine
// Drives the yt-dlp executable to support single videos, full playlists and
// entire channels, with format/quality selection, audio-only extraction and
// subtitle download. Includes livestream support with from-start downloading
// and interactive continuation.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

struct YouTubeEntry {
    QString id;
    QString title;
    QString url;
    double duration = 0;
    bool isLive = false;
};

namespace YouTube {
// Full path of yt-dlp (next to the app, in ./tools, or on PATH); empty if missing.
QString ytDlpPath();
// Extra args pointing yt-dlp at a bundled ffmpeg, if one ships next to the app.
QStringList ffmpegArgs();
QString missingMessage();
}  // namespace YouTube

// Resolves a URL (video / playlist / channel) into a list of entries without
// downloading, so the UI can show a selectable list.
class YouTubeProbe : public QObject {
    Q_OBJECT
public:
    explicit YouTubeProbe(QObject* parent = nullptr);
    ~YouTubeProbe() override;
    void start(const QString& url);

signals:
    void resolved(const QVector<YouTubeEntry>& entries, const QString& kind, bool isLivestream);
    void failed(const QString& message);

private:
    QProcess* m_proc = nullptr;
    QString m_url;
};

class YouTubeDownloadWorker : public QObject {
    Q_OBJECT
public:
    YouTubeDownloadWorker(const QString& itemId, const QString& url, const QString& saveDir, bool audioOnly,
                          const QString& quality, bool subtitles, bool isLivestream, QObject* parent = nullptr);
    ~YouTubeDownloadWorker() override;

    void start();
    void cancel();
    void continueLivestream();  // UI: user chose to keep recording
    void stopLivestream();      // UI: user chose to stop
    bool isRunning() const { return m_running; }

signals:
    void progressUpdated(const QString& id, qint64 downloaded, qint64 total, double speed);
    void statusChanged(const QString& id, const QString& status);
    void titleResolved(const QString& id, const QString& title);
    void fileResolved(const QString& id, const QString& path);
    void errorOccurred(const QString& id, const QString& message);
    void finishedOk(const QString& id);
    void canceled(const QString& id);
    void livestreamPrompt(const QString& id, const QString& message);

private:
    QStringList commonArgs() const;
    QString formatSpec() const;
    void runDownload(const QString& outTemplate, bool liveFromStart, std::function<void(bool ok)> done);
    void checkLive(std::function<void(bool known, bool isLive)> done);
    void handleOutputLine(const QString& line);

    void liveStep();
    void finishOk();
    void fail(const QString& message);

    QString m_id;
    QString m_url;
    QString m_saveDir;
    bool m_audioOnly;
    QString m_quality;
    bool m_subtitles;
    bool m_isLivestream;

    QPointer<QProcess> m_proc;
    QString m_lastError;
    QByteArray m_stdoutBuf;
    bool m_canceled = false;
    bool m_running = false;
    bool m_awaitingDecision = false;
    int m_segment = 1;
    QTimer m_decisionTimeout;
};
