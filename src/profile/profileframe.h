#pragma once

#include <QString>
#include <QJsonObject>

struct ProfileFrame {
    QString name;
    double temperature = 93.0;      // Target temperature (Celsius)
    QString sensor = "coffee";      // "coffee" (basket) or "water" (mix temp)
    QString pump = "pressure";      // "pressure" or "flow"
    QString transition = "fast";    // "fast" (instant) or "smooth" (interpolate)
    double pressure = 9.0;          // Target pressure (bar)
    double flow = 2.0;              // Target flow (mL/s)
    double seconds = 30.0;          // Frame duration
    double volume = 0.0;            // Max volume (mL, 0 = no limit)

    // Exit conditions
    bool exitIf = false;
    QString exitType;               // "pressure_over", "pressure_under", "flow_over", "flow_under"
    double exitPressureOver = 0.0;
    double exitPressureUnder = 0.0;
    double exitFlowOver = 0.0;
    double exitFlowUnder = 0.0;

    // Limiter (optional)
    double maxFlowOrPressure = 0.0;
    double maxFlowOrPressureRange = 0.6;

    // Convert to/from JSON
    QJsonObject toJson() const;
    static ProfileFrame fromJson(const QJsonObject& json);

    // Compute frame flags for BLE
    uint8_t computeFlags() const;

    // Get the set value (pressure or flow depending on pump mode)
    double getSetVal() const;

    // Get the trigger value for exit condition
    double getTriggerVal() const;
};
