#pragma once

#include <QJsonObject>
#include <QJsonValue>

class Actions;
class QIODevice;

// `omaplan mcp`: a Model Context Protocol server over stdio. One JSON-RPC 2.0
// message per line in, one per line out (no batches). The tools are the CLI's
// commands, each a thin call into Actions with the source "mcp", so an agent
// over MCP and one over the CLI can never see different behaviour.
class McpServer {
public:
    explicit McpServer(Actions *actions) : m_actions(actions) {}

    // Handles one message. Returns the response, or an undefined value for
    // notifications (which get no answer).
    QJsonValue handle(const QJsonValue &message);

    // Reads lines from `in` until EOF and writes responses to `out`.
    int serve(QIODevice *in, QIODevice *out);

    static QJsonObject toolList();

private:
    QJsonObject callTool(const QString &name, const QJsonObject &arguments);

    Actions *m_actions;
};
