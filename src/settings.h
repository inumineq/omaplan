#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "config.h"
#include "datewords.h"

class UiLanguage;

// config.toml as the window sees it, followed live: a change made in the
// settings panel, by `omaplan config set` or by hand in an editor reaches
// the window through the file. Sets the process's date convention
// (DateConvention::setActive) from the file and the interface language.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(bool dayFirst READ dayFirst NOTIFY changed)
    Q_PROPERTY(bool dottedIsTime READ dottedIsTime NOTIFY changed)
    Q_PROPERTY(int weekStart READ weekStart NOTIFY changed)   // 1 = Monday, 7 = Sunday
    Q_PROPERTY(bool clock24 READ clock24 NOTIFY changed)
    // project name -> palette slot ("blue")
    Q_PROPERTY(QVariantMap projectColors READ projectColors NOTIFY changed)
    // Every setting by its dotted name ("engine.claude.model") -> the value in
    // the file, "" when the file does not set it.
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    // For the panel's choices: the interface languages there are
    // translations for, and the palette slots in their fixed order.
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QStringList paletteSlots READ paletteSlots CONSTANT)

public:
    // `path` empty = AppConfig::defaultPath().
    explicit Settings(UiLanguage *language, const QString &path = {}, QObject *parent = nullptr);

    QString path() const { return m_path; }
    bool dayFirst() const { return m_convention.dayFirst; }
    bool dottedIsTime() const { return m_convention.dottedIsTime; }
    int weekStart() const { return m_convention.weekStart; }
    bool clock24() const { return m_convention.clock24; }
    QVariantMap projectColors() const;
    QVariantMap values() const { return m_values; }
    QStringList languages() const;
    QStringList paletteSlots() const { return ConfigKeys::paletteSlots(); }
    const AppConfig &config() const { return m_config; }

    // Opens config.toml in the user's editor: $EDITOR in a terminal
    // (xdg-terminal-exec), or xdg-open without $EDITOR. Creates the file
    // (with a comment) when it does not exist yet. False when nothing could
    // be started.
    Q_INVOKABLE bool openInEditor();

public slots:
    void reload();

signals:
    void changed();

private:
    void watch();

    UiLanguage *m_language;
    QString m_path;
    AppConfig m_config;
    DateConvention m_convention;
    QVariantMap m_values;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
