#pragma once

#include "themefonts.h"

#include <QList>
#include <QVariantList>
#include <QVariantMap>

// Shared editor bounds/defaults and CRT uniform defaults. No host state.
namespace ThemeEditorOptions {
struct ShaderParameter {
    const char* id;
    const char* label;
    double minimum;
    double maximum;
    double step;
    double defaultValue;
};

inline const QList<ShaderParameter>& shaderParameters()
{
    static const QList<ShaderParameter> values = {
        {"scanlineIntensity", "Scanline intensity", 0, .5, .01, .36},
        {"scanlineSize", "Scanline size (px)", 1, 10, .5, 4.5},
        {"noiseIntensity", "Noise", 0, .15, .005, .08},
        {"noiseSize", "Noise detail", 1, 10, .5, 3.5},
        {"bloomStrength", "Glow / bloom", 0, .8, .02, .52},
        {"glowStart", "Glow start", 0, 1, .05, 0},
        {"aberration", "Colour fringing", 0, 4, .1, 0},
        {"jitterAmount", "Line jitter", 0, 3, .1, 1.4},
        {"vignetteStrength", "Vignette", 0, 1.5, .05, 1.4},
        {"tintStrength", "Phosphor tint", 0, 1, .01, 1},
        {"flickerAmount", "Flicker", 0, .2, .005, .05},
        {"glitchRate", "Glitch lines", 0, 1, .05, 1},
        {"reflectionStrength", "Glass reflection", 0, .3, .01, .22},
        {"colorGain", "Gain", .5, 3, .05, 1},
        {"colorContrast", "Contrast", .5, 3, .05, 1},
        {"colorSaturation", "Saturation", 0, 3, .05, 1},
        {"hueShift", "Hue shift", 0, 360, 1, 0}
    };
    return values;
}

inline QVariantMap shaderDefaults()
{
    QVariantMap result;
    for (const auto& parameter : shaderParameters())
        result.insert(QString::fromLatin1(parameter.id), parameter.defaultValue);
    return result;
}

inline QVariantList shaderControls()
{
    QVariantList result;
    for (const auto& parameter : shaderParameters()) {
        const QString id = QString::fromLatin1(parameter.id);
        result.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("labelKey"), QStringLiteral("settings.themes.crt.") + id},
            {QStringLiteral("label"), QString::fromLatin1(parameter.label)},
            {QStringLiteral("minimum"), parameter.minimum},
            {QStringLiteral("maximum"), parameter.maximum},
            {QStringLiteral("step"), parameter.step},
            {QStringLiteral("defaultValue"), parameter.defaultValue}});
    }
    return result;
}

inline QVariantList fontControls()
{
    QVariantList result;
    for (auto it = ThemeFonts::roles().cbegin(); it != ThemeFonts::roles().cend(); ++it) {
        QString label = it.key().chopped(4); // Canonical roles end in "Size".
        label[0] = label.at(0).toUpper();
        result.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("labelKey"), QStringLiteral("settings.themes.font.") + it.key()},
            {QStringLiteral("label"), label},
            {QStringLiteral("minimum"), it.value().min},
            {QStringLiteral("maximum"), it.value().max},
            {QStringLiteral("step"), 1},
            {QStringLiteral("defaultValue"), it.value().def}});
    }
    return result;
}
} // namespace ThemeEditorOptions
