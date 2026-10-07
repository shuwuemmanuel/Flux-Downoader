// Flux Downloader
// A full-featured, cross-platform download manager built with C++ / Qt 6.
//
// Usage:  FluxDownloader [url | magnet | file.torrent ...]

#include "ui/MainWindow.h"

#include "core/SettingsManager.h"

#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>

#include <curl/curl.h>

namespace {

QString instanceKey()
{
    // One instance per user account.
    QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", "user"));
    return QStringLiteral("FluxDownloader-instance-") + user;
}

// If another instance is running, forward our arguments (or a "show" request)
// to it and return true.
bool forwardToRunningInstance(const QStringList& urls)
{
    QLocalSocket socket;
    socket.connectToServer(instanceKey());
    if (!socket.waitForConnected(500))
        return false;
    QByteArray msg = urls.isEmpty() ? QByteArray("SHOW\n") : urls.join('\n').toUtf8() + '\n';
    socket.write(msg);
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Flux Downloader");
    QApplication::setOrganizationName("Flux");
    QApplication::setApplicationVersion(QStringLiteral(FLUX_VERSION_STRING));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(":/app_icon.png"));

    const QStringList urls = app.arguments().mid(1);

    // Single-instance: hand off to the running copy instead of starting a second one.
    if (forwardToRunningInstance(urls))
        return 0;

    QLocalServer instanceServer;
    QLocalServer::removeServer(instanceKey());  // clean up after a crash (Unix)
    instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
    instanceServer.listen(instanceKey());

    curl_global_init(CURL_GLOBAL_ALL);

    int rc;
    {
        MainWindow window;
        window.applyTheme();

        QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&] {
            while (QLocalSocket* s = instanceServer.nextPendingConnection()) {
                QObject::connect(s, &QLocalSocket::readyRead, &window, [s, &window] {
                    if (!s->canReadLine())
                        return;
                    while (s->canReadLine()) {
                        const QString line = QString::fromUtf8(s->readLine()).trimmed();
                        if (line == QLatin1String("SHOW"))
                            window.showAndRaise();
                        else if (!line.isEmpty())
                            window.handleIncomingUrl(line);
                    }
                });
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            }
        });

        if (window.settings()->getBool("start_minimized"))
            window.hide();
        else
            window.show();

        for (const QString& u : urls)
            window.handleIncomingUrl(u);

        rc = app.exec();
    }

    curl_global_cleanup();
    return rc;
}
