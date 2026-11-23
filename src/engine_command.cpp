#include <QFileInfo>
#include "engine.h"

#include <QProcess>

// Arbitrary command engine: the full prompt goes to the command's stdin and
// the command must write schema JSON to stdout. The command line is split
// with QProcess::splitCommand, so quoting works as in a shell.
class CommandEngine : public AiEngine {
public:
    explicit CommandEngine(const QString &commandLine, QObject *parent = nullptr)
        : AiEngine(parent)
        , m_commandLine(commandLine)
    {
        const QStringList parts = QProcess::splitCommand(commandLine);
        if (!parts.isEmpty())
            setRequiredTool(parts.first());
    }

    QString id() const override { return QStringLiteral("command"); }
    QString displayName() const override
    {
        // Only the script or program name: the full command line can be long
        // and hold paths that do not belong in the window.
        const QStringList parts = QProcess::splitCommand(m_commandLine);
        return QStringLiteral("Command (%1)")
            .arg(parts.isEmpty() ? QStringLiteral("?") : QFileInfo(parts.last()).fileName());
    }

    void setCommandLine(const QString &commandLine)
    {
        m_commandLine = commandLine;
        const QStringList parts = QProcess::splitCommand(commandLine);
        setRequiredTool(parts.isEmpty() ? QString() : parts.first());
    }
    QString commandLine() const { return m_commandLine; }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        const QStringList parts = QProcess::splitCommand(m_commandLine);
        if (parts.isEmpty()) {
            fail(QStringLiteral("No command configured. Set a command that reads the prompt on stdin "
                                "and writes JSON to stdout."));
            return;
        }
        const QString fullPrompt = systemPrompt + QStringLiteral("\n\n") + userPrompt;
        launch(parts.first(), parts.mid(1), fullPrompt.toUtf8(),
               QStringLiteral("The configured command is not installed or not on PATH."));
    }

private:
    QString m_commandLine;
};

std::unique_ptr<AiEngine> createCommandEngine(const QString &commandLine, QObject *parent)
{
    return std::make_unique<CommandEngine>(commandLine, parent);
}
