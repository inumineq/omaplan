#pragma once

#include "prompt.h"
#include "proposal.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <memory>

class QTimer;

// Abstract AI engine. Subclasses (vibe, claude, ollama, command) implement
// start() and deliver the raw response via finishWithRaw() or an error via
// fail(). propose() is asynchronous: the result arrives as finished() or
// failed(). A timeout (default 90 s) and cancel() are handled here, so every
// engine behaves the same and the GUI never freezes.
class AiEngine : public QObject {
    Q_OBJECT
public:
    static constexpr int defaultTimeoutMs = 90 * 1000;

    explicit AiEngine(QObject *parent = nullptr);
    ~AiEngine() override;

    virtual QString id() const = 0;        // stable id, e.g. "vibe"
    virtual QString displayName() const = 0;

    // False when a required external tool is missing (e.g. "vibe" not on
    // PATH); whyNot then says what is missing. Engines that need no external
    // tool (ollama is checked at request time) return true.
    virtual bool available(QString *whyNot = nullptr) const;

    void propose(const QString &text, const PromptContext &context = PromptContext());
    void cancel();
    bool isRunning() const { return m_running; }

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }

signals:
    void finished(const Proposal &proposal, const QString &rawResponse);
    void failed(const QString &message);

protected:
    // Called by propose() with the built prompts. Must eventually call
    // finishWithRaw() or fail() exactly once.
    virtual void start(const QString &systemPrompt, const QString &userPrompt) = 0;

    // Stops any subprocess/network request. Called by cancel() and on timeout.
    virtual void kill();

    // For QProcess-based engines: starts program with arguments, feeds
    // stdinData on stdin, and routes stdout to finishWithRaw() / stderr +
    // exit code to fail(). missingToolHint makes a FailedToStart a clear
    // "tool not installed" error instead of a generic crash message.
    QProcess *launch(const QString &program, const QStringList &arguments,
                     const QByteArray &stdinData, const QString &missingToolHint);

    void finishWithRaw(const QString &rawResponse);
    void fail(const QString &message);

    // Declares an external binary this engine needs; used by available().
    void setRequiredTool(const QString &toolName);

private slots:
    void onTimeout();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    void stopTimer();

    bool m_running = false;
    int m_timeoutMs = defaultTimeoutMs;
    QTimer *m_timer = nullptr;
    QProcess *m_process = nullptr;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QString m_requiredTool;
    QString m_missingToolHint;
    bool m_startFailed = false;
};

// Factories; the concrete classes live in their own engine_*.cpp files.
std::unique_ptr<AiEngine> createVibeEngine(QObject *parent = nullptr);
std::unique_ptr<AiEngine> createClaudeEngine(QObject *parent = nullptr);
std::unique_ptr<AiEngine> createOllamaEngine(const QString &model = QString(), QObject *parent = nullptr);
std::unique_ptr<AiEngine> createCommandEngine(const QString &commandLine, QObject *parent = nullptr);
