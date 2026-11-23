#pragma once

#include <QString>
#include <QStringList>

// The bundled agent skill (src/skill/SKILL.md, embedded as :/skill/SKILL.md)
// plus `omaplan skill install/uninstall`: the skill is copied into every
// agent directory that exists under the user's home (~/.claude/skills,
// ~/.codex/skills, ~/.vibe/skills, ~/.config/opencode/skills), as
// <root>/omaplan/SKILL.md. Writes are atomic (temp file + rename).
// `home` defaults to QDir::homePath(), so HOME can be overridden in tests.
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

// Existing candidate roots under `home` (only dirs that exist are returned).
QStringList candidateRoots(const QString &home);

Result install(const QString &home);
Result uninstall(const QString &home);

} // namespace Skill
