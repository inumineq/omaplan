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
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        else if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
            value = value.mid(1, value.size() - 2);
        // Strip an inline comment outside quotes.
        if (!value.isEmpty() && value.contains(QLatin1Char('#')) && !value.contains(QLatin1Char('"')))
            value = value.left(value.indexOf(QLatin1Char('#'))).trimmed();

        if (section == QLatin1String("motor")) {
            if (key == QLatin1String("engine"))
                config.engine = value;
            else if (key == QLatin1String("command"))
                config.command = value;
        } else if (section == QLatin1String("motor.lokal")) {
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
