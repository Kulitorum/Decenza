#include "settings_theme.h"

#include <algorithm>
#include "backgroundpresets.h"
#include "settings.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QtMath>
#include <QColor>
#include <QGuiApplication>
#include <QStyleHints>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// The four background kinds. Spelled once here rather than as literals at each comparison
// so a typo is a link error rather than a source that silently never matches.
namespace {
constexpr const char* kBackgroundSourceNone = "none";
constexpr const char* kBackgroundSourceColour = "colour";
constexpr const char* kBackgroundSourceImage = "image";
constexpr const char* kBackgroundSourceShot = "shot";
}  // namespace

#ifdef Q_OS_IOS
#include "screensaver/iosbrightness.h"
#endif

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

SettingsTheme::SettingsTheme(QObject* parent)
    : QObject(parent)
#ifdef DECENZA_TESTING
    , m_settings(Settings::testQSettingsPath(), QSettings::IniFormat)
#else
    , m_settings("DecentEspresso", "DE1Qt")
#endif
{
}

bool SettingsTheme::glassChrome() const {
    return m_settings.value("theme/glassChrome", false).toBool();
}

void SettingsTheme::setGlassChrome(bool enabled) {
    if (glassChrome() != enabled) {
        m_settings.setValue("theme/glassChrome", enabled);
        emit glassChromeChanged();
    }
}

QString SettingsTheme::skin() const {
    return m_settings.value("ui/skin", "default").toString();
}

void SettingsTheme::setSkin(const QString& skin) {
    if (this->skin() != skin) {
        m_settings.setValue("ui/skin", skin);
        emit skinChanged();
    }
}

QString SettingsTheme::backgroundImagePath() const {
    return m_settings.value("theme/backgroundImagePath", "").toString();
}

void SettingsTheme::setBackgroundImagePath(const QString& path) {
    const QString before = backgroundSource();
    if (backgroundImagePath() != path) {
        m_settings.setValue("theme/backgroundImagePath", path);
        emit backgroundImagePathChanged();
    }
    // Image, colour and shot chart are one choice in one chooser, so picking an image
    // clears the others. Done outside the != guard: re-selecting the image already set
    // must still clear a colour, or the two could both be live.
    if (!path.isEmpty()) {
        storeBackgroundSource(kBackgroundSourceImage);
        clearBackgroundPreset("a background image was chosen instead");
    }
    notifyBackgroundSourceChanged(before);
}

QString SettingsTheme::backgroundPreset() const {
    const QString id = m_settings.value("theme/backgroundPreset", "").toString();
    // An id we do not recognise — a downgrade, a hand-edited ini, a colour removed in a
    // later release — reads back as "none" rather than rendering an undefined background.
    return BackgroundPresets::hasColour(id) ? id : QString();
}

void SettingsTheme::setBackgroundPreset(const QString& id) {
    const QString before = backgroundSource();
    // An unrecognised id is a caller bug, not a request to clear. Erasing on it meant a
    // restore from a backup naming a colour this build does not know silently wiped the
    // colour the device already had — and reported success. Empty is how a caller asks
    // for none; anything else unknown is refused and logged, as setFontSize does.
    if (!id.isEmpty() && !BackgroundPresets::hasColour(id)) {
        qWarning() << "[Theme] Ignoring unknown background colour id:" << id
                   << "- keeping" << (backgroundPreset().isEmpty() ? QStringLiteral("none")
                                                                   : backgroundPreset());
        return;
    }
    if (backgroundPreset() != id) {
        if (!id.isEmpty() || !backgroundPreset().isEmpty()) {
            qInfo() << "[Theme] Background colour:"
                    << (id.isEmpty() ? QStringLiteral("none") : id);
        }
        m_settings.setValue("theme/backgroundPreset", id);
        emit backgroundPresetChanged();
    }
    if (!id.isEmpty()) {
        storeBackgroundSource(kBackgroundSourceColour);
        setBackgroundImagePath(QString());
    }
    notifyBackgroundSourceChanged(before);
}

// --- Which KIND of background is active -------------------------------------------
//
// The source is stored, but a stored value is only believed when its PARAMETER backs it
// up: "colour" with no colour, or "image" with no path, describes something no renderer
// can draw. Rather than trust it, fall through to deriving the source from the values
// that are actually set — which is also exactly what an install predating this key needs,
// so the migration and the self-healing are one code path instead of two.
//
// Nothing is rewritten on read. The derivation is unambiguous, and rewriting stored
// user-set values is a thing this project deliberately does not do.
QString SettingsTheme::backgroundSource() const {
    const QString stored = m_settings.value("theme/backgroundSource", "").toString();
    if (stored == kBackgroundSourceShot)
        return stored;
    if (stored == kBackgroundSourceColour && !backgroundPreset().isEmpty())
        return stored;
    if (stored == kBackgroundSourceImage && !backgroundImagePath().isEmpty())
        return stored;
    if (stored == kBackgroundSourceNone && backgroundPreset().isEmpty()
        && backgroundImagePath().isEmpty())
        return stored;

    if (!backgroundPreset().isEmpty())
        return QString::fromLatin1(kBackgroundSourceColour);
    if (!backgroundImagePath().isEmpty())
        return QString::fromLatin1(kBackgroundSourceImage);
    return QString::fromLatin1(kBackgroundSourceNone);
}

bool SettingsTheme::backgroundShotAdvanced() const {
    return m_settings.value("theme/backgroundShotAdvanced", false).toBool();
}

// Writes the stored key ONLY. The signal is the other half, and it is deliberately not
// emitted here — see notifyBackgroundSourceChanged.
void SettingsTheme::storeBackgroundSource(const QString& source) {
    if (source != kBackgroundSourceNone && source != kBackgroundSourceColour
        && source != kBackgroundSourceImage && source != kBackgroundSourceShot) {
        // Same stance as an unknown colour id: a bad value is a caller bug, not a request
        // to wipe the user's background.
        qWarning() << "[Theme] Ignoring unknown background source:" << source
                   << "- keeping" << backgroundSource();
        return;
    }
    m_settings.setValue("theme/backgroundSource", source);
}

// Emit if the DERIVED source moved, comparing against a snapshot taken before the change.
//
// It has to be the derived value, not the stored key, because the two move independently:
// clearing an image path leaves the key saying "image" while the getter correctly derives
// "none". A previous version guarded on the stored key and so never fired on any clear —
// and since Theme.hasBackgroundImage and Theme.glassChrome were rewired onto this property,
// roughly seventy chrome call sites would have stayed in translucent-over-a-photo mode with
// no photo. Deleting the personal image you were using as a background reached exactly that
// state, which is the case ScreensaverVideoManager's own clearing code exists to prevent.
void SettingsTheme::notifyBackgroundSourceChanged(const QString& before) {
    if (backgroundSource() != before)
        emit backgroundSourceChanged();
}

void SettingsTheme::selectShotChartBackground(bool advanced) {
    const QString before = backgroundSource();
    const bool advancedChanged = backgroundShotAdvanced() != advanced;
    if (advancedChanged)
        m_settings.setValue("theme/backgroundShotAdvanced", advanced);

    qInfo() << "[Theme] Background: last shot chart" << (advanced ? "(advanced)" : "(basic)");
    storeBackgroundSource(QString::fromLatin1(kBackgroundSourceShot));
    clearBackgroundPreset("the last-shot chart was chosen instead");
    setBackgroundImagePath(QString());

    // The advanced flag NOTIFYs on backgroundSourceChanged, and flipping it while already on
    // the chart moves no source — so without this the Advanced entry would be a silent no-op
    // forever: the cache key would never change, the renderer would never re-grab, and the
    // picker would still show it as selected.
    if (advancedChanged && backgroundSource() == before)
        emit backgroundSourceChanged();
    else
        notifyBackgroundSourceChanged(before);
}

void SettingsTheme::clearBackground() {
    const QString before = backgroundSource();
    // Parameters first, source last, so that when the signal fires the getter already
    // reports the final answer. The other order emits while the old image path is still
    // set, and a binding reading backgroundSource inside its own change handler — which is
    // what every QML binding does — latches the value being cleared.
    clearBackgroundPreset("the background was cleared");
    setBackgroundImagePath(QString());
    storeBackgroundSource(QString::fromLatin1(kBackgroundSourceNone));
    notifyBackgroundSourceChanged(before);
}

// Clearing the colour is a SIDE EFFECT of several unrelated actions, so it says which one
// did it. A field report of "my background keeps resetting" is otherwise undiagnosable
// from a log, and users' AI assistants read these logs.
void SettingsTheme::clearBackgroundPreset(const char* reason) {
    if (backgroundPreset().isEmpty())
        return;
    qInfo() << "[Theme] Clearing background colour" << backgroundPreset() << "-" << reason;
    setBackgroundPreset(QString());
}

QString SettingsTheme::backgroundPattern() const {
    const QString id = m_settings.value("theme/backgroundPattern", "").toString();
    return BackgroundPresets::hasPattern(id) ? id : QString();
}

void SettingsTheme::setBackgroundPattern(const QString& id) {
    if (!id.isEmpty() && !BackgroundPresets::hasPattern(id)) {
        qWarning() << "[Theme] Ignoring unknown background pattern id:" << id;
        return;
    }
    if (backgroundPattern() != id) {
        m_settings.setValue("theme/backgroundPattern", id);
        emit backgroundPatternChanged();
    }
}

QVariantList SettingsTheme::backgroundPresets() const {
    return BackgroundPresets::coloursAsVariantList();
}

QVariantList SettingsTheme::backgroundPatterns() const {
    return BackgroundPresets::patternsAsVariantList();
}

QVariantMap SettingsTheme::activeBackgroundPreset() const {
    return BackgroundPresets::colourToVariantMap(BackgroundPresets::colourById(backgroundPreset()));
}

QVariantMap SettingsTheme::activeBackgroundPattern() const {
    return BackgroundPresets::patternToVariantMap(BackgroundPresets::patternById(backgroundPattern()));
}

QVariantMap SettingsTheme::deriveColorsFor(const QString& colourId) const {
    const BackgroundPresets::Colour c = BackgroundPresets::colourById(colourId);
    return c.id.isEmpty() ? QVariantMap()
                          : BackgroundPresets::deriveAsVariantMap(QColor(c.value));
}

QString SettingsTheme::adjustedForContrast(const QString& foreground, const QString& background) const {
    const QColor fg(foreground);
    const QColor bg(background);
    // An unreadable input is not something to guess at — hand the caller back exactly what
    // it passed so a bad colour shows up as itself rather than as a silent black.
    if (!fg.isValid() || !bg.isValid())
        return foreground;
    // Against the page as the densest pattern renders it, so the palette does not move when
    // the pattern does — see pageUnderDensestPattern().
    return BackgroundPresets::adjustForContrast(
               fg, BackgroundPresets::pageUnderDensestPattern(bg)).name();
}

QVariantMap SettingsTheme::derivedBackgroundColors() const {
    const BackgroundPresets::Colour c = BackgroundPresets::colourById(backgroundPreset());
    return c.id.isEmpty() ? QVariantMap()
                          : BackgroundPresets::deriveAsVariantMap(QColor(c.value));
}

QString SettingsTheme::skinPath() const {
    // Look for skins in standard locations
    QStringList searchPaths = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/skins/" + skin(),
        ":/skins/" + skin(),
        "./skins/" + skin()
    };

    for (const QString& path : searchPaths) {
        if (QDir(path).exists()) {
            return path;
        }
    }

    // Default fallback
    return ":/skins/default";
}

// -- Light/Dark default palettes --

const QVariantMap& SettingsTheme::darkDefaults() {
    static const QVariantMap defaults = {
        {"backgroundColor", "#1a1a2e"},
        {"surfaceColor", "#252538"},
        {"primaryColor", "#4e85f4"},
        {"secondaryColor", "#c0c5e3"},
        {"textColor", "#ffffff"},
        {"textSecondaryColor", "#a0a8b8"},
        {"accentColor", "#e94560"},
        {"successColor", "#00cc6d"},
        {"warningColor", "#ffaa00"},
        {"highlightColor", "#ffaa00"},
        {"errorColor", "#ff4444"},
        {"borderColor", "#3a3a4e"},
        {"pressureColor", "#18c37e"},
        {"pressureGoalColor", "#69fdb3"},
        {"flowColor", "#4e85f4"},
        {"flowGoalColor", "#7aaaff"},
        {"temperatureColor", "#e73249"},
        {"temperatureGoalColor", "#ffa5a6"},
        {"weightColor", "#a2693d"},
        {"weightFlowColor", "#d4a574"},
        {"resistanceColor", "#eae83d"},
        {"waterLevelColor", "#4e85f4"},
        {"dyeDoseColor", "#6F4E37"},
        {"dyeOutputColor", "#9C27B0"},
        {"dyeTdsColor", "#FF9800"},
        {"dyeEyColor", "#a2693d"},
        {"buttonDisabled", "#555555"},
        {"stopMarkerColor", "#FF6B6B"},
        {"frameMarkerColor", "#66ffffff"},
        {"modifiedIndicatorColor", "#FFCC00"},
        {"simulationIndicatorColor", "#E65100"},
        {"warningButtonColor", "#FFA500"},
        {"successButtonColor", "#2E7D32"},
        {"rowAlternateColor", "#1a1a1a"},
        {"rowAlternateLightColor", "#222222"},
        {"sourceBadgeBlueColor", "#4a90d9"},
        {"sourceBadgeGreenColor", "#4ad94a"},
        {"sourceBadgeOrangeColor", "#d9a04a"},
        {"trackOnTargetColor", "#00cc6d"},
        {"trackDriftingColor", "#f0ad4e"},
        {"trackOffTargetColor", "#e94560"},
        {"primaryContrastColor", "#ffffff"},
        {"iconColor", "#ffffff"},
        {"bottomBarColor", "#4e85f4"},
        {"actionButtonContentColor", "#ffffff"}
    };
    return defaults;
}

const QVariantMap& SettingsTheme::lightDefaults() {
    // Light palette carries the dark theme's blue-purple DNA as subtle tints
    // rather than going neutral gray — same family, different brightness.
    static const QVariantMap defaults = {
        // Core UI — blue-gray tinted backgrounds echo the navy dark theme
        {"backgroundColor", "#eef0f6"},
        {"surfaceColor", "#ffffff"},
        {"primaryColor", "#d1daee"},
        {"secondaryColor", "#8890a8"},
        {"textColor", "#1a1a2e"},
        {"textSecondaryColor", "#5d6478"},
        {"accentColor", "#d93050"},
        {"successColor", "#00a856"},
        {"warningColor", "#e69500"},
        {"highlightColor", "#e69500"},
        {"errorColor", "#d93030"},
        {"borderColor", "#c4c9d6"},
        // Chart — slightly deeper than dark-mode goal colors for white-background contrast
        {"pressureColor", "#12a86b"},
        {"pressureGoalColor", "#40d898"},
        {"flowColor", "#3a6fd0"},
        {"flowGoalColor", "#6898e8"},
        {"temperatureColor", "#d42840"},
        {"temperatureGoalColor", "#f07080"},
        {"weightColor", "#8a5830"},
        {"weightFlowColor", "#c08858"},
        {"resistanceColor", "#a89800"},
        {"waterLevelColor", "#2c5fc0"},
        // DYE metadata — same across modes (strong hues work on both)
        {"dyeDoseColor", "#6F4E37"},
        {"dyeOutputColor", "#9C27B0"},
        {"dyeTdsColor", "#FF9800"},
        {"dyeEyColor", "#a2693d"},
        // UI indicators — blue-gray tinted where neutral before
        {"buttonDisabled", "#b0b5c2"},
        {"stopMarkerColor", "#e85050"},
        {"frameMarkerColor", "#40303048"},
        {"modifiedIndicatorColor", "#CC9900"},
        {"simulationIndicatorColor", "#E65100"},
        {"warningButtonColor", "#e69500"},
        {"successButtonColor", "#2E7D32"},
        // Lists — subtle blue-gray alternation
        {"rowAlternateColor", "#e8eaf2"},
        {"rowAlternateLightColor", "#e0e3ed"},
        {"sourceBadgeBlueColor", "#3a78c0"},
        {"sourceBadgeGreenColor", "#38b038"},
        {"sourceBadgeOrangeColor", "#c89030"},
        // Tracking indicators
        {"trackOnTargetColor", "#00a856"},
        {"trackDriftingColor", "#d99a00"},
        {"trackOffTargetColor", "#d93050"},
        {"primaryContrastColor", "#4e85f4"},
        {"iconColor", "#1a1a2e"},
        {"bottomBarColor", "#ffffff"},
        {"actionButtonContentColor", "#ffffff"}
    };
    return defaults;
}

// -- Theme mode --

QString SettingsTheme::themeMode() const {
    return m_settings.value("theme/mode", "dark").toString();
}

void SettingsTheme::setThemeMode(const QString& mode) {
    if (themeMode() != mode) {
        m_settings.setValue("theme/mode", mode);
        emit themeModeChanged();
        updateResolvedMode();
    }
}

void SettingsTheme::initSystemThemeDetection() {
    auto* hints = QGuiApplication::styleHints();
    if (hints) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (themeMode() == "system") {
                updateResolvedMode();
            }
        });
    }
    // Resolve initial mode (for "system" mode)
    updateResolvedMode();
}

void SettingsTheme::updateResolvedMode() {
    bool wasDark = m_isDarkMode;
    QString mode = themeMode();

    if (mode == "light") {
        m_isDarkMode = false;
    } else if (mode == "dark") {
        m_isDarkMode = true;
    } else {
        // "system" — follow OS
        auto* hints = QGuiApplication::styleHints();
        if (hints) {
            m_isDarkMode = (hints->colorScheme() != Qt::ColorScheme::Light);
        } else {
            m_isDarkMode = true;  // fallback to dark
        }
    }

    if (wasDark != m_isDarkMode) {
        emit isDarkModeChanged();
        emit customThemeColorsChanged();       // Active palette changed
        emit activeThemeNameChanged();         // Derived from dark/lightThemeName
    }
#ifdef Q_OS_IOS
    ios_setStatusBarStyle(m_isDarkMode);
#endif
}

void SettingsTheme::setEditingPalette(const QString& palette) {
    QString p = (palette == "light") ? "light" : "dark";
    if (m_editingPalette != p) {
        m_editingPalette = p;
        emit editingPaletteChanged();
    }
}

QVariantMap SettingsTheme::editingPaletteColors() const {
    const QString key = (m_editingPalette == "light") ? "theme/customColorsLight" : "theme/customColorsDark";
    const QVariantMap& defaults = (m_editingPalette == "light") ? lightDefaults() : darkDefaults();

    QVariantMap result = defaults;  // Start with full defaults
    QByteArray data = m_settings.value(key).toByteArray();
    if (!data.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QVariantMap stored = doc.object().toVariantMap();
        // Merge stored on top of defaults
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void SettingsTheme::setEditingPaletteColor(const QString& colorName, const QString& colorValue) {
    const QString key = (m_editingPalette == "light") ? "theme/customColorsLight" : "theme/customColorsDark";

    QByteArray data = m_settings.value(key).toByteArray();
    QJsonObject obj;
    if (!data.isEmpty()) {
        obj = QJsonDocument::fromJson(data).object();
    }
    // A background preset overrides exactly the value being set here, so an explicit
    // later choice of that colour wins — otherwise the user drags a colour in the theme
    // editor and nothing happens, which is an expensive bug to reproduce. Compared
    // against the previous value so that re-writing the same colour (a no-op edit, or a
    // palette round-trip) does not silently drop the preset.
    const bool backgroundColorChanged =
        colorName == QLatin1String("backgroundColor") && obj.value(colorName).toString() != colorValue;

    obj[colorName] = colorValue;
    m_settings.setValue(key, QJsonDocument(obj).toJson());

    // Only when the palette being edited is the one on screen — the theme editor can edit
    // the inactive palette, and that must not drop a background the user is looking at.
    if (backgroundColorChanged && (m_editingPalette == "dark") == m_isDarkMode)
        clearBackgroundPreset("its background colour was edited in the theme editor");

    // If editing the active palette, notify QML
    bool editingActive = (m_editingPalette == "dark") == m_isDarkMode;
    if (editingActive) {
        emit customThemeColorsChanged();
    }

    // Mark the edited palette's mode as custom
    if (m_editingPalette == "dark") {
        if (darkThemeName() != "Custom")
            setDarkThemeName("Custom");
    } else {
        if (lightThemeName() != "Custom")
            setLightThemeName("Custom");
    }
}

// Theme settings
QVariantMap SettingsTheme::customThemeColors() const {
    // Resolve active palette based on isDarkMode
    const QString key = m_isDarkMode ? "theme/customColorsDark" : "theme/customColorsLight";
    const QVariantMap& defaults = m_isDarkMode ? darkDefaults() : lightDefaults();

    QVariantMap result = defaults;  // Start with full defaults
    QByteArray data = m_settings.value(key).toByteArray();
    if (!data.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QVariantMap stored = doc.object().toVariantMap();
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void SettingsTheme::setCustomThemeColors(const QVariantMap& colors) {
    // Write to the active palette
    const QString key = m_isDarkMode ? "theme/customColorsDark" : "theme/customColorsLight";
    // Same rule as setEditingPaletteColor: an explicit new background colour wins over an
    // active preset, which would otherwise override it and make the edit look inert.
    const bool backgroundColorChanged =
        colors.contains("backgroundColor") &&
        colors.value("backgroundColor").toString() != customThemeColors().value("backgroundColor").toString();

    QJsonObject obj = QJsonObject::fromVariantMap(colors);
    m_settings.setValue(key, QJsonDocument(obj).toJson());

    if (backgroundColorChanged)
        clearBackgroundPreset("the palette's background colour was set directly");

    emit customThemeColorsChanged();
}

QVariantList SettingsTheme::colorGroups() const {
    QByteArray data = m_settings.value("theme/colorGroups").toByteArray();
    if (data.isEmpty()) {
        return QVariantList();
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

void SettingsTheme::setColorGroups(const QVariantList& groups) {
    QJsonArray arr;
    for (const QVariant& v : groups) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    m_settings.setValue("theme/colorGroups", QJsonDocument(arr).toJson());
    emit colorGroupsChanged();
}

QString SettingsTheme::activeThemeName() const {
    return m_isDarkMode ? darkThemeName() : lightThemeName();
}

void SettingsTheme::setActiveThemeName(const QString& name) {
    if (m_isDarkMode)
        setDarkThemeName(name);
    else
        setLightThemeName(name);
}

QString SettingsTheme::darkThemeName() const {
    // Migrate from old single activeThemeName if per-mode names not set
    if (!m_settings.contains("theme/darkThemeName")) {
        QString old = m_settings.value("theme/activeName", "Default Dark").toString();
        return (old == "Default") ? "Default Dark" : old;
    }
    return m_settings.value("theme/darkThemeName", "Default Dark").toString();
}

void SettingsTheme::setDarkThemeName(const QString& name) {
    if (darkThemeName() != name) {
        m_settings.setValue("theme/darkThemeName", name);
        emit darkThemeNameChanged();
        if (m_isDarkMode)
            emit activeThemeNameChanged();
    }
}

QString SettingsTheme::lightThemeName() const {
    return m_settings.value("theme/lightThemeName", "Default Light").toString();
}

void SettingsTheme::setLightThemeName(const QString& name) {
    if (lightThemeName() != name) {
        m_settings.setValue("theme/lightThemeName", name);
        emit lightThemeNameChanged();
        if (!m_isDarkMode)
            emit activeThemeNameChanged();
    }
}

QStringList SettingsTheme::themeNames() const {
    QStringList names;
    names << "Default Dark" << "Default Light";

    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QString name = val.toObject()["name"].toString();
        if (!name.isEmpty())
            names << name;
    }
    return names;
}

void SettingsTheme::applyDarkTheme(const QString& name) {
    if (name == "Default Dark") {
        m_settings.remove("theme/customColorsDark");
    } else if (name == "Default Light") {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(QJsonObject::fromVariantMap(lightDefaults())).toJson());
    } else {
        // Look for user theme
        QJsonArray userThemes = QJsonDocument::fromJson(
            m_settings.value("theme/userThemes", "[]").toByteArray()
        ).array();
        for (const QJsonValue& val : userThemes) {
            QJsonObject obj = val.toObject();
            if (obj["name"].toString() == name) {
                if (obj.contains("colorsDark"))
                    m_settings.setValue("theme/customColorsDark",
                        QJsonDocument(obj["colorsDark"].toObject()).toJson());
                else
                    m_settings.remove("theme/customColorsDark");
                break;
            }
        }
    }
    const bool changed = (darkThemeName() != name);
    setDarkThemeName(name);
    // A theme carries its own background colour, so choosing one is an explicit choice of
    // the value a background colour overrides — the later choice wins. Two guards:
    //
    //   changed      — a combo box emits activated() when you re-pick the entry already
    //                  selected. Without this, opening the dropdown to see what is set and
    //                  tapping it destroyed the user's background having changed nothing.
    //   m_isDarkMode — this function only touches the dark palette, so it must not clear a
    //                  background the user is looking at in light mode. The colour is one
    //                  global value; the theme slots are not.
    if (changed && m_isDarkMode)
        clearBackgroundPreset("dark theme changed, and it carries its own background colour");
    if (m_isDarkMode)
        emit customThemeColorsChanged();
}

void SettingsTheme::applyLightTheme(const QString& name) {
    if (name == "Default Light") {
        m_settings.remove("theme/customColorsLight");
    } else if (name == "Default Dark") {
        m_settings.setValue("theme/customColorsLight",
            QJsonDocument(QJsonObject::fromVariantMap(darkDefaults())).toJson());
    } else {
        // Look for user theme
        QJsonArray userThemes = QJsonDocument::fromJson(
            m_settings.value("theme/userThemes", "[]").toByteArray()
        ).array();
        for (const QJsonValue& val : userThemes) {
            QJsonObject obj = val.toObject();
            if (obj["name"].toString() == name) {
                if (obj.contains("colorsLight"))
                    m_settings.setValue("theme/customColorsLight",
                        QJsonDocument(obj["colorsLight"].toObject()).toJson());
                else
                    m_settings.remove("theme/customColorsLight");
                break;
            }
        }
    }
    const bool changed = (lightThemeName() != name);
    setLightThemeName(name);
    if (changed && !m_isDarkMode)
        clearBackgroundPreset("light theme changed, and it carries its own background colour");
    if (!m_isDarkMode)
        emit customThemeColorsChanged();
}

QString SettingsTheme::activeShader() const {
    return m_settings.value("theme/activeShader", "").toString();
}

void SettingsTheme::setActiveShader(const QString& shader) {
    if (activeShader() != shader) {
        m_settings.setValue("theme/activeShader", shader);
        emit activeShaderChanged();
    }
}

// Known parameter names per effect (for const-correct enumeration)
static const QMap<QString, QStringList>& knownEffectParams() {
    static const QMap<QString, QStringList> params = {
        {"crt", {
            "scanlineIntensity", "scanlineSize", "noiseIntensity", "bloomStrength",
            "aberration", "jitterAmount", "vignetteStrength", "tintStrength",
            "flickerAmount", "glitchRate", "glowStart", "noiseSize",
            "reflectionStrength",
            "colorGain", "colorContrast", "colorSaturation", "hueShift"
        }}
        // Future: {"vhs", {"trackingError", "colorBleed", ...}}
    };
    return params;
}

QVariantMap SettingsTheme::shaderParams() const {
    return effectParams(activeShader());
}

void SettingsTheme::setShaderParam(const QString& name, double value) {
    setEffectParam(activeShader(), name, value);
}

QVariantMap SettingsTheme::effectParams(const QString& effectId) const {
    if (effectId.isEmpty()) return {};
    const auto& allParams = knownEffectParams();
    if (!allParams.contains(effectId)) return {};

    QVariantMap params;
    for (const QString& name : allParams[effectId]) {
        const QString key = "shader/" + effectId + "/" + name;
        if (m_settings.contains(key))
            params[name] = m_settings.value(key).toDouble();
    }
    return params;
}

void SettingsTheme::setEffectParam(const QString& effectId, const QString& name, double value) {
    if (effectId.isEmpty()) return;
    const QString key = "shader/" + effectId + "/" + name;
    double current = m_settings.value(key, -99999.0).toDouble();
    if (qAbs(current - value) > 0.0001) {
        m_settings.setValue(key, value);
        emit shaderParamsChanged();
    }
}

QJsonObject SettingsTheme::screenEffectJson() const {
    QJsonObject result;
    result["active"] = activeShader();

    QJsonObject effects;
    const auto& allParams = knownEffectParams();
    for (auto it = allParams.begin(); it != allParams.end(); ++it) {
        QVariantMap params = effectParams(it.key());
        if (!params.isEmpty()) {
            effects[it.key()] = QJsonObject::fromVariantMap(params);
        }
    }
    result["effects"] = effects;
    return result;
}

void SettingsTheme::applyScreenEffect(const QJsonObject& screenEffect) {
    // Set active effect (empty = none)
    QString active = screenEffect["active"].toString();
    setActiveShader(active);

    // Restore per-effect params
    QJsonObject effects = screenEffect["effects"].toObject();
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        QString effectId = it.key();
        QJsonObject params = it.value().toObject();
        for (auto pit = params.begin(); pit != params.end(); ++pit) {
            setEffectParam(effectId, pit.key(), pit.value().toDouble());
        }
    }
}

double SettingsTheme::screenBrightness() const {
    return m_settings.value("theme/screenBrightness", 1.0).toDouble();
}

void SettingsTheme::setScreenBrightness(double brightness) {
    double clamped = qBound(0.0, brightness, 1.0);
    if (qAbs(screenBrightness() - clamped) > 0.001) {
        m_settings.setValue("theme/screenBrightness", clamped);

#ifdef Q_OS_ANDROID
        // Must run on Android UI thread
        float androidBrightness = (clamped < 0.01) ? 0.01f : static_cast<float>(clamped);
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([androidBrightness]() {
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            if (activity.isValid()) {
                QJniObject window = activity.callObjectMethod(
                    "getWindow", "()Landroid/view/Window;");
                if (window.isValid()) {
                    QJniObject params = window.callObjectMethod(
                        "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
                    if (params.isValid()) {
                        params.setField<jfloat>("screenBrightness", androidBrightness);
                        window.callMethod<void>("setAttributes",
                            "(Landroid/view/WindowManager$LayoutParams;)V",
                            params.object());
                    }
                }
            }
        });
#endif

        emit screenBrightnessChanged();
    }
}

void SettingsTheme::setThemeColor(const QString& colorName, const QString& colorValue) {
    setEditingPaletteColor(colorName, colorValue);
}

QString SettingsTheme::getThemeColor(const QString& colorName) const {
    QVariantMap colors = customThemeColors();
    return colors.value(colorName).toString();
}

void SettingsTheme::resetThemeToDefault() {
    m_settings.remove("theme/customColorsDark");
    m_settings.remove("theme/customColorsLight");
    m_settings.remove("theme/colorGroups");
    setDarkThemeName("Default Dark");
    setLightThemeName("Default Light");
    emit customThemeColorsChanged();
    emit colorGroupsChanged();
}

// Font size customization
//
// Canonical defaults live here and nowhere else. They were previously duplicated in
// qml/Theme.qml (as `|| 32` style fallbacks) and in shotserver_theme.cpp (as its own
// QMap) — two tables free to drift, where the size the UI renders at and the size the
// web editor reports could silently disagree.
namespace {
// Set from main.cpp before the QML engine is created; read-only thereafter.
QString g_bundledFontFamily;
QString g_symbolFontFamily;
}

void SettingsTheme::setBundledFontFamily(const QString& family) { g_bundledFontFamily = family; }
QString SettingsTheme::bundledFontFamily() { return g_bundledFontFamily; }
void SettingsTheme::setSymbolFontFamily(const QString& family) { g_symbolFontFamily = family; }
QString SettingsTheme::symbolFontFamily() { return g_symbolFontFamily; }

const QMap<QString, SettingsTheme::FontRole>& SettingsTheme::fontRoles() {
    //                          default  min  max
    static const QMap<QString, FontRole> roles = {
        {"headingSize",  {32, 16,  64}},
        {"titleSize",    {24, 12,  48}},
        {"subtitleSize", {18, 10,  36}},
        {"bodySize",     {18, 10,  36}},
        {"labelSize",    {14,  8,  28}},
        {"captionSize",  {12,  8,  24}},
        {"valueSize",    {48, 24,  96}},
        {"timerSize",    {72, 36, 120}}
    };
    return roles;
}

const QMap<QString, int>& SettingsTheme::fontSizeDefaults() {
    static const QMap<QString, int> defaults = [] {
        QMap<QString, int> d;
        for (auto it = fontRoles().constBegin(); it != fontRoles().constEnd(); ++it)
            d.insert(it.key(), it.value().def);
        return d;
    }();
    return defaults;
}

QVariantMap SettingsTheme::effectiveFontSizes() const {
    const QVariantMap overrides = customFontSizes();
    QVariantMap effective;
    for (auto it = fontSizeDefaults().constBegin(); it != fontSizeDefaults().constEnd(); ++it) {
        const int stored = overrides.value(it.key()).toInt();
        effective[it.key()] = stored > 0 ? stored : it.value();
    }
    return effective;
}

QVariantMap SettingsTheme::fontSizeOverrides() const {
    const QVariantMap overrides = customFontSizes();
    QVariantMap changed;
    for (auto it = fontSizeDefaults().constBegin(); it != fontSizeDefaults().constEnd(); ++it) {
        const int stored = overrides.value(it.key()).toInt();
        // > 0 filters unset/garbage; != default is what makes it an override. Storing a
        // value equal to the default is not a customization worth reporting.
        if (stored > 0 && stored != it.value())
            changed[it.key()] = stored;
    }
    return changed;
}

QVariantMap SettingsTheme::customFontSizes() const {
    const QByteArray data = m_settings.value("theme/customFontSizes").toByteArray();
    if (data.isEmpty()) {
        return QVariantMap();
    }
    // Check the parse. Without this a corrupt blob returns an empty map — byte-for-byte
    // identical to "the user has no overrides" — and the startup log deliberately stays
    // silent in that case, so the corruption is actively camouflaged as normal. The user
    // sees stock sizes, assumes an update reset them, nudges one slider, and setFontSize()
    // then writes a fresh map over the top: every other override is destroyed, silently.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[Font] Stored font-size overrides are corrupt at offset" << err.offset
                   << ":" << err.errorString()
                   << "— falling back to default sizes. The stored value will be overwritten"
                      " on the next font-size change.";
        return QVariantMap();
    }
    return doc.object().toVariantMap();
}

void SettingsTheme::setCustomFontSizes(const QVariantMap& sizes) {
    QJsonObject obj = QJsonObject::fromVariantMap(sizes);
    m_settings.setValue("theme/customFontSizes", QJsonDocument(obj).toJson());
    emit customFontSizesChanged();
}

void SettingsTheme::setFontSize(const QString& fontName, int size) {
    // Validate here rather than trusting callers. POST /api/theme/font takes a JSON body,
    // so an out-of-range or unknown role is reachable without touching the editor's
    // sliders — and an unbounded value (timerSize 100000) renders the app unusable while
    // an unknown key writes junk nothing will ever surface or clean up.
    auto it = fontRoles().constFind(fontName);
    if (it == fontRoles().constEnd()) {
        qWarning() << "[Font] Ignoring unknown font role:" << fontName;
        return;
    }
    const int clamped = std::clamp(size, it->min, it->max);
    if (clamped != size)
        qWarning() << "[Font] Clamped" << fontName << size << "->" << clamped;

    QVariantMap sizes = customFontSizes();
    sizes[fontName] = clamped;
    setCustomFontSizes(sizes);
}

int SettingsTheme::getFontSize(const QString& fontName) const {
    QVariantMap sizes = customFontSizes();
    return sizes.value(fontName, 0).toInt();
}

void SettingsTheme::resetFontSizesToDefault() {
    m_settings.remove("theme/customFontSizes");
    emit customFontSizesChanged();
}

void SettingsTheme::setCurrentPageColors(const QStringList& colors) {
    if (m_currentPageColors != colors) {
        m_currentPageColors = colors;
        emit currentPageColorsChanged();
    }
}

void SettingsTheme::flashThemeColor(const QString& colorName) {
    // Stop any existing flash
    if (m_flashTimer) {
        m_flashTimer->stop();
        m_flashTimer->deleteLater();
        m_flashTimer = nullptr;
    }

    m_flashColorName = colorName;
    m_flashPhase = 1;
    emit flashColorNameChanged();
    emit flashPhaseChanged();

    m_flashTimer = new QTimer(this);
    m_flashTimer->setInterval(130);
    connect(m_flashTimer, &QTimer::timeout, this, [this]() {
        m_flashPhase++;
        if (m_flashPhase > 6) {
            m_flashTimer->stop();
            m_flashTimer->deleteLater();
            m_flashTimer = nullptr;
            m_flashColorName.clear();
            m_flashPhase = 0;
            emit flashColorNameChanged();
        }
        emit flashPhaseChanged();
    });
    m_flashTimer->start();
}

QVariantList SettingsTheme::getPresetThemes() const {
    QVariantList themes;

    // Default Dark theme (built-in)
    QVariantMap defaultDark;
    defaultDark["name"] = "Default Dark";
    defaultDark["primaryColor"] = "#4e85f4";
    defaultDark["isBuiltIn"] = true;
    themes.append(defaultDark);

    // Default Light theme (built-in)
    QVariantMap defaultLight;
    defaultLight["name"] = "Default Light";
    defaultLight["primaryColor"] = "#d1daee";
    defaultLight["isBuiltIn"] = true;
    themes.append(defaultLight);

    // Load user-saved themes
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        QVariantMap theme;
        theme["name"] = obj["name"].toString();
        // Read from colorsDark (new format) or colors (old format)
        QJsonObject colors = obj.contains("colorsDark") ? obj["colorsDark"].toObject() : obj["colors"].toObject();
        theme["primaryColor"] = colors["primaryColor"].toString();
        theme["isBuiltIn"] = false;
        themes.append(theme);
    }

    return themes;
}

void SettingsTheme::applyPresetTheme(const QString& name) {
    if (name == "Default" || name == "Default Dark") {
        // Only reset the dark palette — preserve user's custom light palette
        m_settings.remove("theme/customColorsDark");
        setActiveShader("");
        setDarkThemeName("Default Dark");
        clearBackgroundPreset("a theme was applied, and it carries its own background colour");
        // Switch to dark mode (skip if already resolved to dark, e.g. "system" on a dark OS)
        if (!m_isDarkMode) {
            setThemeMode("dark");
        } else if (themeMode() != "dark" && themeMode() != "system") {
            setThemeMode("dark");
        } else {
            emit customThemeColorsChanged();  // Palette changed, mode didn't
        }
        return;
    }
    if (name == "Default Light") {
        // Only reset the light palette — preserve user's custom dark palette
        m_settings.remove("theme/customColorsLight");
        setActiveShader("");
        setLightThemeName("Default Light");
        clearBackgroundPreset("a theme was applied, and it carries its own background colour");
        // Switch to light mode (skip if already resolved to light, e.g. "system" on a light OS)
        if (m_isDarkMode) {
            setThemeMode("light");
        } else if (themeMode() != "light" && themeMode() != "system") {
            setThemeMode("light");
        } else {
            emit customThemeColorsChanged();  // Palette changed, mode didn't
        }
        return;
    }

    // Look for user theme
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        if (obj["name"].toString() == name) {
            // Apply dark palette
            if (obj.contains("colorsDark")) {
                m_settings.setValue("theme/customColorsDark",
                    QJsonDocument(obj["colorsDark"].toObject()).toJson());
            } else {
                m_settings.remove("theme/customColorsDark");
            }
            // Apply light palette
            if (obj.contains("colorsLight")) {
                m_settings.setValue("theme/customColorsLight",
                    QJsonDocument(obj["colorsLight"].toObject()).toJson());
            } else {
                m_settings.remove("theme/customColorsLight");
            }
            // Restore screen effect (or disable for old presets without it)
            if (obj.contains("screenEffect"))
                applyScreenEffect(obj["screenEffect"].toObject());
            else
                setActiveShader("");
            setDarkThemeName(name);
            setLightThemeName(name);
            clearBackgroundPreset("a theme was applied, and it carries its own background colour");
            emit customThemeColorsChanged();
            return;
        }
    }
}

void SettingsTheme::saveCurrentTheme(const QString& name) {
    if (name.isEmpty() || name == "Default" || name == "Default Dark" || name == "Default Light") {
        return; // Can't save with empty name or overwrite built-in themes
    }

    // Load existing user themes
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    // Remove existing theme with same name (if any)
    for (int i = static_cast<int>(userThemes.size()) - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    // Create new theme entry with both palettes
    QJsonObject newTheme;
    newTheme["name"] = name;

    // Save dark palette
    QByteArray darkData = m_settings.value("theme/customColorsDark").toByteArray();
    if (!darkData.isEmpty())
        newTheme["colorsDark"] = QJsonDocument::fromJson(darkData).object();
    else
        newTheme["colorsDark"] = QJsonObject::fromVariantMap(darkDefaults());

    // Save light palette
    QByteArray lightData = m_settings.value("theme/customColorsLight").toByteArray();
    if (!lightData.isEmpty())
        newTheme["colorsLight"] = QJsonDocument::fromJson(lightData).object();
    else
        newTheme["colorsLight"] = QJsonObject::fromVariantMap(lightDefaults());

    newTheme["screenEffect"] = screenEffectJson();
    userThemes.append(newTheme);

    // Save to settings
    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));
    setDarkThemeName(name);
    setLightThemeName(name);
    emit themeNamesChanged();
}

void SettingsTheme::deleteUserTheme(const QString& name) {
    if (name == "Default Dark" || name == "Default Light") {
        return; // Can't delete built-in themes
    }

    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (qsizetype i = userThemes.size() - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));
    emit themeNamesChanged();

    // If we deleted a theme that was active for either mode, reset that mode
    if (darkThemeName() == name)
        applyDarkTheme("Default Dark");
    if (lightThemeName() == name)
        applyLightTheme("Default Light");
}

bool SettingsTheme::saveThemeToFile(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    QJsonObject root;
    root["name"] = activeThemeName();

    // Save both palettes
    QByteArray darkData = m_settings.value("theme/customColorsDark").toByteArray();
    if (!darkData.isEmpty())
        root["colorsDark"] = QJsonDocument::fromJson(darkData).object();
    else
        root["colorsDark"] = QJsonObject::fromVariantMap(darkDefaults());

    QByteArray lightData = m_settings.value("theme/customColorsLight").toByteArray();
    if (!lightData.isEmpty())
        root["colorsLight"] = QJsonDocument::fromJson(lightData).object();
    else
        root["colorsLight"] = QJsonObject::fromVariantMap(lightDefaults());

    QJsonArray groupsArr;
    for (const QVariant& g : colorGroups()) {
        groupsArr.append(QJsonObject::fromVariantMap(g.toMap()));
    }
    root["groups"] = groupsArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

bool SettingsTheme::loadThemeFromFile(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    if (root.contains("name")) {
        QString themeName = root["name"].toString();
        setDarkThemeName(themeName);
        setLightThemeName(themeName);
    }
    // New dual-palette format
    if (root.contains("colorsDark")) {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(root["colorsDark"].toObject()).toJson());
    }
    if (root.contains("colorsLight")) {
        m_settings.setValue("theme/customColorsLight",
            QJsonDocument(root["colorsLight"].toObject()).toJson());
    }
    // Backward compat: old single-palette format
    if (root.contains("colors") && !root.contains("colorsDark")) {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(root["colors"].toObject()).toJson());
    }
    emit customThemeColorsChanged();

    if (root.contains("groups")) {
        QVariantList groups;
        for (const QJsonValue& v : root["groups"].toArray()) {
            groups.append(v.toObject().toVariantMap());
        }
        setColorGroups(groups);
    }

    return true;
}

// Helper function to create HSL color string
static QString hslColor(double h, double s, double l) {
    // Normalize values
    h = fmod(h, 360.0);
    if (h < 0) h += 360.0;
    s = qBound(0.0, s, 100.0);
    l = qBound(0.0, l, 100.0);

    // Convert HSL to RGB
    double c = (1.0 - qAbs(2.0 * l / 100.0 - 1.0)) * s / 100.0;
    double x = c * (1.0 - qAbs(fmod(h / 60.0, 2.0) - 1.0));
    double m = l / 100.0 - c / 2.0;

    double r, g, b;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    int ri = qRound((r + m) * 255);
    int gi = qRound((g + m) * 255);
    int bi = qRound((b + m) * 255);

    return QString("#%1%2%3")
        .arg(ri, 2, 16, QChar('0'))
        .arg(gi, 2, 16, QChar('0'))
        .arg(bi, 2, 16, QChar('0'));
}

QVariantMap SettingsTheme::generatePalette(double baseHue, double baseSat, double baseLight) const {
    QVariantMap palette;

    // Use color harmony - different hues for different roles
    const double complementary = baseHue + 180.0;      // Opposite
    const double triadic1 = baseHue + 120.0;           // Triadic
    const double triadic2 = baseHue + 240.0;           // Triadic
    const double splitComp1 = baseHue + 150.0;         // Split-complementary
    const double splitComp2 = baseHue + 210.0;         // Split-complementary
    const double analogous1 = baseHue + 30.0;          // Analogous
    const double analogous2 = baseHue - 30.0;          // Analogous

    // Vibrant saturation range
    double sat = qBound(60.0, baseSat, 100.0);
    double light = qBound(45.0, baseLight, 65.0);

    // Core UI colors - use different harmonies for variety!
    palette["primaryColor"] = hslColor(baseHue, sat, light);
    palette["accentColor"] = hslColor(complementary, sat, light);
    palette["secondaryColor"] = hslColor(analogous1, sat * 0.7, 60.0);

    // Backgrounds - GO WILD! Any color, any brightness!
    double bgLight = 5.0 + fmod(baseHue, 60.0);  // 5-65% based on hue - could be dark OR bright!
    double surfLight = 10.0 + fmod(baseHue * 1.5, 50.0);  // 10-60%
    palette["backgroundColor"] = hslColor(complementary, 60.0 + fmod(baseSat, 30.0), bgLight);
    palette["surfaceColor"] = hslColor(triadic1, 55.0 + fmod(baseSat, 35.0), surfLight);
    palette["borderColor"] = hslColor(triadic2, 70.0, 40.0 + fmod(baseHue, 30.0));

    // Text - adaptive! Dark text on light bg, light text on dark bg
    double textLight = (bgLight > 40.0) ? 10.0 : 95.0;  // Dark or light based on background
    double textSecLight = (bgLight > 40.0) ? 25.0 : 70.0;
    palette["textColor"] = hslColor(analogous2, 15.0, textLight);
    palette["textSecondaryColor"] = hslColor(analogous1, 20.0, textSecLight);

    // Status colors - tinted versions of semantic colors
    palette["successColor"] = hslColor(140.0 + (baseHue * 0.1), 80.0, 50.0);
    palette["warningColor"] = hslColor(35.0 + (baseHue * 0.1), 90.0, 55.0);
    palette["errorColor"] = hslColor(fmod(360.0 + baseHue * 0.1, 360.0), 75.0, 55.0);

    // Chart colors - spread across the wheel using golden angle from different starting points
    const double goldenAngle = 137.5;
    palette["pressureColor"] = hslColor(triadic1 + goldenAngle * 0, 80.0, 55.0);
    palette["flowColor"] = hslColor(triadic2 + goldenAngle * 1, 80.0, 55.0);
    palette["temperatureColor"] = hslColor(complementary + goldenAngle * 2, 80.0, 55.0);
    palette["weightColor"] = hslColor(splitComp1 + goldenAngle * 3, 65.0, 50.0);

    // Goal variants - lighter, desaturated versions of chart colors
    palette["pressureGoalColor"] = hslColor(triadic1 + goldenAngle * 0, 55.0, 75.0);
    palette["flowGoalColor"] = hslColor(triadic2 + goldenAngle * 1, 55.0, 75.0);
    palette["temperatureGoalColor"] = hslColor(complementary + goldenAngle * 2, 55.0, 75.0);

    // Weight flow - lighter variant of weight color (same relationship as goal colors)
    palette["weightFlowColor"] = hslColor(splitComp1 + goldenAngle * 3, 55.0, 70.0);

    // Water level - analogous to flow color
    palette["waterLevelColor"] = hslColor(analogous1 + goldenAngle * 2, 70.0, 55.0);

    // Highlight color - analogous to warning for attention-drawing
    palette["highlightColor"] = palette["warningColor"];

    // DYE measurement colors - spread using golden angle from split-complementary
    palette["dyeDoseColor"] = hslColor(splitComp2 + goldenAngle * 0, 50.0, 40.0);
    palette["dyeOutputColor"] = hslColor(splitComp2 + goldenAngle * 1, 65.0, 50.0);
    palette["dyeTdsColor"] = hslColor(splitComp2 + goldenAngle * 2, 75.0, 55.0);
    palette["dyeEyColor"] = hslColor(splitComp2 + goldenAngle * 3, 55.0, 45.0);

    // Button states
    palette["buttonDisabled"] = hslColor(baseHue, 10.0, 35.0);

    // UI indicator colors - derived from status colors
    palette["stopMarkerColor"] = hslColor(fmod(360.0 + baseHue * 0.1, 360.0), 70.0, 65.0);  // Error-ish but lighter
    palette["modifiedIndicatorColor"] = hslColor(50.0 + (baseHue * 0.1), 85.0, 55.0);  // Yellow-ish
    palette["simulationIndicatorColor"] = hslColor(25.0 + (baseHue * 0.1), 85.0, 50.0);  // Orange-ish
    palette["warningButtonColor"] = palette["warningColor"];
    palette["successButtonColor"] = hslColor(140.0 + (baseHue * 0.1), 70.0, 35.0);  // Darker success

    // Frame markers - semi-transparent overlay, adapts to background brightness
    palette["frameMarkerColor"] = (bgLight > 40.0) ? "#66000000" : "#66ffffff";

    // Row alternate backgrounds - very dark/light variants of background
    double rowLight = qBound(3.0, bgLight - 5.0, 15.0);
    palette["rowAlternateColor"] = hslColor(complementary, 20.0, rowLight);
    palette["rowAlternateLightColor"] = hslColor(complementary, 15.0, rowLight + 5.0);

    // Source badge colors - 3 distinct hues spread across the wheel
    palette["sourceBadgeBlueColor"] = hslColor(baseHue + 210.0, 65.0, 55.0);   // Cool tone
    palette["sourceBadgeGreenColor"] = hslColor(baseHue + 90.0, 65.0, 55.0);   // Green-ish
    palette["sourceBadgeOrangeColor"] = hslColor(baseHue + 330.0, 65.0, 55.0); // Warm tone

    // Primary contrast - white on dark primaries, dark on light primaries
    // Use relative luminance of the primary color to decide
    QColor primary(palette["primaryColor"].toString());
    double lum = 0.2126 * primary.redF() + 0.7152 * primary.greenF() + 0.0722 * primary.blueF();
    palette["primaryContrastColor"] = (lum > 0.4) ? "#1a1a2e" : "#ffffff";

    // Derived colors
    palette["focusColor"] = palette["primaryColor"];
    palette["shadowColor"] = "#40000000";

    return palette;
}
