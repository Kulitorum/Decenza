#include "recipeanalyzer.h"
#include <QDebug>
#include <cmath>

bool RecipeAnalyzer::canConvertToRecipe(const Profile& profile) {
    const auto& steps = profile.steps();

    // Need at least 2 frames (Fill + Pour) and at most 9 frames
    // D-Flow: Fill → [Infuse] → Pour
    // A-Flow: Fill → [Infuse] → [2ndFill] → [Pause] → [RampUp] → [RampDown] → PourStart → Pour
    if (steps.size() < 2 || steps.size() > 9) {
        return false;
    }

    // Check for basic D-Flow pattern
    // Pattern 1: Fill → Pour (2 frames)
    // Pattern 2: Fill → Infuse → Pour (3 frames)
    // First frame should be a fill frame
    if (!isFillFrame(steps[0])) {
        return false;
    }

    // Last frame should be a pour frame
    int pourIndex = static_cast<int>(steps.size()) - 1;

    if (pourIndex < 1) {
        return false;
    }

    if (!isPourFrame(steps[pourIndex])) {
        return false;
    }

    return true;
}

namespace {

// The plugins' two rounding helpers (de1app vars.tcl), used verbatim wherever
// prep uses them. Where prep does NOT round, neither do we — `soaking(pressure)`
// reaching the editor unrounded is the plugin's behaviour, not an oversight.
double roundToOneDigit(double v) { return std::round(v * 10.0) / 10.0; }
double roundToInteger(double v)  { return std::round(v); }

}  // namespace

// D-Flow's `proc prep` — plugin.tcl:195-210.
//
//   array set filling [lindex $::settings(advanced_shot) 0]
//   array set soaking [lindex $::settings(advanced_shot) 1]
//   array set pouring [lindex $::settings(advanced_shot) 2]
//
// Fixed indices, no pattern matching. Note pouring_pressure comes from the
// LIMITER (max_flow_or_pressure), not the frame's pressure setpoint — the pour
// frame is flow-driven and the pressure the user set is its cap.
RecipeParams RecipeAnalyzer::prepDFlow(const Profile& profile) {
    RecipeParams params = profile.recipeParams();
    params.editorType = EditorType::DFlow;
    params.targetWeight = profile.targetWeight();
    params.targetVolume = profile.targetVolume();

    const auto& steps = profile.steps();
    if (steps.size() < 3) {
        // The plugin would leave its globals at whatever the last profile set;
        // we keep the profile's own params and say so, rather than inventing a
        // shape the frames do not have.
        qWarning() << "prepDFlow:" << profile.title() << "has" << steps.size()
                   << "frames, expected 3 — parameters left as they were";
        return params;
    }

    const ProfileFrame& filling = steps[0];
    const ProfileFrame& soaking = steps[1];
    const ProfileFrame& pouring = steps[2];

    params.fillTemperature = filling.temperature;
    params.infuseTime      = roundToOneDigit(soaking.seconds);
    params.infusePressure  = soaking.pressure;
    params.infuseVolume    = soaking.volume;
    params.infuseWeight    = soaking.exitWeight;
    params.pourFlow        = roundToOneDigit(pouring.flow);
    params.pourPressure    = pouring.maxFlowOrPressure;
    params.pourTemperature = pouring.temperature;
    return params;
}

// A-Flow's `proc prep` — code.tcl:194-240, with roles from `set_profile_index`
// (code.tcl:171-190).
//
// Three things here are not guessable from Decenza's side and are the whole of
// findings AF-1 through AF-5:
//
//   * pouring_flow comes from POURING_START, not from pouring. The pouring frame
//     carries the DOUBLED flow when flow-up is on, so reading it there doubles
//     the value on every save (AF-1).
//   * ramp_updown_seconds is the SUM of both ramp frames, not either one (AF-3).
//   * all three toggles are derived from frame structure, never stored (AF-2/4).
RecipeParams RecipeAnalyzer::prepAFlow(const Profile& profile) {
    RecipeParams params = profile.recipeParams();
    params.editorType = EditorType::AFlow;
    params.targetWeight = profile.targetWeight();
    params.targetVolume = profile.targetVolume();

    const auto& steps = profile.steps();
    const qsizetype n = steps.size();

    // set_profile_index: `> 8` selects the 9-frame layout, else the legacy one.
    const bool nine = n > 8;
    const qsizetype iFilling      = nine ? 1 : 0;
    const qsizetype iSoaking      = nine ? 2 : 1;
    const qsizetype iPause        = nine ? 4 : -1;
    const qsizetype iRampUp       = nine ? 5 : 2;
    const qsizetype iRampDown     = nine ? 6 : 3;
    const qsizetype iPouringStart = nine ? 7 : 4;
    const qsizetype iPouring      = nine ? 8 : 5;

    if (n < (nine ? 9 : 6)) {
        qWarning() << "prepAFlow:" << profile.title() << "has" << n
                   << "frames, too few for either A-Flow layout — parameters left as they were";
        return params;
    }

    const ProfileFrame& filling      = steps[iFilling];
    const ProfileFrame& soaking      = steps[iSoaking];
    const ProfileFrame& rampUp       = steps[iRampUp];
    const ProfileFrame& rampDown     = steps[iRampDown];
    const ProfileFrame& pouringStart = steps[iPouringStart];
    const ProfileFrame& pouring      = steps[iPouring];

    params.fillTemperature = filling.temperature;
    // prep also reads filling(flow) into ::Aflow_filling_flow, but only the demo
    // graph consumes it (code.tcl:570) — update_A-Flow never writes that field, so
    // Decenza has no parameter for it either.
    params.infuseTime      = roundToOneDigit(soaking.seconds);
    params.infusePressure  = soaking.pressure;
    params.infuseVolume    = soaking.volume;
    params.infuseWeight    = soaking.exitWeight;
    params.rampTime        = roundToInteger(rampUp.seconds + rampDown.seconds);
    params.pourFlow        = roundToOneDigit(pouringStart.flow);
    params.pourPressure    = rampUp.pressure;
    params.pourTemperature = rampUp.temperature;

    // The toggles. Each is a property of the frames, which is why an A-Flow
    // profile needs no stored state to round-trip.
    params.rampDownEnabled  = rampDown.seconds > 0;
    params.flowExtractionUp = pouring.flow > params.pourFlow;  // vs the ROUNDED value
    params.secondFillEnabled = nine && steps[iPause].seconds > 0;

    return params;
}

RecipeParams RecipeAnalyzer::extractRecipeParams(const Profile& profile) {
    RecipeParams params;
    const auto& steps = profile.steps();

    if (steps.isEmpty()) {
        return params;
    }

    // A profile the plugins own is read by the plugins' own rule. Everything
    // below this point is pattern detection for profiles that have no plugin —
    // useful for converting an arbitrary profile into recipe mode, and exactly
    // wrong for one of these (it is a three-frame D-Flow shape detector, and
    // pointing it at a nine-frame A-Flow profile is findings AF-1..AF-5).
    const QString et = profile.editorType();
    if (et == QLatin1String("dflow")) return prepDFlow(profile);
    if (et == QLatin1String("aflow")) return prepAFlow(profile);

    // Extract targets from profile
    params.targetWeight = profile.targetWeight();
    params.targetVolume = profile.targetVolume();

    // Default temperatures from profile
    double profileTemp = profile.espressoTemperature();
    params.fillTemperature = profileTemp;
    params.pourTemperature = profileTemp;

    // Find frame indices
    int fillIndex = 0;
    int infuseIndex = -1;
    int rampIndex = -1;
    int pourIndex = -1;

    // First frame is fill
    if (isFillFrame(steps[0])) {
        fillIndex = 0;
    }

    // Find pour frame (last frame)
    for (int i = static_cast<int>(steps.size()) - 1; i >= 1; i--) {
        if (isPourFrame(steps[i])) {
            pourIndex = i;
            break;
        }
    }

    // Find infuse and ramp frames (between fill and pour)
    for (int i = fillIndex + 1; i < pourIndex; i++) {
        if (isRampFrame(steps[i])) {
            rampIndex = i;
        } else if (isInfuseFrame(steps[i])) {
            infuseIndex = i;
        }
    }

    // Extract fill parameters
    if (fillIndex >= 0 && fillIndex < steps.size()) {
        const auto& fillFrame = steps[fillIndex];
        // Use fill frame temperature
        if (fillFrame.temperature > 0) {
            params.fillTemperature = fillFrame.temperature;
        }
    }

    // Extract infuse parameters
    if (infuseIndex >= 0 && infuseIndex < steps.size()) {
        const auto& infuseFrame = steps[infuseIndex];
        params.infusePressure = extractInfusePressure(infuseFrame);
        params.infuseTime = extractInfuseTime(infuseFrame);
        params.infuseVolume = infuseFrame.volume > 0 ? infuseFrame.volume : 100.0;
        params.infuseWeight = infuseFrame.exitWeight;  // App-side weight exit (0 = no weight exit)
    }

    // Extract ramp time
    if (rampIndex >= 0 && rampIndex < steps.size()) {
        params.rampTime = steps[rampIndex].seconds;
    }

    // Extract pour parameters
    if (pourIndex >= 0 && pourIndex < steps.size()) {
        const auto& pourFrame = steps[pourIndex];

        if (pourFrame.pump == "flow") {
            params.pourFlow = extractPourFlow(pourFrame);
            params.pourPressure = extractPressureLimit(pourFrame);
            if (params.pourPressure <= 0) params.pourPressure = 9.0;
        } else {
            params.pourPressure = extractPourPressure(pourFrame);
            double flowLimit = extractFlowLimit(pourFrame);
            params.pourFlow = flowLimit > 0 ? flowLimit : 2.0;
        }

        // Use pour frame temperature
        if (pourFrame.temperature > 0) {
            params.pourTemperature = pourFrame.temperature;
        }
    }

    return params;
}

bool RecipeAnalyzer::convertToRecipeMode(Profile& profile) {
    if (!canConvertToRecipe(profile)) {
        qDebug() << "Profile" << profile.title() << "cannot be converted to recipe mode";
        return false;
    }

    RecipeParams params = extractRecipeParams(profile);
    profile.setRecipeParams(params);

    qDebug() << "Converted profile" << profile.title() << "to recipe mode";
    return true;
}

void RecipeAnalyzer::forceConvertToRecipe(Profile& profile) {
    // Try normal conversion first
    if (canConvertToRecipe(profile)) {
        RecipeParams params = extractRecipeParams(profile);
        profile.setRecipeParams(params);
        qDebug() << "Profile" << profile.title() << "converted to recipe mode (standard)";
        return;
    }

    // Force conversion for complex profiles
    // Extract what we can from the frames and fill in defaults for the rest
    const auto& steps = profile.steps();
    RecipeParams params;

    // Get target weight and temperature from profile
    params.targetWeight = profile.targetWeight() > 0 ? profile.targetWeight() : 36.0;
    double profileTemp = profile.espressoTemperature();
    params.fillTemperature = profileTemp > 0 ? profileTemp : 93.0;
    params.pourTemperature = profileTemp > 0 ? profileTemp : 93.0;

    if (steps.isEmpty()) {
        // No frames at all, use pure defaults
        profile.setRecipeParams(params);
        qDebug() << "Profile" << profile.title() << "converted to recipe mode (empty, using defaults)";
        return;
    }

    // Try to identify key frames and extract their parameters
    bool foundFill = false;
    bool foundInfuse = false;
    bool foundPour = false;

    for (int i = 0; i < steps.size(); i++) {
        const auto& frame = steps[i];

        // Look for fill-like frame (first frame with exit condition, or explicitly named)
        if (!foundFill && (isFillFrame(frame) || i == 0)) {
            foundFill = true;
            if (frame.temperature > 0) {
                params.fillTemperature = frame.temperature;
            }
            continue;
        }

        // Look for infuse-like frame
        if (foundFill && !foundInfuse && (isInfuseFrame(frame) || frame.pump == "pressure")) {
            foundInfuse = true;
            params.infusePressure = extractInfusePressure(frame);
            params.infuseTime = extractInfuseTime(frame);
            params.infuseVolume = frame.volume > 0 ? frame.volume : 100.0;
            continue;
        }

        // Look for pour-like frame (last significant frame, or high pressure/flow)
        if (foundFill && (isPourFrame(frame) || frame.pressure >= 6.0 || frame.pump == "flow")) {
            foundPour = true;
            if (frame.pump == "flow") {
                params.pourFlow = extractPourFlow(frame);
                params.pourPressure = extractPressureLimit(frame);
                if (params.pourPressure <= 0) params.pourPressure = 9.0;
            } else {
                params.pourPressure = extractPourPressure(frame);
                double flowLimit = extractFlowLimit(frame);
                params.pourFlow = flowLimit > 0 ? flowLimit : 2.0;
            }
            if (frame.temperature > 0) {
                params.pourTemperature = frame.temperature;
            }
            break;  // Pour found, we're done
        }
    }

    // If we didn't find a pour frame, use the last frame as pour
    if (!foundPour && steps.size() > 0) {
        const auto& lastFrame = steps.last();
        if (lastFrame.pump == "flow") {
            params.pourFlow = lastFrame.flow > 0 ? lastFrame.flow : 2.0;
            params.pourPressure = lastFrame.maxFlowOrPressure > 0 ? lastFrame.maxFlowOrPressure : 9.0;
        } else {
            params.pourPressure = lastFrame.pressure > 0 ? lastFrame.pressure : 9.0;
            params.pourFlow = 2.0;
        }
        if (lastFrame.temperature > 0) {
            params.pourTemperature = lastFrame.temperature;
        }
    }

    profile.setRecipeParams(params);
    qDebug() << "Profile" << profile.title() << "force-converted to recipe mode (simplified from"
             << steps.size() << "frames)";
}

// === Frame Pattern Detection ===

bool RecipeAnalyzer::isFillFrame(const ProfileFrame& frame) {
    // Fill frame characteristics:
    // - Usually named "Fill" or "Filling"
    // - Low pressure (1-6 bar)
    // - Has exit condition (pressure_over) to detect puck saturation
    // - Usually flow or pressure pump mode

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("fill")) {
        return true;
    }

    // Heuristic: first frame with low pressure and pressure_over exit
    if (frame.pressure <= 6.0 && frame.exitIf && frame.exitType == "pressure_over") {
        return true;
    }

    return false;
}

bool RecipeAnalyzer::isRampFrame(const ProfileFrame& frame) {
    // Ramp frame characteristics:
    // - Usually named "Ramp"
    // - Smooth transition
    // - Short duration (2-10s)
    // - Between infuse and pour

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("ramp") && !nameLower.contains("down")) {
        return true;
    }

    // Heuristic: smooth transition with short duration
    if (frame.transition == "smooth" && frame.seconds > 0 && frame.seconds <= 15) {
        return true;
    }

    return false;
}

bool RecipeAnalyzer::isInfuseFrame(const ProfileFrame& frame) {
    // Infuse frame characteristics:
    // - Usually named "Infuse", "Infusing", "Soak", "Preinfusion"
    // - Low-medium pressure (2-6 bar)
    // - Pressure pump mode
    // - Often no exit condition (time-based)

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("infus") || nameLower.contains("soak") ||
        nameLower.contains("preinf")) {
        return true;
    }

    // Heuristic: pressure mode, low pressure, time-based
    if (frame.pump == "pressure" && frame.pressure <= 6.0 &&
        frame.seconds > 0 && frame.seconds <= 60) {
        return true;
    }

    return false;
}

bool RecipeAnalyzer::isPourFrame(const ProfileFrame& frame) {
    // Pour frame characteristics:
    // - Usually named "Pour", "Pouring", "Extraction", "Hold"
    // - Higher pressure (6-12 bar) or flow mode
    // - Long duration or until weight stops it

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("pour") || nameLower.contains("extract") ||
        nameLower.contains("hold")) {
        return true;
    }

    // Heuristic: higher pressure or flow mode with long duration
    if ((frame.pressure >= 6.0 || frame.pump == "flow") && frame.seconds >= 30) {
        return true;
    }

    return false;
}

// === Parameter Extraction ===

double RecipeAnalyzer::extractInfusePressure(const ProfileFrame& frame) {
    return frame.pressure > 0 ? frame.pressure : 3.0;
}

double RecipeAnalyzer::extractInfuseTime(const ProfileFrame& frame) {
    return frame.seconds > 0 ? frame.seconds : 20.0;
}

double RecipeAnalyzer::extractPourPressure(const ProfileFrame& frame) {
    return frame.pressure > 0 ? frame.pressure : 9.0;
}

double RecipeAnalyzer::extractPourFlow(const ProfileFrame& frame) {
    return frame.flow > 0 ? frame.flow : 2.0;
}

double RecipeAnalyzer::extractFlowLimit(const ProfileFrame& frame) {
    // Flow limit is stored in maxFlowOrPressure when in pressure mode
    if (frame.pump == "pressure" && frame.maxFlowOrPressure > 0) {
        return frame.maxFlowOrPressure;
    }
    return 0.0;
}

double RecipeAnalyzer::extractPressureLimit(const ProfileFrame& frame) {
    // Pressure limit is stored in maxFlowOrPressure when in flow mode
    if (frame.pump == "flow" && frame.maxFlowOrPressure > 0) {
        return frame.maxFlowOrPressure;
    }
    return 0.0;
}

