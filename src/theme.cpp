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

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    deriveTextColors();

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression line(R"re(^\s*([a-z_0-9]+)\s*=\s*["']([^"']+)["'])re",
                                         QRegularExpression::MultilineOption);
    auto it = line.globalMatch(QString::fromUtf8(file.readAll()));
    while (it.hasNext()) {
        const auto match = it.next();
        colors.insert(match.captured(1), match.captured(2).trimmed());
    }
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
    refreshRounding();
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
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto accent = readAccent(m_directory + "/theme/colors.toml");
    const auto colors = readColors(m_directory + "/theme/colors.toml");
    if (accent != m_accent || colors != m_colors) {
        m_accent = accent;
        m_colors = colors;
        deriveTextColors();
        emit changed();
    }
}
