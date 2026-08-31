#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
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
    // ADDING A FIELD HERE? Decide its fate in
    // Profile::restoreFieldsThePluginNeverWrites() (profile.cpp) as well.
    //
    // That function reinstates the de1app plugins' in-place-mutation semantics
    // for D-Flow/A-Flow profiles: a frame field the plugin's update_* proc never
    // assigns must keep the value the profile's author gave it. It works from a
    // hand-maintained list of fields — so a field added here and not mentioned
    // there is silently never restored, for every frame role, permanently. That
    // is exactly findings DF-1, DF-2, DF-5 and AF-6, which is what the function
    // exists to close. Nothing will warn you; the compiler is happy either way.
    //
    // nonStockPreFillSecondFillAndPauseSurviveARegenerate and
    // restoredFieldPartitionIsPinned (tst_recipeeditorparity) are the tests that
    // will fail if you forget.

    // The name a generated Pressure profile's forced-rise frame(s) carry
    // (RecipeGenerator::generatePressureFrames, Profile's own settings_2a generator) and
    // what Profile::countPreinfuseFramesWithForcedRise() matches against to exclude them
    // from Stop-at-Volume's pour count. One name, five sites — a mismatch here silently
    // reopens the bug this exists to fix, with every existing count-based test still green.
    //
    // de1app renamed this frame when it started attaching the flow limiter to it
    // (skialpine/de1app@fdd091f3): "without limit" stopped being true. Both names are
    // live — every profile written before that carries the legacy one — so identify a
    // forced-rise frame with isForcedRiseName(), never by comparing to one constant.
    static inline const QString kForcedRiseName = QStringLiteral("forced rise");
    static inline const QString kForcedRiseWithoutLimitName = QStringLiteral("forced rise without limit");

    static bool isForcedRiseName(const QString& name) {
        return name == kForcedRiseName || name == kForcedRiseWithoutLimitName;
    }

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

    // Step keys we understand. Anything else in a step is a hard import failure
    // (see unknownJsonKeys), NOT a key we quietly carry along.
    //
    // The asymmetry with the top-level passthrough is deliberate. A top-level key
    // we do not model is usually an app's own metadata block — our `recipe` is
    // exactly that, and it is "unknown" to Decaid — and preserving it costs
    // nothing because it does not touch the shot. A key inside a STEP is
    // different: steps are what the machine executes. If a future app version
    // adds, say, a new exit condition or pump mode to a frame, round-tripping the
    // key verbatim would NOT save us — we would still ignore it while brewing and
    // silently pour a different shot than the file describes.
    //
    // So an unrecognised step key means we cannot promise the same coffee, and
    // the honest response is to refuse the profile and say which key we did not
    // understand, so it gets reported rather than quietly mis-brewed.
    //
    // Verified against every profile available at the time of writing: the 93
    // shipped built-ins, the 93-file pre-change golden corpus, and 96 Decaid
    // profiles use exactly 13 distinct step keys between them, all listed here.
    static const QSet<QString>& knownJsonKeys();

    // Keys present in `json` that knownJsonKeys() does not cover, sorted.
    static QStringList unknownJsonKeys(const QJsonObject& json);

    // The Tcl frame vocabulary — exactly what fromTclList reads, which is NOT
    // the same set as knownJsonKeys(). See the definition: reusing the JSON set
    // here let a .tcl frame carrying `exit_weight` pass as understood while the
    // parser ignored it entirely.
    static const QSet<QString>& knownTclKeys();

    // Keys in a de1app Tcl frame that knownTclKeys() does not cover, sorted.
    static QStringList unknownTclKeys(const QString& tclList);

    // Numeric frame values that cannot be interpreted, as "key=raw", sorted.
    // The value-level twin of unknownTclKeys(): that one catches a key we do
    // not understand, this one a known key whose value we cannot read. Both
    // make the profile invalid, because toDouble() yields 0.0 on failure and 0
    // is legal for all of these — so a garbled value is otherwise undetectable.
    static QStringList malformedTclValues(const QString& tclList);

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
