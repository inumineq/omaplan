// The Omarchy language-file watching follows the Omacut/Monologue app
// templates; see LICENSE for the attribution.
#include "uilanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "config.h"

static QString s_current;

UiLanguage::UiLanguage(QCoreApplication *app, const QString &flag, bool watch, const QString &omarchyFile,
                       QObject *parent)
    : QObject(parent), m_app(app), m_flag(flag),
      m_omarchyFile(omarchyFile.isEmpty() ? defaultOmarchyFile() : omarchyFile), m_watch(watch) {
    if (watch) {
        m_debounce.setSingleShot(true);
        m_debounce.setInterval(80);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
        connect(&m_debounce, &QTimer::timeout, this, &UiLanguage::reload);
    }
    reload();
}

UiLanguage::~UiLanguage() {
    if (m_app) {
        m_app->removeTranslator(&m_translator);
        m_app->removeTranslator(&m_english);
    }
}

QString UiLanguage::defaultOmarchyFile() {
    return QDir::homePath() + QStringLiteral("/.config/omarchy/language");
}

QString UiLanguage::readOmarchyLanguage(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

QString UiLanguage::normalize(const QString &text) {
    static const QRegularExpression code(QStringLiteral("^([A-Za-z]{2,3})(?:[-_.@].*)?$"));
    const auto m = code.match(text.trimmed());
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QStringList UiLanguage::available() {
    QStringList languages{QStringLiteral("en")};
    QDirIterator it(QStringLiteral(":/i18n"), {QStringLiteral("omaplan_*.qm")}, QDir::Files);
    while (it.hasNext()) {
        const QString name = QFileInfo(it.next()).completeBaseName().mid(int(qstrlen("omaplan_")));
        if (!languages.contains(name))
            languages << name;
    }
    return languages;
}

QString UiLanguage::resolve(const Sources &sources, const QStringList &available) {
    QStringList candidates{sources.flag, sources.config, sources.omarchy};
    candidates << sources.system;
    for (const QString &candidate : candidates) {
        const QString code = normalize(candidate);
        if (!code.isEmpty() && available.contains(code))
            return code;
    }
    return QStringLiteral("en");
}

QString UiLanguage::current() {
    if (!s_current.isEmpty())
        return s_current;
    return resolve({{}, AppConfig::load().language, readOmarchyLanguage(defaultOmarchyFile()),
                    QLocale::system().uiLanguages()},
                   available());
}

QLocale UiLanguage::formatLocale(const QString &language) {
    // English with day-month order and a 24-hour clock, like the window's
    // own formats; every other language as Qt knows it.
    if (language == QLatin1String("en"))
        return QLocale(QLocale::English, QLocale::UnitedKingdom);
    return QLocale(language);
}

void UiLanguage::reload() {
    if (m_watch) {
        // omarchy-language replaces the file, which drops it from the watch,
        // and a first pick creates it; watch its directory too and re-arm.
        const auto paths = m_watcher.files() + m_watcher.directories();
        if (!paths.isEmpty())
            m_watcher.removePaths(paths);
        const QString directory = QFileInfo(m_omarchyFile).absolutePath();
        if (QFileInfo::exists(directory))
            m_watcher.addPath(directory);
        if (QFileInfo::exists(m_omarchyFile))
            m_watcher.addPath(m_omarchyFile);
    }

    const QString language = resolve({m_flag, AppConfig::load().language, readOmarchyLanguage(m_omarchyFile),
                                      QLocale::system().uiLanguages()},
                                     available());
    if (language == m_language)
        return;
    install(language);
    m_language = language;
    s_current = language;
    QLocale::setDefault(formatLocale(language));
    emit changed();
}

void UiLanguage::install(const QString &language) {
    if (!m_app)
        return;
    m_app->removeTranslator(&m_translator);
    m_app->removeTranslator(&m_english);
    // English first, so a string the language lacks still gets English
    // plural forms; the language goes on top (Qt asks the newest first).
    if (m_english.load(QStringLiteral(":/i18n/omaplan_en.qm")))
        m_app->installTranslator(&m_english);
    if (language != QLatin1String("en")
        && m_translator.load(QStringLiteral(":/i18n/omaplan_%1.qm").arg(language)))
        m_app->installTranslator(&m_translator);
}
