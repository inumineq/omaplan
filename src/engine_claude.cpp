#include "engine.h"

// Claude engine via `claude -p` on the user's subscription. No API key in
// the app: the claude CLI already holds the user's login. The full prompt
// (system + user text) goes on stdin, so it is neither visible in `ps` nor
// bound by the kernel's limit on one argument. The run is plain text in,
// JSON out: no tools, no MCP servers, none of the user's settings files
// (hooks, permissions), no slash commands and no saved session. The text can
// come from others (pasted, or an agent via MCP), so it must not reach an
// agent that can act. The JSON answer arrives in an envelope
// ({"result": "..."}) which parseProposal unwraps.
class ClaudeEngine : public AiEngine {
public:
    explicit ClaudeEngine(QObject *parent = nullptr)
        : AiEngine(parent)
    {
        setRequiredTool(QStringLiteral("claude"));
    }

    QString id() const override { return QStringLiteral("claude"); }
    QString displayName() const override { return QStringLiteral("Claude (claude -p)"); }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        const QString fullPrompt = systemPrompt + QStringLiteral("\n\n") + userPrompt;
        // Empty temporary working directory, so no project AGENTS.md/context
        // is picked up and mistaken for instructions.
        launch(QStringLiteral("claude"),
               {QStringLiteral("-p"),
                QStringLiteral("--output-format"), QStringLiteral("json"),
                QStringLiteral("--tools"), QString(),
                QStringLiteral("--strict-mcp-config"),
                QStringLiteral("--setting-sources"), QString(),
                QStringLiteral("--disable-slash-commands"),
                QStringLiteral("--no-session-persistence")},
               fullPrompt.toUtf8(),
               QStringLiteral("Install the Claude CLI (the 'claude' command) and log in to use the Claude engine."),
               useIsolatedWorkingDirectory());
    }
};

std::unique_ptr<AiEngine> createClaudeEngine(QObject *parent)
{
    return std::make_unique<ClaudeEngine>(parent);
}
