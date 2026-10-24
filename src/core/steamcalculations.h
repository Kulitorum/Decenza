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

inline double netMilkForPitcher(const QVariantMap& preset, double scaleReading)
{
    if (preset.isEmpty() || preset.value(QStringLiteral("disabled")).toBool())
        return 0.0;
    const double pitcherWeight = preset.value(QStringLiteral("pitcherWeightG"), 0.0).toDouble();
    if (pitcherWeight <= 0.0)
        return 0.0;
    const double milk = scaleReading - pitcherWeight;
    return (milk >= 50.0 && milk <= 1500.0) ? milk : 0.0;
}

inline int scaledSteamTime(const QVariantMap& preset, bool milkAutoCaptureEnabled,
                           double secondsPerGram, double milkG)
{
    if (!milkAutoCaptureEnabled || preset.isEmpty()
        || preset.value(QStringLiteral("disabled")).toBool()
        || !std::isfinite(milkG) || !std::isfinite(secondsPerGram)
        || milkG <= 0.0 || secondsPerGram <= 0.0)
        return 0;
    const double bounded = qBound(5.0, secondsPerGram * milkG, 120.0);
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
                                             bool milkAutoCaptureEnabled,
                                             double secondsPerGram, double milkG)
{
    SteamDuration result;
    result.presetMissing = preset.isEmpty();
    if (result.presetMissing)
        return result;
    result.heaterOff = preset.value(QStringLiteral("disabled")).toBool();
    if (result.heaterOff)
        return result;

    result.scaledSeconds = scaledSteamTime(preset, milkAutoCaptureEnabled,
                                           secondsPerGram, milkG);
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
