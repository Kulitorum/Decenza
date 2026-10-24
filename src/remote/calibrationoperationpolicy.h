#pragma once

#include <QString>

// Host-side allow-list and numeric limits for the shared calibration RPC.
// This policy intentionally exposes a fixed set of settings; callers cannot
// pass arbitrary QObject property names.
namespace CalibrationOperationPolicy {

inline bool supported(const QString& operation)
{
    return operation == QLatin1String("setAutoFlowCalibration")
        || operation == QLatin1String("clearProfileFlowCalibration")
        || operation == QLatin1String("resetSawLearningForProfile")
        || operation == QLatin1String("resetSawLearning")
        || operation == QLatin1String("setUseFlowScale")
        || operation == QLatin1String("setIgnoreVolumeWithScale")
        || operation == QLatin1String("setSteamTwoTapStop")
        || operation == QLatin1String("clearSteamHealthHistory")
        || operation == QLatin1String("setHeaterValue")
        || operation == QLatin1String("setHeaterDefaults")
        || operation == QLatin1String("setHeaterVoltage");
}

inline bool heaterValueValid(const QString& name, int value)
{
    if (name == QLatin1String("heaterIdleTemp")) return value >= 0 && value <= 990;
    if (name == QLatin1String("heaterWarmupFlow")) return value >= 5 && value <= 60;
    if (name == QLatin1String("heaterTestFlow")) return value >= 5 && value <= 80;
    if (name == QLatin1String("heaterWarmupTimeout")) return value >= 10 && value <= 300;
    if (name == QLatin1String("fanThreshold")) return value >= 0 && value <= 60;
    return false;
}

inline bool heaterVoltageValid(int value) { return value == 120 || value == 230; }

} // namespace CalibrationOperationPolicy
