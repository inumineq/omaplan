#include "engine.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

AiEngine::AiEngine(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &AiEngine::onTimeout);
}

AiEngine::~AiEngine()
{
    kill();
}

bool AiEngine::available(QString *whyNot) const
{
    if (m_requiredTool.isEmpty())
        return true;
    if (!QStandardPaths::findExecutable(m_requiredTool).isEmpty())
        return true;
    if (whyNot)
        *whyNot = QStringLiteral("The '%1' command was not found on PATH. Install it to use the %2 engine.")
                      .arg(m_requiredTool, id());
    return false;
}

void AiEngine::setRequiredTool(const QString &toolName)
{
    m_requiredTool = toolName;
}

void AiEngine::propose(const QString &text, const PromptContext &context)
{
    if (m_running)
        cancel();
    m_running = true;
    m_stdout.clear();
    m_stderr.clear();
    m_startFailed = false;
    m_timer->start(m_timeoutMs);
    start(buildSystemPrompt(context), buildUserPrompt(text));
}

void AiEngine::cancel()
{
    if (!m_running)
        return;
    stopTimer();
    kill();
    m_running = false;
    emit failed(QStringLiteral("Cancelled."));
}

void AiEngine::kill()
{
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            // Bounded wait so the object is never destroyed while the
            // process still runs (SIGKILL is effectively immediate).
            m_process->waitForFinished(2000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void AiEngine::stopTimer()
{
    m_timer->stop();
}

QProcess *AiEngine::launch(const QString &program, const QStringList &arguments,
                           const QByteArray &stdinData, const QString &missingToolHint)
{
    kill();
    m_stdout.clear();
    m_stderr.clear();
    m_startFailed = false;
    m_missingToolHint = missingToolHint;

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &AiEngine::onProcessReadyRead);
    connect(m_process, &QProcess::finished, this, &AiEngine::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &AiEngine::onProcessErrorOccurred);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    // Write stdin only once the process has actually started; writing or
    // closing the channel before start() can lose the EOF, and a command
    // like `cat` would then wait for stdin forever.
    connect(m_process, &QProcess::started, m_process, [this, stdinData] {
        if (!stdinData.isEmpty())
            m_process->write(stdinData);
        m_process->closeWriteChannel();
    });
    m_process->start(program, arguments);
    return m_process;
}

void AiEngine::onProcessReadyRead()
{
    if (m_process)
        m_stdout += m_process->readAllStandardOutput();
}

void AiEngine::onProcessErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart && m_running && !m_startFailed) {
        m_startFailed = true;
        const QString program = m_process ? m_process->program() : QStringLiteral("?");
        const QString hint = m_missingToolHint.isEmpty()
                                 ? QStringLiteral("The command is not installed or not on PATH.")
                                 : m_missingToolHint;
        fail(QStringLiteral("Could not start '%1'. %2").arg(program, hint));
    }
}

void AiEngine::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_running)
        return;
    // Drain anything still buffered. m_running is cleared by fail() /
    // finishWithRaw() below, which also emit exactly one signal.
    if (m_process) {
        m_stdout += m_process->readAllStandardOutput();
        m_stderr += m_process->readAllStandardError();
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString err = QString::fromUtf8(m_stderr).trimmed();
        fail(QStringLiteral("Engine '%1' exited with code %2.%3")
                 .arg(displayName())
                 .arg(exitCode)
                 .arg(err.isEmpty() ? QString() : QStringLiteral("\n\n") + err));
        return;
    }
    finishWithRaw(QString::fromUtf8(m_stdout));
}

void AiEngine::onTimeout()
{
    if (!m_running)
        return;
    fail(QStringLiteral("Timeout after %1 s: engine '%2' did not answer.")
             .arg(m_timeoutMs / 1000.0, 0, 'f', 0)
             .arg(displayName()));
}

void AiEngine::finishWithRaw(const QString &rawResponse)
{
    if (!m_running)
        return;
    stopTimer();
    m_running = false;
    const ParseResult parsed = parseProposal(rawResponse);
    if (parsed.ok)
        emit finished(parsed.proposal, rawResponse);
    else
        emit failed(QStringLiteral("Invalid response from engine '%1': %2")
                        .arg(displayName(), parsed.error));
}

void AiEngine::fail(const QString &message)
{
    if (!m_running)
        return;
    stopTimer();
    kill();
    m_running = false;
    emit failed(message);
}
