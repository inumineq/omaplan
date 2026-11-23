#pragma once

#include <QFileSystemWatcher>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

class QCoreApplication;

// The language of the interface (window, CLI text output, error messages and
// date formats) and the Qt translators that carry it. Translations are Qt
// Linguist files, i18n/omaplan_<lang>.ts, compiled into the binary as
// :/i18n/omaplan_<lang>.qm; a language is available when its .qm is there
// (English always is: it is the source language).
//
// The language is the first of these that names an available language:
//   1. --lang on the command line
//   2. `language` in config.toml
//   3. Omarchy's language (~/.config/omarchy/language)
//   4. the system locale (QLocale::system().uiLanguages())
//   5. English
// A source naming a language without a translation is passed over, so
// "language = fr" falls through to the next source instead of to English.
//
// While the window is open, a change of the Omarchy language switches the
// interface live (when nothing earlier in the list decides).
class UiLanguage : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)

public:
    struct Sources {
        QString flag;               // --lang
        QString config;             // config.toml language
        QString omarchy;            // first line of the Omarchy language file
        QStringList system;         // QLocale::system().uiLanguages()
    };

    // `flag` is the --lang value (empty = none). `omarchyFile` empty = the
    // default path; `watch` follows that file for live switching.
    UiLanguage(QCoreApplication *app, const QString &flag, bool watch, const QString &omarchyFile = {},
               QObject *parent = nullptr);
    ~UiLanguage() override;

    QString language() const { return m_language; }

    // The language picked last by any UiLanguage in this process, or, when
    // none was made, the one the sources give without a flag. For code that
    // needs the language but has no UiLanguage at hand (the AI prompt, the
    // plan.json snapshot).
    static QString current();

    // Pure precedence, for tests: the first source naming one of `available`
    // ("da_DK", "da-DK" and "DA" all name "da"), else "en".
    static QString resolve(const Sources &sources, const QStringList &available);
    // Languages with an embedded translation, always including "en".
    static QStringList available();
    // The language code a text names, e.g. "da" for "da_DK.UTF-8"; empty when
    // it names none.
    static QString normalize(const QString &text);
    // The locale dates and numbers are formatted with in that language.
    static QLocale formatLocale(const QString &language);
    static QString defaultOmarchyFile();
    static QString readOmarchyLanguage(const QString &path);

signals:
    void changed();

private:
    void reload();
    void install(const QString &language);

    QCoreApplication *m_app;
    QString m_flag;
    QString m_omarchyFile;
    QString m_language;
    bool m_watch;
    QTranslator m_english;  // the plural forms of the English source strings
    QTranslator m_translator;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
