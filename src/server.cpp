#include "server.h"

#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include "render.h"
#include "snapshot.h"

PlanServer::PlanServer(const Options &options, QObject *parent)
    : QObject(parent), m_options(options), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &PlanServer::accept);
}

bool PlanServer::listen(QString *error) {
    if (m_server->listen(m_options.address, m_options.port))
        return true;
    if (error) *error = m_server->errorString();
    return false;
}

quint16 PlanServer::port() const { return m_server->serverPort(); }

void PlanServer::accept() {
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        if (m_open >= kMaxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++m_open;
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            --m_open;
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        // Slow or silent clients do not get to hold a connection.
        QTimer::singleShot(kTimeoutMs, socket, [socket] { socket->abort(); });
    }
}

static const char *reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

static PlanServer::Response text(int status, const QByteArray &body) {
    return {status, "text/plain; charset=utf-8", body + "\n", {}};
}

void PlanServer::readRequest(QTcpSocket *socket) {
    if (socket->property("answered").toBool()) {
        socket->readAll();
        return;
    }
    QByteArray buffer = socket->property("request").toByteArray() + socket->readAll();
    int end = buffer.indexOf("\r\n\r\n");
    if (end < 0)
        end = buffer.indexOf("\n\n");
    if (end < 0 && buffer.size() <= kMaxRequestBytes) {
        socket->setProperty("request", buffer);
        return;
    }
    socket->setProperty("answered", true);
    socket->setProperty("request", QVariant());
    if (end < 0 || end > kMaxRequestBytes) {
        send(socket, text(431, "Request too large"), false);
        return;
    }
    const QByteArray requestLine = buffer.left(buffer.indexOf('\n')).trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() != 3 || !parts[2].startsWith("HTTP/1.")) {
        send(socket, text(400, "Bad request"), false);
        return;
    }
    send(socket, respond(parts[0], parts[1]), parts[0] == "HEAD");
}

QString PlanServer::htmlStyle(const QJsonObject &plan) const {
    // The theme of this machine when it runs Omarchy (follows it live),
    // otherwise the theme the app wrote into the snapshot.
    const QJsonObject local = Snapshot::omarchyTheme(m_options.themeDirectory);
    return Snapshot::themeStyle(local.isEmpty() ? plan.value("theme").toObject() : local);
}

PlanServer::Response PlanServer::respond(const QByteArray &method, const QByteArray &target) const {
    if (method != "GET" && method != "HEAD")
        return {405, "text/plain; charset=utf-8", "Only GET and HEAD\n", "GET, HEAD"};
    if (!target.startsWith('/'))
        return text(400, "Bad request");
    const int q = target.indexOf('?');
    const QByteArray rawPath = q < 0 ? target : target.left(q);
    const QString query = q < 0 ? QString() : QString::fromUtf8(target.mid(q + 1));
    const QByteArray path = QByteArray::fromPercentEncoding(rawPath);
    if (path.contains("..") || path.contains('\\') || path.contains('\0'))
        return text(400, "Bad path");

    if (path == "/" || path == "/index.html") {
        QFile file(m_options.snapshotPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            const QString style = htmlStyle({});
            return {200, "text/html; charset=utf-8",
                    Render::messagePage(QStringLiteral("Ingen plan endnu"),
                                        QStringLiteral("Øjebliksbilledet (plan.json) findes ikke endnu. Omaplan skriver "
                                                       "det, når planen ændres, eller med kommandoen omaplan snapshot. "
                                                       "Opdatér siden bagefter."),
                                        style).toUtf8(),
                    {}};
        }
        QJsonParseError parseError;
        const QJsonObject plan = QJsonDocument::fromJson(file.readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError || plan.value("format").toString() != QLatin1String("omaplan-plan"))
            return {500, "text/html; charset=utf-8",
                    Render::messagePage(QStringLiteral("Planen kunne ikke læses"),
                                        QStringLiteral("plan.json er ikke et gyldigt øjebliksbillede fra Omaplan. "
                                                       "Skriv det igen med omaplan snapshot."),
                                        htmlStyle({})).toUtf8(),
                    {}};
        const QString view = QUrlQuery(query).queryItemValue(QStringLiteral("visning"));
        return {200, "text/html; charset=utf-8",
                Render::page(plan, m_options.today(), htmlStyle(plan), view).toUtf8(), {}};
    }
    if (path == "/plan.json") {
        QFile file(m_options.snapshotPath);
        if (!file.open(QIODevice::ReadOnly))
            return {404, "application/json; charset=utf-8", "{\"error\": \"plan.json does not exist yet\"}\n", {}};
        return {200, "application/json; charset=utf-8", file.readAll(), {}};
    }
    if (path == "/style.css" || path == "/app.js") {
        QFile file(QStringLiteral(":/web") + QString::fromUtf8(path));
        file.open(QIODevice::ReadOnly);
        return {200, path.endsWith(".css") ? "text/css; charset=utf-8" : "text/javascript; charset=utf-8",
                file.readAll(), {}};
    }
    return text(404, "Not found");
}

void PlanServer::send(QTcpSocket *socket, const Response &response, bool head) {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reason(response.status) + "\r\n";
    out += "Content-Type: " + response.contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    out += "Cache-Control: no-cache\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Referrer-Policy: no-referrer\r\n";
    // Same origin only; inline style attributes carry the theme and the Gantt positions.
    out += "Content-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'\r\n";
    if (!response.allow.isEmpty())
        out += "Allow: " + response.allow + "\r\n";
    out += "Connection: close\r\n\r\n";
    if (!head)
        out += response.body;
    socket->write(out);
    socket->disconnectFromHost();
}
