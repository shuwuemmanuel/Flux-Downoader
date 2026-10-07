// Core engine tests: segmented download, pause/resume, cancel, checksum,
// retry, priority preemption and the browser-integration server.
// Uses an in-process HTTP server so no network access is needed.

#include "core/DownloadEngine.h"
#include "core/LocalServer.h"
#include "core/QueueManager.h"
#include "core/SettingsManager.h"
#include "core/YouTubeEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <curl/curl.h>

// Minimal HTTP/1.1 file server with Range support and optional throttling.
class TestHttpServer : public QObject {
public:
    QByteArray payload;
    bool honorRange = true;
    int bytesPerTick = 0;  // 0 = unthrottled; else bytes per 10 ms per connection
    int requests = 0;

    explicit TestHttpServer(QObject* parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onRead(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }
    QString url(const QString& path) const
    {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(path);
    }

private:
    void onRead(QTcpSocket* s)
    {
        QByteArray& buf = m_bufs[s];
        buf += s->readAll();
        const qsizetype end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = buf.left(end);
        buf.clear();
        ++requests;
        const QByteArray path = head.split(' ').value(1);
        if (path.startsWith("/missing")) {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }
        qint64 start = 0, stop = payload.size() - 1;
        bool partial = false;
        static const QRegularExpression re(QStringLiteral(R"(Range:\s*bytes=(\d+)-(\d*))"),
                                           QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(QString::fromLatin1(head));
        if (m.hasMatch() && honorRange) {
            start = m.captured(1).toLongLong();
            if (!m.captured(2).isEmpty())
                stop = m.captured(2).toLongLong();
            partial = true;
        }
        QByteArray resp = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        resp += "Accept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(stop - start + 1) + "\r\n";
        if (partial)
            resp += "Content-Range: bytes " + QByteArray::number(start) + '-' + QByteArray::number(stop) + '/' +
                    QByteArray::number(payload.size()) + "\r\n";
        resp += "Connection: close\r\n\r\n";
        s->write(resp);
        const QByteArray body = payload.mid(start, stop - start + 1);
        if (bytesPerTick <= 0) {
            s->write(body);
            s->disconnectFromHost();
            return;
        }
        auto* timer = new QTimer(s);
        auto offset = std::make_shared<qint64>(0);
        const int tick = bytesPerTick;  // fixed per connection
        connect(timer, &QTimer::timeout, s, [s, body, offset, timer, tick] {
            if (s->state() != QAbstractSocket::ConnectedState) {
                timer->stop();
                return;
            }
            s->write(body.mid(*offset, tick));
            *offset += tick;
            if (*offset >= body.size()) {
                timer->stop();
                s->disconnectFromHost();
            }
        });
        timer->start(10);
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_bufs;
};

class TestCore : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_home;
    SettingsManager* m_settings = nullptr;
    TestHttpServer* m_http = nullptr;

    QString dlDir() const { return QDir(m_home.path()).filePath("dl"); }

    DownloadItem makeItem(const QString& url, const QString& name)
    {
        DownloadItem it;
        it.url = url;
        it.filename = name;
        it.saveDir = dlDir();
        return it;
    }
    QByteArray fileData(const QString& name) const
    {
        QFile f(QDir(dlDir()).filePath(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("NO_PROXY", "*");
        curl_global_init(CURL_GLOBAL_ALL);
    }

    void init()
    {
        QDir(m_home.path()).removeRecursively();
        QDir().mkpath(m_home.path());
        m_settings = new SettingsManager(this);
        m_settings->set("download_dir", dlDir());
        m_settings->set("auto_retry_delay_sec", 0);
        m_http = new TestHttpServer(this);
        QByteArray data(6 * 1024 * 1024, Qt::Uninitialized);
        QRandomGenerator gen(42);
        gen.fillRange(reinterpret_cast<quint32*>(data.data()), data.size() / 4);
        m_http->payload = data;
    }

    void cleanup()
    {
        delete m_http;
        delete m_settings;
    }

    void guessAndUniqueFilenames()
    {
        QCOMPARE(guessFilename("https://x.org/a/b/My%20File.zip?x=1"), QString("My File.zip"));
        QCOMPARE(guessFilename("https://x.org/"), QString("download"));
        QDir().mkpath(dlDir());
        QFile f(QDir(dlDir()).filePath("a.tar.gz"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QCOMPARE(uniqueFilename(dlDir(), "a.tar.gz"), QString("a (1).tar.gz"));
        QCOMPARE(uniqueFilename(dlDir(), "b.zip"), QString("b.zip"));
    }

    void segmentedDownload()
    {
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        const QString id = q.add(makeItem(m_http->url("file.bin"), "file.bin"));
        QVERIFY(done.wait(20000));
        QCOMPARE(q.item(id)->status, Status::Completed);
        QVERIFY(q.item(id)->connections > 1);
        QVERIFY(q.item(id)->resumable);
        QCOMPARE(fileData("file.bin"), m_http->payload);
        QVERIFY(!QFile::exists(stateFilePath(dlDir(), "file.bin")));
    }

    void singleConnectionWhenNoRange()
    {
        m_http->honorRange = false;
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        const QString id = q.add(makeItem(m_http->url("file.bin"), "file.bin"));
        QVERIFY(done.wait(20000));
        QCOMPARE(q.item(id)->connections, 1);
        QVERIFY(!q.item(id)->resumable);
        QCOMPARE(fileData("file.bin"), m_http->payload);
    }

    void pauseAndResume()
    {
        m_http->bytesPerTick = 8 * 1024;
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        const QString id = q.add(makeItem(m_http->url("file.bin"), "file.bin"));
        QTRY_VERIFY_WITH_TIMEOUT(q.item(id)->downloaded > 512 * 1024, 20000);
        q.pause(id);
        QCOMPARE(q.item(id)->status, Status::Paused);
        QTRY_VERIFY_WITH_TIMEOUT(!q.isRunning(id), 10000);
        QCOMPARE(q.item(id)->status, Status::Paused);
        QVERIFY(QFile::exists(partFilePath(QDir(dlDir()).filePath("file.bin"), 0)));
        const qint64 before = q.item(id)->downloaded;
        QTest::qWait(300);
        QCOMPARE(q.item(id)->downloaded, before);  // really stopped

        m_http->bytesPerTick = 0;
        q.resume(id);
        QVERIFY(done.wait(20000));
        QCOMPARE(fileData("file.bin"), m_http->payload);
    }

    void cancelAndRemoveDeletesPartials()
    {
        m_http->bytesPerTick = 4 * 1024;
        QueueManager q(m_settings);
        const QString id = q.add(makeItem(m_http->url("file.bin"), "file.bin"));
        QTRY_VERIFY_WITH_TIMEOUT(q.item(id)->downloaded > 0, 20000);
        q.cancel(id);
        QCOMPARE(q.item(id)->status, Status::Canceled);
        q.remove(id, true);
        QVERIFY(!q.item(id));
        QTRY_VERIFY_WITH_TIMEOUT(QDir(dlDir()).entryList(QDir::Files | QDir::Hidden).isEmpty(), 5000);
    }

    void checksumVerification()
    {
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        QSignalSpy err(&q, &QueueManager::itemError);

        DownloadItem good = makeItem(m_http->url("file.bin"), "good.bin");
        good.checksumExpected = QCryptographicHash::hash(m_http->payload, QCryptographicHash::Md5).toHex();
        DownloadItem bad = makeItem(m_http->url("file.bin"), "bad.bin");
        bad.checksumExpected = QString(64, 'a');
        const QString goodId = q.add(good);
        const QString badId = q.add(bad);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1 && err.count() == 1, 20000);
        QCOMPARE(q.item(goodId)->status, Status::Completed);
        QCOMPARE(q.item(badId)->status, Status::Error);
        QVERIFY(q.item(badId)->errorMessage.contains("Checksum mismatch"));
    }

    void permanentHttpErrorFailsFast()
    {
        m_settings->set("auto_retry_count", 3);
        QueueManager q(m_settings);
        QSignalSpy err(&q, &QueueManager::itemError);
        const QString id = q.add(makeItem(m_http->url("missing.bin"), "missing.bin"));
        QVERIFY(err.wait(5000));
        QCOMPARE(q.item(id)->status, Status::Error);
        QVERIFY(q.item(id)->errorMessage.contains("404"));
        QCOMPARE(m_http->requests, 2);  // probe + one GET, no pointless retries
    }

    void priorityPreemption()
    {
        m_settings->set("max_concurrent_downloads", 1);
        m_http->bytesPerTick = 8 * 1024;
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        DownloadItem low = makeItem(m_http->url("file.bin"), "low.bin");
        low.priority = "Low";
        const QString lowId = q.add(low);
        QTRY_VERIFY_WITH_TIMEOUT(q.item(lowId)->downloaded > 0, 20000);

        DownloadItem high = makeItem(m_http->url("file.bin"), "high.bin");
        high.priority = "Highest";
        const QString highId = q.add(high);
        QTRY_VERIFY_WITH_TIMEOUT(q.isRunning(highId) && !q.isRunning(lowId), 10000);
        QCOMPARE(q.item(lowId)->status, Status::Queued);

        m_http->bytesPerTick = 0;
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 2, 30000);
        QCOMPARE(done.at(0).at(0).toString(), highId);
        QCOMPARE(fileData("low.bin"), m_http->payload);
        QCOMPARE(fileData("high.bin"), m_http->payload);
    }

    void globalSpeedLimit()
    {
        m_settings->set("global_speed_limit_kbps", 2048);  // 2 MB/s for a 6 MB file
        QueueManager q(m_settings);
        QSignalSpy done(&q, &QueueManager::itemCompleted);
        QElapsedTimer t;
        t.start();
        q.add(makeItem(m_http->url("file.bin"), "limited.bin"));
        QVERIFY(done.wait(20000));
        // One second of burst is allowed, so expect >= ~2 s rather than 3 s.
        QVERIFY2(t.elapsed() >= 1800, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(fileData("limited.bin"), m_http->payload);
    }

    void raisingConcurrencyStartsQueued()
    {
        m_settings->set("max_concurrent_downloads", 1);
        m_http->bytesPerTick = 4 * 1024;
        QueueManager q(m_settings);
        const QString a = q.add(makeItem(m_http->url("file.bin"), "a.bin"));
        const QString b = q.add(makeItem(m_http->url("file.bin"), "b.bin"));
        QTRY_VERIFY_WITH_TIMEOUT(q.isRunning(a), 5000);
        QVERIFY(!q.isRunning(b));
        m_settings->set("max_concurrent_downloads", 2);
        QTRY_VERIFY_WITH_TIMEOUT(q.isRunning(b), 5000);
        q.cancel(a);
        q.cancel(b);
    }

    void queuePersistsAcrossSessions()
    {
        QString id;
        {
            QueueManager q(m_settings);
            DownloadItem it = makeItem(m_http->url("file.bin"), "later.bin");
            id = q.add(it, false);
            QCOMPARE(q.item(id)->status, Status::Paused);
        }  // destructor saves
        QueueManager q2(m_settings);
        q2.loadSaved();
        QVERIFY(q2.item(id));
        QCOMPARE(q2.item(id)->filename, QString("later.bin"));
        QCOMPARE(q2.item(id)->status, Status::Paused);
    }

    // Exercises the yt-dlp integration (probe + download + progress parsing)
    // against a local MP4. Skipped when yt-dlp or the sample clip is missing.
    void youtubeEngineWithYtDlp()
    {
        if (YouTube::ytDlpPath().isEmpty())
            QSKIP("yt-dlp not installed");
        QFile clip(qEnvironmentVariable("FLUX_TEST_CLIP"));
        if (!clip.open(QIODevice::ReadOnly))
            QSKIP("set FLUX_TEST_CLIP to a small .mp4 to run this test");
        m_http->payload = clip.readAll();
        const QString url = m_http->url("clip.mp4");

        YouTubeProbe probe;
        QSignalSpy resolved(&probe, &YouTubeProbe::resolved);
        QSignalSpy failed(&probe, &YouTubeProbe::failed);
        probe.start(url);
        QTRY_VERIFY_WITH_TIMEOUT(resolved.count() + failed.count() == 1, 30000);
        QCOMPARE(failed.count(), 0);
        const auto entries = resolved.at(0).at(0).value<QVector<YouTubeEntry>>();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(resolved.at(0).at(1).toString(), QString("youtube_video"));

        const QString outDir = QDir(m_home.path()).filePath("yt");
        YouTubeDownloadWorker w("id1", url, outDir, false, "best", false, false);
        QSignalSpy ok(&w, &YouTubeDownloadWorker::finishedOk);
        QSignalSpy err(&w, &YouTubeDownloadWorker::errorOccurred);
        QSignalSpy progress(&w, &YouTubeDownloadWorker::progressUpdated);
        QSignalSpy file(&w, &YouTubeDownloadWorker::fileResolved);
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(ok.count() + err.count() == 1, 60000);
        if (!err.isEmpty())
            QFAIL(qPrintable(err.at(0).at(1).toString()));
        QVERIFY(!progress.isEmpty());
        QCOMPARE(file.count(), 1);
        const QString path = file.at(0).at(1).toString();
        QVERIFY2(QFileInfo(path).size() > 0, qPrintable(path));
        QVERIFY(path.endsWith(".mp4"));
    }

    void localServerAcceptsLinks()
    {
        LocalServer server;
        const quint16 port = 38997;
        QVERIFY(server.start(port));
        QList<IncomingLink> links;
        connect(&server, &LocalServer::linkReceived, this, [&](const IncomingLink& l) { links << l; });

        auto request = [&](const QByteArray& raw) {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, port);
            if (!s.waitForConnected(2000))
                return QByteArray();
            s.write(raw);
            QByteArray resp;
            QElapsedTimer t;
            t.start();
            while (s.state() == QAbstractSocket::ConnectedState && t.elapsed() < 3000) {
                QTest::qWait(10);
                resp += s.readAll();
            }
            return resp + s.readAll();
        };
        const QByteArray body = R"({"url":"https://example.com/f.zip","filename":"f.zip"})";
        QByteArray resp = request("POST /add HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
        QVERIFY(resp.startsWith("HTTP/1.1 200"));
        QVERIFY(resp.contains("queued"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links.first().url, QString("https://example.com/f.zip"));
        QCOMPARE(links.first().filename, QString("f.zip"));

        resp = request("GET /ping HTTP/1.1\r\nHost: x\r\n\r\n");
        QVERIFY(resp.contains("Flux Downloader"));
        resp = request("POST /add HTTP/1.1\r\nContent-Length: 3\r\n\r\n{x}");
        QVERIFY(resp.startsWith("HTTP/1.1 400"));
        resp = request("OPTIONS /add HTTP/1.1\r\n\r\n");
        QVERIFY(resp.startsWith("HTTP/1.1 204"));
        QVERIFY(resp.contains("Access-Control-Allow-Origin: *"));
    }
};

Q_DECLARE_METATYPE(YouTubeEntry)

QTEST_MAIN(TestCore)
#include "test_core.moc"
