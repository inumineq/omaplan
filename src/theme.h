#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The rest of Omarchy's colors.toml, so no view hardcodes a color.
    // Missing keys fall back to a dark default palette.
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)     // panels, cards
    Q_PROPERTY(QString raised READ raised NOTIFY changed)       // hover, selection
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString danger READ danger NOTIFY changed)       // overdue, errors
    Q_PROPERTY(QString success READ success NOTIFY changed)
    // The accent as a text colour: the theme's accent, lifted until it reads
    // on the background. Fills and borders keep the exact accent.
    Q_PROPERTY(QString accentText READ accentText NOTIFY changed)
    // Omarchy's font (omarchy-font-set: fontconfig's monospace), by its real
    // family. Followed live once followSystemFont() is called; until then,
    // the generic monospace.
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontChanged)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }

    static QString readAccent(const QString &path);
    // key -> "#rrggbb" (or text for "mode") from a colors.toml.
    static QHash<QString, QString> readColors(const QString &path);

    bool dark() const { return color("mode", "dark") != QLatin1String("light"); }
    QString background() const { return color("background", dark() ? "#121214" : "#f6f6f4"); }
    QString surface() const;
    QString raised() const;
    QString border() const;
    QString foreground() const { return color("foreground", dark() ? "#e6e6e6" : "#1d1d1f"); }
    QString muted() const { return m_muted; }
    QString danger() const { return m_danger; }
    QString success() const { return m_success; }
    QString accentText() const { return m_accentText; }
    QString fontFamily() const { return m_fontFamily; }
    // A palette slot of colors.toml ("blue", "bright_red"; "color4" as an
    // alias) as a text colour: the theme's own, lifted like the other text
    // colours until it reads on the background. Empty for an unknown slot.
    // Project colours name these, so they follow a theme switch.
    Q_INVOKABLE QString paletteColor(const QString &slot) const;

    // Follows Omarchy's font from now on, as the application's font and
    // through fontFamily: omarchy-font-set rewrites
    // $XDG_CONFIG_HOME/fontconfig/fonts.conf, which is watched.
    void followSystemFont();
    // The family fontconfig gives for `family` (an alias like monospace) as
    // its configuration on disk says now, or `family` when it gives none.
    static QString resolveFamily(const QString &family);
    // WCAG contrast ratio of two colours, 1 to 21.
    static double contrast(const QString &a, const QString &b);

signals:
    void changed();
    void fontChanged();

private:
    QString color(const char *key, const char *fallback) const;
    QString readable(const QString &color) const;
    void deriveTextColors();
    void reload();
    void readSystemFont();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QHash<QString, QString> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
    QString m_muted, m_danger, m_success, m_accentText;
    QHash<QString, QString> m_palette; // slot -> readable colour
    QString m_fontFamily = "monospace";
    bool m_followingFont = false;
    QFileSystemWatcher m_fontWatcher;
    QTimer m_fontDebounce;
};
