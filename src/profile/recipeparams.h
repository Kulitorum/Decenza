#pragma once

#include <QString>
#include <QJsonObject>
#include <QVariantMap>

/**
 * RecipeParams holds the high-level "coffee concept" parameters
 * for the Recipe Editor. These parameters are converted to DE1
 * frames by RecipeGenerator.
 *
 * Supports two editor types:
 * - D-Flow (by Damian Brakel): Fill → Infuse → Pour (flow-driven with pressure limit)
 * - A-Flow (by Janek, forked from D-Flow): Fill → Infuse → Pressure Ramp → Pour
 *
 * Editor type is determined by profile title prefix ("D-Flow" or "A-Flow"),
 * matching de1app behavior.
 */
struct RecipeParams {
    // === Core Parameters ===
    double targetWeight = 36.0;         // Stop at weight (grams)
    double targetVolume = 0.0;          // Stop at volume (mL, 0 = disabled)
    double dose = 18.0;                 // Input dose for ratio display (grams)

    // === Fill Phase ===
    double fillTemperature = 88.0;      // Fill water temperature (Celsius)
    double fillPressure = 3.0;          // Fill pressure (bar)
    double fillFlow = 8.0;              // Fill flow rate (mL/s)
    double fillTimeout = 25.0;          // Max fill duration (seconds)
    double fillExitPressure = 3.0;      // Exit to infuse when pressure over (bar)

    // === Infuse Phase (Preinfusion/Soak) ===
    bool infuseEnabled = true;          // Enable infuse phase
    double infusePressure = 3.0;        // Soak pressure (bar)
    double infuseTime = 20.0;           // Soak duration (seconds)
    bool infuseByWeight = false;        // Exit on weight instead of time
    double infuseWeight = 4.0;          // Weight to exit infuse (grams)
    double infuseVolume = 100.0;        // Max volume during infuse (mL)
    bool bloomEnabled = false;          // Enable bloom (pause with 0 flow)
    double bloomTime = 10.0;            // Bloom pause duration (seconds)

    // === Pour Phase (Extraction) ===
    // Pour is always flow-driven with a pressure limit (matching de1app D-Flow/A-Flow model).
    // pourFlow = flow setpoint, pourPressure = pressure cap (max_flow_or_pressure).
    double pourTemperature = 93.0;      // Pour water temperature (Celsius)
    double pourPressure = 9.0;          // Pressure limit/cap (bar) — max_flow_or_pressure
    double pourFlow = 2.0;              // Extraction flow setpoint (mL/s)
    bool rampEnabled = true;            // Enable ramp transition phase
    double rampTime = 5.0;              // Transition ramp duration (seconds)

    // === Decline Phase (D-Flow only) ===
    bool declineEnabled = false;        // Enable flow decline during extraction
    double declineTo = 1.0;             // Target flow to decline to (mL/s)
    double declineTime = 30.0;          // Decline duration (seconds)

    // === Simple Profile Parameters (pressure/flow editors) ===
    double preinfusionTime = 20.0;        // Preinfusion duration (seconds)
    double preinfusionFlowRate = 8.0;     // Preinfusion flow rate (mL/s)
    double preinfusionStopPressure = 4.0; // Exit preinfusion at this pressure (bar)
    double holdTime = 10.0;               // Hold phase duration (seconds)
    double espressoPressure = 8.4;        // Pressure setpoint for pressure profiles (bar)
    double holdFlow = 2.2;                // Flow setpoint for flow profiles (mL/s)
    double simpleDeclineTime = 30.0;      // Decline phase duration (seconds, 0=disabled)
    double pressureEnd = 6.0;             // End pressure for pressure decline (bar)
    double flowEnd = 1.8;                 // End flow for flow decline (mL/s)
    double limiterValue = 3.5;            // Flow limiter for pressure / Pressure limiter for flow
    double limiterRange = 1.0;            // Limiter P/I range

    // === Per-Step Temperatures (pressure/flow editors) ===
    // Always used — profile temp at top is a convenience to set all at once
    double tempStart = 90.0;              // Start temperature (Celsius)
    double tempPreinfuse = 90.0;          // Preinfusion temperature (Celsius)
    double tempHold = 90.0;              // Rise and Hold temperature (Celsius)
    double tempDecline = 90.0;           // Decline temperature (Celsius)

    // === Editor Type ===
    QString editorType = "dflow";       // "dflow", "aflow", "pressure", or "flow" — determines frame generation

    // === Serialization ===
    QJsonObject toJson() const;
    static RecipeParams fromJson(const QJsonObject& json);

    // === QML Integration ===
    QVariantMap toVariantMap() const;
    static RecipeParams fromVariantMap(const QVariantMap& map);

};
