#include "skill.h"

#include <QCryptographicHash>
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

// SHA-256 of every SKILL.md an earlier release bundled. A file equal to one
// of them was never edited, so an upgrade replaces it without --force. Add
// the outgoing text's hash here whenever src/skill/SKILL.md changes.
static bool wasBundled(const QByteArray &text) {
    static const QByteArrayList released = {
        "84ee6058c8e97366ed9458ed86771a567ec3cc2d5a100df79fa8e03bc96f53d7",
        "6eac4cf7073004d35e146481b4b64a552ddd6b4ef731d3a426372ec9b5700221",
        "827619b7f5fa1e2a439a9b91a0e68cae74f2b9c15549a71dc4b05b304b95c803",
        "335860ffa5fffc2650255053cd00003b1f15e76c75829241bdbecb0ecec3cdb5",
        "9a5d827c6aca394ddd680d522c2b9aa2a9f18f65faf129acc3207cd44ea9f9dd",
    };
    return released.contains(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex());
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
            const QByteArray current = existing.readAll();
            if (current == md.toUtf8()) {
                r.alreadyThere.append(root);
                continue;
            }
            if (!force && !wasBundled(current)) {
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
        out.setDirectWriteFallback(false);
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
