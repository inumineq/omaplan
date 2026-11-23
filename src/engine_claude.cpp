#include "engine.h"

// Claude engine via `claude -p` on the user's subscription. No API key in
// the app: the claude CLI already holds the user's login. The full prompt
// (system + user text) is passed as the -p argument; the JSON answer arrives
// in an envelope ({"result": "..."}) which parseProposal unwraps.
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
        launch(QStringLiteral("claude"),
               {QStringLiteral("-p"), fullPrompt,
                QStringLiteral("--output-format"), QStringLiteral("json")},
               QByteArray(),
               QStringLiteral("Install the Claude CLI (the 'claude' command) and log in to use the Claude engine."));
    }
};

std::unique_ptr<AiEngine> createClaudeEngine(QObject *parent)
{
    return std::make_unique<ClaudeEngine>(parent);
}
