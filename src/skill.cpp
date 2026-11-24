#include "skill.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Skill {

QString name()
{
    return QStringLiteral("omaplan");
}

QString markdown()
{
    QFile f(QStringLiteral(":/skill/SKILL.md"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

static QStringList baseNames()
{
    return {QStringLiteral(".claude"), QStringLiteral(".codex"), QStringLiteral(".vibe"),
            QStringLiteral(".config/opencode")};
}

QStringList candidateBases(const QString &home)
{
    const QDir dir(home);
    QStringList existing;
    for (const QString &base : baseNames()) {
        if (dir.exists(base))
            existing.append(dir.filePath(base));
    }
    return existing;
}

QStringList candidateRoots(const QString &home)
{
    QStringList roots;
    for (const QString &base : candidateBases(home))
        roots.append(QDir(base).filePath(QStringLiteral("skills")));
    // Only roots that actually exist (uninstall must not create anything).
    QStringList existing;
    for (const QString &r : roots) {
        if (QDir(r).exists())
            existing.append(r);
    }
    return existing;
}

static QString skillFilePath(const QString &root)
{
    return QDir(root).filePath(name() + QStringLiteral("/SKILL.md"));
}

Result install(const QString &home, bool force)
{
    Result r;
    const QString md = markdown();
    if (md.isEmpty()) {
        r.error = QStringLiteral("The bundled skill (:/skill/SKILL.md) is missing from the binary.");
        return r;
    }
    // Install into every agent home dir that exists; create its skills/
    // subdir when missing (e.g. ~/.claude exists but ~/.claude/skills does not).
    const QStringList bases = candidateBases(home);
    if (bases.isEmpty()) {
        r.error = QStringLiteral("No agent directories found (looked in ~/.claude, ~/.codex, "
                                 "~/.vibe, ~/.config/opencode).");
        return r;
    }
    for (const QString &base : bases) {
        const QString root = QDir(base).filePath(QStringLiteral("skills"));
        const QString target = skillFilePath(root);
        QFile existing(target);
        if (existing.exists()) {
            if (!existing.open(QIODevice::ReadOnly)) {
                r.error = QStringLiteral("Could not read %1: %2").arg(target, existing.errorString());
                return r;
            }
            if (existing.readAll() == md.toUtf8()) {
                r.alreadyThere.append(root);
                continue;
            }
            if (!force) {
                r.conflicts.append(target);
                continue;
            }
        }
        // Atomic write: QSaveFile writes a temp file and renames on commit.
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            r.error = QStringLiteral("Could not create directory for %1").arg(target);
            return r;
        }
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            r.error = QStringLiteral("Could not write %1: %2").arg(target, out.errorString());
            return r;
        }
        const QByteArray content = md.toUtf8();
        if (out.write(content) != content.size()) {
            r.error = QStringLiteral("Could not write %1: %2").arg(target, out.errorString());
            out.cancelWriting();
            return r;
        }
        if (!out.commit()) {
            r.error = QStringLiteral("Could not write %1: %2").arg(target, out.errorString());
            return r;
        }
        r.installed.append(root);
    }
    r.ok = r.conflicts.isEmpty() && ( !r.installed.isEmpty() || !r.alreadyThere.isEmpty());
    if (!r.conflicts.isEmpty())
        r.error = QStringLiteral("Customized skill files were preserved; rerun with --force to replace them: %1")
                      .arg(r.conflicts.join(QStringLiteral(", ")));
    if (!r.ok && r.conflicts.isEmpty())
        r.error = QStringLiteral("No agent skill directories found under the home directory.");
    return r;
}

Result uninstall(const QString &home)
{
    Result r;
    r.ok = true;
    const QStringList roots = candidateRoots(home);
    for (const QString &root : roots) {
        const QString target = skillFilePath(root);
        if (!QFile::exists(target)) {
            r.alreadyThere.append(root); // nothing installed here
            continue;
        }
        if (!QFile::remove(target)) {
            r.ok = false;
            r.error = QStringLiteral("Could not remove %1").arg(target);
            return r;
        }
        // Remove the skill dir too when it is now empty.
        QDir().rmdir(QFileInfo(target).absolutePath());
        r.removed.append(root);
    }
    return r;
}

} // namespace Skill
