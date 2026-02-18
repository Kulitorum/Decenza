#include "recipegenerator.h"
#include "profile.h"
#include <QDebug>
#include <cmath>

QList<ProfileFrame> RecipeGenerator::generateFrames(const RecipeParams& recipe) {
    // Branch on editor type
    switch (recipe.editorType) {
    case EditorType::Pressure:
        return generatePressureFrames(recipe);
    case EditorType::Flow:
        return generateFlowFrames(recipe);
    case EditorType::AFlow:
        return generateAFlowFrames(recipe);
    case EditorType::DFlow:
        break;  // Fall through to D-Flow generation below
    }

    // D-Flow frame generation
    // Always 3 core frames (matching de1app): Filling, Infusing, Pouring
    // Optional Decenza extras: Bloom (before Infusing), Decline (after Pouring)
    QList<ProfileFrame> frames;

    // Filling - pressure mode to saturate puck (always first)
    frames.append(createFillFrame(recipe));

    // Bloom - optional pause for CO2 release (Decenza extra, not in de1app)
    if (recipe.bloomEnabled && recipe.bloomTime > 0) {
        frames.append(createBloomFrame(recipe));
    }

    // Infusing - hold at soak pressure (if enabled)
    if (recipe.infuseEnabled) {
        frames.append(createInfuseFrame(recipe));
    }

    // Pouring - main extraction phase
    frames.append(createPourFrame(recipe));

    // Decline - optional flow decline
    if (recipe.declineEnabled) {
        frames.append(createDeclineFrame(recipe));
    }

    // Fallback: add empty frame if no frames were created (consistency with other generators)
    if (frames.isEmpty()) {
        qWarning() << "RecipeGenerator: D-Flow generateFrames produced 0 frames, adding fallback";
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "fast";
        empty.flow = 0.0;
        empty.seconds = 0.0;
        frames.append(empty);
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
    if (recipe.editorType == EditorType::Pressure) {
        profile.setProfileType("settings_2a");
    } else if (recipe.editorType == EditorType::Flow) {
        profile.setProfileType("settings_2b");
    } else {
        profile.setProfileType("settings_2c");
    }

    // Targets
    profile.setTargetWeight(recipe.targetWeight);
    profile.setTargetVolume(recipe.targetVolume > 0 ? recipe.targetVolume : 100.0);
    // For pressure/flow profiles, use tempHold as the machine's baseline temp
    // (tempStart is a 2-second boost and doesn't represent the main extraction temp)
    if (recipe.editorType == EditorType::Pressure || recipe.editorType == EditorType::Flow) {
        profile.setEspressoTemperature(recipe.tempHold);
    } else {
        profile.setEspressoTemperature(recipe.pourTemperature);
    }

    // Mode
    profile.setMode(Profile::Mode::FrameBased);

    // Generate and set frames
    profile.setSteps(generateFrames(recipe));

    if (profile.steps().size() == 1 && profile.steps()[0].name == "empty") {
        qWarning() << "RecipeGenerator::createProfile: recipe produced fallback empty frame for" << title;
    }

    // Count preinfuse frames from actual generated frames (authoritative)
    profile.setPreinfuseFrameCount(Profile::countPreinfuseFrames(profile.steps()));

    // Store recipe params for re-editing
    profile.setRecipeMode(true);
    profile.setRecipeParams(recipe);

    return profile;
}

ProfileFrame RecipeGenerator::createFillFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Filling";
    frame.pump = "pressure";
    frame.pressure = recipe.infusePressure;
    frame.flow = recipe.fillFlow;
    frame.temperature = recipe.fillTemperature;
    frame.seconds = recipe.fillTimeout;
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = 100.0;

    // Exit when pressure builds (indicates puck is saturated)
    // de1app formula: exit_pressure_over = infusePressure, halved+0.6 when >= 2.8, min 1.2
    double exitP = recipe.infusePressure;
    if (exitP >= 2.8)
        exitP = std::round((exitP / 2.0 + 0.6) * 10.0) / 10.0;
    if (exitP < 1.2)
        exitP = 1.2;
    frame.exitIf = true;
    frame.exitType = "pressure_over";
    frame.exitPressureOver = exitP;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 6.0;
    frame.exitFlowUnder = 0.0;

    // No extension limiter (de1app: max_flow_or_pressure=0)
    frame.maxFlowOrPressure = 0.0;
    frame.maxFlowOrPressureRange = 0.2;

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
    frame.maxFlowOrPressureRange = 0.2;

    return frame;
}

ProfileFrame RecipeGenerator::createInfuseFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Infusing";
    frame.pump = "pressure";
    frame.pressure = recipe.infusePressure;
    frame.flow = 8.0;
    frame.temperature = recipe.pourTemperature;  // de1app uses pouring temp for infuse
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = recipe.infuseVolume;

    // Duration depends on mode
    if (recipe.infuseByWeight) {
        // Long timeout: app monitors scale weight and sends SkipToNext when target is reached
        frame.seconds = 60.0;
        // Set exit weight for app-side SkipToNext (independent of exitIf per CLAUDE.md)
        frame.exitWeight = recipe.infuseWeight;
    } else {
        frame.seconds = recipe.infuseTime;
    }

    // No machine-side exit condition; time-based exits via frame timeout, weight-based exits via app-side SkipToNext
    // Dead exit fields stored for de1app compatibility
    frame.exitIf = false;
    frame.exitType = "pressure_over";
    frame.exitPressureOver = recipe.infusePressure;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 6.0;
    frame.exitFlowUnder = 0.0;

    // No extension limiter (de1app: max_flow_or_pressure=0)
    frame.maxFlowOrPressure = 0.0;
    frame.maxFlowOrPressureRange = 0.2;

    return frame;
}

ProfileFrame RecipeGenerator::createPourFrame(const RecipeParams& recipe) {
    ProfileFrame frame;

    frame.name = "Pouring";
    frame.temperature = recipe.pourTemperature;
    frame.seconds = 127.0;  // Max duration - weight system stops the shot
    frame.transition = "fast";
    frame.sensor = "coffee";
    frame.volume = 0.0;

    // Flow mode with pressure limiter (de1app D-Flow model)
    frame.pump = "flow";
    frame.flow = recipe.pourFlow;
    frame.pressure = 4.8;  // Vestigial field - de1app never updates it
    frame.maxFlowOrPressure = recipe.pourPressure;
    frame.maxFlowOrPressureRange = 0.2;

    // No machine-side exit condition - weight system handles shot termination
    // Dead exit fields stored for de1app compatibility
    frame.exitIf = false;
    frame.exitType = "flow_over";
    frame.exitPressureOver = 11.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 2.80;
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
    frame.maxFlowOrPressureRange = 0.2;

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
// Matches de1app's update_A-Flow proc. All 9 frames built inline (not shared with D-Flow).
// Pre Fill → Fill → Infuse → 2nd Fill → Pause → Pressure Up → Pressure Decline → Flow Start → Flow Extraction
//
// Key differences from D-Flow:
// - Fill: flow pump with pressure limiter (8.0 bar), range 0.6
// - Infuse: flow=0 (pressure hold), uses fill temperature, limiter=1.0, range 0.6
// - pourFlow = user's target flow (de1app Aflow_pouring_flow), goes into Flow Start
// - Extraction flow derived: flowExtractionUp ? pourFlow*2 : 0
// - rampDownEnabled splits rampTime between Pressure Up and Decline

QList<ProfileFrame> RecipeGenerator::generateAFlowFrames(const RecipeParams& recipe) {
    QList<ProfileFrame> frames;

    // Frame 0: Pre Fill (1s workaround for DE1 "skip first step" bug)
    {
        ProfileFrame preFill;
        preFill.name = "Pre Fill";
        preFill.pump = "flow";
        preFill.flow = 8.0;
        preFill.pressure = 3.0;
        preFill.temperature = recipe.fillTemperature;
        preFill.seconds = 1.0;
        preFill.transition = "fast";
        preFill.sensor = "coffee";
        preFill.volume = 100.0;
        preFill.maxFlowOrPressure = 8.0;
        preFill.maxFlowOrPressureRange = 0.6;
        preFill.exitIf = false;
        preFill.exitType = "pressure_over";
        preFill.exitPressureOver = 3.0;
        preFill.exitPressureUnder = 0.0;
        preFill.exitFlowOver = 6.0;
        preFill.exitFlowUnder = 0.0;
        frames.append(preFill);
    }

    // Frame 1: Fill — flow pump with pressure limiter at 8.0 bar
    {
        ProfileFrame fill;
        fill.name = "Fill";
        fill.pump = "flow";
        fill.flow = recipe.fillFlow;
        fill.pressure = recipe.fillPressure;
        fill.temperature = recipe.fillTemperature;
        fill.seconds = recipe.fillTimeout;
        fill.transition = "fast";
        fill.sensor = "coffee";
        fill.volume = 100.0;
        fill.exitIf = true;
        fill.exitType = "pressure_over";
        fill.exitPressureOver = recipe.fillPressure;  // de1app A-Flow template uses fill pressure (3.0)
        fill.exitPressureUnder = 0.0;
        fill.exitFlowOver = 6.0;
        fill.exitFlowUnder = 0.0;
        fill.maxFlowOrPressure = 8.0;
        fill.maxFlowOrPressureRange = 0.6;
        frames.append(fill);
    }

    // Frame 2: Infuse — pressure hold with zero flow, uses fill temperature
    if (recipe.infuseEnabled) {
        ProfileFrame infuse;
        infuse.name = "Infuse";
        infuse.pump = "pressure";
        infuse.flow = 0.0;
        infuse.pressure = recipe.infusePressure;
        infuse.temperature = recipe.fillTemperature;  // A-Flow uses fill temp, not pour temp
        infuse.transition = "fast";
        infuse.sensor = "coffee";
        infuse.volume = recipe.infuseVolume;

        if (recipe.infuseByWeight) {
            infuse.seconds = 60.0;
            infuse.exitWeight = recipe.infuseWeight;
        } else {
            infuse.seconds = recipe.infuseTime;
        }

        // Dead exit fields (exit_if=false, but stored for de1app compatibility)
        infuse.exitIf = false;
        infuse.exitType = "pressure_over";
        infuse.exitPressureOver = 3.0;
        infuse.exitPressureUnder = 0.0;
        infuse.exitFlowOver = 6.0;
        infuse.exitFlowUnder = 0.0;
        infuse.maxFlowOrPressure = 1.0;
        infuse.maxFlowOrPressureRange = 0.6;
        frames.append(infuse);
    }

    // Frame 3: 2nd Fill (active when secondFillEnabled, 0s otherwise)
    {
        ProfileFrame secondFill;
        secondFill.name = "2nd Fill";
        secondFill.pump = "flow";
        secondFill.flow = 8.0;
        secondFill.pressure = 0.0;
        secondFill.temperature = recipe.secondFillEnabled ? recipe.pourTemperature : 95.0;
        secondFill.seconds = recipe.secondFillEnabled ? 15.0 : 0.0;
        secondFill.transition = "fast";
        secondFill.sensor = "coffee";
        secondFill.volume = 100.0;
        secondFill.maxFlowOrPressure = 3.0;
        secondFill.maxFlowOrPressureRange = 0.6;
        secondFill.exitIf = true;
        secondFill.exitType = "pressure_over";
        secondFill.exitPressureOver = 2.5;
        secondFill.exitPressureUnder = 0.0;
        secondFill.exitFlowOver = 6.0;
        secondFill.exitFlowUnder = 0.0;
        frames.append(secondFill);
    }

    // Frame 4: Pause (active when secondFillEnabled, 0s otherwise)
    {
        ProfileFrame pause;
        pause.name = "Pause";
        pause.pump = "pressure";
        pause.pressure = 1.0;
        pause.flow = 6.0;
        pause.temperature = recipe.secondFillEnabled ? recipe.pourTemperature : 95.0;
        pause.seconds = recipe.secondFillEnabled ? 15.0 : 0.0;
        pause.transition = "fast";
        pause.sensor = "coffee";
        pause.volume = 100.0;
        pause.maxFlowOrPressure = 1.0;
        pause.maxFlowOrPressureRange = 0.6;
        pause.exitIf = true;
        pause.exitType = "flow_under";
        pause.exitPressureOver = 0.0;
        pause.exitPressureUnder = 0.0;
        pause.exitFlowOver = 6.0;
        pause.exitFlowUnder = 1.0;
        frames.append(pause);
    }

    // Compute pressureUp seconds once — used for both Pressure Up and Flow Start activation
    double pressureUpSeconds = recipe.rampDownEnabled
        ? recipe.rampTime / 2.0
        : recipe.rampTime;

    // Frame 5: Pressure Up — smooth ramp to pour pressure
    // rampDownEnabled splits rampTime between Up and Decline
    {
        ProfileFrame pressureUp;
        pressureUp.name = "Pressure Up";
        pressureUp.pump = "pressure";
        pressureUp.pressure = recipe.pourPressure;
        pressureUp.flow = 8.0;
        pressureUp.temperature = recipe.pourTemperature;
        pressureUp.transition = "smooth";
        pressureUp.sensor = "coffee";
        pressureUp.volume = 100.0;

        pressureUp.seconds = pressureUpSeconds;

        pressureUp.exitIf = true;
        pressureUp.exitType = "flow_over";
        // When rampDownEnabled, exit at higher flow (pourFlow*2) since decline handles the rest
        pressureUp.exitFlowOver = recipe.rampDownEnabled
            ? recipe.pourFlow * 2.0
            : recipe.pourFlow;
        pressureUp.exitPressureOver = 8.5;
        pressureUp.exitPressureUnder = 0.0;
        pressureUp.exitFlowUnder = 0.0;
        pressureUp.maxFlowOrPressure = 0.0;
        pressureUp.maxFlowOrPressureRange = 0.6;
        frames.append(pressureUp);
    }

    // Frame 6: Pressure Decline — decline to 1 bar, exit when flow drops
    // rampDownEnabled gives remaining time to Decline; otherwise 0 (exit-controlled)
    {
        ProfileFrame pressureDecline;
        pressureDecline.name = "Pressure Decline";
        pressureDecline.pump = "pressure";
        pressureDecline.pressure = 1.0;
        pressureDecline.flow = 8.0;
        pressureDecline.temperature = recipe.pourTemperature;
        pressureDecline.transition = "smooth";
        pressureDecline.sensor = "coffee";
        pressureDecline.volume = 100.0;

        pressureDecline.seconds = recipe.rampDownEnabled
            ? recipe.rampTime - recipe.rampTime / 2.0
            : 0.0;

        pressureDecline.exitIf = true;
        pressureDecline.exitType = "flow_under";
        pressureDecline.exitFlowUnder = recipe.pourFlow + 0.1;
        pressureDecline.exitFlowOver = 3.0;
        pressureDecline.exitPressureOver = 11.0;
        pressureDecline.exitPressureUnder = 1.0;
        pressureDecline.maxFlowOrPressure = 0.0;
        pressureDecline.maxFlowOrPressureRange = 0.6;
        frames.append(pressureDecline);
    }

    // Frame 7: Flow Start — conditionally activated when pressureUpSeconds < 1
    {
        ProfileFrame flowStart;
        flowStart.name = "Flow Start";
        flowStart.pump = "flow";
        flowStart.flow = recipe.pourFlow;
        flowStart.pressure = 3.0;  // Vestigial template constant
        flowStart.temperature = recipe.pourTemperature;
        flowStart.transition = "fast";
        flowStart.sensor = "coffee";
        flowStart.volume = 100.0;
        flowStart.maxFlowOrPressure = 0.0;
        flowStart.maxFlowOrPressureRange = 0.6;

        if (pressureUpSeconds < 1.0) {
            // Activated: becomes an exit frame that waits for flow to stabilize
            flowStart.seconds = 10.0;
            flowStart.exitIf = true;
            flowStart.exitType = "flow_over";
            flowStart.exitFlowOver = recipe.pourFlow - 0.1;
            flowStart.exitPressureOver = 11.0;
            flowStart.exitPressureUnder = 0.0;
            flowStart.exitFlowUnder = 0.0;
        } else {
            // Passthrough: zero seconds, no exit
            flowStart.seconds = 0.0;
            flowStart.exitIf = false;
            flowStart.exitType = "pressure_under";  // Dead template value
            flowStart.exitFlowOver = 6.0;
            flowStart.exitPressureOver = 11.0;
            flowStart.exitPressureUnder = 0.0;
            flowStart.exitFlowUnder = 0.0;
        }
        frames.append(flowStart);
    }

    // Frame 8: Flow Extraction — main extraction with pressure limiter
    // flowExtractionUp: pourFlow*2 with smooth ramp; otherwise 0 (flat, pressure-limited)
    {
        ProfileFrame extraction;
        extraction.name = "Flow Extraction";
        extraction.pump = "flow";
        extraction.flow = recipe.flowExtractionUp ? recipe.pourFlow * 2.0 : 0.0;
        extraction.pressure = 3.0;  // Vestigial template constant
        extraction.temperature = recipe.pourTemperature;
        extraction.seconds = 60.0;  // Long duration - weight system stops the shot
        extraction.transition = "smooth";
        extraction.sensor = "coffee";
        extraction.volume = 100.0;
        extraction.maxFlowOrPressure = recipe.pourPressure;
        extraction.maxFlowOrPressureRange = 0.6;
        // Dead exit fields
        extraction.exitIf = false;
        extraction.exitType = "pressure_under";
        extraction.exitFlowOver = 6.0;
        extraction.exitPressureOver = 11.0;
        extraction.exitPressureUnder = 0.0;
        extraction.exitFlowUnder = 0.0;
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
        bool needTempBoost = !qFuzzyCompare(1.0 + tempStart, 1.0 + tempPreinfuse);
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
        // possible decrement) and decline is long enough to split off 3s.
        // NOTE: holdTime is the post-decrement value (decremented above when > 3s),
        // matching de1app's pressure_to_advanced_list() which also uses the mutated value.
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
        if (recipe.limiterValue > 0) {
            decline.maxFlowOrPressure = recipe.limiterValue;
            decline.maxFlowOrPressureRange = recipe.limiterRange;
        }
        frames.append(decline);
    }

    // Fallback: add empty frame if no frames were created
    if (frames.isEmpty()) {
        qWarning() << "generatePressureFrames: all time parameters are zero, adding empty fallback frame";
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
        bool needTempBoost = !qFuzzyCompare(1.0 + tempStart, 1.0 + tempPreinfuse);
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
        qWarning() << "generateFlowFrames: all time parameters are zero, adding empty fallback frame";
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
