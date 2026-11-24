// The Omarchy language-file watching follows the Omacut/Monologue app
// templates; see LICENSE for the attribution.
#include "uilanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include "config.h"
#include "translator.h"

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

// The Translator unregisters itself from the application as it goes.
UiLanguage::~UiLanguage() = default;

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
    // "pt_BR.UTF-8" and "pt-br" are pt-BR: Omarchy's and the catalogs' spelling.
    static const QRegularExpression code(QStringLiteral("^([A-Za-z]{2,3})(?:[-_]([A-Za-z]{2}))?(?:[-_.@].*)?$"));
    const auto m = code.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    const QString language = m.captured(1).toLower();
    return m.captured(2).isEmpty() ? language : language + QLatin1Char('-') + m.captured(2).toUpper();
}

QStringList UiLanguage::available() {
    QStringList languages{QStringLiteral("en")};
    QDirIterator it(QStringLiteral(":/i18n"), {QStringLiteral("*.json")}, QDir::Files);
    while (it.hasNext()) {
        const QString name = QFileInfo(it.next()).completeBaseName();
        if (!languages.contains(name))
            languages << name;
    }
    languages.sort();
    return languages;
}

QString UiLanguage::resolve(const Sources &sources, const QStringList &available) {
    // A regional catalog (pt-BR) by its full code, else the language's own.
    const auto catalog = [&available](const QString &code) {
        const QString language = code.section(QLatin1Char('-'), 0, 0);
        return available.contains(code) ? code : available.contains(language) ? language : QString();
    };
    for (const QString &choice : {sources.flag, sources.config, sources.omarchy}) {
        const QString code = normalize(choice);
        if (code.isEmpty())
            continue;
        // Without a catalog the choice still counts, region and all: its
        // formats and direction follow it.
        const QString found = catalog(code);
        return found.isEmpty() ? code : found;
    }
    for (const QString &guess : sources.system) {
        const QString found = catalog(normalize(guess));
        if (!found.isEmpty())
            return found;
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
    // own formats; every other language as Omarchy formats it.
    if (language == QLatin1String("en"))
        return QLocale(QLocale::English, QLocale::UnitedKingdom);
    return Translator::formatLocale(language);
}

void UiLanguage::reload() {
    if (m_watch) {
        // omarchy-language replaces the file, which drops it from the watch,
        // and a first pick creates it, and maybe its directory; watch the
        // nearest existing directory too and re-arm.
        const auto paths = m_watcher.files() + m_watcher.directories();
        if (!paths.isEmpty())
            m_watcher.removePaths(paths);
        QString directory = QFileInfo(m_omarchyFile).absolutePath();
        while (!QFileInfo::exists(directory) && directory != QLatin1String("/"))
            directory = QFileInfo(directory).absolutePath();
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
    emit changed();
}

void UiLanguage::install(const QString &language) {
    // The Translator reads the choice from a file and watches its folder, so
    // the file gets a folder of its own (owner only): in the shared /tmp every
    // other program's file would wake the app, and a cleanup of old files
    // could take the choice away from a window left open for days.
    if (!m_pickDirectory) {
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        m_pickDirectory = runtime.isEmpty() ? std::make_unique<QTemporaryDir>()
                                            : std::make_unique<QTemporaryDir>(runtime + QStringLiteral("/omaplan-XXXXXX"));
    }
    auto pick = std::make_unique<QFile>(m_pickDirectory->filePath(QStringLiteral("language")));
    if (pick->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pick->write(language.toUtf8() + '\n');
        pick->close();
    }
    // The Translator loads its catalogs, and sets the default locale and the
    // layout direction, as it is made. The old one goes after the new one is
    // in, so no string is asked for with neither.
    auto translator = std::make_unique<Translator>(pick->fileName());
    if (m_app)
        m_app->installTranslator(translator.get());
    m_translator = std::move(translator);
    m_pick = std::move(pick);
    QLocale::setDefault(formatLocale(language));
}
