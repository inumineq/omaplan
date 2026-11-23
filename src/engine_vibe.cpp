#include "engine.h"

#include <QTemporaryFile>

// Mistral engine via the Mistral Vibe CLI on the user's Pro quota. No API
// key in the app: vibe already holds the user's login. The system prompt is
// passed as a file so the prompt text stays identical to the other engines.
class VibeEngine : public AiEngine {
public:
    explicit VibeEngine(QObject *parent = nullptr)
        : AiEngine(parent)
    {
        setRequiredTool(QStringLiteral("vibe"));
    }

    QString id() const override { return QStringLiteral("vibe"); }
    QString displayName() const override { return QStringLiteral("Mistral (vibe)"); }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        // Keep the temp file alive until the process has read it.
        m_systemPromptFile = std::make_unique<QTemporaryFile>();
        if (!m_systemPromptFile->open()) {
            fail(tr("Could not create a temporary file for the system prompt."));
            return;
        }
        m_systemPromptFile->write(systemPrompt.toUtf8());
        m_systemPromptFile->flush();

        // Run in an empty temporary directory: vibe would otherwise load the
        // current project's AGENTS.md/context and could read the system prompt
        // as an injection from the user. Tools are disabled with a glob that
        // matches no real tool (headless: --enabled-tools disables all others),
        // so vibe cannot wander off into tool calls (turn_limit).
        launch(QStringLiteral("vibe"),
               {QStringLiteral("-p"), userPrompt,
                QStringLiteral("--output"), QStringLiteral("json"),
                QStringLiteral("--max-turns"), QStringLiteral("1"),
                QStringLiteral("--enabled-tools"), QStringLiteral("__omaplan_no_tools__"),
                QStringLiteral("--system-prompt-file"), m_systemPromptFile->fileName()},
               QByteArray(),
               QStringLiteral("Install Mistral Vibe (the 'vibe' command) to use the Mistral engine."),
               useIsolatedWorkingDirectory());
    }

    void kill() override
    {
        // Only stop the process here. The system prompt file must outlive
        // the process (vibe reads it asynchronously after start), so it is
        // NOT reset in kill() -- launch() calls kill() right before starting,
        // and resetting here would delete the file before vibe reads it.
        // The file is replaced at the next start() and freed by the
        // unique_ptr at destruction.
        AiEngine::kill();
    }

private:
    std::unique_ptr<QTemporaryFile> m_systemPromptFile;
};

std::unique_ptr<AiEngine> createVibeEngine(QObject *parent)
{
    return std::make_unique<VibeEngine>(parent);
}
