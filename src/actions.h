#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <map>
#include <memory>
#include <QString>

#include "engine.h"
#include "store.h"
#include "task.h"

struct ActionResult {
    bool ok = false;
    QString error;
    QString text;         // human-readable output (already translated)
    QJsonObject payload;  // machine-readable output for --json
};

struct ListFilter {
    bool hasFrom = false;
    QDate from;
    bool hasTo = false;
    QDate to;
    QString project;      // empty = all
    QString status;       // empty = all, "todo"/"done"
};

// Every change to the plan goes through here — CLI, MCP and QML alike — so the
// three frontends can never behave differently.
class Actions : public QObject {
    Q_OBJECT
public:
    explicit Actions(Store *store, QObject *parent = nullptr) : QObject(parent), m_store(store) {}

    ActionResult create(const Task &task, const QString &source);
    ActionResult update(const QString &id, const QJsonObject &patch, const QString &source);
    ActionResult move(const QString &id, const QDateTime &when, bool hasTime, const QString &source);
    // `move <id> <when>` from the CLI and MCP: `when` as text; without a time
    // in it the task keeps its own ("move <id> monday" keeps 14:00).
    ActionResult moveTo(const QString &id, const QString &when, const QString &source);
    ActionResult setDone(const QString &id, bool done, const QString &source);
    ActionResult remove(const QString &id, const QString &source);
    // Inverts the newest change that no undo has inverted yet, so undo works
    // across processes (GUI, CLI, MCP share the database).
    ActionResult undo(const QString &source);
    // Re-applies the newest undo that no redo has re-applied yet.
    ActionResult redo(const QString &source);

    ActionResult list(const ListFilter &filter);
    ActionResult show(const QString &id);
    ActionResult projects();
    ActionResult summary(bool week); // true = this week (from dates.week_start), false = this month

    // Pure date parsing, no I/O: ISO dates and datetimes plus Danish/English
    // expressions ("i morgen", "fredag 12", "tomorrow", "next monday", with an
    // optional time like "kl. 14:30"). `today` is the reference date, so tests
    // can pass a fixed one.
    // Upcoming (the default) reads a date as its next occurrence: "monday",
    // "the 3rd" or "1 november" never lie before today. InPeriod reads it in
    // today's week, month or year instead, for filters: "monday" is this
    // week's, "1 november" this year's. "next ..." is upcoming either way.
    enum class DateReading { Upcoming, InPeriod };
    static bool parseDateTime(const QString &text, const QDate &today, QDateTime *out, bool *hasTime,
                              QString *error = nullptr, DateReading reading = DateReading::Upcoming);
    // A --from/--to date (DateReading::InPeriod), without the time.
    static bool parseFilterDate(const QString &text, const QDate &today, QDate *out, QString *error);

    // Task fields by their CLI/MCP names (title, notes, project, priority,
    // start, duration, deadline, status), as text: dates take everything
    // parseDateTime accepts, an empty value clears a field. Shared by the
    // CLI, the MCP server and the window, so all accept exactly the same
    // input. The Danish names of 0.1 (titel, noter, projekt,
    // prioritet, varighed, frist) are accepted as aliases.
    using Fields = QHash<QString, QString>;
    static const QHash<QString, QString> &legacyFieldNames();
    static bool taskFromFields(const Fields &fields, Task *task, QString *error);
    static bool patchFromFields(const Fields &fields, QJsonObject *patch, QString *error);

    // QML-friendly wrappers (source "gui"); each returns
    // {"ok": bool, "error": string, "text": string, "payload": object}.
    Q_INVOKABLE QJsonObject guiUpdate(const QString &id, const QVariantMap &patch);
    Q_INVOKABLE QJsonObject guiMove(const QString &id, const QDateTime &when, bool hasTime);
    Q_INVOKABLE QJsonObject guiSetDone(const QString &id, bool done);
    Q_INVOKABLE QJsonObject guiRemove(const QString &id);
    Q_INVOKABLE QJsonObject guiUndo();
    Q_INVOKABLE QJsonObject guiRedo();
    Q_INVOKABLE QJsonObject guiProjects();
    // The task as JSON (payload.task), for the editor.
    Q_INVOKABLE QJsonObject guiTask(const QString &id);
    // Edit and create from the editor's text fields, by their CLI names
    // (title, start, deadline, duration, ...), so the window accepts the same
    // date expressions as the CLI.
    Q_INVOKABLE QJsonObject guiEdit(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE QJsonObject guiNew(const QVariantMap &fields);
    // Live preview while typing a date: {ok, error, date (ISO), hasTime}.
    Q_INVOKABLE QJsonObject guiParseDate(const QString &text) const;

    // Sends free text to an AI engine (engineChoice empty = config default). The
    // raw text and the date-annotated variant are stored in `captures` with
    // engine and model. Used by `omaplan add` (synchronous, with event loop).
    ActionResult propose(const QString &rawText, const QString &engineChoice = QString(),
                         const QString &modelOverride = QString(), const QString &endpointOverride = QString(),
                         QString *captureIdOut = nullptr);
    // Creates tasks from a proposal JSON (schema keys: tasks/title/guessed/...)
    // and links them to the capture. Used by `omaplan add --yes` and the GUI.
    ActionResult approve(const QJsonObject &proposalJson, const QString &captureId, const QString &source);

    // Async variant for QML: results arrive as proposalReady/proposalFailed,
    // so the GUI never blocks.
    Q_INVOKABLE void guiPropose(const QString &text, const QString &engineChoice = QString());
    Q_INVOKABLE void guiCancelProposal();
    // Compare: the same text to the two engines of `compare` in
    // config.toml (default vibe and claude) at once. Each side answers with
    // compareReady/compareFailed (side 0 or 1) and stores its own capture,
    // so both answers are kept whichever one is approved.
    Q_INVOKABLE void guiCompare(const QString &text);
    // Display names for the window: {"default": "...", "compare": ["...", "..."]}.
    Q_INVOKABLE QJsonObject guiEngineNames() const;

    // config.toml (actions_config.cpp), the same for `omaplan config`, MCP and
    // the settings panel. Keys are ConfigKeys names ("engine.claude.model",
    // "projects.Blog"); an empty value (or "auto" for dates.*) removes the
    // key, so the default applies again. Comments and unknown keys stay.
    static ActionResult configList();
    static ActionResult configGet(const QString &key);
    static ActionResult configSet(const QString &key, const QString &value);
    // Each engine: whether its tool is on PATH (or its endpoint/command is
    // set), and the model or endpoint it would use. Runs nothing.
    static ActionResult engines();
    Q_INVOKABLE QJsonObject guiConfigSet(const QString &key, const QString &value);
    Q_INVOKABLE QJsonObject guiEngines() const;
    // The settings panel's Test button, and only that: one short real
    // request to the engine, answered by engineTested. Stores nothing.
    Q_INVOKABLE void guiTestEngine(const QString &id);
    Q_INVOKABLE QJsonObject guiApprove(const QJsonObject &proposalJson, const QString &captureId);

signals:
    // Emitted after every mutation, so models and views can reload.
    void planChanged();
    void proposalReady(const QJsonObject &proposalJson, const QString &captureId);
    void proposalFailed(const QString &message);
    void compareReady(int side, const QJsonObject &proposalJson, const QString &captureId, const QString &engine);
    void compareFailed(int side, const QString &message, const QString &engine);
    // ok: the engine answered with valid JSON; message: what it proposed, or
    // the error; ms: how long it took.
    void engineTested(const QString &id, bool ok, const QString &message, int ms);

private:
    ActionResult locked(const std::function<ActionResult()> &body);
    ActionResult updateLocked(const QString &id, const QJsonObject &patch, const QString &source);
    ActionResult moveLocked(const QString &id, const QDateTime &when, bool hasTime, const QString &source);
    ActionResult runEngine(const QString &rawText, const QString &engineChoice, const QString &modelOverride,
                           const QString &endpointOverride, QString *captureIdOut, bool wait);

    // Builds and starts an engine for the GUI; on success `onReady` gets the
    // proposal and the new capture id, otherwise `onFailed` the message.
    // Cancelled runs report nothing.
    std::unique_ptr<AiEngine> startGuiEngine(const QString &engineChoice, const QString &text,
                                             std::function<void(const QJsonObject &, const QString &)> onReady,
                                             std::function<void(const QString &)> onFailed);

    Store *m_store;
    std::unique_ptr<AiEngine> m_engine;
    std::unique_ptr<AiEngine> m_compare[2];
    std::map<QString, std::unique_ptr<AiEngine>> m_tests; // engine id -> its running Test
};
