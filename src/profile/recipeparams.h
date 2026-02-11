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
 * - A-Flow (by Janek, forked from D-Flow): Adds pressure ramp, ramp down, flow up, 2nd fill
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

    // === Editor Type ===
    QString editorType = "dflow";       // "dflow" or "aflow" — determines frame generation

    // === A-Flow Extensions ===
    bool secondFillEnabled = false;     // 2nd fill step after infuse
    bool rampDownEnabled = false;       // Pressure ramp down phase after ramp up
    double rampDownPressure = 4.0;      // Target pressure for ramp down (bar)
    bool flowUpEnabled = false;         // Gradually increase flow during extraction

    // === Serialization ===
    QJsonObject toJson() const;
    static RecipeParams fromJson(const QJsonObject& json);

    // === QML Integration ===
    QVariantMap toVariantMap() const;
    static RecipeParams fromVariantMap(const QVariantMap& map);

    // === D-Flow Presets ===
    static RecipeParams classic();      // Traditional 9-bar Italian
    static RecipeParams londinium();    // Lever machine style with decline
    static RecipeParams turbo();        // Fast high-extraction flow profile
    static RecipeParams blooming();     // Long bloom, lower pressure
    static RecipeParams dflowDefault(); // D-Flow default (Damian's style)

    // === A-Flow Presets ===
    static RecipeParams aflowDefault(); // A-Flow default
    static RecipeParams aflowMedium();  // A-Flow medium with flow up
    static RecipeParams aflowLever();   // A-Flow lever style with 2nd fill
};
