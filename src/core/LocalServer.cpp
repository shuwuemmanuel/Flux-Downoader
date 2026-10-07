#include "core/LocalServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

namespace {
constexpr qsizetype kMaxRequestBytes = 1024 * 1024;

QByteArray statusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    default: return "Error";
    }
}

const QByteArray kCorsHeaders =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Access-Control-Allow-Private-Network: true\r\n";
}  // namespace

LocalServer::LocalServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &LocalServer::onNewConnection);
}

bool LocalServer::start(quint16 port)
{
    stop();
    return m_server.listen(QHostAddress::LocalHost, port);
}

void LocalServer::stop()
{
    if (m_server.isListening())
        m_server.close();
}

void LocalServer::onNewConnection()
{
    while (QTcpSocket* sock = m_server.nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
            m_buffers.remove(sock);
            sock->deleteLater();
        });
    }
}

void LocalServer::sendJson(QTcpSocket* sock, int code, const QByteArray& body)
{
    QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + ' ' + statusText(code) + "\r\n";
    resp += kCorsHeaders;
    if (code != 204) {
        resp += "Content-Type: application/json\r\n";
        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    resp += "Connection: close\r\n\r\n";
    if (code != 204)
        resp += body;
    sock->write(resp);
    sock->disconnectFromHost();
}

void LocalServer::onReadyRead(QTcpSocket* sock)
{
    QByteArray& buf = m_buffers[sock];
    buf += sock->readAll();
    if (buf.size() > kMaxRequestBytes) {
        sendJson(sock, 413, R"({"error": "request too large"})");
        return;
    }
    const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;  // wait for the rest of the headers

    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    const QByteArray method = requestLine.value(0).toUpper();
    const QByteArray path = requestLine.value(1).split('?').value(0);
    qsizetype contentLength = 0;
    for (const QByteArray& l : lines.mid(1)) {
        const qsizetype colon = l.indexOf(':');
        if (colon > 0 && l.left(colon).trimmed().toLower() == "content-length")
            contentLength = l.mid(colon + 1).trimmed().toLongLong();
    }
    if (buf.size() < headerEnd + 4 + contentLength)
        return;  // wait for the body
    const QByteArray body = buf.mid(headerEnd + 4, contentLength);
    m_buffers.remove(sock);

    if (method == "OPTIONS") {
        sendJson(sock, 204, {});
    } else if (method == "GET") {
        if (path == "/ping")
            sendJson(sock, 200, R"({"status": "ok", "app": "Flux Downloader"})");
        else
            sendJson(sock, 404, R"({"error": "not found"})");
    } else if (method == "POST") {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(body.isEmpty() ? QByteArray("{}") : body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            sendJson(sock, 400, R"({"error": "invalid json"})");
            return;
        }
        if (path != "/add") {
            sendJson(sock, 404, R"({"error": "not found"})");
            return;
        }
        const QJsonObject o = doc.object();
        IncomingLink link;
        link.url = o.value("url").toString().trimmed();
        if (link.url.isEmpty()) {
            sendJson(sock, 400, R"({"error": "missing url"})");
            return;
        }
        link.filename = o.value("filename").toString();
        link.referrer = o.value("referrer").toString();
        link.kind = o.value("kind").toString("generic");
        sendJson(sock, 200, R"({"status": "queued"})");
        emit linkReceived(link);
    } else {
        sendJson(sock, 404, R"({"error": "not found"})");
    }
}
