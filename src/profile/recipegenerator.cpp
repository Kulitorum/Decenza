#include "recipegenerator.h"
#include "profile.h"

QList<ProfileFrame> RecipeGenerator::generateFrames(const RecipeParams& recipe) {
    // Branch on editor type
    if (recipe.editorType == "pressure") {
        return generatePressureFrames(recipe);
    }
    if (recipe.editorType == "flow") {
        return generateFlowFrames(recipe);
    }
    if (recipe.editorType == "aflow") {
        return generateAFlowFrames(recipe);
    }

    // D-Flow frame generation
    QList<ProfileFrame> frames;

    // Frame 0: Fill - flow mode to saturate puck
    frames.append(createFillFrame(recipe));

    // Frame 1: Bloom - optional pause for CO2 release (before infuse)
    if (recipe.bloomEnabled && recipe.bloomTime > 0) {
        frames.append(createBloomFrame(recipe));
    }

    // Frame 2: Infuse - hold at soak pressure (if enabled)
    if (recipe.infuseEnabled) {
        frames.append(createInfuseFrame(recipe));
    }

    // Frame 3: Ramp - smooth transition to pour setpoint (if enabled and has duration)
    if (recipe.rampEnabled && recipe.rampTime > 0) {
        frames.append(createRampFrame(recipe));
    }

    // Frame 4: Pour - main extraction phase
    frames.append(createPourFrame(recipe));

    // Frame 5: Decline - optional flow decline
    if (recipe.declineEnabled) {
        frames.append(createDeclineFrame(recipe));
    }

    return frames;
}

Profile RecipeGenerator::createProfile(const RecipeParams& recipe, const QString& title) {
    Profile profile;

    // Metadata
    profile.setTitle(title);
    profile.setAuthor("Recipe Editor");
    profile.setBeverageType("espresso");

    // Set profile type based on editor type
    if (recipe.editorType == "pressure") {
        profile.setProfileType("settings_2a");
    } else if (recipe.editorType == "flow") {
        profile.setProfileType("settings_2b");
    } else {
        profile.setProfileType("settings_2c");
    }

    // Targets
    profile.setTargetWeight(recipe.targetWeight);
    profile.setTargetVolume(recipe.targetVolume > 0 ? recipe.targetVolume : 100.0);
    // For pressure/flow profiles, use tempStart as the machine's initial target temp
    if (recipe.editorType == "pressure" || recipe.editorType == "flow") {
        profile.setEspressoTemperature(recipe.tempStart);
    } else {
        profile.setEspressoTemperature(recipe.pourTemperature);
    }

    // Mode
    profile.setMode(Profile::Mode::FrameBased);

    // Generate and set frames
    profile.setSteps(generateFrames(recipe));

    // Calculate preinfuse frame count
    int preinfuseCount = 1;  // Preinfusion is always first
    if (recipe.editorType == "pressure" || recipe.editorType == "flow") {
        // Simple profiles: count preinfusion frames (1 or 2 with temp boost)
        if (recipe.preinfusionTime > 0) {
            bool hasTempBoost = (recipe.tempStart != recipe.tempPreinfuse);
            if (hasTempBoost && recipe.preinfusionTime > 2.0) {
                preinfuseCount = 2;  // boost + preinfusion
            } else {
                preinfuseCount = 1;  // single frame (preinfusion or boost only)
            }
        } else {
            preinfuseCount = 0;
        }
    } else if (recipe.editorType == "aflow") {
        // A-Flow: only Fill counts as preinfuse (matches de1plus profiles)
    } else {
        if (recipe.bloomEnabled && recipe.bloomTime > 0) preinfuseCount++;
        if (recipe.infuseEnabled) preinfuseCount++;
        if (recipe.rampEnabled && recipe.rampTime > 0) preinfuseCount++;
    }
    profile.setPreinfuseFrameCount(preinfuseCount);

    // Store recipe params for re-editing
    profile.setRecipeMode(true);
    profile.setRecipeParams(recipe);

    return profile;
}

ProfileFrame RecipeGenerator::createFillFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Fill";
    frame.pump = "flow";
    frame.flow = recipe.fillFlow;
    frame.pressure = recipe.fillPressure;  // Pressure limit
    frame.temperature = recipe.fillTemperature;
    frame.seconds = recipe.fillTimeout;
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = 100.0;

    // Exit when pressure builds (indicates puck is saturated)
    frame.exitIf = true;
    frame.exitType = "pressure_over";
    frame.exitPressureOver = recipe.fillExitPressure;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 6.0;
    frame.exitFlowUnder = 0.0;

    // Pressure limiter in flow mode
    frame.maxFlowOrPressure = recipe.fillPressure;
    frame.maxFlowOrPressureRange = 0.6;

    return frame;
}

ProfileFrame RecipeGenerator::createBloomFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Bloom";
    frame.pump = "flow";
    frame.flow = 0.0;  // Zero flow - let puck rest
    frame.pressure = 0.0;
    frame.temperature = recipe.fillTemperature;
    frame.seconds = recipe.bloomTime;
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = 0.0;

    // Exit when pressure drops (CO2 has escaped)
    frame.exitIf = true;
    frame.exitType = "pressure_under";
    frame.exitPressureOver = 11.0;
    frame.exitPressureUnder = 0.5;
    frame.exitFlowOver = 6.0;
    frame.exitFlowUnder = 0.0;

    frame.maxFlowOrPressure = 0.0;
    frame.maxFlowOrPressureRange = 0.6;

    return frame;
}

ProfileFrame RecipeGenerator::createInfuseFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Infuse";
    frame.pump = "pressure";
    frame.pressure = recipe.infusePressure;
    frame.flow = 8.0;
    frame.temperature = recipe.fillTemperature;  // Use fill temp for infuse
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = recipe.infuseVolume;

    // Duration depends on mode
    if (recipe.infuseByWeight) {
        // Long timeout, actual exit handled by weight popup
        frame.seconds = 60.0;
    } else {
        frame.seconds = recipe.infuseTime;
    }

    // No exit condition for time-based, or weight-based handled by popup
    frame.exitIf = false;
    frame.exitType = "";
    frame.exitPressureOver = 0.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 0.0;
    frame.exitFlowUnder = 0.0;

    // Flow limiter
    frame.maxFlowOrPressure = 1.0;
    frame.maxFlowOrPressureRange = 0.6;

    return frame;
}

ProfileFrame RecipeGenerator::createRampFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Ramp";
    frame.temperature = recipe.pourTemperature;
    frame.seconds = recipe.rampTime;
    frame.transition = "smooth";  // Smooth transition creates the ramp
    frame.sensor = "coffee";
    frame.volume = 100.0;

    // Always flow mode with pressure limit (matching de1app D-Flow model)
    frame.pump = "flow";
    frame.flow = recipe.pourFlow;
    frame.pressure = recipe.pourPressure;
    frame.maxFlowOrPressure = recipe.pourPressure;
    frame.maxFlowOrPressureRange = 0.6;

    // No exit condition - fixed duration
    frame.exitIf = false;
    frame.exitType = "";
    frame.exitPressureOver = 0.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 0.0;
    frame.exitFlowUnder = 0.0;

    return frame;
}

ProfileFrame RecipeGenerator::createPourFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Pour";
    frame.temperature = recipe.pourTemperature;
    frame.seconds = 60.0;  // Long duration - weight system stops the shot
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = 100.0;

    // Always flow mode with pressure limit (matching de1app D-Flow model)
    frame.pump = "flow";
    frame.flow = recipe.pourFlow;
    frame.pressure = recipe.pourPressure;
    frame.maxFlowOrPressure = recipe.pourPressure;
    frame.maxFlowOrPressureRange = 0.6;

    // No exit condition - weight system handles shot termination
    frame.exitIf = false;
    frame.exitType = "";
    frame.exitPressureOver = 0.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 0.0;
    frame.exitFlowUnder = 0.0;

    return frame;
}

ProfileFrame RecipeGenerator::createDeclineFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Decline";
    frame.temperature = recipe.pourTemperature;
    frame.seconds = recipe.declineTime;
    frame.transition = "smooth";  // Key: smooth ramp creates the decline curve
    frame.sensor = "coffee";
    frame.volume = 100.0;

    // Flow mode decline - reduce flow over time
    frame.pump = "flow";
    frame.flow = recipe.declineTo;
    frame.pressure = recipe.pourPressure;
    frame.maxFlowOrPressure = recipe.pourPressure;
    frame.maxFlowOrPressureRange = 0.6;

    // No exit condition - time/weight handles termination
    frame.exitIf = false;
    frame.exitType = "";
    frame.exitPressureOver = 0.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 0.0;
    frame.exitFlowUnder = 0.0;

    return frame;
}

// === A-Flow Frame Generation ===
// Matches de1plus A-Flow profile structure:
// Fill → Infuse → Pressure Up → Pressure Decline → Flow Start → Flow Extraction

QList<ProfileFrame> RecipeGenerator::generateAFlowFrames(const RecipeParams& recipe) {
    QList<ProfileFrame> frames;

    // Frame 0: Fill (same as D-Flow)
    frames.append(createFillFrame(recipe));

    // Frame 1: Infuse (same as D-Flow, if enabled)
    if (recipe.infuseEnabled) {
        frames.append(createInfuseFrame(recipe));
    }

    // Frame 2: Pressure Up - smooth ramp to pour pressure
    {
        ProfileFrame pressureUp;
        pressureUp.name = "Pressure Up";
        pressureUp.pump = "pressure";
        pressureUp.pressure = recipe.pourPressure;
        pressureUp.flow = 8.0;
        pressureUp.temperature = recipe.pourTemperature;
        pressureUp.seconds = recipe.rampTime;
        pressureUp.transition = "smooth";
        pressureUp.sensor = "coffee";
        pressureUp.volume = 100.0;
        // Exit when flow exceeds pour flow (puck is saturated)
        pressureUp.exitIf = true;
        pressureUp.exitType = "flow_over";
        pressureUp.exitPressureOver = 8.5;  // Constant from de1plus A-Flow template
        pressureUp.exitPressureUnder = 0.0;
        pressureUp.exitFlowOver = recipe.pourFlow;
        pressureUp.exitFlowUnder = 0.0;
        pressureUp.maxFlowOrPressure = 0.0;
        pressureUp.maxFlowOrPressureRange = 0.6;
        frames.append(pressureUp);
    }

    // Frame 3: Pressure Decline - decline to 1 bar, exit when flow drops
    {
        ProfileFrame pressureDecline;
        pressureDecline.name = "Pressure Decline";
        pressureDecline.pump = "pressure";
        pressureDecline.pressure = 1.0;
        pressureDecline.flow = 8.0;
        pressureDecline.temperature = recipe.pourTemperature;
        pressureDecline.seconds = 0.0;  // Exit condition controls duration
        pressureDecline.transition = "smooth";
        pressureDecline.sensor = "coffee";
        pressureDecline.volume = 100.0;
        pressureDecline.exitIf = true;
        pressureDecline.exitType = "flow_under";
        pressureDecline.exitPressureOver = 11.0;
        pressureDecline.exitPressureUnder = 1.0;
        pressureDecline.exitFlowOver = 3.0;
        pressureDecline.exitFlowUnder = recipe.pourFlow + 0.1;
        pressureDecline.maxFlowOrPressure = 0.0;
        pressureDecline.maxFlowOrPressureRange = 0.6;
        frames.append(pressureDecline);
    }

    // Frame 4: Flow Start - transition to flow control
    {
        ProfileFrame flowStart;
        flowStart.name = "Flow Start";
        flowStart.pump = "flow";
        flowStart.flow = recipe.pourFlow;
        flowStart.pressure = recipe.pourPressure;
        flowStart.temperature = recipe.pourTemperature;
        flowStart.seconds = 0.0;
        flowStart.transition = "fast";
        flowStart.sensor = "coffee";
        flowStart.volume = 100.0;
        flowStart.exitIf = false;
        flowStart.exitType = "";
        flowStart.exitPressureOver = 0.0;
        flowStart.exitPressureUnder = 0.0;
        flowStart.exitFlowOver = 0.0;
        flowStart.exitFlowUnder = 0.0;
        flowStart.maxFlowOrPressure = 0.0;
        flowStart.maxFlowOrPressureRange = 0.6;
        frames.append(flowStart);
    }

    // Frame 5: Flow Extraction - main extraction at target flow with pressure limiter
    {
        ProfileFrame extraction;
        extraction.name = "Flow Extraction";
        extraction.pump = "flow";
        extraction.flow = recipe.pourFlow;
        extraction.pressure = recipe.pourPressure;
        extraction.temperature = recipe.pourTemperature;
        extraction.seconds = 60.0;  // Long duration - weight system stops the shot
        extraction.transition = "smooth";
        extraction.sensor = "coffee";
        extraction.volume = 100.0;
        extraction.exitIf = false;
        extraction.exitType = "";
        extraction.exitPressureOver = 0.0;
        extraction.exitPressureUnder = 0.0;
        extraction.exitFlowOver = 0.0;
        extraction.exitFlowUnder = 0.0;
        extraction.maxFlowOrPressure = recipe.pourPressure;
        extraction.maxFlowOrPressureRange = 0.6;
        frames.append(extraction);
    }

    return frames;
}

// === Simple Pressure Profile Frame Generation (settings_2a) ===
// Matches de1app's pressure_to_advanced_list():
// Preinfusion → (Forced Rise) → Hold → Decline

QList<ProfileFrame> RecipeGenerator::generatePressureFrames(const RecipeParams& recipe) {
    QList<ProfileFrame> frames;
    double tempStart = recipe.tempStart;
    double tempPreinfuse = recipe.tempPreinfuse;
    double tempHold = recipe.tempHold;
    double tempDecline = recipe.tempDecline;

    // Preinfusion frame(s) (flow pump, exit on pressure_over)
    // When tempStart != tempPreinfuse, split into a 2-second temp boost at tempStart
    // followed by remaining time at tempPreinfuse (matches de1app's temp_bump_time_seconds)
    if (recipe.preinfusionTime > 0) {
        bool needTempBoost = (tempStart != tempPreinfuse);
        double boostDuration = 2.0;  // de1app: temp_bump_time_seconds

        if (needTempBoost) {
            double boostLen = qMin(boostDuration, recipe.preinfusionTime);
            double remainLen = recipe.preinfusionTime - boostDuration;
            if (remainLen < 0) remainLen = 0;

            // Temp boost frame at tempStart (no flow exit)
            ProfileFrame boost;
            boost.name = "preinfusion temp boost";
            boost.temperature = tempStart;
            boost.sensor = "coffee";
            boost.pump = "flow";
            boost.transition = "fast";
            boost.pressure = 1.0;
            boost.flow = recipe.preinfusionFlowRate;
            boost.seconds = boostLen;
            boost.volume = 0;
            boost.exitIf = true;
            boost.exitType = "pressure_over";
            boost.exitPressureOver = recipe.preinfusionStopPressure;
            // exitFlowOver = 0 (default) - no flow exit during temp boost
            frames.append(boost);

            // Preinfusion frame at tempPreinfuse (with flow exit)
            if (remainLen > 0) {
                ProfileFrame preinfusion;
                preinfusion.name = "preinfusion";
                preinfusion.temperature = tempPreinfuse;
                preinfusion.sensor = "coffee";
                preinfusion.pump = "flow";
                preinfusion.transition = "fast";
                preinfusion.pressure = 1.0;
                preinfusion.flow = recipe.preinfusionFlowRate;
                preinfusion.seconds = remainLen;
                preinfusion.volume = 0;
                preinfusion.exitIf = true;
                preinfusion.exitType = "pressure_over";
                preinfusion.exitPressureOver = recipe.preinfusionStopPressure;
                preinfusion.exitFlowOver = 6.0;
                frames.append(preinfusion);
            }
        } else {
            // Single preinfusion frame (no temp boost needed)
            ProfileFrame preinfusion;
            preinfusion.name = "preinfusion";
            preinfusion.temperature = tempPreinfuse;
            preinfusion.sensor = "coffee";
            preinfusion.pump = "flow";
            preinfusion.transition = "fast";
            preinfusion.pressure = 1.0;
            preinfusion.flow = recipe.preinfusionFlowRate;
            preinfusion.seconds = recipe.preinfusionTime;
            preinfusion.volume = 0;
            preinfusion.exitIf = true;
            preinfusion.exitType = "pressure_over";
            preinfusion.exitPressureOver = recipe.preinfusionStopPressure;
            preinfusion.exitFlowOver = 6.0;
            frames.append(preinfusion);
        }
    }

    // Rise and hold frame (pressure pump)
    double holdTime = recipe.holdTime;
    if (holdTime > 0) {
        // If hold time > 3s, add a forced rise frame without limiter first
        if (holdTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = tempHold;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = recipe.espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            holdTime -= 3;
        }

        ProfileFrame hold;
        hold.name = "rise and hold";
        hold.temperature = tempHold;
        hold.sensor = "coffee";
        hold.pump = "pressure";
        hold.transition = "fast";
        hold.pressure = recipe.espressoPressure;
        hold.seconds = holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        if (recipe.limiterValue > 0) {
            hold.maxFlowOrPressure = recipe.limiterValue;
            hold.maxFlowOrPressureRange = recipe.limiterRange;
        }
        frames.append(hold);
    }

    // Decline frame (pressure pump, smooth transition)
    double declineTime = recipe.simpleDeclineTime;
    if (declineTime > 0) {
        // Match de1app: add forced rise before decline when hold was short (< 3s after
        // possible decrement) and decline is long enough to split off 3s
        if (holdTime < 3 && declineTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = tempDecline;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = recipe.espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            declineTime -= 3;
        }

        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = tempDecline;
        decline.sensor = "coffee";
        decline.pump = "pressure";
        decline.transition = "smooth";
        decline.pressure = recipe.pressureEnd;
        decline.seconds = declineTime;
        decline.volume = 0;
        decline.exitIf = false;
        decline.exitFlowOver = 6.0;
        if (recipe.limiterValue > 0) {
            decline.maxFlowOrPressure = recipe.limiterValue;
            decline.maxFlowOrPressureRange = recipe.limiterRange;
        }
        frames.append(decline);
    }

    // Fallback: add empty frame if no frames were created
    if (frames.isEmpty()) {
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}

// === Simple Flow Profile Frame Generation (settings_2b) ===
// Matches de1app's flow_to_advanced_list():
// Preinfusion → Hold → Decline

QList<ProfileFrame> RecipeGenerator::generateFlowFrames(const RecipeParams& recipe) {
    QList<ProfileFrame> frames;
    double tempStart = recipe.tempStart;
    double tempPreinfuse = recipe.tempPreinfuse;
    double tempHold = recipe.tempHold;
    double tempDecline = recipe.tempDecline;

    // Preinfusion frame(s) (flow pump, exit on pressure_over)
    // When tempStart != tempPreinfuse, split into a 2-second temp boost at tempStart
    // followed by remaining time at tempPreinfuse (matches de1app's temp_bump_time_seconds)
    if (recipe.preinfusionTime > 0) {
        bool needTempBoost = (tempStart != tempPreinfuse);
        double boostDuration = 2.0;  // de1app: temp_bump_time_seconds

        if (needTempBoost) {
            double boostLen = qMin(boostDuration, recipe.preinfusionTime);
            double remainLen = recipe.preinfusionTime - boostDuration;
            if (remainLen < 0) remainLen = 0;

            // Temp boost frame at tempStart (no flow exit)
            ProfileFrame boost;
            boost.name = "preinfusion boost";
            boost.temperature = tempStart;
            boost.sensor = "coffee";
            boost.pump = "flow";
            boost.transition = "fast";
            boost.pressure = 1.0;
            boost.flow = recipe.preinfusionFlowRate;
            boost.seconds = boostLen;
            boost.volume = 0;
            boost.exitIf = true;
            boost.exitType = "pressure_over";
            boost.exitPressureOver = recipe.preinfusionStopPressure;
            // exitFlowOver = 0 (default) - no flow exit during temp boost
            frames.append(boost);

            // Preinfusion frame at tempPreinfuse (no flow exit for flow profiles)
            if (remainLen > 0) {
                ProfileFrame preinfusion;
                preinfusion.name = "preinfusion";
                preinfusion.temperature = tempPreinfuse;
                preinfusion.sensor = "coffee";
                preinfusion.pump = "flow";
                preinfusion.transition = "fast";
                preinfusion.pressure = 1.0;
                preinfusion.flow = recipe.preinfusionFlowRate;
                preinfusion.seconds = remainLen;
                preinfusion.volume = 0;
                preinfusion.exitIf = true;
                preinfusion.exitType = "pressure_over";
                preinfusion.exitPressureOver = recipe.preinfusionStopPressure;
                // exitFlowOver = 0 (default) - flow profiles don't use flow exit
                frames.append(preinfusion);
            }
        } else {
            // Single preinfusion frame (no temp boost needed)
            ProfileFrame preinfusion;
            preinfusion.name = "preinfusion";
            preinfusion.temperature = tempPreinfuse;
            preinfusion.sensor = "coffee";
            preinfusion.pump = "flow";
            preinfusion.transition = "fast";
            preinfusion.pressure = 1.0;
            preinfusion.flow = recipe.preinfusionFlowRate;
            preinfusion.seconds = recipe.preinfusionTime;
            preinfusion.volume = 0;
            preinfusion.exitIf = true;
            preinfusion.exitType = "pressure_over";
            preinfusion.exitPressureOver = recipe.preinfusionStopPressure;
            frames.append(preinfusion);
        }
    }

    // Hold frame (flow pump)
    if (recipe.holdTime > 0) {
        ProfileFrame hold;
        hold.name = "hold";
        hold.temperature = tempHold;
        hold.sensor = "coffee";
        hold.pump = "flow";
        hold.transition = "fast";
        hold.flow = recipe.holdFlow;
        hold.seconds = recipe.holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        hold.exitFlowOver = 6.0;
        if (recipe.limiterValue > 0) {
            hold.maxFlowOrPressure = recipe.limiterValue;
            hold.maxFlowOrPressureRange = recipe.limiterRange;
        }
        frames.append(hold);
    }

    // Decline frame (flow pump, smooth transition)
    // de1app: decline is only generated when holdTime > 0 (not declineTime > 0)
    if (recipe.holdTime > 0) {
        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = tempDecline;
        decline.sensor = "coffee";
        decline.pump = "flow";
        decline.transition = "smooth";
        decline.flow = recipe.flowEnd;
        decline.seconds = recipe.simpleDeclineTime;
        decline.volume = 0;
        decline.exitIf = false;
        if (recipe.limiterValue > 0) {
            decline.maxFlowOrPressure = recipe.limiterValue;
            decline.maxFlowOrPressureRange = recipe.limiterRange;
        }
        frames.append(decline);
    }

    // Fallback: add empty frame if no frames were created
    if (frames.isEmpty()) {
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}
