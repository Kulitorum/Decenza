#pragma once

#include "settings_brew.h"
#include "settings_hardware.h"
#include "wateramount.h"

#include <QVariantMap>
#include <QMetaType>
#include <cmath>
#include <limits>

#include <functional>

// Shared settings-only half of Home preset selection. MainController supplies
// the machine-apply callback so callers do not duplicate preset field mapping.
namespace MachineControlPresets {

// Apply recipe values without changing an existing shared vessel's fixed row.
// Reusing a resolved ratio amount here also repairs settings displaced by a
// standalone preset commit, without recomputing the multiplier.
inline bool applyRecipeWater(SettingsBrew* brew, SettingsHardware* hardware,
                             const QJsonObject& snapshot, int ratioAmountG,
                             const std::function<void()>& apply, bool forceApply = false)
{
    if (!brew || !hardware || !snapshot.value(QStringLiteral("hasWater")).toBool()) return false;
    const QString name = snapshot.value(QStringLiteral("vesselName")).toString();
    if (name.trimmed().isEmpty()) return false;
    int index = -1;
    const auto presets = brew->waterVesselPresets();
    for (qsizetype i = 0; i < presets.size(); ++i) {
        if (presets.at(i).toMap().value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
            index = static_cast<int>(i);
            break;
        }
    }
    const auto water = WaterAmount::withPresetFallback(snapshot,
        QJsonObject::fromVariantMap(brew->getWaterVesselPreset(index)));
    const bool ratio = WaterAmount::isRatio(water);
    const int amount = ratio ? ratioAmountG : water.value(QStringLiteral("volume")).toInt();
    if (amount <= 0) return false;
    const QString mode = ratio ? QStringLiteral("weight")
        : water.value(QStringLiteral("mode")).toString(QStringLiteral("weight"));
    const int flow = water.value(QStringLiteral("flowRate")).toInt(40);
    const double temperature = water.value(QStringLiteral("temperatureC")).toDouble(brew->waterTemperature());
    if (index < 0) {
        brew->addWaterVesselPreset(name, amount, mode, flow, temperature);
        const auto added = brew->waterVesselPresets();
        if (added.size() != presets.size() + 1) return false;
        index = static_cast<int>(added.size()) - 1;
    }
    const bool changed = brew->selectedWaterVessel() != index || brew->waterVolume() != amount
        || brew->waterVolumeMode() != mode || brew->waterTemperature() != temperature
        || hardware->hotWaterFlowRate() != flow;
    brew->setSelectedWaterCup(index);
    brew->setWaterVolume(amount);
    brew->setWaterVolumeMode(mode);
    brew->setWaterTemperature(temperature);
    hardware->setHotWaterFlowRate(flow);
    if ((changed || forceApply) && apply) apply();
    return true;
}

inline bool matchesSnapshot(const QVariantMap& expected, const QVariantMap& current)
{
    return !expected.isEmpty() && expected == current;
}

inline bool matchesSelection(int expectedIndex, int selectedIndex,
                             const QVariantMap& expectedPreset,
                             const QVariantMap& currentPreset)
{
    return expectedIndex >= 0 && expectedIndex == selectedIndex
        && matchesSnapshot(expectedPreset, currentPreset);
}

inline bool readPositiveId(const QVariant& input, qint64* value)
{
    if (!value)
        return false;
    const int type = input.metaType().id();
    if (type == QMetaType::Bool)
        return false;

    qint64 parsed = 0;
    switch (type) {
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong: {
        bool ok = false;
        parsed = input.toLongLong(&ok);
        if (!ok)
            return false;
        break;
    }
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong: {
        bool ok = false;
        const qulonglong unsignedValue = input.toULongLong(&ok);
        if (!ok || unsignedValue > static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return false;
        parsed = static_cast<qint64>(unsignedValue);
        break;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        bool ok = false;
        const double number = input.toDouble(&ok);
        if (!ok || !std::isfinite(number) || std::trunc(number) != number
            || number < 1.0 || number > 9007199254740991.0)
            return false;
        parsed = static_cast<qint64>(number);
        break;
    }
    default:
        return false;
    }

    if (parsed <= 0)
        return false;
    *value = parsed;
    return true;
}

inline bool selectWaterVessel(SettingsBrew* brew, SettingsHardware* hardware, int index,
                              const std::function<void()>& apply)
{
    if (!brew || !hardware)
        return false;

    const QVariantMap preset = brew->getWaterVesselPreset(index);
    if (preset.isEmpty() || !preset.contains(QStringLiteral("volume")))
        return false;

    const QString storedMode = preset.value(QStringLiteral("mode")).toString();
    const QString mode = storedMode.isEmpty() ? QStringLiteral("weight") : storedMode;
    const int flowRate = preset.contains(QStringLiteral("flowRate"))
        ? preset.value(QStringLiteral("flowRate")).toInt() : 40;
    const double temperature = preset.contains(QStringLiteral("temperature"))
        ? preset.value(QStringLiteral("temperature")).toDouble() : brew->waterTemperature();

    brew->setSelectedWaterCup(index);
    brew->setWaterVolume(preset.value(QStringLiteral("volume")).toInt());
    brew->setWaterVolumeMode(mode);
    hardware->setHotWaterFlowRate(flowRate);
    brew->setWaterTemperature(temperature);
    if (apply)
        apply();
    return true;
}

inline bool selectFlushPreset(SettingsBrew* brew, int index,
                              const std::function<void()>& apply)
{
    if (!brew)
        return false;

    const QVariantMap preset = brew->getFlushPreset(index);
    if (preset.isEmpty())
        return false;

    brew->setSelectedFlushPreset(index);
    brew->setFlushFlow(preset.value(QStringLiteral("flow")).toDouble());
    brew->setFlushSeconds(preset.value(QStringLiteral("seconds")).toDouble());
    if (apply)
        apply();
    return true;
}

} // namespace MachineControlPresets
