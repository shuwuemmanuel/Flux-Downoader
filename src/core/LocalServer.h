// Flux Downloader - Local browser-integration server.
//
// The Chrome extension POSTs intercepted download links (and YouTube page
// URLs) here as JSON:   POST /add {url, filename, referrer, kind}
// and checks the app is alive with GET /ping. Listens on 127.0.0.1 only.
#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

struct IncomingLink {
    QString url;
    QString filename;
    QString referrer;
    QString kind;
};

class LocalServer : public QObject {
    Q_OBJECT
public:
    explicit LocalServer(QObject* parent = nullptr);

    bool start(quint16 port);
    void stop();
    bool isListening() const { return m_server.isListening(); }
    QString errorString() const { return m_server.errorString(); }

signals:
    void linkReceived(const IncomingLink& link);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* sock);
    void sendJson(QTcpSocket* sock, int code, const QByteArray& body);

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};
