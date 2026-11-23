#pragma once

#include <QString>
#include <QStringList>

// The bundled agent skill (src/skill/SKILL.md, embedded as :/skill/SKILL.md)
// plus `omaplan skill install/uninstall`: the skill is copied into every
// agent's home directory that exists under the user's home (~/.claude,
// ~/.codex, ~/.vibe, ~/.config/opencode), as <home>/<base>/skills/omaplan/
// SKILL.md — the skills/ subdir is created when missing. Writes are atomic
// (temp file + rename). `home` defaults to QDir::homePath(), so HOME can be
// overridden in tests.
namespace Skill {

struct Result {
    bool ok = false;
    QStringList installed; // install: dirs written; uninstall: not used
    QStringList removed;   // uninstall: dirs cleaned
    QStringList alreadyThere; // install: unchanged; uninstall: nothing to remove
    QString error;
};

QString name();       // "omaplan"
QString markdown();   // the bundled SKILL.md text

// Agent home dirs that exist under `home` (~/.claude, ~/.codex, ~/.vibe,
// ~/.config/opencode) — install creates their skills/ subdir when missing.
QStringList candidateBases(const QString &home);
// Existing <base>/skills roots under `home` (used by uninstall).
QStringList candidateRoots(const QString &home);

Result install(const QString &home);
Result uninstall(const QString &home);

} // namespace Skill
