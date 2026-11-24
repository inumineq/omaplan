// Theme watching follows the Omacut/Monologue app templates; see LICENSE for
// the attribution.
#include "theme.h"
#include "config.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <fontconfig/fontconfig.h>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl, const QString &hyprlandEvents)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory),
      m_shellConfig(directory.isEmpty() ? QDir::homePath() + "/.config/omarchy/shell.toml"
                                        : directory + "/shell.toml") {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    deriveTextColors();

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    const bool hyprland = directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl : hyprland ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code == 0 && status == QProcess::NormalExit && value.isDouble() && value.toInt(-1) >= 0
            && value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
        if (m_roundingAgain) {
            m_roundingAgain = false;
            refreshRounding();
        }
    });

    // Hyprland announces a reload on its event socket once the new values are
    // in effect (a theme switch, the user's own config), so ask again then
    // rather than polling.
    const QString events = !hyprlandEvents.isEmpty() ? hyprlandEvents
                         : hyprland ? qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/"
                                          + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE") + "/.socket2.sock"
                                    : QString();
    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        while (m_events.canReadLine())
            if (m_events.readLine().startsWith("configreloaded>>"))
                refreshRounding();
    });
    // Asked again on every connection, since a reload may have come between.
    connect(&m_events, &QLocalSocket::connected, this, [this] {
        m_reconnect.setInterval(1000);
        refreshRounding();
    });
    // A socket that can't be reached or drops is tried again, less often each time.
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(1000);
    connect(&m_events, &QLocalSocket::disconnected, &m_reconnect, qOverload<>(&QTimer::start));
    connect(&m_events, &QLocalSocket::errorOccurred, &m_reconnect, qOverload<>(&QTimer::start));
    connect(&m_reconnect, &QTimer::timeout, this, [this, events] {
        m_reconnect.setInterval(std::min(m_reconnect.interval() * 2, 60000));
        m_events.abort();
        m_events.connectToServer(events, QIODevice::ReadOnly);
    });
    if (!events.isEmpty())
        m_events.connectToServer(events, QIODevice::ReadOnly);
    refreshRounding();
    reload();
}

Theme::~Theme() {
    // Nothing reconnects or asks again while the members go.
    m_events.disconnect();
    m_roundingProcess.disconnect();
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty())
        return;
    // A query still running may have read the value from before the reload.
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingAgain = true;
        return;
    }
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

int Theme::readBaseSize(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    bool inFont = false;
    static const QRegularExpression section(QStringLiteral("^\\s*\\[([^\\]]*)\\]"));
    static const QRegularExpression baseSize(QStringLiteral("^\\s*base-size\\s*=\\s*([0-9]+)"));
    for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (const auto match = section.match(line); match.hasMatch())
            inFont = match.captured(1).trimmed() == QLatin1String("font");
        else if (const auto match = baseSize.match(line); inFont && match.hasMatch())
            return match.captured(1).toInt();
    }
    return 0;
}

QString Theme::readAccent(const QString &path) {
    return readColors(path).value(QStringLiteral("accent"), QStringLiteral("#FFD60A"));
}

// Every key = "#rrggbb" in a colors.toml, quoted or bare, with or without a
// trailing comment, and whether the theme is light: its mode, a light.mode
// file beside it, or failing both a light background, as Omarchy decides it.
QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression entry(
        QStringLiteral("^\\s*([a-z0-9_]+)\\s*=\\s*([\"']?)(#[0-9a-fA-F]{6}|light|dark)\\2\\s*(#.*)?$"));
    QHash<QString, QString> modes;
    while (!file.atEnd()) {
        const auto match = entry.match(QString::fromUtf8(file.readLine()).trimmed());
        if (!match.hasMatch())
            continue;
        const QString key = match.captured(1), value = match.captured(3);
        (value.startsWith('#') ? colors : modes).insert(key, value);
    }
    if (colors.isEmpty())
        return colors;
    // Older and hand-made themes name some colours differently. Each key
    // takes the first of its other names the theme has, in the order
    // omarchy-theme-color (Omarchy's full resolver) tries them.
    static const QList<QStringList> aliases{
        {"background", "bg", "color0"}, {"foreground", "fg", "color7"}, {"red", "color1"},
        {"green", "color2"}, {"yellow", "color3"}, {"blue", "color4"}, {"magenta", "color5", "purple"},
        {"cyan", "color6"},
    };
    for (const QStringList &names : aliases)
        for (const QString &name : names.mid(1))
            if (!colors.contains(names.first()) && colors.contains(name))
                colors.insert(names.first(), colors.value(name));

    QString mode = modes.value(QStringLiteral("mode"), modes.value(QStringLiteral("theme_type")));
    if (mode.isEmpty() && QFileInfo::exists(QFileInfo(path).absolutePath() + "/light.mode"))
        mode = QStringLiteral("light");
    if (mode.isEmpty() && colors.contains(QStringLiteral("background"))) {
        const QColor background(colors.value(QStringLiteral("background")));
        mode = background.red() + background.green() + background.blue() > 382 ? QStringLiteral("light")
                                                                               : QStringLiteral("dark");
    }
    if (!mode.isEmpty())
        colors.insert(QStringLiteral("mode"), mode);
    return colors;
}

QString Theme::color(const char *key, const char *fallback) const {
    const QString value = m_colors.value(QLatin1String(key));
    if (QLatin1String(key) == QLatin1String("mode"))
        return value.isEmpty() ? QString::fromLatin1(fallback) : value;
    return QColor::isValidColorName(value) ? value : QString::fromLatin1(fallback);
}

// Mixes two colors; t = 0 gives a, 1 gives b.
static QString mix(const QString &a, const QString &b, double t) {
    const QColor x(a), y(b);
    return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t, x.greenF() + (y.greenF() - x.greenF()) * t,
                            x.blueF() + (y.blueF() - x.blueF()) * t).name();
}

QString Theme::surface() const {
    const QString lighter = color("lighter_background", "");
    return lighter.isEmpty() ? mix(background(), foreground(), 0.06) : mix(background(), lighter, 0.6);
}

QString Theme::raised() const { return mix(background(), foreground(), 0.13); }
QString Theme::border() const { return mix(background(), foreground(), 0.18); }

// WCAG relative luminance.
static double luminance(const QColor &c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

double Theme::contrast(const QString &a, const QString &b) {
    const double x = luminance(QColor(a)), y = luminance(QColor(b));
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}

// `color` as text: unchanged when it reads on the background and on panels
// (WCAG AA, 4.5:1), otherwise moved toward white (dark themes) or black
// (light ones) just far enough. Omarchy's palettes are terminal colours, and
// their dim ones (muted, often red or green on light themes) are too faint
// for small text.
QString Theme::readable(const QString &color) const {
    const QString bg = background(), panel = surface();
    const QString target = dark() ? "#ffffff" : "#000000";
    QString result = color;
    for (int step = 1; step <= 20 && std::min(contrast(result, bg), contrast(result, panel)) < 4.5; ++step)
        result = mix(color, target, step / 20.0);
    return result;
}

// Secondary text. A theme's own muted is a terminal's dim colour, about 2:1 on
// most backgrounds, so it is only the starting point; without one, Folio's
// mix of foreground into background.
// Computed once per palette, not on every binding that reads them.
void Theme::deriveTextColors() {
    const QString value = color("muted", "");
    m_muted = readable(value.isEmpty() ? mix(background(), foreground(), 0.68) : value);
    m_danger = readable(color("red", "#e5534b"));
    m_success = readable(color("green", "#57ab5a"));
    m_accentText = readable(m_accent);
    // A theme without a slot (some leave out orange or brown) gets Tokyo
    // Night's, lifted the same way.
    static const QHash<QString, const char *> fallback = {
        {"red", "#f7768e"},          {"orange", "#eb927b"},        {"yellow", "#e0af68"},
        {"green", "#9ece6a"},        {"cyan", "#449dab"},          {"blue", "#7aa2f7"},
        {"magenta", "#ad8ee6"},      {"brown", "#75493d"},         {"bright_red", "#ff7a93"},
        {"bright_yellow", "#ff9e64"}, {"bright_green", "#b9f27c"}, {"bright_cyan", "#0db9d7"},
        {"bright_blue", "#7da6ff"},  {"bright_magenta", "#bb9af7"}};
    m_palette.clear();
    for (const QString &slot : ConfigKeys::paletteSlots())
        m_palette.insert(slot, readable(color(slot.toLatin1().constData(), fallback.value(slot))));
}

QString Theme::paletteColor(const QString &slot) const {
    return m_palette.value(ConfigKeys::paletteSlot(slot));
}

QVariantMap Theme::palette() const {
    QVariantMap map;
    for (auto it = m_palette.cbegin(); it != m_palette.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

// Black or white, whichever reads on the accent.
QString Theme::accentForeground() const { return luminance(QColor(m_accent)) > .179 ? "black" : "white"; }

QString Theme::resolveFamily(const QString &family) {
    // A configuration loaded afresh rather than the one Qt read at startup, so
    // a choice made since (omarchy-font-set) counts, and Qt's is left alone.
    QString resolved = family;
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config)
        return resolved;
    if (FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(family.toUtf8().constData()))) {
        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        if (FcPattern *match = FcFontMatch(config, pattern, &result)) {
            FcChar8 *name = nullptr;
            if (FcPatternGetString(match, FC_FAMILY, 0, &name) == FcResultMatch && name)
                resolved = QString::fromUtf8(reinterpret_cast<const char *>(name));
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pattern);
    }
    FcConfigDestroy(config);
    return resolved;
}

void Theme::followSystemFont() {
    if (m_followingFont)
        return;
    m_followingFont = true;
    m_fontDebounce.setSingleShot(true);
    m_fontDebounce.setInterval(200);
    connect(&m_fontDebounce, &QTimer::timeout, this, &Theme::readSystemFont);
    // omarchy-font-set writes the user's fontconfig file; the folder is
    // watched too, for when the file is made or replaced.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/fontconfig";
    const QString file = folder + "/fonts.conf";
    QDir().mkpath(folder);
    m_fontWatcher.addPath(folder);
    if (QFileInfo::exists(file))
        m_fontWatcher.addPath(file);
    connect(&m_fontWatcher, &QFileSystemWatcher::directoryChanged, this, [this, file] {
        // A file written anew stops being watched; watch it again.
        if (QFileInfo::exists(file) && !m_fontWatcher.files().contains(file))
            m_fontWatcher.addPath(file);
        m_fontDebounce.start();
    });
    connect(&m_fontWatcher, &QFileSystemWatcher::fileChanged, this, [this] { m_fontDebounce.start(); });
    readSystemFont();
}

void Theme::readSystemFont() {
    const QString family = resolveFamily("monospace");
    // New controls and popups take the application's font; what is on screen
    // binds fontFamily.
    QFont font = QGuiApplication::font();
    font.setFamily(family);
    font.setStyleHint(QFont::Monospace);
    if (font != QGuiApplication::font())
        QGuiApplication::setFont(font);
    if (family != m_fontFamily) {
        m_fontFamily = family;
        emit fontChanged();
    }
}

void Theme::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml",
                           QFileInfo(m_shellConfig).absolutePath(), m_shellConfig};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    // The user's override wins, then the theme's, then the shell's default,
    // the same order the Omarchy shell resolves it in.
    int textSize = readBaseSize(m_shellConfig);
    if (textSize <= 0)
        textSize = readBaseSize(m_directory + "/theme/shell.toml");
    textSize = textSize <= 0 ? 12 : std::clamp(textSize, 8, 48);

    const auto colors = readColors(m_directory + "/theme/colors.toml");
    const QString accent = colors.value(QStringLiteral("accent"), QStringLiteral("#FFD60A"));
    if (accent != m_accent || colors != m_colors || textSize != m_textSize) {
        m_accent = accent;
        m_colors = colors;
        m_textSize = textSize;
        deriveTextColors();
        emit changed();
    }
}
