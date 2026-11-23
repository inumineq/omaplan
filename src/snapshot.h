#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "task.h"

class Store;

// plan.json, the read-only edition's snapshot (docs/eksport.md,
// docs/eksport-skema.json). Written atomically (QSaveFile: write beside,
// rename) once after every committed change, whichever process made it.
namespace Snapshot {

// Where the snapshot goes: OMAPLAN_SNAPSHOT, else `path` under [snapshot]
// in config.toml, else plan.json beside the database (by default
// ~/.local/share/omaplan/plan.json). Empty for an in-memory database.
QString path(const QString &databasePath);

// Omarchy's current theme as the plan.json `tema` object; empty when no
// Omarchy theme is installed. `directory` defaults to Omarchy's current
// theme state (~/.local/state/omarchy/current).
QJsonObject omarchyTheme(const QString &directory = {});
// `tema` as inline CSS variables (--bg:#..;--accent:#..), for serve.
QString themeStyle(const QJsonObject &tema);

QJsonObject build(const QList<Task> &tasks, const QDateTime &exported, const QJsonObject &tema);
// Reads the plan from the store and writes it to `path` atomically.
bool write(Store *store, const QString &path, QString *error = nullptr);

} // namespace Snapshot

// Rewrites the snapshot after every outermost commit of `store`. A failed
// snapshot never fails the change itself; it is reported on stderr.
class Snapshotter : public QObject {
    Q_OBJECT
public:
    Snapshotter(Store *store, const QString &path, QObject *parent = nullptr);
    QString path() const { return m_path; }
    void write();

private:
    Store *m_store;
    QString m_path;
};
