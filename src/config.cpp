#include "config.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString AppConfig::defaultPath() {
    const QByteArray override = qgetenv("OMAPLAN_CONFIG");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath("omaplan/config.toml");
}

// Minimal TOML subset: [section] headers and key = "value" lines. Comments
// (# ...) and blank lines are ignored; quotes around values are optional.
AppConfig AppConfig::load(const QString &path) {
    AppConfig config;
    QFile file(path.isEmpty() ? defaultPath() : path);
    if (!file.open(QIODevice::ReadOnly))
        return config; // no config file: defaults
    QString section;
    const QString text = QString::fromUtf8(file.readAll());
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        // A quoted value ends at its closing quote, so a comment after it
        // ("vibe"   # default engine) is dropped; an unquoted value ends at #.
        // In "double quotes" \" and \\ are escapes, as in TOML ('single'
        // quotes are literal).
        if (!value.isEmpty() && value.front() == QLatin1Char('"')) {
            QString unescaped;
            for (int i = 1; i < value.size() && value.at(i) != QLatin1Char('"'); ++i) {
                if (value.at(i) == QLatin1Char('\\') && i + 1 < value.size()
                    && (value.at(i + 1) == QLatin1Char('"') || value.at(i + 1) == QLatin1Char('\\')))
                    ++i;
                unescaped += value.at(i);
            }
            value = unescaped;
        } else if (!value.isEmpty() && value.front() == QLatin1Char('\'')) {
            const int close = value.indexOf(value.front(), 1);
            value = close > 0 ? value.mid(1, close - 1) : value.mid(1);
        } else if (value.contains(QLatin1Char('#'))) {
            value = value.left(value.indexOf(QLatin1Char('#'))).trimmed();
        }

        // Engine ids of 0.1: "lokal" is now "local".
        const auto engineId = [](const QString &id) {
            return id == QLatin1String("lokal") ? QStringLiteral("local") : id;
        };
        if (section.isEmpty()) {
            if (key == QLatin1String("language"))
                config.language = value.toLower();
        } else if (section == QLatin1String("engine") || section == QLatin1String("motor")) {
            if (key == QLatin1String("default") || key == QLatin1String("engine"))
                config.engine = engineId(value);
            else if (key == QLatin1String("command"))
                config.command = value;
            else if (key == QLatin1String("compare")) {
                // compare = "vibe, claude": the two engines of Compare.
                const QStringList ids = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (ids.size() == 2)
                    config.compare = {engineId(ids.at(0).trimmed()), engineId(ids.at(1).trimmed())};
            }
        } else if (section == QLatin1String("snapshot")) {
            if (key == QLatin1String("path"))
                config.snapshotPath = value.startsWith(QLatin1String("~/")) ? QDir::homePath() + value.mid(1) : value;
        } else if (section == QLatin1String("engine.local") || section == QLatin1String("motor.lokal")) {
            if (key == QLatin1String("api"))
                config.localApi = value.toLower();
            else if (key == QLatin1String("endpoint"))
                config.localEndpoint = value;
            else if (key == QLatin1String("model"))
                config.localModel = value;
        }
    }
    return config;
}
