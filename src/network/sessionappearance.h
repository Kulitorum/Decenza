#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "../core/backgroundpresets.h"
#include "../core/graphpreferences.h"

#include <cmath>

// Shared host/client schema for canonical Theme.qml inputs, font-size roles,
// display units, shader settings, and bounded background metadata. Image bytes
// use a revision-addressed same-origin endpoint; publishers supply no path/URL.
namespace SessionAppearance {

inline const QStringList& fontSizeRoles()
{
    static const QStringList roles{
        QStringLiteral("headingSize"), QStringLiteral("titleSize"),
        QStringLiteral("subtitleSize"), QStringLiteral("bodySize"),
        QStringLiteral("labelSize"), QStringLiteral("captionSize"),
        QStringLiteral("valueSize"), QStringLiteral("timerSize")
    };
    return roles;
}

// Custom palette inputs consumed directly by qml/Theme.qml.
inline const QStringList& themeCustomColorKeys()
{
    static const QStringList keys{
        QStringLiteral("accentColor"), QStringLiteral("actionButtonContentColor"),
        QStringLiteral("backgroundColor"), QStringLiteral("borderColor"),
        QStringLiteral("bottomBarColor"), QStringLiteral("buttonDisabled"),
        QStringLiteral("conductanceColor"), QStringLiteral("conductanceDerivativeColor"),
        QStringLiteral("darcyResistanceColor"), QStringLiteral("dyeDoseColor"),
        QStringLiteral("dyeEyColor"), QStringLiteral("dyeOutputColor"),
        QStringLiteral("dyeTdsColor"), QStringLiteral("errorColor"),
        QStringLiteral("flowColor"), QStringLiteral("flowGoalColor"),
        QStringLiteral("frameMarkerColor"), QStringLiteral("highlightColor"),
        QStringLiteral("iconColor"), QStringLiteral("modifiedIndicatorColor"),
        QStringLiteral("portalEcColor"), QStringLiteral("portalTemperatureColor"),
        QStringLiteral("pressureColor"), QStringLiteral("pressureGoalColor"),
        QStringLiteral("primaryColor"), QStringLiteral("primaryContrastColor"),
        QStringLiteral("resistanceColor"), QStringLiteral("rowAlternateColor"),
        QStringLiteral("rowAlternateLightColor"), QStringLiteral("secondaryColor"),
        QStringLiteral("simulationIndicatorColor"), QStringLiteral("sourceBadgeBlueColor"),
        QStringLiteral("sourceBadgeGreenColor"), QStringLiteral("sourceBadgeOrangeColor"),
        QStringLiteral("stopMarkerColor"), QStringLiteral("successButtonColor"),
        QStringLiteral("successColor"), QStringLiteral("surfaceColor"),
        QStringLiteral("temperatureColor"), QStringLiteral("temperatureGoalColor"),
        QStringLiteral("temperatureMixColor"), QStringLiteral("temperatureMixGoalColor"),
        QStringLiteral("textColor"), QStringLiteral("textSecondaryColor"),
        QStringLiteral("trackDriftingColor"), QStringLiteral("trackOffTargetColor"),
        QStringLiteral("trackOnTargetColor"), QStringLiteral("warningButtonColor"),
        QStringLiteral("warningColor"), QStringLiteral("waterLevelColor"),
        QStringLiteral("weightColor"), QStringLiteral("weightFlowColor")
    };
    return keys;
}

inline const QStringList& defaultThemeColorKeys()
{
    static const QStringList keys{
        QStringLiteral("accentColor"), QStringLiteral("actionButtonContentColor"),
        QStringLiteral("backgroundColor"), QStringLiteral("borderColor"),
        QStringLiteral("bottomBarColor"), QStringLiteral("buttonDisabled"),
        QStringLiteral("dyeDoseColor"), QStringLiteral("dyeEyColor"),
        QStringLiteral("dyeOutputColor"), QStringLiteral("dyeTdsColor"),
        QStringLiteral("errorColor"), QStringLiteral("flowColor"),
        QStringLiteral("flowGoalColor"), QStringLiteral("frameMarkerColor"),
        QStringLiteral("highlightColor"), QStringLiteral("iconColor"),
        QStringLiteral("modifiedIndicatorColor"), QStringLiteral("portalEcColor"),
        QStringLiteral("portalTemperatureColor"), QStringLiteral("pressureColor"),
        QStringLiteral("pressureGoalColor"), QStringLiteral("primaryColor"),
        QStringLiteral("primaryContrastColor"), QStringLiteral("resistanceColor"),
        QStringLiteral("rowAlternateColor"), QStringLiteral("rowAlternateLightColor"),
        QStringLiteral("secondaryColor"), QStringLiteral("simulationIndicatorColor"),
        QStringLiteral("sourceBadgeBlueColor"), QStringLiteral("sourceBadgeGreenColor"),
        QStringLiteral("sourceBadgeOrangeColor"), QStringLiteral("stopMarkerColor"),
        QStringLiteral("successButtonColor"), QStringLiteral("successColor"),
        QStringLiteral("surfaceColor"), QStringLiteral("temperatureColor"),
        QStringLiteral("temperatureGoalColor"), QStringLiteral("textColor"),
        QStringLiteral("textSecondaryColor"), QStringLiteral("trackDriftingColor"),
        QStringLiteral("trackOffTargetColor"), QStringLiteral("trackOnTargetColor"),
        QStringLiteral("warningButtonColor"), QStringLiteral("warningColor"),
        QStringLiteral("waterLevelColor"), QStringLiteral("weightColor"),
        QStringLiteral("weightFlowColor")
    };
    return keys;
}

inline const QStringList& crtShaderParameterKeys()
{
    static const QStringList keys{
        QStringLiteral("scanlineIntensity"), QStringLiteral("scanlineSize"),
        QStringLiteral("noiseIntensity"), QStringLiteral("bloomStrength"),
        QStringLiteral("aberration"), QStringLiteral("jitterAmount"),
        QStringLiteral("vignetteStrength"), QStringLiteral("tintStrength"),
        QStringLiteral("flickerAmount"), QStringLiteral("glitchRate"),
        QStringLiteral("glowStart"), QStringLiteral("noiseSize"),
        QStringLiteral("reflectionStrength"), QStringLiteral("colorGain"),
        QStringLiteral("colorContrast"), QStringLiteral("colorSaturation"),
        QStringLiteral("hueShift")
    };
    return keys;
}

inline bool isHexColor(const QString& value)
{
    if ((value.size() != 7 && value.size() != 9) || value.front() != QLatin1Char('#'))
        return false;
    for (qsizetype index = 1; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (!(character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            && !(character >= QLatin1Char('a') && character <= QLatin1Char('f'))
            && !(character >= QLatin1Char('A') && character <= QLatin1Char('F'))) {
            return false;
        }
    }
    return true;
}

inline bool validateBackground(const QVariant& value, QVariantMap* validated = nullptr)
{
    if (value.metaType().id() != QMetaType::QVariantMap)
        return false;
    const QVariantMap background = value.toMap();
    if (background.size() != 4
        || !background.contains(QStringLiteral("source"))
        || !background.contains(QStringLiteral("patternId"))
        || !background.contains(QStringLiteral("patternInk"))
        || !background.contains(QStringLiteral("imageRevision")))
        return false;
    const QVariant sourceValue = background.value(QStringLiteral("source"));
    const QVariant patternValue = background.value(QStringLiteral("patternId"));
    const QVariant inkValue = background.value(QStringLiteral("patternInk"));
    const QVariant revisionValue = background.value(QStringLiteral("imageRevision"));
    if (sourceValue.metaType().id() != QMetaType::QString
        || patternValue.metaType().id() != QMetaType::QString
        || inkValue.metaType().id() != QMetaType::QString
        || revisionValue.metaType().id() != QMetaType::QString)
        return false;
    const QString source = sourceValue.toString();
    const QString patternId = patternValue.toString();
    const QString revision = revisionValue.toString();
    if (source != QStringLiteral("none") && source != QStringLiteral("colour")
        && source != QStringLiteral("image") && source != QStringLiteral("shot"))
        return false;
    if ((!patternId.isEmpty() && !BackgroundPresets::hasPattern(patternId))
        || !isHexColor(inkValue.toString())
        || revision.size() != 32)
        return false;
    for (QChar character : revision) {
        if (!(character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            && !(character >= QLatin1Char('a') && character <= QLatin1Char('f')))
            return false;
    }
    if (validated)
        *validated = background;
    return true;
}

inline bool validate(const QVariantMap& value, QVariantMap* validated = nullptr,
                     QString* error = nullptr)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (value.size() != 6 || !value.contains(QStringLiteral("darkMode"))
        || !value.contains(QStringLiteral("fahrenheit"))
        || !value.contains(QStringLiteral("fontSizes"))
        || !value.contains(QStringLiteral("background"))
        || !value.contains(QStringLiteral("themeInputs"))
        || !value.contains(QStringLiteral("graph"))) {
        return fail(QStringLiteral("Appearance requires darkMode, fahrenheit, fontSizes, background, themeInputs, and graph"));
    }
    if (value.value(QStringLiteral("darkMode")).metaType().id() != QMetaType::Bool
        || value.value(QStringLiteral("fahrenheit")).metaType().id() != QMetaType::Bool) {
        return fail(QStringLiteral("Appearance display units must be boolean"));
    }
    if (!GraphPreferences::validate(value.value(QStringLiteral("graph")).toMap()))
        return fail(QStringLiteral("Appearance graph preferences are invalid"));
    QVariantMap normalizedBackground;
    if (!validateBackground(value.value(QStringLiteral("background")), &normalizedBackground))
        return fail(QStringLiteral("Appearance background is missing or invalid"));

    const QVariant themeInputsValue = value.value(QStringLiteral("themeInputs"));
    if (themeInputsValue.metaType().id() != QMetaType::QVariantMap)
        return fail(QStringLiteral("Appearance themeInputs must be an object"));
    const QVariantMap themeInputs = themeInputsValue.toMap();
    const QStringList themeInputKeys{
        QStringLiteral("customThemeColors"), QStringLiteral("backgroundPreset"),
        QStringLiteral("glassChrome"), QStringLiteral("flashColorName"),
        QStringLiteral("flashPhase"), QStringLiteral("bundledFontFamily"),
        QStringLiteral("symbolFontFamily"), QStringLiteral("activeShader"),
        QStringLiteral("shaderParams"), QStringLiteral("use12HourTime"),
        QStringLiteral("backgroundShotAdvanced")
    };
    if (themeInputs.size() != themeInputKeys.size())
        return fail(QStringLiteral("Appearance themeInputs must provide every required theme input"));
    for (const QString& key : themeInputKeys) {
        if (!themeInputs.contains(key))
            return fail(QStringLiteral("Appearance theme input %1 is missing").arg(key));
    }
    const QVariant customColorsValue = themeInputs.value(QStringLiteral("customThemeColors"));
    if (customColorsValue.metaType().id() != QMetaType::QVariantMap)
        return fail(QStringLiteral("Appearance customThemeColors must be an object"));
    const QVariantMap customColors = customColorsValue.toMap();
    if (customColors.size() < defaultThemeColorKeys().size() || customColors.size() > 128)
        return fail(QStringLiteral("Appearance customThemeColors has an invalid number of colors"));
    for (const QString& key : defaultThemeColorKeys()) {
        if (!customColors.contains(key))
            return fail(QStringLiteral("Appearance custom theme color %1 is missing").arg(key));
    }
    QVariantMap normalizedCustomColors;
    for (auto it = customColors.cbegin(); it != customColors.cend(); ++it) {
        // SettingsTheme merges stored palette JSON onto built-in defaults and
        // preserves future/legacy keys. Theme.qml only reads the allowlisted
        // values above, so discard unrelated keys instead of making an otherwise
        // usable native palette incompatible with its paired browser bundle.
        if (!themeCustomColorKeys().contains(it.key()))
            continue;
        if (it->metaType().id() != QMetaType::QString || !isHexColor(it->toString()))
            return fail(QStringLiteral("Appearance custom theme color %1 is invalid").arg(it.key()));
        normalizedCustomColors.insert(it.key(), it.value());
    }
    const QVariant presetValue = themeInputs.value(QStringLiteral("backgroundPreset"));
    const QVariant flashNameValue = themeInputs.value(QStringLiteral("flashColorName"));
    const QVariant bundledFontValue = themeInputs.value(QStringLiteral("bundledFontFamily"));
    const QVariant symbolFontValue = themeInputs.value(QStringLiteral("symbolFontFamily"));
    const QVariant flashPhaseValue = themeInputs.value(QStringLiteral("flashPhase"));
    const QVariant glassValue = themeInputs.value(QStringLiteral("glassChrome"));
    const QVariant activeShaderValue = themeInputs.value(QStringLiteral("activeShader"));
    const QVariant shaderParamsValue = themeInputs.value(QStringLiteral("shaderParams"));
    const QVariant use12HourValue = themeInputs.value(QStringLiteral("use12HourTime"));
    if (presetValue.metaType().id() != QMetaType::QString
        || (!presetValue.toString().isEmpty()
            && !BackgroundPresets::hasColour(presetValue.toString())))
        return fail(QStringLiteral("Appearance backgroundPreset is invalid"));
    if (glassValue.metaType().id() != QMetaType::Bool
        || use12HourValue.metaType().id() != QMetaType::Bool
        || themeInputs.value(QStringLiteral("backgroundShotAdvanced")).metaType().id() != QMetaType::Bool)
        return fail(QStringLiteral("Appearance theme switches must be boolean"));
    if (activeShaderValue.metaType().id() != QMetaType::QString
        || (!activeShaderValue.toString().isEmpty()
            && activeShaderValue.toString() != QStringLiteral("crt"))
        || shaderParamsValue.metaType().id() != QMetaType::QVariantMap)
        return fail(QStringLiteral("Appearance shader settings are invalid"));
    const QString activeShader = activeShaderValue.toString();
    const QVariantMap shaderParams = shaderParamsValue.toMap();
    if (shaderParams.size() > crtShaderParameterKeys().size()
        || (activeShader != QStringLiteral("crt") && !shaderParams.isEmpty()))
        return fail(QStringLiteral("Appearance shader parameters are invalid"));
    QVariantMap normalizedShaderParams;
    for (auto it = shaderParams.cbegin(); it != shaderParams.cend(); ++it) {
        if (!crtShaderParameterKeys().contains(it.key()))
            return fail(QStringLiteral("Appearance shader parameter %1 is unknown").arg(it.key()));
        const int type = it->metaType().id();
        if (type != QMetaType::Int && type != QMetaType::UInt
            && type != QMetaType::LongLong && type != QMetaType::ULongLong
            && type != QMetaType::Double)
            return fail(QStringLiteral("Appearance shader parameter %1 is invalid").arg(it.key()));
        bool ok = false;
        const double number = it->toDouble(&ok);
        if (!ok || !std::isfinite(number) || std::abs(number) > 1000000.0)
            return fail(QStringLiteral("Appearance shader parameter %1 is outside bounds")
                            .arg(it.key()));
        normalizedShaderParams.insert(it.key(), number);
    }
    const auto validText = [](const QVariant& input, qsizetype maxBytes) {
        return input.metaType().id() == QMetaType::QString
            && input.toString().toUtf8().size() <= maxBytes;
    };
    if (!validText(flashNameValue, 128) || !validText(bundledFontValue, 128)
        || !validText(symbolFontValue, 128))
        return fail(QStringLiteral("Appearance theme text input is invalid or too long"));
    const int flashPhaseType = flashPhaseValue.metaType().id();
    if (flashPhaseType != QMetaType::Int && flashPhaseType != QMetaType::UInt
        && flashPhaseType != QMetaType::LongLong && flashPhaseType != QMetaType::ULongLong
        && flashPhaseType != QMetaType::Double)
        return fail(QStringLiteral("Appearance flashPhase must be an integer"));
    bool flashPhaseOk = false;
    const double flashPhaseNumber = flashPhaseValue.toDouble(&flashPhaseOk);
    if (!flashPhaseOk || !std::isfinite(flashPhaseNumber)
        || std::floor(flashPhaseNumber) != flashPhaseNumber
        || flashPhaseNumber < 0 || flashPhaseNumber > 1000000)
        return fail(QStringLiteral("Appearance flashPhase is outside 0..1000000"));
    const int flashPhase = static_cast<int>(flashPhaseNumber);

    // SettingsTheme::fontRoles() caps all supported roles at 120 px; its
    // effectiveFontSizes() also preserves positive legacy values, so accept
    // those within the same globally bounded 1..120 range rather than applying
    // each role's newer slider minimum. See src/core/settings_theme.cpp:949-977.
    const QVariant fontSizesValue = value.value(QStringLiteral("fontSizes"));
    if (fontSizesValue.metaType().id() != QMetaType::QVariantMap)
        return fail(QStringLiteral("Appearance fontSizes must be an object"));
    const QVariantMap fontSizes = fontSizesValue.toMap();
    if (fontSizes.size() != fontSizeRoles().size())
        return fail(QStringLiteral("Appearance must provide every required font size"));
    QVariantMap normalizedFontSizes;
    for (const QString& role : fontSizeRoles()) {
        const auto it = fontSizes.constFind(role);
        if (it == fontSizes.cend())
            return fail(QStringLiteral("Appearance font size %1 is missing or invalid").arg(role));
        const int type = it->metaType().id();
        if (type != QMetaType::Int && type != QMetaType::UInt
            && type != QMetaType::LongLong && type != QMetaType::ULongLong
            && type != QMetaType::Double) {
            return fail(QStringLiteral("Appearance font size %1 is missing or invalid").arg(role));
        }
        bool ok = false;
        const double size = it->toDouble(&ok);
        if (!ok || !std::isfinite(size) || size < 1 || size > 120 || std::floor(size) != size)
            return fail(QStringLiteral("Appearance font size %1 is outside 1..120 px").arg(role));
        normalizedFontSizes.insert(role, static_cast<int>(size));
    }

    if (validated) {
        *validated = value;
        validated->insert(QStringLiteral("fontSizes"), normalizedFontSizes);
        validated->insert(QStringLiteral("background"), normalizedBackground);
        QVariantMap normalizedThemeInputs = themeInputs;
        normalizedThemeInputs.insert(QStringLiteral("customThemeColors"), normalizedCustomColors);
        normalizedThemeInputs.insert(QStringLiteral("flashPhase"), flashPhase);
        normalizedThemeInputs.insert(QStringLiteral("shaderParams"), normalizedShaderParams);
        validated->insert(QStringLiteral("themeInputs"), normalizedThemeInputs);
    }
    if (error)
        error->clear();
    return true;
}

inline QJsonObject toJson(const QVariantMap& value)
{
    QVariantMap validated;
    if (!validate(value, &validated))
        return {};
    return QJsonObject::fromVariantMap(validated);
}

} // namespace SessionAppearance
