#include "recipegenerator.h"
#include "profile.h"

QList<ProfileFrame> RecipeGenerator::generateFrames(const RecipeParams& recipe) {
    // Branch on editor type
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
    profile.setProfileType("settings_2c");

    // Targets
    profile.setTargetWeight(recipe.targetWeight);
    profile.setTargetVolume(100.0);  // Volume as backup
    profile.setEspressoTemperature(recipe.pourTemperature);

    // Mode
    profile.setMode(Profile::Mode::FrameBased);

    // Generate and set frames
    profile.setSteps(generateFrames(recipe));

    // Calculate preinfuse frame count
    int preinfuseCount = 1;  // Fill is always preinfuse
    if (recipe.bloomEnabled && recipe.bloomTime > 0) {
        preinfuseCount++;
    }
    if (recipe.infuseTime > 0 || recipe.infuseByWeight) {
        preinfuseCount++;
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

QList<ProfileFrame> RecipeGenerator::generateAFlowFrames(const RecipeParams& recipe) {
    QList<ProfileFrame> frames;

    // Frame 0: Fill (same as D-Flow)
    frames.append(createFillFrame(recipe));

    // Frame 1: Infuse (same as D-Flow, if enabled)
    if (recipe.infuseEnabled) {
        frames.append(createInfuseFrame(recipe));
    }

    // Frame 2-3: 2nd Fill + Pause (optional, for low-pressure infusion recovery)
    if (recipe.secondFillEnabled) {
        // 2nd Fill: high flow, low pressure, exits when pressure builds
        ProfileFrame secondFill;
        secondFill.name = "2nd Fill";
        secondFill.pump = "flow";
        secondFill.flow = 8.0;
        secondFill.pressure = 0.0;
        secondFill.temperature = recipe.fillTemperature;
        secondFill.seconds = 25.0;
        secondFill.transition = "fast";
        secondFill.sensor = "coffee";
        secondFill.volume = 100.0;
        secondFill.exitIf = true;
        secondFill.exitType = "pressure_over";
        secondFill.exitPressureOver = recipe.fillExitPressure;
        secondFill.exitPressureUnder = 0.0;
        secondFill.exitFlowOver = 6.0;
        secondFill.exitFlowUnder = 0.0;
        secondFill.maxFlowOrPressure = recipe.fillPressure;
        secondFill.maxFlowOrPressureRange = 0.6;
        frames.append(secondFill);

        // Pause: pressure mode, low pressure, exits when flow drops
        ProfileFrame pause;
        pause.name = "Pause";
        pause.pump = "pressure";
        pause.pressure = 1.0;
        pause.flow = 8.0;
        pause.temperature = recipe.fillTemperature;
        pause.seconds = 10.0;
        pause.transition = "fast";
        pause.sensor = "coffee";
        pause.volume = 100.0;
        pause.exitIf = true;
        pause.exitType = "flow_under";
        pause.exitPressureOver = 11.0;
        pause.exitPressureUnder = 0.0;
        pause.exitFlowOver = 6.0;
        pause.exitFlowUnder = 0.5;
        pause.maxFlowOrPressure = 1.0;
        pause.maxFlowOrPressureRange = 0.6;
        frames.append(pause);
    }

    // Frame 4: Ramp Up - pressure mode, smooth transition to pour pressure
    double rampUpTime = recipe.rampTime;
    if (recipe.rampDownEnabled) {
        rampUpTime = recipe.rampTime / 2.0;  // Split ramp time between up and down
    }

    if (recipe.rampEnabled && rampUpTime > 0) {
        ProfileFrame rampUp;
        rampUp.name = "Ramp Up";
        rampUp.pump = "pressure";
        rampUp.pressure = recipe.pourPressure;
        rampUp.flow = 8.0;
        rampUp.temperature = recipe.pourTemperature;
        rampUp.seconds = rampUpTime;
        rampUp.transition = "smooth";
        rampUp.sensor = "coffee";
        rampUp.volume = 100.0;
        rampUp.exitIf = false;
        rampUp.exitType = "";
        rampUp.exitPressureOver = 0.0;
        rampUp.exitPressureUnder = 0.0;
        rampUp.exitFlowOver = 0.0;
        rampUp.exitFlowUnder = 0.0;
        rampUp.maxFlowOrPressure = 0.0;
        frames.append(rampUp);
    }

    // Frame 5: Ramp Down (optional) - pressure decline from pour to lower target
    if (recipe.rampDownEnabled) {
        double rampDownTime = recipe.rampTime / 2.0;
        ProfileFrame rampDown;
        rampDown.name = "Ramp Down";
        rampDown.pump = "pressure";
        rampDown.pressure = recipe.rampDownPressure;
        rampDown.flow = 8.0;
        rampDown.temperature = recipe.pourTemperature;
        rampDown.seconds = rampDownTime;
        rampDown.transition = "smooth";
        rampDown.sensor = "coffee";
        rampDown.volume = 100.0;
        // Exit when flow drops to pour flow level (puck is ready for flow control)
        rampDown.exitIf = true;
        rampDown.exitType = "flow_under";
        rampDown.exitPressureOver = 11.0;
        rampDown.exitPressureUnder = 0.0;
        rampDown.exitFlowOver = 6.0;
        rampDown.exitFlowUnder = recipe.pourFlow + 0.1;
        rampDown.maxFlowOrPressure = 0.0;
        frames.append(rampDown);
    }

    // Frame 6: Pour Start - flow mode, quick transition
    {
        ProfileFrame pourStart;
        pourStart.name = "Pour Start";
        pourStart.pump = "flow";
        pourStart.flow = recipe.pourFlow;
        pourStart.pressure = recipe.pourPressure;
        pourStart.temperature = recipe.pourTemperature;
        // Quick transition if ramp was used, slower (10s) if not
        pourStart.seconds = (recipe.rampEnabled && recipe.rampTime > 0) ? 2.0 : 10.0;
        pourStart.transition = "smooth";
        pourStart.sensor = "coffee";
        pourStart.volume = 100.0;
        pourStart.exitIf = false;
        pourStart.exitType = "";
        pourStart.exitPressureOver = 0.0;
        pourStart.exitPressureUnder = 0.0;
        pourStart.exitFlowOver = 0.0;
        pourStart.exitFlowUnder = 0.0;
        pourStart.maxFlowOrPressure = recipe.pourPressure;
        pourStart.maxFlowOrPressureRange = 0.6;
        frames.append(pourStart);
    }

    // Frame 7: Pour - main extraction, flow mode
    {
        ProfileFrame pour;
        pour.name = "Pour";
        pour.pump = "flow";
        // Flow Up: gradually increase flow (target = 2x pour flow over 127s)
        pour.flow = recipe.flowUpEnabled ? (recipe.pourFlow * 2.0) : recipe.pourFlow;
        pour.pressure = recipe.pourPressure;
        pour.temperature = recipe.pourTemperature;
        pour.seconds = 127.0;  // Long duration - weight system stops the shot
        // Slow transition for flow-up effect, fast if no flow-up
        pour.transition = recipe.flowUpEnabled ? "smooth" : "fast";
        pour.sensor = "coffee";
        pour.volume = 100.0;
        pour.exitIf = false;
        pour.exitType = "";
        pour.exitPressureOver = 0.0;
        pour.exitPressureUnder = 0.0;
        pour.exitFlowOver = 0.0;
        pour.exitFlowUnder = 0.0;
        pour.maxFlowOrPressure = recipe.pourPressure;
        pour.maxFlowOrPressureRange = 0.6;
        frames.append(pour);
    }

    return frames;
}
