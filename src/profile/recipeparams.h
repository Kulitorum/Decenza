#pragma once

#include <QString>
#include <QJsonObject>
#include <QVariantMap>

/**
 * RecipeParams holds the high-level "coffee concept" parameters
 * for the Recipe Editor. These parameters are converted to DE1
 * frames by RecipeGenerator.
 *
 * This provides a D-Flow-style simplified interface where users
 * edit intuitive values like "infuse pressure" instead of raw
 * machine frames.
 *
 * Based on the D-Flow plugin by Damian Brakel.
 */
struct RecipeParams {
    // === Core Parameters ===
    double targetWeight = 36.0;         // Stop at weight (grams)
    double dose = 18.0;                 // Input dose for ratio display (grams)

    // === Fill Phase ===
    double fillTemperature = 88.0;      // Fill water temperature (Celsius)
    double fillPressure = 3.0;          // Fill pressure (bar)
    double fillFlow = 8.0;              // Fill flow rate (mL/s)
    double fillTimeout = 25.0;          // Max fill duration (seconds)
    double fillExitPressure = 3.0;      // Exit to infuse when pressure over (bar)

    // === Infuse Phase (Preinfusion/Soak) ===
    double infusePressure = 3.0;        // Soak pressure (bar)
    double infuseTime = 20.0;           // Soak duration (seconds)
    bool infuseByWeight = false;        // Exit on weight instead of time
    double infuseWeight = 4.0;          // Weight to exit infuse (grams)
    double infuseVolume = 100.0;        // Max volume during infuse (mL)
    bool bloomEnabled = false;          // Enable bloom (pause with 0 flow)
    double bloomTime = 10.0;            // Bloom pause duration (seconds)

    // === Pour Phase (Extraction) ===
    double pourTemperature = 93.0;      // Pour water temperature (Celsius)
    QString pourStyle = "flow";         // "pressure" or "flow"
    double pourPressure = 9.0;          // Extraction pressure (bar)
    double pourFlow = 2.0;              // Extraction flow (mL/s) - if flow mode
    double flowLimit = 0.0;             // Max flow in pressure mode (0=disabled)
    double pressureLimit = 6.0;         // Max pressure in flow mode (bar, 0=disabled)
    double rampTime = 5.0;              // Transition ramp duration (seconds)

    // === Decline Phase (Optional) ===
    bool declineEnabled = false;        // Enable pressure ramp-down
    double declineTo = 6.0;             // Target end pressure (bar)
    double declineTime = 30.0;          // Ramp duration (seconds)

    // === Serialization ===
    QJsonObject toJson() const;
    static RecipeParams fromJson(const QJsonObject& json);

    // === QML Integration ===
    QVariantMap toVariantMap() const;
    static RecipeParams fromVariantMap(const QVariantMap& map);

    // === Presets ===
    static RecipeParams classic();      // Traditional 9-bar Italian
    static RecipeParams londinium();    // Lever machine style with decline
    static RecipeParams turbo();        // Fast high-extraction flow profile
    static RecipeParams blooming();     // Long bloom, lower pressure
    static RecipeParams dflowDefault(); // D-Flow default (Damian's style)
};
