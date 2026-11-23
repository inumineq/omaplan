#include "snapshot.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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
    const AppConfig config = AppConfig::load();
    if (!config.snapshotPath.isEmpty())
        return config.snapshotPath;
    if (databasePath.isEmpty() || databasePath == QLatin1String(":memory:"))
        return {};
    return QFileInfo(databasePath).absoluteDir().filePath(QStringLiteral("plan.json"));
}

QJsonObject omarchyTheme(const QString &directory) {
    const QString dir = directory.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/state/omarchy/current") : directory;
    if (!QFileInfo::exists(dir + QStringLiteral("/theme/colors.toml")))
        return {};
    // An explicit directory keeps Theme from polling hyprctl.
    const Theme theme(dir, nullptr, QString());
    auto hex = [](const QString &color) { return QColor(color).name(); };
    QJsonObject tema;
    tema["accent"] = hex(theme.accent());
    tema["accent_fg"] = hex(theme.accentForeground());
    tema["bg"] = hex(theme.background());
    tema["bg_raised"] = hex(theme.surface());
    tema["fg"] = hex(theme.foreground());
    tema["muted"] = hex(theme.muted());
    tema["border"] = hex(theme.border());
    tema["danger"] = hex(theme.danger());
    tema["ok"] = hex(theme.success());
    return tema;
}

QString themeStyle(const QJsonObject &tema) {
    static const char *const keys[][2] = {{"bg", "--bg"},         {"bg_raised", "--bg-raised"}, {"fg", "--fg"},
                                          {"muted", "--muted"},   {"border", "--border"},       {"accent", "--accent"},
                                          {"accent_fg", "--accent-fg"}, {"danger", "--danger"}, {"ok", "--ok"}};
    QStringList parts;
    for (const auto &key : keys) {
        const QString value = tema.value(QLatin1String(key[0])).toString();
        // Only #rrggbb gets through: the value lands inside a style attribute.
        if (value.size() == 7 && value.startsWith('#') && QColor::isValidColorName(value))
            parts << QLatin1String(key[1]) + ':' + value.toLower();
    }
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

// Task's internal guess names -> the ASCII names of docs/eksport-skema.json.
static QString guessKey(const QString &field) {
    if (field == QLatin1String("duration_minutes")) return QStringLiteral("varighed_min");
    if (field == QLatin1String("deadline")) return QStringLiteral("frist");
    if (field == QLatin1String("project")) return QStringLiteral("projekt");
    if (field == QLatin1String("priority")) return QStringLiteral("prioritet");
    return field;
}

QJsonObject build(const QList<Task> &tasks, const QDateTime &exported, const QJsonObject &tema) {
    QJsonArray items;
    for (const Task &task : tasks) {
        QJsonObject o;
        o["id"] = task.id;
        o["titel"] = task.title;
        if (!task.notes.isEmpty())
            o["noter"] = task.notes;
        if (!task.project.isEmpty())
            o["projekt"] = task.project;
        o["prioritet"] = task.priority;
        o["status"] = Task::statusToString(task.status);
        o["start"] = task.start.isValid() ? QJsonValue(localIso(task.start, task.startHasTime)) : QJsonValue();
        if (task.durationMinutes > 0)
            o["varighed_min"] = task.durationMinutes;
        if (task.deadline.isValid())
            o["frist"] = localIso(task.deadline, task.deadlineHasTime);
        QJsonArray guesses;
        for (const QString &g : task.guesses)
            guesses.append(guessKey(g));
        o["gaet"] = guesses;
        o["kilde"] = task.captureId.isEmpty() ? QJsonValue() : QJsonValue(task.captureId);
        o["oprettet"] = withOffset(task.created.isValid() ? task.created : exported);
        o["aendret"] = withOffset(task.modified.isValid() ? task.modified : exported);
        items.append(o);
    }
    QJsonObject plan;
    plan["format"] = QStringLiteral("omaplan-plan");
    plan["version"] = 1;
    plan["eksporteret"] = withOffset(exported);
    const QString zone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    plan["tidszone"] = zone.isEmpty() ? QStringLiteral("UTC") : zone;
    plan["sprog"] = QStringLiteral("da"); // the read-only edition is Danish (web/MARKUP.md)
    if (!tema.isEmpty())
        plan["tema"] = tema;
    plan["opgaver"] = items;
    return plan;
}

bool write(Store *store, const QString &path, QString *error) {
    QString readError;
    const QList<Task> tasks = store->tasks(&readError);
    if (!readError.isEmpty()) {
        if (error) *error = readError;
        return false;
    }
    const QByteArray bytes = QJsonDocument(build(tasks, QDateTime::currentDateTime(), omarchyTheme())).toJson();
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("cannot create directory %1").arg(dir.path());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
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
    if (!m_path.isEmpty() && !Snapshot::write(m_store, m_path, &error))
        QTextStream(stderr) << "omaplan: snapshot: " << error << "\n";
}
