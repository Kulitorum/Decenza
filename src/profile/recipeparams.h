#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>

/**
 * EditorType determines which frame generation strategy is used.
 */
enum class EditorType {
    DFlow,      // D-Flow (Damian Brakel): Fill → Infuse → Pour
    AFlow,      // A-Flow (Janek): Fill → Infuse → Pressure Ramp → Pour
    Pressure,   // Simple pressure profile (settings_2a)
    Flow        // Simple flow profile (settings_2b)
};

inline QString editorTypeToString(EditorType type) {
    switch (type) {
    case EditorType::DFlow:    return QStringLiteral("dflow");
    case EditorType::AFlow:    return QStringLiteral("aflow");
    case EditorType::Pressure: return QStringLiteral("pressure");
    case EditorType::Flow:     return QStringLiteral("flow");
    }
    return QStringLiteral("dflow");
}

inline EditorType editorTypeFromString(const QString& str) {
    if (str == QLatin1String("aflow"))    return EditorType::AFlow;
    if (str == QLatin1String("pressure")) return EditorType::Pressure;
    if (str == QLatin1String("flow"))     return EditorType::Flow;
    return EditorType::DFlow;  // Default fallback
}

/**
 * RecipeParams holds the high-level "coffee concept" parameters
 * for the Recipe Editor. These parameters are converted to DE1
 * frames by RecipeGenerator.
 *
 * Supports four editor types via EditorType enum:
 * - DFlow: Fill → [Infuse] → Pour
 * - AFlow: Fill → [Infuse] → Pressure Up → Pressure Decline → Flow Start → Flow Extraction
 * - Pressure: Preinfusion → [Forced Rise] → Hold → Decline (settings_2a)
 * - Flow: Preinfusion → Hold → Decline (settings_2b)
 */
struct RecipeParams {
    // === Core Parameters ===
    double targetWeight = 36.0;         // Stop at weight (grams)
    double targetVolume = 0.0;          // Stop at volume (mL, 0 = disabled)

    // No `dose` here, deliberately. It was carried in the persisted recipe block and
    // consumed by nothing — not by either frame generator, not by any QML binding,
    // and explicitly excluded from frameAffectingFieldsEqual as "metadata only". The
    // per-profile dose that IS consumed is Profile's recommended_dose /
    // has_recommended_dose pair (the advanced editor's control, dialing_get_context,
    // the AI advisor), and the MCP `dose` parameter now writes that.

    // ADDING A FIELD HERE? Decide whether it affects frame GENERATION and update
    // frameAffectingFieldsEqual() in recipeparams.cpp to match. That function is
    // a hand-written field-by-field comparison with no structural link to this
    // list; it decides whether a save regenerates frames at all. Miss a field and
    // an edit to it silently short-circuits — the user changes a value, saves,
    // and nothing happens.

    // === Fill Phase ===
    // Fill pressure, flow and duration are NOT here, deliberately. Neither plugin
    // exposes them: A-Flow's update_* writes only the fill frame's temperature,
    // D-Flow additionally DERIVES its pressure and pressure-over exit from the
    // soak pressure. Decenza used to carry fillPressure/fillFlow/fillTimeout and
    // write them into the frames, which rewrote fields the plugins preserve — the
    // AF-6 half of the parity findings. Whatever those frame fields hold is
    // preserved across a regenerate (Profile::restoreFieldsThePluginNeverWrites).
    double fillTemperature = 88.0;      // Fill water temperature (Celsius)

    // === Infuse Phase (Preinfusion/Soak) ===
    // No infuseEnabled toggle: the plugins express "no soak" as infuseTime 0,
    // and a second way to say the same thing is a second thing to keep in sync.
    double infusePressure = 3.0;        // Soak pressure (bar)
    double infuseTime = 20.0;           // Soak duration (seconds)
    double infuseWeight = 4.0;          // Weight to exit infuse (grams, 0 = disabled)
    double infuseVolume = 100.0;        // Max volume during infuse (mL)

    // === Pour Phase (Extraction) ===
    // Pour is always flow-driven with a pressure limit (matching de1app D-Flow/A-Flow model).
    // pourFlow = flow setpoint, pourPressure = pressure cap (max_flow_or_pressure).
    double pourTemperature = 93.0;      // Pour water temperature (Celsius)
    double pourPressure = 9.0;          // Pressure limit/cap (bar) — max_flow_or_pressure
    double pourFlow = 2.0;              // Extraction flow setpoint (mL/s)
    double rampTime = 5.0;              // Transition ramp duration (seconds)

    // === A-Flow Specific ===
    bool rampDownEnabled = false;       // Split pressure ramp into up + decline phases
    bool flowExtractionUp = true;       // Flow ramps up during extraction (smooth vs fast)
    bool secondFillEnabled = false;     // Add 2nd Fill + Pause frames before pressure ramp

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
    EditorType editorType = EditorType::DFlow;  // Determines frame generation strategy

    // === BLE Header ===
    int preinfuseFrameCount = -1;  // NumberOfPreinfuseFrames for BLE header (-1 = use countPreinfuseFrames())

    // Apply de1app-matching defaults for the current editorType.
    // Call after setting editorType but before clamp().
    void applyEditorDefaults();

    // === Comparison ===
    // Returns true if all frame-affecting fields are equal (excludes metadata-only
    // fields: targetWeight, targetVolume). Used to skip frame regeneration
    // when only metadata changed — matches de1app behavior where changing weight
    // doesn't recompute frames.
    //
    // WHAT IT IS COMPARED AGAINST matters as much as what it compares, and differs
    // by editor type — see ProfileManager::uploadRecipeProfile. For D-Flow/A-Flow the
    // baseline is now RecipeAnalyzer::extractRecipeParams(profile), i.e. the frames,
    // because no recipe block is stored any more and profile.recipeParams() is a
    // default-constructed struct. For ADVANCED profiles, which share that code path,
    // the baseline must stay profile.recipeParams(): frame-derived params would never
    // compare equal to the advanced editor's defaults, and the resulting permanent
    // "changed" verdict silently skips the branch that applies target weight/volume.
    bool frameAffectingFieldsEqual(const RecipeParams& other) const;

    // === Validation ===
    // Returns list of issues found (empty = valid)
    QStringList validate() const;

    // Clamp all values to hardware-safe ranges
    void clamp();

    // === Serialization ===
    QJsonObject toJson() const;
    static RecipeParams fromJson(const QJsonObject& json);

    // === QML Integration ===
    QVariantMap toVariantMap() const;
    static RecipeParams fromVariantMap(const QVariantMap& map);

};
