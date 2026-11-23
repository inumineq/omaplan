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
    QString muted() const;
    QString danger() const { return color("red", "#e5534b"); }
    QString success() const { return color("green", "#57ab5a"); }

signals:
    void changed();

private:
    QString color(const char *key, const char *fallback) const;
    void reload();
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
};
