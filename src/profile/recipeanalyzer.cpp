#include "recipeanalyzer.h"
#include <QDebug>

bool RecipeAnalyzer::canConvertToRecipe(const Profile& profile) {
    const auto& steps = profile.steps();

    // Need at least 2 frames (Fill + Pour) and at most 9 frames
    // D-Flow: Fill → [Bloom] → [Infuse] → [Ramp] → Pour → [Decline]
    // A-Flow: Fill → [Infuse] → [2ndFill] → [Pause] → [RampUp] → [RampDown] → PourStart → Pour
    if (steps.size() < 2 || steps.size() > 9) {
        return false;
    }

    // Check for basic D-Flow pattern
    // Pattern 1: Fill → Pour (2 frames)
    // Pattern 2: Fill → Infuse → Pour (3 frames)
    // Pattern 3: Fill → Infuse → Ramp → Pour (4 frames)
    // Pattern 4: Fill → Infuse → Pour → Decline (4 frames)
    // Pattern 5: Fill → Infuse → Ramp → Pour → Decline (5 frames)

    // First frame should be a fill frame
    if (!isFillFrame(steps[0])) {
        return false;
    }

    // Last frame (or second-to-last if decline) should be a pour frame
    int pourIndex = static_cast<int>(steps.size()) - 1;
    if (steps.size() >= 2 && isDeclineFrame(steps[pourIndex], &steps[pourIndex - 1])) {
        pourIndex--;
    }

    if (pourIndex < 1) {
        return false;
    }

    if (!isPourFrame(steps[pourIndex])) {
        return false;
    }

    return true;
}

RecipeParams RecipeAnalyzer::extractRecipeParams(const Profile& profile) {
    RecipeParams params;
    const auto& steps = profile.steps();

    if (steps.isEmpty()) {
        return params;
    }

    // Extract targets from profile
    params.targetWeight = profile.targetWeight();
    params.targetVolume = profile.targetVolume();

    // Default temperatures from profile
    double profileTemp = profile.espressoTemperature();
    params.fillTemperature = profileTemp;
    params.pourTemperature = profileTemp;

    // Find frame indices
    int fillIndex = 0;
    int bloomIndex = -1;
    int infuseIndex = -1;
    int rampIndex = -1;
    int pourIndex = -1;
    int declineIndex = -1;

    // First frame is fill
    if (isFillFrame(steps[0])) {
        fillIndex = 0;
    }

    // Find pour frame (last non-decline frame)
    for (int i = static_cast<int>(steps.size()) - 1; i >= 1; i--) {
        if (i > 0 && isDeclineFrame(steps[i], &steps[i - 1])) {
            declineIndex = i;
            continue;
        }
        if (isPourFrame(steps[i])) {
            pourIndex = i;
            break;
        }
    }

    // Find bloom, infuse, and ramp frames (between fill and pour)
    for (int i = fillIndex + 1; i < pourIndex; i++) {
        if (isBloomFrame(steps[i])) {
            bloomIndex = i;
        } else if (isRampFrame(steps[i])) {
            rampIndex = i;
        } else if (isInfuseFrame(steps[i])) {
            infuseIndex = i;
        }
    }

    // Extract fill parameters
    if (fillIndex >= 0 && fillIndex < steps.size()) {
        const auto& fillFrame = steps[fillIndex];
        params.fillPressure = extractFillPressure(fillFrame);
        params.fillTimeout = fillFrame.seconds;
        params.fillFlow = fillFrame.flow > 0 ? fillFrame.flow : 8.0;
        params.fillExitPressure = fillFrame.exitPressureOver > 0 ? fillFrame.exitPressureOver : 3.0;
        // Use fill frame temperature
        if (fillFrame.temperature > 0) {
            params.fillTemperature = fillFrame.temperature;
        }
    }

    // Extract bloom parameters
    if (bloomIndex >= 0 && bloomIndex < steps.size()) {
        params.bloomEnabled = true;
        params.bloomTime = steps[bloomIndex].seconds;
    } else {
        params.bloomEnabled = false;
    }

    // Extract infuse parameters
    if (infuseIndex >= 0 && infuseIndex < steps.size()) {
        const auto& infuseFrame = steps[infuseIndex];
        params.infusePressure = extractInfusePressure(infuseFrame);
        params.infuseTime = extractInfuseTime(infuseFrame);
        params.infuseVolume = infuseFrame.volume > 0 ? infuseFrame.volume : 100.0;
        params.infuseByWeight = false;  // Hard to detect from frames
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

    // Extract decline parameters (decline is now flow-based: declineTo = target flow in mL/s)
    if (declineIndex >= 0 && declineIndex < steps.size()) {
        const auto& declineFrame = steps[declineIndex];
        params.declineEnabled = true;
        // New model: decline is flow reduction. Extract target flow from decline frame.
        if (declineFrame.pump == "flow" && declineFrame.flow > 0) {
            params.declineTo = declineFrame.flow;
        } else {
            // Legacy pressure-mode decline: approximate flow decline from pour flow
            params.declineTo = params.pourFlow * 0.5;
        }
        params.declineTime = extractDeclineTime(declineFrame);
    } else {
        params.declineEnabled = false;
    }

    return params;
}

bool RecipeAnalyzer::convertToRecipeMode(Profile& profile) {
    if (!canConvertToRecipe(profile)) {
        qDebug() << "Profile" << profile.title() << "cannot be converted to recipe mode";
        return false;
    }

    RecipeParams params = extractRecipeParams(profile);
    profile.setRecipeMode(true);
    profile.setRecipeParams(params);

    qDebug() << "Converted profile" << profile.title() << "to recipe mode";
    return true;
}

void RecipeAnalyzer::forceConvertToRecipe(Profile& profile) {
    // Try normal conversion first
    if (canConvertToRecipe(profile)) {
        RecipeParams params = extractRecipeParams(profile);
        profile.setRecipeMode(true);
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
        profile.setRecipeMode(true);
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
            params.fillPressure = extractFillPressure(frame);
            params.fillTimeout = frame.seconds > 0 ? frame.seconds : 25.0;
            params.fillFlow = frame.flow > 0 ? frame.flow : 8.0;
            params.fillExitPressure = frame.exitPressureOver > 0 ? frame.exitPressureOver : 3.0;
            if (frame.temperature > 0) {
                params.fillTemperature = frame.temperature;
            }
            continue;
        }

        // Look for bloom frame
        if (foundFill && !foundInfuse && isBloomFrame(frame)) {
            params.bloomEnabled = true;
            params.bloomTime = frame.seconds > 0 ? frame.seconds : 10.0;
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
            // Check next frame for decline
            if (i + 1 < steps.size()) {
                const auto& nextFrame = steps[i + 1];
                if (isDeclineFrame(nextFrame, &frame)) {
                    params.declineEnabled = true;
                    if (nextFrame.pump == "flow" && nextFrame.flow > 0) {
                        params.declineTo = nextFrame.flow;
                    } else {
                        params.declineTo = params.pourFlow * 0.5;
                    }
                    params.declineTime = extractDeclineTime(nextFrame);
                }
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

    profile.setRecipeMode(true);
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

bool RecipeAnalyzer::isBloomFrame(const ProfileFrame& frame) {
    // Bloom frame characteristics:
    // - Usually named "Bloom"
    // - Zero or very low flow
    // - Short duration (5-30s)
    // - Pressure_under exit condition (waiting for CO2 to release)

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("bloom")) {
        return true;
    }

    // Heuristic: zero flow with pressure_under exit
    if (frame.flow <= 0.1 && frame.exitIf && frame.exitType == "pressure_under") {
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

bool RecipeAnalyzer::isDeclineFrame(const ProfileFrame& frame, const ProfileFrame* previousFrame) {
    // Decline frame characteristics:
    // - Usually named "Decline", "Ramp Down"
    // - Smooth transition
    // - Lower pressure than previous frame
    // - Pressure pump mode

    QString nameLower = frame.name.toLower();
    if (nameLower.contains("decline") || nameLower.contains("ramp down")) {
        return true;
    }

    // Heuristic: smooth transition to lower pressure or lower flow
    if (frame.transition == "smooth" && previousFrame) {
        if (frame.pump == "pressure" && frame.pressure < previousFrame->pressure) {
            return true;
        }
        if (frame.pump == "flow" && frame.flow < previousFrame->flow) {
            return true;
        }
    }

    return false;
}

// === Parameter Extraction ===

double RecipeAnalyzer::extractFillPressure(const ProfileFrame& frame) {
    // For fill frame, use the setpoint pressure
    if (frame.pump == "pressure") {
        return frame.pressure;
    }
    // For flow mode fill, use exit pressure as approximation
    if (frame.exitPressureOver > 0) {
        return frame.exitPressureOver;
    }
    return 2.0;  // Default
}

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

double RecipeAnalyzer::extractDeclinePressure(const ProfileFrame& frame) {
    return frame.pressure > 0 ? frame.pressure : 6.0;
}

double RecipeAnalyzer::extractDeclineTime(const ProfileFrame& frame) {
    return frame.seconds > 0 ? frame.seconds : 30.0;
}
