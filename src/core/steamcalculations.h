#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QtMath>
#include <cmath>
#include <limits>

namespace SteamCalculations {

inline QString heaterOffEnglishLabel()
{
    return QStringLiteral("Heater off");
}

inline int steamPitcherCount(const QVariantList& displayPresets)
{
    if (displayPresets.isEmpty())
        return 0;
    const qsizetype count = displayPresets.size() - 1;
    return count <= std::numeric_limits<int>::max() ? static_cast<int>(count) : 0;
}

inline bool nameTakenIn(const QVariantList& presets, const QString& name, int ignoreIndex = -1)
{
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return false;

    for (qsizetype index = 0; index < presets.size(); ++index) {
        if (index == ignoreIndex)
            continue;
        if (presets.at(index).toMap().value(QStringLiteral("name")).toString()
                .trimmed().compare(wanted, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

inline bool steamPitcherNameTaken(const QVariantList& presets, const QString& name,
                                  int ignoreIndex = -1)
{
    if (name.trimmed().compare(heaterOffEnglishLabel(), Qt::CaseInsensitive) == 0)
        return true;
    return nameTakenIn(presets, name, ignoreIndex);
}

// Upper bound for a rate ENTERED by hand or over MCP. A rate measured from a
// pour, or carried over from the old global setting, is stored as it is.
inline constexpr double kMaxSecondsPerGram = 2.0;

// A preset's own steam rate in seconds per gram of milk. 0 = uncalibrated.
inline double presetSecondsPerGram(const QVariantMap& preset)
{
    const double rate = preset.value(QStringLiteral("secondsPerGram")).toDouble();
    return std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
}

// Why a milk weight cannot scale this preset's steam time, or nullptr when it can.
inline const char* steamScalingBlocker(const QVariantMap& preset, bool milkAutoCaptureEnabled)
{
    if (!milkAutoCaptureEnabled)
        return "weight-timed steaming off";
    if (preset.isEmpty())
        return "no pitcher preset selected";
    if (preset.value(QStringLiteral("disabled")).toBool())
        return "heater-off preset";
    if (presetSecondsPerGram(preset) <= 0.0)
        return "pitcher has no steam seconds-per-gram rate";
    return nullptr;
}

inline int scaledSteamTime(const QVariantMap& preset, bool milkAutoCaptureEnabled, double milkG)
{
    if (steamScalingBlocker(preset, milkAutoCaptureEnabled)
        || !std::isfinite(milkG) || milkG <= 0.0)
        return 0;
    const double bounded = qBound(5.0, presetSecondsPerGram(preset) * milkG, 120.0);
    return qRound(bounded);
}

struct SteamDuration {
    int scaledSeconds = 0;
    int baseSeconds = 0;
    int effectiveSeconds = 0;
    bool presetMissing = false;
    bool heaterOff = false;
    bool baseDurationInvalid = false;
};

inline SteamDuration effectiveSteamDuration(const QVariantMap& preset,
                                             bool milkAutoCaptureEnabled, double milkG)
{
    SteamDuration result;
    result.presetMissing = preset.isEmpty();
    if (result.presetMissing)
        return result;
    result.heaterOff = preset.value(QStringLiteral("disabled")).toBool();
    if (result.heaterOff)
        return result;

    result.scaledSeconds = scaledSteamTime(preset, milkAutoCaptureEnabled, milkG);
    if (result.scaledSeconds > 0) {
        result.effectiveSeconds = result.scaledSeconds;
        return result;
    }
    const double base = preset.value(QStringLiteral("duration"), 0.0).toDouble();
    if (!std::isfinite(base)
        || base < std::numeric_limits<int>::min()
        || base > std::numeric_limits<int>::max()) {
        result.baseDurationInvalid = true;
        return result;
    }
    result.baseSeconds = qRound(base);
    result.effectiveSeconds = result.baseSeconds;
    return result;
}

} // namespace SteamCalculations
