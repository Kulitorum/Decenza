#pragma once

#include "../machine/machinephase.h"

#include <QJsonObject>
#include <QString>

// Keep the wire fields, struct members, parser and serializer on one schema.
#define DECENZA_MACHINE_UI_SNAPSHOT_FIELDS(BOOL_FIELD, STRING_FIELD, DOUBLE_FIELD, INT_FIELD, PHASE_FIELD) \
    BOOL_FIELD(de1Connected)                   \
    BOOL_FIELD(de1GuiEnabled)                   \
    BOOL_FIELD(simulationMode)                 \
    BOOL_FIELD(de1IsHeadless)                  \
    INT_FIELD(de1State, 0)                     \
    INT_FIELD(de1SubState, 0)                  \
    BOOL_FIELD(scaleConnected)                 \
    BOOL_FIELD(scaleIsFlowScale)               \
    STRING_FIELD(scaleName, QString())          \
    INT_FIELD(scaleBatteryLevel, -1)           \
    BOOL_FIELD(scaleCharging)                  \
    BOOL_FIELD(bleScaleConnectionFailed)       \
    BOOL_FIELD(usbScaleConnected)              \
    STRING_FIELD(stateString, QString())       \
    STRING_FIELD(subStateString, QString())    \
    STRING_FIELD(primaryScaleAddress, QString())\
    DOUBLE_FIELD(temperature)                  \
    DOUBLE_FIELD(steamTemperature)             \
    DOUBLE_FIELD(waterLevel)                   \
    DOUBLE_FIELD(waterLevelMm)                 \
    INT_FIELD(waterLevelMl, 0)                 \
    INT_FIELD(refillKitDetected, -1)           \
    STRING_FIELD(appWaterLevelDisplayUnit, QStringLiteral("percent"))\
    INT_FIELD(appWaterRefillPoint, 5)          \
    INT_FIELD(appRefillKitOverride, 2)         \
    PHASE_FIELD(phase)                         \
    BOOL_FIELD(isFlowing)                      \
    BOOL_FIELD(isHeating)                      \
    BOOL_FIELD(isReady)                        \
    DOUBLE_FIELD(scaleWeight)                  \
    DOUBLE_FIELD(scaleFlowRate)                \
    DOUBLE_FIELD(cumulativeVolume)             \
    DOUBLE_FIELD(preinfusionVolume)            \
    DOUBLE_FIELD(pourVolume)                   \
    DOUBLE_FIELD(shotTime)                     \
    DOUBLE_FIELD(targetWeight)                 \
    DOUBLE_FIELD(targetVolume)                 \
    DOUBLE_FIELD(smoothedScaleFlowRate)        \
    BOOL_FIELD(canDecreaseTargetWeight)        \
    DOUBLE_FIELD(hotWaterTargetWeight)         \
    DOUBLE_FIELD(filteredGoalPressure)          \
    DOUBLE_FIELD(filteredGoalFlow)              \
    BOOL_FIELD(standbySwitchOpen)              \
    BOOL_FIELD(steamHeaterOn)                  \
    BOOL_FIELD(steamSoftStopped)               \
    BOOL_FIELD(hardwareSteamTwoTapStop)        \
    DOUBLE_FIELD(steamAutoFlushCountdown)      \
    BOOL_FIELD(temperatureIsRealOverride)      \
    BOOL_FIELD(brewHasTemperatureOverride)     \
    DOUBLE_FIELD(brewTemperatureOverride)      \
    DOUBLE_FIELD(brewSteamTemperature)         \
    INT_FIELD(brewSteamTimeout, 0)             \
    INT_FIELD(brewSteamFlow, 0)                \
    INT_FIELD(brewSteamAutoFlushSeconds, 0)    \
    BOOL_FIELD(brewMilkAutoCaptureEnabled)     \
    DOUBLE_FIELD(brewSteamSecondsPerGram)      \
    DOUBLE_FIELD(brewLastSteamMilkG)           \
    DOUBLE_FIELD(brewLastSteamTimeS)           \
    DOUBLE_FIELD(brewDoseCupTareWeight)        \
    DOUBLE_FIELD(profileTargetTemperature)     \
    DOUBLE_FIELD(profileTargetWeight)          \
    DOUBLE_FIELD(profileEffectiveTargetWeight) \
    DOUBLE_FIELD(profileBrewByRatio)           \
    DOUBLE_FIELD(profileBrewByRatioDose)       \
    BOOL_FIELD(profileBrewByRatioActive)       \
    DOUBLE_FIELD(activeBaselineYieldG)         \
    DOUBLE_FIELD(activeRecipeTempOffsetC)      \
    BOOL_FIELD(yieldIsRealOverride)            \
    STRING_FIELD(portalState, QStringLiteral("disconnected"))\
    BOOL_FIELD(portalActive)                   \
    BOOL_FIELD(portalHasReading)               \
    DOUBLE_FIELD(portalTemperatureC)           \
    DOUBLE_FIELD(portalEcRaw)                  \
    BOOL_FIELD(portalOwned)                    \
    BOOL_FIELD(portalMachineBusy)              \
    STRING_FIELD(controlContextId, QString())


struct MachineUiSnapshot {
#define DECENZA_DECLARE_BOOL_FIELD(name) bool name = false;
#define DECENZA_DECLARE_STRING_FIELD(name, defaultValue) QString name = defaultValue;
#define DECENZA_DECLARE_DOUBLE_FIELD(name) double name = 0.0;
#define DECENZA_DECLARE_INT_FIELD(name, defaultValue) int name = defaultValue;
#define DECENZA_DECLARE_PHASE_FIELD(name) MachinePhase name = MachinePhase::Disconnected;
    DECENZA_MACHINE_UI_SNAPSHOT_FIELDS(
        DECENZA_DECLARE_BOOL_FIELD,
        DECENZA_DECLARE_STRING_FIELD,
        DECENZA_DECLARE_DOUBLE_FIELD,
        DECENZA_DECLARE_INT_FIELD,
        DECENZA_DECLARE_PHASE_FIELD)
#undef DECENZA_DECLARE_BOOL_FIELD
#undef DECENZA_DECLARE_STRING_FIELD
#undef DECENZA_DECLARE_DOUBLE_FIELD
#undef DECENZA_DECLARE_INT_FIELD
#undef DECENZA_DECLARE_PHASE_FIELD

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& object, MachineUiSnapshot* snapshot,
                      QString* error = nullptr);
};

