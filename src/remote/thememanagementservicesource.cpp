#include "thememanagementservicesource.h"

#include "core/settings.h"
#include "core/settings_theme.h"

#include <QColor>
#include <QRandomGenerator>
#include <QSet>

#include <initializer_list>

namespace {
constexpr qint64 kMaxSafeRevision = 9007199254740991LL;
}

NativeThemeManagementSource::NativeThemeManagementSource(Settings* settings,
                                                         BackgroundImageCatalog* screensaver,
                                                         QObject* parent)
    : ThemeManagementStateSimpleSource(parent), m_theme(settings ? settings->theme() : nullptr),
      m_screensaver(screensaver)
{
    if (m_theme) {
        connect(m_theme, &SettingsTheme::themeNamesChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::customThemeColorsChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::activeThemeNameChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::isDarkModeChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::themeModeChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::darkThemeNameChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::lightThemeNameChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::glassChromeChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::backgroundPresetChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::backgroundSourceChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::backgroundPatternChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
        connect(m_theme, &SettingsTheme::backgroundImagePathChanged, this, [this] {
            if (!m_suppressChangeSignals) publishSnapshot(true);
        });
    }
    if (m_screensaver) {
        m_screensaverOwner = m_screensaver->ownerObject();
        const auto refresh = [this] { if (!m_suppressChangeSignals) publishSnapshot(true); };
        m_screensaver->watchBackgroundImageChanges(this, refresh);
        connect(m_screensaverOwner, &QObject::destroyed, this, [this] {
            m_screensaver = nullptr;
            publishSnapshot(true);
        });
    }
    publishSnapshot(false);
}

void NativeThemeManagementSource::refresh()
{
    if (!m_theme) {
        setReady(false);
        setErrorMessage(QStringLiteral("Theme settings are unavailable"));
        return;
    }
    setErrorMessage({});
    publishSnapshot(false);
}

bool NativeThemeManagementSource::validName(const QString& name) const
{
    if (name.trimmed() != name || name.isEmpty() || name.size() > 128
        || name == QLatin1String("Default") || isBuiltIn(name))
        return false;
    for (const QChar ch : name)
        if (ch.isNull() || ch.category() == QChar::Other_Control
            || ch == QLatin1Char('/') || ch == QLatin1Char('\\')) return false;
    return true;
}

bool NativeThemeManagementSource::isBuiltIn(const QString& name) const
{
    if (!m_theme) return false;
    for (const QVariant& value : m_theme->getPresetThemes()) {
        const QVariantMap preset = value.toMap();
        if (preset.value(QStringLiteral("name")).toString() == name
            && preset.value(QStringLiteral("isBuiltIn")).toBool()) return true;
    }
    return false;
}

bool NativeThemeManagementSource::isUserTheme(const QString& name) const
{
    for (const QVariant& value : m_presets) {
        const QVariantMap preset = value.toMap();
        if (preset.value(QStringLiteral("name")).toString() == name
            && !preset.value(QStringLiteral("isBuiltIn")).toBool()) return true;
    }
    return false;
}

void NativeThemeManagementSource::publishSnapshot(bool advanceRevision)
{
    if (!m_theme) return;
    QVariantList safePresets;
    const QVariantList canonical = m_theme->getPresetThemes();
    safePresets.reserve(canonical.size());
    for (const QVariant& value : canonical) {
        const QVariantMap preset = value.toMap();
        const QString name = preset.value(QStringLiteral("name")).toString();
        const QString primary = preset.value(QStringLiteral("primaryColor")).toString();
        if (name.isEmpty()) continue;
        QVariantMap safe;
        safe.insert(QStringLiteral("name"), name);
        safe.insert(QStringLiteral("primaryColor"), QColor::isValidColorName(primary) ? primary : QString());
        safe.insert(QStringLiteral("isBuiltIn"), preset.value(QStringLiteral("isBuiltIn")).toBool());
        safePresets.append(safe);
    }
    const QVariantMap canonicalColors = m_theme->customThemeColors();
    const QVariantMap& allowedColors = m_theme->isDarkMode()
        ? SettingsTheme::darkDefaults() : SettingsTheme::lightDefaults();
    QVariantMap colors;
    for (auto it = allowedColors.cbegin(); it != allowedColors.cend(); ++it) {
        const QVariant value = canonicalColors.value(it.key());
        if (value.metaType().id() == QMetaType::QString && value.toString().size() <= 128
            && QColor::isValidColorName(value.toString()))
            colors.insert(it.key(), value.toString());
    }
    const QString name = m_theme->activeThemeName();
    const bool dark = m_theme->isDarkMode();
    const QString themeMode = m_theme->themeMode();
    const QStringList themeNames = m_theme->themeNames();
    const QString darkTheme = m_theme->darkThemeName();
    const QString lightTheme = m_theme->lightThemeName();
    const bool glass = m_theme->glassChrome();
    const QString backgroundPreset = m_theme->backgroundPreset();
    const QString backgroundSource = m_theme->backgroundSource();
    const QString backgroundPattern = m_theme->backgroundPattern();
    const bool shotAdvanced = m_theme->backgroundShotAdvanced();
    const QVariantList backgroundPresets = m_theme->backgroundPresets();
    const QVariantList backgroundPatterns = m_theme->backgroundPatterns();
    const QVariantList backgroundImages = m_screensaver
        ? m_screensaver->backgroundPickerImages() : QVariantList();
    const bool backgroundImageCacheEnabled = m_screensaver
        && m_screensaver->backgroundImageCacheEnabled();
    const QString backgroundImageId = m_screensaver
        ? m_screensaver->backgroundImageTokenForPath(m_theme->backgroundImagePath()) : QString();
    const bool changed = safePresets != m_presets || colors != m_activeColors
        || name != m_activeThemeName || dark != m_darkMode
        || themeMode != m_themeMode || themeNames != m_themeNames
        || darkTheme != m_darkThemeName || lightTheme != m_lightThemeName
        || glass != m_glassChrome || backgroundPreset != m_backgroundPreset
        || backgroundSource != m_backgroundSource || backgroundPattern != m_backgroundPattern
        || shotAdvanced != m_backgroundShotAdvanced || backgroundImages != m_backgroundImages
        || backgroundImageCacheEnabled != m_backgroundImageCacheEnabled
        || backgroundImageId != m_backgroundImageId
        || backgroundPresets != m_backgroundPresets || backgroundPatterns != m_backgroundPatterns;
    QVariantMap palettes;
    const auto safePalette = [this](const QString& mode) {
        const QVariantMap palette = m_theme->paletteColors(mode);
        const QVariantMap& allowed = mode == QLatin1String("dark")
            ? SettingsTheme::darkDefaults() : SettingsTheme::lightDefaults();
        QVariantMap safe;
        for (auto it = allowed.cbegin(); it != allowed.cend(); ++it) {
            const QVariant value = palette.value(it.key());
            if (value.metaType().id() == QMetaType::QString && value.toString().size() <= 128
                && QColor::isValidColorName(value.toString()))
                safe.insert(it.key(), value.toString());
        }
        return safe;
    };
    palettes.insert(QStringLiteral("dark"), safePalette(QStringLiteral("dark")));
    palettes.insert(QStringLiteral("light"), safePalette(QStringLiteral("light")));
    const bool palettesChanged = palettes != m_palettes;
    const bool snapshotChanged = changed || palettesChanged;
    if (!snapshotChanged && !advanceRevision && ready()) return;
    if (advanceRevision && m_revision < kMaxSafeRevision) ++m_revision;
    m_presets = safePresets;
    m_activeColors = colors;
    m_palettes = palettes;
    m_activeThemeName = name;
    m_darkMode = dark;
    m_themeMode = themeMode;
    m_themeNames = themeNames;
    m_darkThemeName = darkTheme;
    m_lightThemeName = lightTheme;
    m_glassChrome = glass;
    m_backgroundPreset = backgroundPreset;
    m_backgroundSource = backgroundSource;
    m_backgroundPattern = backgroundPattern;
    m_backgroundShotAdvanced = shotAdvanced;
    m_backgroundImages = backgroundImages;
    m_backgroundImageCacheEnabled = backgroundImageCacheEnabled;
    m_backgroundImageId = backgroundImageId;
    m_backgroundPresets = backgroundPresets;
    m_backgroundPatterns = backgroundPatterns;
    setPresets(m_presets);
    setActiveColors(m_activeColors);
    setPalettes(m_palettes);
    setActiveThemeName(m_activeThemeName);
    setDarkMode(m_darkMode);
    setRevision(m_revision);
    setThemeMode(m_themeMode);
    setThemeNames(m_themeNames);
    setDarkThemeName(m_darkThemeName);
    setLightThemeName(m_lightThemeName);
    setGlassChrome(m_glassChrome);
    setBackgroundPreset(m_backgroundPreset);
    setBackgroundSource(m_backgroundSource);
    setBackgroundPattern(m_backgroundPattern);
    setBackgroundShotAdvanced(m_backgroundShotAdvanced);
    setBackgroundImages(m_backgroundImages);
    setBackgroundImageCacheEnabled(m_backgroundImageCacheEnabled);
    setBackgroundImageId(m_backgroundImageId);
    setBackgroundPresets(m_backgroundPresets);
    setBackgroundPatterns(m_backgroundPatterns);
    if (!ready()) setReady(true);
    if (!errorMessage().isEmpty()) setErrorMessage({});
}

void NativeThemeManagementSource::finish(const QString& requestId, const QString& operation,
                                          bool success, const QString& reason)
{
    if (!m_requests.finish(requestId)) return;
    emit operationCompleted(requestId, operation, success, m_revision,
        {{QStringLiteral("presets"), m_presets},
         {QStringLiteral("activeColors"), m_activeColors},
         {QStringLiteral("palettes"), m_palettes},
         {QStringLiteral("activeThemeName"), m_activeThemeName},
         {QStringLiteral("darkMode"), m_darkMode},
         {QStringLiteral("themeMode"), m_themeMode},
         {QStringLiteral("themeNames"), m_themeNames},
         {QStringLiteral("darkThemeName"), m_darkThemeName},
         {QStringLiteral("lightThemeName"), m_lightThemeName},
         {QStringLiteral("glassChrome"), m_glassChrome},
         {QStringLiteral("backgroundPreset"), m_backgroundPreset},
         {QStringLiteral("backgroundSource"), m_backgroundSource},
         {QStringLiteral("backgroundPattern"), m_backgroundPattern},
         {QStringLiteral("backgroundShotAdvanced"), m_backgroundShotAdvanced},
         {QStringLiteral("backgroundImages"), m_backgroundImages},
         {QStringLiteral("backgroundImageId"), m_backgroundImageId}}, reason);
}

void NativeThemeManagementSource::execute(QString requestId, QString operation,
                                           QString name, qint64 expectedRevision)
{
    const auto begin = m_requests.begin(requestId);
    if (begin != ServiceRequestIds::Start::Accepted) {
        if (begin == ServiceRequestIds::Start::DuplicateId) return;
        emit operationCompleted(requestId, operation, false, m_revision, {},
            begin == ServiceRequestIds::Start::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_theme) {
        finish(requestId, operation, false, QStringLiteral("unavailable"));
        return;
    }
    if (!ready()) publishSnapshot(false);
    if (expectedRevision != m_revision) {
        finish(requestId, operation, false, QStringLiteral("conflict"));
        return;
    }

    bool success = false;
    m_suppressChangeSignals = true;
    if (operation == QLatin1String("apply")) {
        if (!name.isEmpty() && name.size() <= 128)
            success = m_theme->applyPresetTheme(name);
    } else if (operation == QLatin1String("save")) {
        if (validName(name)) {
            m_theme->saveCurrentTheme(name);
            success = m_theme->themeNames().contains(name);
        }
    } else if (operation == QLatin1String("delete")) {
        if (name.size() <= 128 && isUserTheme(name) && !isBuiltIn(name)) {
            m_theme->deleteUserTheme(name);
            success = !m_theme->themeNames().contains(name);
        }
    } else if (operation == QLatin1String("random")) {
        const double hue = QRandomGenerator::global()->generateDouble() * 360.0;
        const double saturation = 65.0 + QRandomGenerator::global()->generateDouble() * 20.0;
        const double lightness = 50.0 + QRandomGenerator::global()->generateDouble() * 10.0;
        m_theme->setCustomThemeColors(m_theme->generatePalette(hue, saturation, lightness));
        m_theme->setActiveThemeName(QStringLiteral("Custom"));
        success = true;
    }
    m_suppressChangeSignals = false;
    if (success) publishSnapshot(true);
    finish(requestId, operation, success, success ? QString() : QStringLiteral("invalidOperationOrName"));
}

void NativeThemeManagementSource::setPaletteColor(QString requestId, QString palette,
                                                   QString colorName, QString colorValue,
                                                   qint64 expectedRevision)
{
    const QString operation = QStringLiteral("setPaletteColor");
    const auto begin = m_requests.begin(requestId);
    if (begin != ServiceRequestIds::Start::Accepted) {
        if (begin == ServiceRequestIds::Start::DuplicateId) return;
        emit operationCompleted(requestId, operation, false, m_revision, {},
            begin == ServiceRequestIds::Start::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_theme) {
        finish(requestId, operation, false, QStringLiteral("unavailable"));
        return;
    }
    if (!ready()) publishSnapshot(false);
    if (expectedRevision != m_revision) {
        finish(requestId, operation, false, QStringLiteral("conflict"));
        return;
    }
    const bool validPalette = palette == QLatin1String("dark") || palette == QLatin1String("light");
    const QVariantMap& allowed = palette == QLatin1String("dark")
        ? SettingsTheme::darkDefaults() : SettingsTheme::lightDefaults();
    if (!validPalette || !allowed.contains(colorName) || colorName.size() > 64
        || colorValue.size() > 128 || !QColor::isValidColorName(colorValue)) {
        finish(requestId, operation, false, QStringLiteral("invalidColor"));
        return;
    }
    m_suppressChangeSignals = true;
    m_theme->setPaletteColor(palette, colorName, colorValue);
    m_suppressChangeSignals = false;
    publishSnapshot(true);
    finish(requestId, operation, true);
}

void NativeThemeManagementSource::executeSetting(QString requestId, QString operation,
                                                  QVariantMap arguments,
                                                  qint64 expectedRevision)
{
    const auto begin = m_requests.begin(requestId);
    if (begin != ServiceRequestIds::Start::Accepted) {
        if (begin == ServiceRequestIds::Start::DuplicateId) return;
        emit operationCompleted(requestId, operation, false, m_revision, {},
            begin == ServiceRequestIds::Start::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_theme) {
        finish(requestId, operation, false, QStringLiteral("unavailable"));
        return;
    }
    if (!ready()) publishSnapshot(false);
    if (expectedRevision != m_revision) {
        finish(requestId, operation, false, QStringLiteral("conflict"));
        return;
    }

    bool success = false;
    const QVariant value = arguments.value(QStringLiteral("value"));
    const QString text = value.toString();
    const auto exactKeys = [&arguments](std::initializer_list<QString> expected) {
        if (arguments.size() != static_cast<qsizetype>(expected.size())) return false;
        for (const QString& key : expected)
            if (!arguments.contains(key)) return false;
        return true;
    };
    const auto boundedString = [](const QVariant& candidate, qsizetype maxLength,
                                  bool allowEmpty = false) {
        return candidate.metaType().id() == QMetaType::QString
            && candidate.toString().size() <= maxLength
            && (allowEmpty || !candidate.toString().isEmpty());
    };
    const auto validOpaqueImageId = [](const QString& id) {
        if (id.size() != 64) return false;
        for (const QChar ch : id) {
            if (!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                  || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))))
                return false;
        }
        return true;
    };
    m_suppressChangeSignals = true;
    if (operation == QLatin1String("setThemeMode")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 16)
            && (text == QLatin1String("dark") || text == QLatin1String("light")
                || text == QLatin1String("system"))) {
            m_theme->setThemeMode(text);
            success = true;
        }
    } else if (operation == QLatin1String("applyDarkTheme")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 128)
            && m_theme->themeNames().contains(text)) {
            m_theme->applyDarkTheme(text);
            success = true;
        }
    } else if (operation == QLatin1String("applyLightTheme")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 128)
            && m_theme->themeNames().contains(text)) {
            m_theme->applyLightTheme(text);
            success = true;
        }
    } else if (operation == QLatin1String("setGlassChrome")) {
        if (exactKeys({QStringLiteral("value")})
            && value.metaType().id() == QMetaType::Bool) {
            m_theme->setGlassChrome(value.toBool());
            success = true;
        }
    } else if (operation == QLatin1String("setBackgroundPreset")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 128, true)) {
            bool valid = text.isEmpty();
            for (const QVariant& row : m_theme->backgroundPresets())
                valid = valid || row.toMap().value(QStringLiteral("id")).toString() == text;
            if (valid) {
                m_theme->setBackgroundPreset(text);
                success = true;
            }
        }
    } else if (operation == QLatin1String("setBackgroundPattern")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 128, true)) {
            bool valid = text.isEmpty();
            for (const QVariant& row : m_theme->backgroundPatterns())
                valid = valid || row.toMap().value(QStringLiteral("id")).toString() == text;
            if (valid) {
                m_theme->setBackgroundPattern(text);
                success = true;
            }
        }
    } else if (operation == QLatin1String("setShotChartBackground")) {
        if (exactKeys({QStringLiteral("value")})
            && value.metaType().id() == QMetaType::Bool) {
            m_theme->selectShotChartBackground(value.toBool());
            success = true;
        }
    } else if (operation == QLatin1String("setBackgroundImage")) {
        if (exactKeys({QStringLiteral("value")}) && boundedString(value, 64)
            && validOpaqueImageId(text) && m_screensaver) {
            const QString path = m_screensaver->resolveBackgroundImageToken(text);
            if (!path.isEmpty()) {
                m_theme->setBackgroundImagePath(path);
                success = true;
            }
        }
    } else if (operation == QLatin1String("setBackgroundSelection")) {
        const bool validSchema = exactKeys({QStringLiteral("source"), QStringLiteral("presetId"),
                                            QStringLiteral("imageId"), QStringLiteral("shotAdvanced"),
                                            QStringLiteral("patternId")})
            && boundedString(arguments.value(QStringLiteral("source")), 16)
            && boundedString(arguments.value(QStringLiteral("presetId")), 128, true)
            && boundedString(arguments.value(QStringLiteral("imageId")), 64, true)
            && arguments.value(QStringLiteral("shotAdvanced")).metaType().id() == QMetaType::Bool
            && boundedString(arguments.value(QStringLiteral("patternId")), 128, true);
        const QString source = validSchema
            ? arguments.value(QStringLiteral("source")).toString() : QString();
        const QString pattern = validSchema
            ? arguments.value(QStringLiteral("patternId")).toString() : QString();
        bool validPattern = pattern.isEmpty();
        for (const QVariant& row : m_theme->backgroundPatterns())
            validPattern = validPattern || row.toMap().value(QStringLiteral("id")).toString() == pattern;
        const QString preset = validSchema
            ? arguments.value(QStringLiteral("presetId")).toString() : QString();
        const QString imageId = validSchema
            ? arguments.value(QStringLiteral("imageId")).toString() : QString();
        const bool shotAdvanced = arguments.value(QStringLiteral("shotAdvanced")).toBool();
        bool validPreset = preset.isEmpty();
        for (const QVariant& row : m_theme->backgroundPresets())
            validPreset = validPreset || row.toMap().value(QStringLiteral("id")).toString() == preset;
        const bool validChoice = validSchema && ((source == QLatin1String("none")
                && preset.isEmpty() && imageId.isEmpty())
            || (source == QLatin1String("colour") && validPreset && !preset.isEmpty()
                && imageId.isEmpty())
            || (source == QLatin1String("image") && preset.isEmpty() && m_screensaver
                && validOpaqueImageId(imageId)
                && !m_screensaver->resolveBackgroundImageToken(imageId).isEmpty())
            || (source == QLatin1String("shot")
                && preset.isEmpty() && imageId.isEmpty()));
        if (validChoice && validPattern) {
            if (source == QLatin1String("none")) m_theme->clearBackground();
            else if (source == QLatin1String("colour")) m_theme->setBackgroundPreset(preset);
            else if (source == QLatin1String("image"))
                m_theme->setBackgroundImagePath(m_screensaver->resolveBackgroundImageToken(imageId));
            else m_theme->selectShotChartBackground(shotAdvanced);
            m_theme->setBackgroundPattern(pattern);
            success = true;
        }
    }
    m_suppressChangeSignals = false;
    if (success) publishSnapshot(true);
    finish(requestId, operation, success,
           success ? QString() : QStringLiteral("invalidSetting"));
}
