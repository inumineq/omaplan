#include "snapshot.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include "config.h"
#include "store.h"
#include "theme.h"

namespace Snapshot {

QString path(const QString &databasePath) {
    const QByteArray override = qgetenv("OMAPLAN_SNAPSHOT");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    if (databasePath.isEmpty() || databasePath == QLatin1String(":memory:"))
        return {};
    // The configured path is the standard plan's; another database (--db,
    // OMAPLAN_DB) writes beside itself instead of overwriting it.
    const AppConfig config = AppConfig::load();
    if (!config.snapshotPath.isEmpty()
        && QFileInfo(databasePath).absoluteFilePath() == QFileInfo(Store::standardPath()).absoluteFilePath())
        return config.snapshotPath;
    return QFileInfo(databasePath).absoluteDir().filePath(QStringLiteral("plan.json"));
}

// A font family name safe to quote inside a style attribute and CSS:
// letters, digits, space, dot, underscore and hyphen (docs/export-schema.json).
static bool isFontFamily(const QString &family) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9 ._-]{1,64}$"));
    return safe.match(family).hasMatch();
}

QJsonObject omarchyTheme(const QString &directory) {
    const QString dir = directory.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/state/omarchy/current") : directory;
    if (!QFileInfo::exists(dir + QStringLiteral("/theme/colors.toml")))
        return {};
    // An explicit directory keeps Theme from polling hyprctl.
    const Theme theme(dir, nullptr, QString());
    auto hex = [](const QString &color) { return QColor(color).name(); };
    QJsonObject colors;
    colors["accent"] = hex(theme.accent());
    colors["accent_fg"] = hex(theme.accentForeground());
    colors["accent_text"] = hex(theme.accentText());
    colors["bg"] = hex(theme.background());
    colors["bg_raised"] = hex(theme.surface());
    colors["fg"] = hex(theme.foreground());
    colors["muted"] = hex(theme.muted());
    colors["border"] = hex(theme.border());
    colors["danger"] = hex(theme.danger());
    colors["ok"] = hex(theme.success());
    // Omarchy's font, when fontconfig knows a real family for monospace.
    const QString font = Theme::resolveFamily(QStringLiteral("monospace"));
    if (isFontFamily(font) && font.compare(QLatin1String("monospace"), Qt::CaseInsensitive) != 0)
        colors["font"] = font;
    return colors;
}

QString themeStyle(const QJsonObject &theme) {
    static const char *const keys[][2] = {{"bg", "--bg"},         {"bg_raised", "--bg-raised"}, {"fg", "--fg"},
                                          {"muted", "--muted"},   {"border", "--border"},       {"accent", "--accent"},
                                          {"accent_fg", "--accent-fg"}, {"accent_text", "--accent-text"},
                                          {"danger", "--danger"}, {"ok", "--ok"}};
    QStringList parts;
    for (const auto &key : keys) {
        const QString value = theme.value(QLatin1String(key[0])).toString();
        // Only #rrggbb gets through: the value lands inside a style attribute.
        if (value.size() == 7 && value.startsWith('#') && QColor::isValidColorName(value))
            parts << QLatin1String(key[1]) + ':' + value.toLower();
    }
    const QString font = theme.value(QLatin1String("font")).toString();
    if (isFontFamily(font))
        parts << QStringLiteral("--font:'") + font + '\'';
    return parts.join(';');
}

// ISO 8601 with offset and whole seconds: 2026-11-23T13:57:00+01:00
static QString withOffset(const QDateTime &value) {
    const QDateTime local = value.toLocalTime();
    const int offset = local.offsetFromUtc();
    const int minutes = std::abs(offset) / 60;
    return local.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
           + QStringLiteral("%1%2:%3")
                 .arg(offset < 0 ? '-' : '+')
                 .arg(minutes / 60, 2, 10, QLatin1Char('0'))
                 .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

static QString localIso(const QDateTime &value, bool hasTime) {
    return hasTime ? value.toString(QStringLiteral("yyyy-MM-ddTHH:mm")) : value.date().toString(Qt::ISODate);
}

// Task's internal guess names -> the names of docs/export-schema.json.
static QString guessKey(const QString &field) {
    if (field == QLatin1String("duration_minutes"))
        return QStringLiteral("duration_min");
    return field;
}

QJsonObject build(const QList<Task> &tasks, const QDateTime &exported, const QJsonObject &theme,
                  const QString &language) {
    QJsonArray items;
    for (const Task &task : tasks) {
        QJsonObject o;
        o["id"] = task.id;
        o["title"] = task.title;
        if (!task.notes.isEmpty())
            o["notes"] = task.notes;
        if (!task.project.isEmpty())
            o["project"] = task.project;
        o["priority"] = task.priority;
        o["status"] = Task::statusToString(task.status);
        o["start"] = task.start.isValid() ? QJsonValue(localIso(task.start, task.startHasTime)) : QJsonValue();
        if (task.durationMinutes > 0)
            o["duration_min"] = task.durationMinutes;
        if (task.deadline.isValid())
            o["deadline"] = localIso(task.deadline, task.deadlineHasTime);
        QJsonArray guesses;
        for (const QString &g : task.guesses)
            guesses.append(guessKey(g));
        o["guessed"] = guesses;
        o["source"] = task.captureId.isEmpty() ? QJsonValue() : QJsonValue(task.captureId);
        o["created"] = withOffset(task.created.isValid() ? task.created : exported);
        o["modified"] = withOffset(task.modified.isValid() ? task.modified : exported);
        items.append(o);
    }
    QJsonObject plan;
    plan["format"] = QStringLiteral("omaplan-plan");
    plan["version"] = 2;
    plan["exported"] = withOffset(exported);
    const QString zone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    plan["timezone"] = zone.isEmpty() ? QStringLiteral("UTC") : zone;
    plan["language"] = language;
    if (!theme.isEmpty())
        plan["theme"] = theme;
    plan["tasks"] = items;
    return plan;
}

bool write(Store *store, const QString &path, QString *error, const QString &language) {
    QString readError;
    const QList<Task> tasks = store->tasks(&readError);
    if (!readError.isEmpty()) {
        if (error) *error = readError;
        return false;
    }
    const QByteArray bytes = QJsonDocument(build(tasks, QDateTime::currentDateTime(), omarchyTheme(), language)).toJson();
    const QString dir = QFileInfo(path).absolutePath();
    if (!Store::makePrivateDirectory(dir)) {
        if (error) *error = QStringLiteral("cannot create directory %1").arg(dir);
        return false;
    }
    // Owner only: the plan is private, whatever the umask.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace Snapshot

Snapshotter::Snapshotter(Store *store, const QString &path, QObject *parent)
    : QObject(parent), m_store(store), m_path(path) {
    if (!m_path.isEmpty())
        connect(store, &Store::committed, this, &Snapshotter::write);
}

void Snapshotter::write() {
    QString error;
    if (!m_path.isEmpty() && !Snapshot::write(m_store, m_path, &error, m_language))
        QTextStream(stderr) << "omaplan: snapshot: " << error << "\n";
}
