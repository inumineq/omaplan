#pragma once

#include <QByteArray>
#include <QDate>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QTcpServer;
class QTcpSocket;

// `omaplan serve`: a small read-only HTTP server over QTcpServer (no extra
// dependency). It reads plan.json on every request -- never the database --
// and renders the page server-side (render.h), plus /plan.json, /style.css
// and /app.js from the resources. GET and HEAD only; one request per
// connection.
class PlanServer : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString snapshotPath;
        QHostAddress address = QHostAddress::LocalHost;
        quint16 port = 8787;
        // Omarchy theme state; empty = ~/.local/state/omarchy/current. Tests
        // point it at an empty directory.
        QString themeDirectory;
        // "Today" for the overview; tests fix it.
        std::function<QDate()> today = [] { return QDate::currentDate(); };
    };

    static constexpr int kMaxRequestBytes = 8 * 1024;
    static constexpr int kTimeoutMs = 10000;
    static constexpr int kMaxConnections = 64;

    explicit PlanServer(const Options &options, QObject *parent = nullptr);
    bool listen(QString *error = nullptr);
    quint16 port() const;

    struct Response {
        int status = 200;
        QByteArray contentType;
        QByteArray body;
        QByteArray allow; // set on 405
    };
    // The routing, without sockets: method and request target as received.
    Response respond(const QByteArray &method, const QByteArray &target) const;

private:
    void accept();
    void readRequest(QTcpSocket *socket);
    void send(QTcpSocket *socket, const Response &response, bool head);
    QString htmlStyle(const QJsonObject &plan) const;

    Options m_options;
    QTcpServer *m_server;
    int m_open = 0;
};
