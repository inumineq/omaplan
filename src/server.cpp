#include "server.h"

#include <QFile>
#include <QHostInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

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
    case 421: return "Misdirected Request";
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
    QByteArray acceptLanguage, host;
    bool hasHost = false;
    for (const QByteArray &line : buffer.left(end).split('\n').mid(1)) {
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        if (name == "accept-language")
            acceptLanguage = line.mid(colon + 1).trimmed();
        else if (name == "host") {
            host = line.mid(colon + 1).trimmed();
            hasHost = true;
        }
    }
    // Browsers always send Host; a request without one is no rebinding.
    if (hasHost && !hostAllowed(host)) {
        send(socket, {421, "text/plain; charset=utf-8",
                      "Unknown host. Start omaplan serve with --host " + host + " to serve this name.\n", {}},
             parts[0] == "HEAD");
        return;
    }
    send(socket, respond(parts[0], parts[1], acceptLanguage), parts[0] == "HEAD");
}

bool PlanServer::hostAllowed(const QByteArray &header) const {
    QString host = QString::fromLatin1(header).trimmed().toLower();
    if (host.startsWith(QLatin1Char('['))) {
        host = host.mid(1, host.indexOf(QLatin1Char(']')) - 1); // [::1]:8787
    } else if (host.count(QLatin1Char(':')) == 1) {
        host = host.left(host.indexOf(QLatin1Char(':')));
    }
    if (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    if (host.isEmpty())
        return false;
    // An address is no name a site can rebind.
    if (!QHostAddress(host).isNull() || host == QLatin1String("localhost"))
        return true;
    const QString self = QHostInfo::localHostName().toLower();
    if (!self.isEmpty() && host.startsWith(self)) {
        // The machine itself, on the local network, or on its tailnet
        // (<name>.<tailnet>.ts.net); <name>.<any domain> could be anyone's.
        const QString rest = host.mid(self.size());
        static const QRegularExpression own(QStringLiteral("^(|\\.local|\\.[a-z0-9-]+\\.ts\\.net)$"));
        if (own.match(rest).hasMatch())
            return true;
    }
    for (const QString &allowed : m_options.hosts)
        if (host == allowed.trimmed().toLower())
            return true;
    return false;
}

QString PlanServer::language(const QByteArray &acceptLanguage) const {
    // "da-DK,da;q=0.9,en;q=0.8": the first supported language by q, ties in
    // header order.
    struct Choice {
        QString language;
        double q;
    };
    QList<Choice> choices;
    for (const QByteArray &entry : acceptLanguage.split(',')) {
        const QList<QByteArray> params = entry.split(';');
        double q = 1;
        for (int i = 1; i < params.size(); ++i) {
            const QByteArray param = params[i].trimmed();
            if (param.startsWith("q=")) {
                bool ok = false;
                q = param.mid(2).toDouble(&ok);
                if (!ok)
                    q = 0;
            }
        }
        const QString supported = Render::supportedLanguage(QString::fromLatin1(params.first()));
        if (!supported.isEmpty() && q > 0)
            choices << Choice{supported, q};
    }
    std::stable_sort(choices.begin(), choices.end(), [](const Choice &a, const Choice &b) { return a.q > b.q; });
    if (!choices.isEmpty())
        return choices.first().language;
    const QString fallback = Render::supportedLanguage(m_options.language);
    return fallback.isEmpty() ? QStringLiteral("en") : fallback;
}

QString PlanServer::htmlStyle(const QJsonObject &plan) const {
    // The theme of this machine when it runs Omarchy (follows it live),
    // otherwise the theme the app wrote into the snapshot.
    const QJsonObject local = Snapshot::omarchyTheme(m_options.themeDirectory);
    return Snapshot::themeStyle(local.isEmpty() ? plan.value("theme").toObject() : local);
}

PlanServer::Response PlanServer::respond(const QByteArray &method, const QByteArray &target,
                                         const QByteArray &acceptLanguage) const {
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
        const QString lang = language(acceptLanguage);
        QFile file(m_options.snapshotPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            return {200, "text/html; charset=utf-8",
                    Render::messagePage(Render::Message::NoPlan, lang, htmlStyle({})).toUtf8(), {}};
        QJsonParseError parseError;
        const QJsonObject plan = QJsonDocument::fromJson(file.readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError || plan.value("format").toString() != QLatin1String("omaplan-plan"))
            return {500, "text/html; charset=utf-8",
                    Render::messagePage(Render::Message::Unreadable, lang, htmlStyle({})).toUtf8(), {}};
        const QString view = QUrlQuery(query).queryItemValue(QStringLiteral("view"));
        return {200, "text/html; charset=utf-8",
                Render::page(plan, m_options.today(), lang, htmlStyle(plan), view).toUtf8(), {}};
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
    for (const char *lang : Render::kLanguages) {
        if (path == "/i18n/" + QByteArray(lang) + ".json") {
            QFile file(QStringLiteral(":/web") + QString::fromUtf8(path));
            file.open(QIODevice::ReadOnly);
            return {200, "application/json; charset=utf-8", file.readAll(), {}};
        }
    }
    return text(404, "Not found");
}

void PlanServer::send(QTcpSocket *socket, const Response &response, bool head) {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reason(response.status) + "\r\n";
    out += "Content-Type: " + response.contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    out += "Cache-Control: no-cache\r\n";
    out += "Vary: Accept-Language\r\n"; // the page follows the browser's language
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
