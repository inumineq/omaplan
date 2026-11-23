#include "store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

static const int kSchemaVersion = 5;

// The database lives under the user's data directory; OMAPLAN_DB overrides the
// whole path so tests and one-off CLI runs can use a temporary file.
QString Store::defaultPath() {
    const QByteArray override = qgetenv("OMAPLAN_DB");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("omaplan.db");
}

Store::Store(const QString &path, QObject *parent)
    : QObject(parent),
      m_path(path.isEmpty() ? defaultPath() : path),
      m_connectionName(QStringLiteral("omaplan-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
}

Store::~Store() {
    close();
}

void Store::close() {
    if (m_db.isValid()) {
        const QString name = m_connectionName;
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

bool Store::open(QString *error) {
    if (m_db.isOpen())
        return true;

    if (m_path != QLatin1String(":memory:")) {
        const QDir dir = QFileInfo(m_path).absoluteDir();
        if (!dir.exists() && !dir.mkpath(".")) {
            if (error) *error = QStringLiteral("cannot create directory %1").arg(dir.path());
            return false;
        }
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        close();
        return false;
    }

    // WAL plus a busy timeout are what make concurrent writers from separate
    // processes (GUI, CLI, MCP) safe instead of failing with "database is
    // locked".
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    return migrate(error);
}

int Store::schemaVersion() const {
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("PRAGMA user_version"));
    if (query.next())
        return query.value(0).toInt();
    return -1;
}

int Store::dataVersion(bool *ok) const {
    if (!m_db.isOpen()) {
        if (ok) *ok = false;
        return -1;
    }
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA data_version")) || !query.next()) {
        if (ok) *ok = false;
        return -1;
    }
    if (ok) *ok = true;
    return query.value(0).toInt();
}

bool Store::migrate(QString *error) {
    int version = schemaVersion();
    if (version > kSchemaVersion) {
        if (error) *error = QStringLiteral("database schema version %1 is newer than this build (%2)")
                                .arg(version).arg(kSchemaVersion);
        return false;
    }

    QSqlQuery query(m_db);
    while (version < kSchemaVersion) {
        if (version == 0) {
            // Version 1: the three tables (see below).
            if (!createTables(query, error))
                return false;
            version = 1;
        } else if (version == 1) {
            // Version 2: changes.undo_of links an undo to the change it
            // inverted, so repeated undo walks back through the history
            // instead of inverting the same change twice.
            if (!query.exec(QStringLiteral("ALTER TABLE changes ADD COLUMN undo_of INTEGER"))) {
                if (error) *error = query.lastError().text();
                return false;
            }
            version = 2;
        } else if (version == 2) {
            // Version 3: changes.redo_of marks an undo as re-applied by a
            // redo, so redo walks forward through the undos.
            if (!query.exec(QStringLiteral("ALTER TABLE changes ADD COLUMN redo_of INTEGER"))) {
                if (error) *error = query.lastError().text();
                return false;
            }
            version = 3;
        } else if (version == 3) {
            // Version 4: captures.annotated_text keeps the date-annotated
            // text next to the raw input, so a proposal stays traceable.
            if (!query.exec(QStringLiteral(
                    "ALTER TABLE captures ADD COLUMN annotated_text TEXT NOT NULL DEFAULT ''"))) {
                if (error) *error = query.lastError().text();
                return false;
            }
            version = 4;
        } else if (version == 4) {
            // Version 5: changes.batch groups changes made as one step (an
            // approved AI proposal), so undo/redo handle them together.
            if (!query.exec(QStringLiteral("ALTER TABLE changes ADD COLUMN batch TEXT"))) {
                if (error) *error = query.lastError().text();
                return false;
            }
            version = 5;
        } else {
            if (error) *error = QStringLiteral("unknown schema version %1").arg(version);
            return false;
        }
        query.prepare(QStringLiteral("PRAGMA user_version=%1").arg(version));
        if (!query.exec()) {
            if (error) *error = query.lastError().text();
            return false;
        }
    }
    return true;
}

bool Store::createTables(QSqlQuery &query, QString *error) {
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT NOT NULL,"
            "  notes TEXT NOT NULL DEFAULT '',"
            "  project TEXT NOT NULL DEFAULT '',"
            "  priority INTEGER NOT NULL DEFAULT 3,"
            "  status TEXT NOT NULL DEFAULT 'todo',"
            "  start TEXT,"
            "  start_has_time INTEGER NOT NULL DEFAULT 0,"
            "  duration_min INTEGER NOT NULL DEFAULT 0,"
            "  deadline TEXT,"
            "  deadline_has_time INTEGER NOT NULL DEFAULT 0,"
            "  guesses TEXT NOT NULL DEFAULT '[]',"
            "  capture_id TEXT NOT NULL DEFAULT '',"
            "  created TEXT NOT NULL,"
            "  modified TEXT NOT NULL"
            ")"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS captures ("
            "  id TEXT PRIMARY KEY,"
            "  raw_text TEXT NOT NULL,"
            "  engine TEXT NOT NULL DEFAULT '',"
            "  model TEXT NOT NULL DEFAULT '',"
            "  response TEXT NOT NULL DEFAULT '',"
            "  created TEXT NOT NULL"
            ")"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS changes ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  at TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  kind TEXT NOT NULL,"
            "  task_id TEXT NOT NULL,"
            "  before_json TEXT,"
            "  after_json TEXT"
            ")"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

static QString isoNow() {
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

// A default-constructed QString is null, which would violate NOT NULL text
// columns; store a real empty string instead.
static QString nn(const QString &text) {
    return text.isNull() ? QStringLiteral("") : text;
}

static QString isoOf(const QDateTime &dateTime) {
    return dateTime.isValid() ? dateTime.toString(Qt::ISODateWithMs) : QString();
}

Task Store::rowToTask(const QSqlQuery &query) const {
    Task task;
    task.id = query.value("id").toString();
    task.title = query.value("title").toString();
    task.notes = query.value("notes").toString();
    task.project = query.value("project").toString();
    task.priority = query.value("priority").toInt();
    bool statusOk = false;
    task.status = Task::statusFromString(query.value("status").toString(), &statusOk);
    Q_UNUSED(statusOk);
    task.startHasTime = query.value("start_has_time").toBool();
    const QString startText = query.value("start").toString();
    task.start = task.startHasTime ? QDateTime::fromString(startText, Qt::ISODateWithMs)
                                   : QDateTime(QDate::fromString(startText, Qt::ISODate), QTime(0, 0));
    task.durationMinutes = query.value("duration_min").toInt();
    task.deadlineHasTime = query.value("deadline_has_time").toBool();
    const QString deadlineText = query.value("deadline").toString();
    task.deadline = task.deadlineHasTime ? QDateTime::fromString(deadlineText, Qt::ISODateWithMs)
                                         : QDateTime(QDate::fromString(deadlineText, Qt::ISODate), QTime(0, 0));
    const QJsonArray guesses = QJsonDocument::fromJson(query.value("guesses").toByteArray()).array();
    for (const auto &value : guesses)
        task.guesses.append(value.toString());
    task.captureId = query.value("capture_id").toString();
    task.created = QDateTime::fromString(query.value("created").toString(), Qt::ISODateWithMs);
    task.modified = QDateTime::fromString(query.value("modified").toString(), Qt::ISODateWithMs);
    return task;
}

bool Store::writeTask(const Task &task, bool replace, QString *error) {
    const QString startText = task.start.isValid()
            ? (task.startHasTime ? task.start.toString(Qt::ISODateWithMs)
                                 : task.start.date().toString(Qt::ISODate))
            : QString();
    const QString deadlineText = task.deadline.isValid()
            ? (task.deadlineHasTime ? task.deadline.toString(Qt::ISODateWithMs)
                                    : task.deadline.date().toString(Qt::ISODate))
            : QString();
    QJsonArray guesses;
    for (const QString &guess : task.guesses)
        guesses.append(guess);
    const QString guessesJson = QString::fromUtf8(QJsonDocument(guesses).toJson(QJsonDocument::Compact));

    QSqlQuery query(m_db);
    if (replace) {
        query.prepare(QStringLiteral(
            "UPDATE tasks SET title=?, notes=?, project=?, priority=?, status=?, start=?,"
            " start_has_time=?, duration_min=?, deadline=?, deadline_has_time=?, guesses=?,"
            " capture_id=?, modified=? WHERE id=?"));
        query.addBindValue(nn(task.title));
        query.addBindValue(nn(task.notes));
        query.addBindValue(nn(task.project));
        query.addBindValue(task.priority);
        query.addBindValue(Task::statusToString(task.status));
        query.addBindValue(startText);
        query.addBindValue(task.startHasTime ? 1 : 0);
        query.addBindValue(task.durationMinutes);
        query.addBindValue(deadlineText);
        query.addBindValue(task.deadlineHasTime ? 1 : 0);
        query.addBindValue(guessesJson);
        query.addBindValue(nn(task.captureId));
        query.addBindValue(isoNow());
        query.addBindValue(task.id);
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO tasks (id, title, notes, project, priority, status, start,"
            " start_has_time, duration_min, deadline, deadline_has_time, guesses,"
            " capture_id, created, modified) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(task.id);
        query.addBindValue(nn(task.title));
        query.addBindValue(nn(task.notes));
        query.addBindValue(nn(task.project));
        query.addBindValue(task.priority);
        query.addBindValue(Task::statusToString(task.status));
        query.addBindValue(startText);
        query.addBindValue(task.startHasTime ? 1 : 0);
        query.addBindValue(task.durationMinutes);
        query.addBindValue(deadlineText);
        query.addBindValue(task.deadlineHasTime ? 1 : 0);
        query.addBindValue(guessesJson);
        query.addBindValue(nn(task.captureId));
        query.addBindValue(isoOf(task.created));
        query.addBindValue(isoOf(task.modified));
    }
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

bool Store::begin(QString *error) {
    if (m_txDepth > 0) {
        ++m_txDepth;
        return true;
    }
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    m_txDepth = 1;
    return true;
}

bool Store::commit(QString *error) {
    if (m_txDepth == 0) {
        if (error) *error = QStringLiteral("commit without an open transaction");
        return false;
    }
    if (--m_txDepth > 0)
        return true;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("COMMIT"))) {
        if (error) *error = query.lastError().text();
        QSqlQuery(m_db).exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    emit committed();
    return true;
}

void Store::rollback() {
    if (m_txDepth == 0)
        return;
    m_txDepth = 0;
    QSqlQuery(m_db).exec(QStringLiteral("ROLLBACK"));
}

bool Store::insertTask(const Task &task, QString *error) {
    const QString validation = task.validate();
    if (!validation.isEmpty()) {
        if (error) *error = validation;
        return false;
    }
    return writeTask(task, false, error);
}

bool Store::replaceTask(const Task &task, QString *error) {
    const QString validation = task.validate();
    if (!validation.isEmpty()) {
        if (error) *error = validation;
        return false;
    }
    Task existing;
    if (!this->task(task.id, &existing, error))
        return false;
    Task updated = task;
    updated.created = existing.created; // created survives every update
    if (!writeTask(updated, true, error))
        return false;
    return true;
}

bool Store::deleteTask(const QString &id, QString *error) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tasks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0 || true;
}

qlonglong Store::logChange(const QString &source, const QString &kind, const QString &taskId,
                           const Task *before, const Task *after, qlonglong undoOf) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO changes (at, source, kind, task_id, before_json, after_json, undo_of, batch)"
        " VALUES (?,?,?,?,?,?,?,?)"));
    query.addBindValue(isoNow());
    query.addBindValue(source);
    query.addBindValue(kind);
    query.addBindValue(taskId);
    query.addBindValue(before ? QString::fromUtf8(QJsonDocument(before->toJson()).toJson(QJsonDocument::Compact))
                               : QString());
    query.addBindValue(after ? QString::fromUtf8(QJsonDocument(after->toJson()).toJson(QJsonDocument::Compact))
                              : QString());
    query.addBindValue(undoOf > 0 ? QVariant(undoOf) : QVariant());
    query.addBindValue(m_batch.isEmpty() ? QVariant() : QVariant(m_batch));
    if (!query.exec())
        return 0;
    return query.lastInsertId().toLongLong();
}

bool Store::markRedone(qlonglong undoId, qlonglong redoId, QString *error) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE changes SET redo_of=? WHERE id=?"));
    query.addBindValue(redoId);
    query.addBindValue(undoId);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

QString Store::createTask(const Task &input, const QString &source, QString *error) {
    Task task = input;
    if (task.id.isEmpty())
        task.id = Task::newId();
    const QString validation = task.validate();
    if (!validation.isEmpty()) {
        if (error) *error = validation;
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (!task.created.isValid())
        task.created = now;
    task.modified = now;

    if (!begin(error))
        return {};
    if (!insertTask(task, error) || !logChange(source, QStringLiteral("create"), task.id, nullptr, &task)) {
        rollback();
        return {};
    }
    if (!commit(error)) {
        rollback();
        return {};
    }
    return task.id;
}

bool Store::updateTask(const Task &input, const QString &source, QString *error) {
    Task task = input;
    const QString validation = task.validate();
    if (!validation.isEmpty()) {
        if (error) *error = validation;
        return false;
    }
    if (!begin(error))
        return false;
    Task before;
    if (!this->task(task.id, &before, error)) {
        rollback();
        return false;
    }
    task.created = before.created;
    task.modified = QDateTime::currentDateTime();
    if (!writeTask(task, true, error) || !logChange(source, QStringLiteral("update"), task.id, &before, &task)) {
        rollback();
        return false;
    }
    if (!commit(error)) {
        rollback();
        return false;
    }
    return true;
}

bool Store::removeTask(const QString &id, const QString &source, QString *error) {
    if (!begin(error))
        return false;
    Task before;
    if (!task(id, &before, error)) {
        rollback();
        return false;
    }
    if (!deleteTask(id, error) || !logChange(source, QStringLiteral("delete"), id, &before, nullptr)) {
        rollback();
        return false;
    }
    if (!commit(error)) {
        rollback();
        return false;
    }
    return true;
}

bool Store::task(const QString &id, Task *out, QString *error) const {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM tasks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        if (error) *error = tr("No task with id %1").arg(id);
        return false;
    }
    if (out)
        *out = rowToTask(query);
    return true;
}

QList<Task> Store::tasks(QString *error) const {
    QList<Task> result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT * FROM tasks ORDER BY CASE WHEN start IS NULL OR start='' THEN 1 ELSE 0 END,"
            " start, created"))) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next())
        result.append(rowToTask(query));
    return result;
}

QString Store::addCapture(const QString &rawText, const QString &annotatedText, const QString &engine,
                          const QString &model, const QString &response, QString *error) {
    const QString id = Task::newId();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO captures (id, raw_text, annotated_text, engine, model, response, created)"
        " VALUES (?,?,?,?,?,?,?)"));
    query.addBindValue(id);
    query.addBindValue(rawText);
    query.addBindValue(annotatedText);
    query.addBindValue(engine);
    query.addBindValue(model);
    query.addBindValue(response);
    query.addBindValue(isoNow());
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return {};
    }
    return id;
}

QList<Store::Capture> Store::captures(QString *error) const {
    QList<Capture> result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT * FROM captures ORDER BY created"))) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        Capture capture;
        capture.id = query.value("id").toString();
        capture.rawText = query.value("raw_text").toString();
        capture.annotatedText = query.value("annotated_text").toString();
        capture.engine = query.value("engine").toString();
        capture.model = query.value("model").toString();
        capture.response = query.value("response").toString();
        capture.created = QDateTime::fromString(query.value("created").toString(), Qt::ISODateWithMs);
        result.append(capture);
    }
    return result;
}

Store::Change Store::rowToChange(const QSqlQuery &query) {
    Change change;
    change.id = query.value("id").toLongLong();
    change.at = QDateTime::fromString(query.value("at").toString(), Qt::ISODateWithMs);
    change.source = query.value("source").toString();
    change.kind = query.value("kind").toString();
    change.taskId = query.value("task_id").toString();
    change.undoOf = query.value("undo_of").toLongLong();
    change.redoOf = query.value("redo_of").toLongLong();
    change.batch = query.value("batch").toString();
    const QString beforeJson = query.value("before_json").toString();
    const QString afterJson = query.value("after_json").toString();
    bool ok = false;
    if (!beforeJson.isEmpty()) {
        change.before = Task::fromJson(QJsonDocument::fromJson(beforeJson.toUtf8()).object(), &ok);
        change.hasBefore = ok;
    }
    if (!afterJson.isEmpty()) {
        change.after = Task::fromJson(QJsonDocument::fromJson(afterJson.toUtf8()).object(), &ok);
        change.hasAfter = ok;
    }
    return change;
}

bool Store::selectChanges(const QString &sql, const QVariantList &binds, QList<Change> *out,
                          QString *error) const {
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &value : binds)
        query.addBindValue(value);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    while (query.next())
        out->append(rowToChange(query));
    return true;
}

QList<Store::Change> Store::changes(int limit, QString *error) const {
    QList<Change> result;
    selectChanges(QStringLiteral("SELECT * FROM changes ORDER BY id DESC LIMIT ?"), {limit}, &result, error);
    return result;
}

bool Store::newestChange(Change *out, QString *error) const {
    const QList<Change> newest = changes(1, error);
    if (newest.isEmpty())
        return false;
    *out = newest.first();
    return true;
}

// Expands the newest candidate to its whole batch (same filter), newest first.
bool Store::undoTargets(QList<Change> *out, QString *error) const {
    static const QString notUndone = QStringLiteral(
        "kind <> 'undo' AND id NOT IN (SELECT undo_of FROM changes WHERE undo_of IS NOT NULL)");
    QList<Change> newest;
    if (!selectChanges(QStringLiteral("SELECT * FROM changes WHERE %1 ORDER BY id DESC LIMIT 1").arg(notUndone),
                       {}, &newest, error))
        return false;
    if (newest.isEmpty()) {
        if (error) *error = tr("Nothing to undo");
        return false;
    }
    if (newest.first().batch.isEmpty()) {
        *out = newest;
        return true;
    }
    return selectChanges(QStringLiteral("SELECT * FROM changes WHERE %1 AND batch = ? ORDER BY id DESC").arg(notUndone),
                         {newest.first().batch}, out, error);
}

bool Store::redoTargets(QList<Change> *out, QString *error) const {
    // Undos older than the newest original change are no longer redoable.
    static const QString redoable = QStringLiteral(
        "kind = 'undo' AND redo_of IS NULL AND id > (SELECT COALESCE(MAX(id), 0) FROM changes"
        " WHERE kind IN ('create', 'update', 'delete'))");
    QList<Change> newest;
    if (!selectChanges(QStringLiteral("SELECT * FROM changes WHERE %1 ORDER BY id DESC LIMIT 1").arg(redoable),
                       {}, &newest, error))
        return false;
    if (newest.isEmpty()) {
        if (error) *error = tr("Nothing to redo");
        return false;
    }
    if (newest.first().batch.isEmpty()) {
        *out = newest;
        return true;
    }
    return selectChanges(QStringLiteral("SELECT * FROM changes WHERE %1 AND batch = ? ORDER BY id DESC").arg(redoable),
                         {newest.first().batch}, out, error);
}
