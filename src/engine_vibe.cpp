#include "engine.h"

#include <QTemporaryFile>

// Mistral engine via the Mistral Vibe CLI on the user's Pro quota. No API
// key in the app: vibe already holds the user's login. Both prompts are
// passed as private temporary files (0600): the user prompt stays out of `ps`
// and the argument size limit. vibe does not read a prompt piped to it from
// QProcess (`--prompt-file /dev/stdin` failed with "No prompt provided for
// programmatic mode"), so stdin is not used.
class VibeEngine : public AiEngine {
public:
    explicit VibeEngine(const QString &model, QObject *parent = nullptr)
        : AiEngine(parent)
        , m_model(model)
    {
        setRequiredTool(QStringLiteral("vibe"));
        // The prompt files are only needed while vibe runs; every run ends in
        // exactly one of these signals (also on cancel and timeout).
        connect(this, &AiEngine::finished, this, [this] { removePromptFiles(); });
        connect(this, &AiEngine::failed, this, [this] { removePromptFiles(); });
    }

    QString id() const override { return QStringLiteral("vibe"); }
    QString displayName() const override { return QStringLiteral("Mistral (vibe)"); }
    QString modelInfo() const override { return m_model; }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        // Keep the temp files alive until the process has read them.
        m_systemPromptFile = writePrivateFile(systemPrompt);
        m_promptFile = writePrivateFile(userPrompt);
        if (!m_systemPromptFile || !m_promptFile) {
            removePromptFiles();
            fail(AiEngine::tr("Could not create a temporary file for the prompt."));
            return;
        }

        // Run in an empty temporary directory: vibe would otherwise load the
        // current project's AGENTS.md/context and could read the system prompt
        // as an injection from the user. Tools are disabled with a glob that
        // matches no real tool (headless: --enabled-tools disables all others),
        // so vibe cannot wander off into tool calls (turn_limit).
        // A configured model goes to --model (a vibe alias); without one,
        // vibe uses its own active_model.
        QStringList arguments = {QStringLiteral("--prompt-file"), m_promptFile->fileName(),
                                 QStringLiteral("--output"), QStringLiteral("json"),
                                 QStringLiteral("--max-turns"), QStringLiteral("1"),
                                 QStringLiteral("--enabled-tools"), QStringLiteral("__omaplan_no_tools__"),
                                 QStringLiteral("--system-prompt-file"), m_systemPromptFile->fileName()};
        if (!m_model.isEmpty())
            arguments << QStringLiteral("--model") << m_model;
        launch(QStringLiteral("vibe"), arguments,
               QByteArray(),
               QStringLiteral("Install Mistral Vibe (the 'vibe' command) to use the Mistral engine."),
               useIsolatedWorkingDirectory());
    }

    void kill() override
    {
        // Only stop the process here. The prompt files must outlive the
        // process (vibe reads them after start), so they are NOT removed in
        // kill() -- launch() calls kill() right before starting, and removing
        // them here would delete them before vibe reads them. They are
        // removed when the run finishes or fails.
        AiEngine::kill();
    }

private:
    // QTemporaryFile creates the file with mode 0600 and removes it when
    // destroyed. Returns nullptr when the file cannot be written.
    static std::unique_ptr<QTemporaryFile> writePrivateFile(const QString &text)
    {
        auto file = std::make_unique<QTemporaryFile>();
        if (!file->open())
            return nullptr;
        const QByteArray data = text.toUtf8();
        if (file->write(data) != data.size() || !file->flush())
            return nullptr;
        return file;
    }

    void removePromptFiles()
    {
        m_systemPromptFile.reset();
        m_promptFile.reset();
    }

    std::unique_ptr<QTemporaryFile> m_systemPromptFile;
    std::unique_ptr<QTemporaryFile> m_promptFile;
    QString m_model;
};

std::unique_ptr<AiEngine> createVibeEngine(const QString &model, QObject *parent)
{
    return std::make_unique<VibeEngine>(model, parent);
}
