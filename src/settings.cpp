#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "uilanguage.h"

QStringList Settings::languages() const { return UiLanguage::available(); }

Settings::Settings(UiLanguage *language, const QString &path, QObject *parent)
    : QObject(parent)
    , m_language(language)
    , m_path(path.isEmpty() ? AppConfig::defaultPath() : path)
{
    // Editors save by writing a new file and renaming it over the old one,
    // so the directory is watched too, and the file re-added after each change.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &Settings::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    if (m_language)
        connect(m_language, &UiLanguage::changed, this, &Settings::reload);
    reload();
}

void Settings::watch() {
    const QString dir = QFileInfo(m_path).absolutePath();
    if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
    if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);
}

void Settings::reload() {
    watch();
    const AppConfig config = AppConfig::load(m_path);
    const DateConvention convention =
        DateConvention::resolve(config, m_language ? m_language->language() : UiLanguage::current());
    DateConvention::setActive(convention);

    QVariantMap values;
    const ConfigFile file(m_path);
    for (const ConfigKey &key : ConfigKeys::all())
        values.insert(key.name, file.value(key.section, key.key));

    const bool same = values == m_values && convention == m_convention && config.projectColors == m_config.projectColors;
    const bool languageChanged = config.language != m_config.language;
    m_config = config;
    m_convention = convention;
    m_values = values;
    if (!same)
        emit changed();
    // `language` picked in the panel (or by `omaplan config set`) switches the
    // window at once; UiLanguage's changed() brings us back here once, with
    // the language unchanged, so there is no loop.
    if (languageChanged && m_language)
        m_language->reload();
}

QVariantMap Settings::projectColors() const {
    QVariantMap map;
    for (auto it = m_config.projectColors.cbegin(); it != m_config.projectColors.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

bool Settings::openInEditor() {
    if (!QFileInfo::exists(m_path)) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QFile file(m_path);
        if (file.open(QIODevice::WriteOnly))
            file.write("# Omaplan settings. See `omaplan config list` for every key.\n");
    }
    const QString editor = qEnvironmentVariable("EDITOR");
    if (!editor.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec")).isEmpty()) {
        // $EDITOR may carry arguments ("nvim -p"); it is split like a shell would.
        QStringList command = QProcess::splitCommand(editor);
        if (!command.isEmpty()) {
            command << m_path;
            if (QProcess::startDetached(QStringLiteral("xdg-terminal-exec"), command))
                return true;
        }
    }
    return QProcess::startDetached(QStringLiteral("xdg-open"), {m_path});
}
