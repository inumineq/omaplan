#include "store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

static const int kSchemaVersion = 1;

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

bool Store::migrate(QString *error) {
    const int version = schemaVersion();
    if (version > kSchemaVersion) {
        if (error) *error = QStringLiteral("database schema version %1 is newer than this build (%2)")
                                .arg(version).arg(kSchemaVersion);
        return false;
    }
    if (version == kSchemaVersion)
        return true;

    // Version 0 is an empty (or brand new) database: create the tables.
    QSqlQuery query(m_db);
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
    query.prepare(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
    if (!query.exec()) {
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

bool Store::recordChange(const QString &source, const QString &kind, const QString &taskId,
                         const Task *before, const Task *after) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO changes (at, source, kind, task_id, before_json, after_json) VALUES (?,?,?,?,?,?)"));
    query.addBindValue(isoNow());
    query.addBindValue(source);
    query.addBindValue(kind);
    query.addBindValue(taskId);
    query.addBindValue(before ? QString::fromUtf8(QJsonDocument(before->toJson()).toJson(QJsonDocument::Compact))
                               : QString());
    query.addBindValue(after ? QString::fromUtf8(QJsonDocument(after->toJson()).toJson(QJsonDocument::Compact))
                              : QString());
    return query.exec();
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

    if (!m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return {};
    }
    if (!writeTask(task, false, error) || !recordChange(source, QStringLiteral("create"), task.id, nullptr, &task)) {
        m_db.rollback();
        return {};
    }
    if (!m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
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
    Task before;
    if (!this->task(task.id, &before, error))
        return false;
    task.created = before.created;
    task.modified = QDateTime::currentDateTime();

    if (!m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    if (!writeTask(task, true, error) || !recordChange(source, QStringLiteral("update"), task.id, &before, &task)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool Store::removeTask(const QString &id, const QString &source, QString *error) {
    Task before;
    if (!task(id, &before, error))
        return false;

    if (!m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tasks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !recordChange(source, QStringLiteral("delete"), id, &before, nullptr)) {
        if (error) *error = query.lastError().text().isEmpty() ? m_db.lastError().text() : query.lastError().text();
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
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
        if (error) *error = QStringLiteral("no task with id %1").arg(id);
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

QString Store::addCapture(const QString &rawText, const QString &engine, const QString &model,
                          const QString &response, QString *error) {
    const QString id = Task::newId();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO captures (id, raw_text, engine, model, response, created) VALUES (?,?,?,?,?,?)"));
    query.addBindValue(id);
    query.addBindValue(rawText);
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
        capture.engine = query.value("engine").toString();
        capture.model = query.value("model").toString();
        capture.response = query.value("response").toString();
        capture.created = QDateTime::fromString(query.value("created").toString(), Qt::ISODateWithMs);
        result.append(capture);
    }
    return result;
}

QList<Store::Change> Store::changes(int limit, QString *error) const {
    QList<Change> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM changes ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        Change change;
        change.id = query.value("id").toLongLong();
        change.at = QDateTime::fromString(query.value("at").toString(), Qt::ISODateWithMs);
        change.source = query.value("source").toString();
        change.kind = query.value("kind").toString();
        change.taskId = query.value("task_id").toString();
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
        result.append(change);
    }
    return result;
}
