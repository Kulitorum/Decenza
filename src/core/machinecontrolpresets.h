#pragma once

#include "settings_brew.h"
#include "settings_hardware.h"

#include <QVariantMap>
#include <QMetaType>
#include <cmath>
#include <limits>

#include <functional>

// Shared settings-only half of Home preset selection. MainController supplies
// the machine-apply callback so callers do not duplicate preset field mapping.
namespace MachineControlPresets {

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
