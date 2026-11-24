#pragma once

#include <QFileSystemWatcher>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

class QCoreApplication;
class Translator;

// The language of the interface (window, CLI text output, error messages and
// date formats). The catalogs and the per-string fallback to English are the
// Translator's, the one every Omarchy app carries (src/translator.h):
// src/i18n/<code>.json, compiled into the binary as :/i18n/<code>.json. This
// class decides which language the Translator gets.
//
// The language is the first of these that names one:
//   1. --lang on the command line
//   2. `language` in config.toml
//   3. Omarchy's language (~/.config/omarchy/language)
//   4. the system locale (QLocale::system().uiLanguages()), when it names a
//      language with a catalog
//   5. English
// The first three are choices, so they count whether or not the language has
// a catalog: its strings show in English until it gets one, while dates,
// numbers and the layout direction already follow it, as in every Omarchy
// app. The system locale is only a guess, so it needs a catalog to count.
//
// While the window is open, a change of the Omarchy language switches the
// interface live (when nothing earlier in the list decides).
class UiLanguage : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)
    // The locale dates and numbers are formatted with, for Qt.locale().
    Q_PROPERTY(QString localeName READ localeName NOTIFY changed)

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
    QString localeName() const { return formatLocale(m_language).name(); }

    // The language picked last by any UiLanguage in this process, or, when
    // none was made, the one the sources give without a flag. For code that
    // needs the language but has no UiLanguage at hand (the AI prompt, the
    // plan.json snapshot).
    static QString current();

    // Pure precedence, for tests ("da_DK", "da-DK" and "DA" all name "da").
    static QString resolve(const Sources &sources, const QStringList &available);
    // Languages with a catalog, always including "en".
    static QStringList available();
    // The language code a text names, e.g. "da" for "da_DK.UTF-8"; empty when
    // it names none.
    static QString normalize(const QString &text);
    // The locale dates and numbers are formatted with in that language.
    static QLocale formatLocale(const QString &language);
    static QString defaultOmarchyFile();
    static QString readOmarchyLanguage(const QString &path);

    // Picks the language again from every source; emits changed() only when
    // it differs. Settings calls it when `language` in config.toml changes.
    void reload();

signals:
    void changed();

private:
    void install(const QString &language);

    QCoreApplication *m_app;
    QString m_flag;
    QString m_omarchyFile;
    QString m_language;
    bool m_watch;
    // The Translator reads its language from a file, so the pick is written
    // to a private one, and a new Translator made for each pick.
    std::unique_ptr<QTemporaryFile> m_pick;
    std::unique_ptr<Translator> m_translator;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
