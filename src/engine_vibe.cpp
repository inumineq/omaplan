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
            fail(QStringLiteral("Could not create a temporary file for the system prompt."));
            return;
        }
        m_systemPromptFile->write(systemPrompt.toUtf8());
        m_systemPromptFile->flush();

        launch(QStringLiteral("vibe"),
               {QStringLiteral("-p"), userPrompt,
                QStringLiteral("--output"), QStringLiteral("json"),
                QStringLiteral("--max-turns"), QStringLiteral("1"),
                QStringLiteral("--system-prompt-file"), m_systemPromptFile->fileName()},
               QByteArray(),
               QStringLiteral("Install Mistral Vibe (the 'vibe' command) to use the Mistral engine."));
    }

    void kill() override
    {
        AiEngine::kill();
        m_systemPromptFile.reset();
    }

private:
    std::unique_ptr<QTemporaryFile> m_systemPromptFile;
};

std::unique_ptr<AiEngine> createVibeEngine(QObject *parent)
{
    return std::make_unique<VibeEngine>(parent);
}
