#pragma once

#include <QString>
#include <QJsonObject>

/**
 * ProfileFrame represents a single step/frame in an espresso profile.
 *
 * This structure captures ALL possible frame parameters from the DE1 BLE protocol
 * and de1app profile format, enabling both:
 * - Frame-based profiles (uploaded to machine, executed autonomously)
 * - Direct Setpoint Control (app sends live setpoints during extraction)
 *
 * DE1 BLE Frame Wire Format (8 bytes):
 *   FrameToWrite (1), Flag (1), SetVal (U8P4, 1), Temp (U8P1, 1),
 *   FrameLen (F8_1_7, 1), TriggerVal (U8P4, 1), MaxVol (U10P0, 2)
 *
 * Extension Frame (for limiters, +32 to frame number):
 *   FrameToWrite (1), MaxFlowOrPressure (U8P4, 1), Range (U8P4, 1), [padding]
 */
struct ProfileFrame {
    // === Basic Frame Properties ===
    QString name;                   // Human-readable step name (e.g., "Preinfusion")
    double temperature = 93.0;      // Target temperature (Celsius, range 0-127.5)
    QString sensor = "coffee";      // Temperature sensor: "coffee" (basket) or "water" (mix temp)
    QString pump = "pressure";      // Control mode: "pressure" or "flow"
    QString transition = "fast";    // Transition type: "fast" (instant) or "smooth" (interpolate)
    double pressure = 9.0;          // Target pressure (bar, range 0-15.9375)
    double flow = 2.0;              // Target flow (mL/s, range 0-15.9375)
    double seconds = 30.0;          // Frame duration (seconds, max ~127s)
    double volume = 0.0;            // Max volume for this frame (mL, 0 = no limit)

    // === Exit Conditions (DoCompare flag) ===
    // When exitIf is true, frame exits early if the condition is met
    bool exitIf = false;
    QString exitType;               // "pressure_over", "pressure_under", "flow_over", "flow_under", "weight"
    double exitPressureOver = 0.0;  // Exit when pressure exceeds this (bar)
    double exitPressureUnder = 0.0; // Exit when pressure drops below this (bar)
    double exitFlowOver = 0.0;      // Exit when flow exceeds this (mL/s)
    double exitFlowUnder = 0.0;     // Exit when flow drops below this (mL/s)
    double exitWeight = 0.0;        // Exit when weight reaches this (grams) - requires scale

    // === User Notification ===
    QString popup;                  // Message to show user during this frame (e.g., "Swirl now*", "$weight")

    // === Limiter (Extension Frame) ===
    // When in pressure mode, limits max flow; when in flow mode, limits max pressure
    double maxFlowOrPressure = 0.0;      // Limiter value (0 = IgnoreLimit flag set)
    double maxFlowOrPressureRange = 0.6; // Limiter P/I control range

    // === Direct Setpoint Control Fields ===
    // For live control mode, these allow more granular control
    bool moving = false;            // If true, interpolate from previous setpoint
    double previousPressure = 0.0;  // Starting pressure for interpolation
    double previousFlow = 0.0;      // Starting flow for interpolation
    double previousTemperature = 0.0; // Starting temp for interpolation

    // Convert to/from JSON (supports both our format and de1app format)
    QJsonObject toJson() const;
    static ProfileFrame fromJson(const QJsonObject& json);

    // Parse from de1app Tcl list format: {key value key value ...}
    static ProfileFrame fromTclList(const QString& tclList);

    // Serialize to de1app Tcl list format: {key value key value ...}
    QString toTclList() const;

    // Compute frame flags for BLE
    uint8_t computeFlags() const;

    // Get the set value (pressure or flow depending on pump mode)
    double getSetVal() const;

    // Get the trigger value for exit condition
    double getTriggerVal() const;

    // Check if this frame uses flow control (vs pressure control)
    bool isFlowControl() const { return pump == "flow"; }

    // Check if this frame needs an extension frame (for limiters)
    bool needsExtensionFrame() const { return maxFlowOrPressure > 0; }

    // Create a copy with new setpoint values (for direct control)
    ProfileFrame withSetpoint(double pressureOrFlow, double temp) const;
};
