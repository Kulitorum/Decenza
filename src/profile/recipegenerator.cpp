#include "recipegenerator.h"
#include "profile.h"

QList<ProfileFrame> RecipeGenerator::generateFrames(const RecipeParams& recipe) {
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

    // Frame 5: Decline - optional pressure ramp-down
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

    if (recipe.pourStyle == "flow") {
        // Flow mode ramp
        frame.pump = "flow";
        frame.flow = recipe.pourFlow;
        frame.pressure = recipe.pressureLimit;

        // Pressure limiter in flow mode
        if (recipe.pressureLimit > 0) {
            frame.maxFlowOrPressure = recipe.pressureLimit;
            frame.maxFlowOrPressureRange = 0.6;
        } else {
            frame.maxFlowOrPressure = 0.0;
        }
    } else {
        // Pressure mode ramp
        frame.pump = "pressure";
        frame.pressure = recipe.pourPressure;
        frame.flow = 8.0;

        // Flow limiter in pressure mode
        if (recipe.flowLimit > 0) {
            frame.maxFlowOrPressure = recipe.flowLimit;
            frame.maxFlowOrPressureRange = 0.6;
        } else {
            frame.maxFlowOrPressure = 0.0;
        }
    }

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

    if (recipe.pourStyle == "flow") {
        // Flow mode extraction
        frame.pump = "flow";
        frame.flow = recipe.pourFlow;
        frame.pressure = recipe.pressureLimit;

        // Pressure limiter in flow mode
        if (recipe.pressureLimit > 0) {
            frame.maxFlowOrPressure = recipe.pressureLimit;
            frame.maxFlowOrPressureRange = 0.6;
        } else {
            frame.maxFlowOrPressure = 0.0;
        }
    } else {
        // Pressure mode extraction
        frame.pump = "pressure";
        frame.pressure = recipe.pourPressure;
        frame.flow = 8.0;

        // Flow limiter in pressure mode
        if (recipe.flowLimit > 0) {
            frame.maxFlowOrPressure = recipe.flowLimit;
            frame.maxFlowOrPressureRange = 0.6;
        } else {
            frame.maxFlowOrPressure = 0.0;
        }
    }

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

    if (recipe.pourStyle == "flow") {
        // Flow mode decline - reduce flow
        frame.pump = "flow";
        frame.flow = recipe.pourFlow * 0.5;  // Reduce to 50%
        frame.pressure = recipe.pressureLimit;

        if (recipe.pressureLimit > 0) {
            frame.maxFlowOrPressure = recipe.pressureLimit;
            frame.maxFlowOrPressureRange = 0.6;
        }
    } else {
        // Pressure mode decline
        frame.pump = "pressure";
        frame.pressure = recipe.declineTo;
        frame.flow = 8.0;

        // Maintain flow limiter from pour phase
        if (recipe.flowLimit > 0) {
            frame.maxFlowOrPressure = recipe.flowLimit;
            frame.maxFlowOrPressureRange = 0.6;
        } else {
            frame.maxFlowOrPressure = 0.0;
        }
    }

    // No exit condition - time/weight handles termination
    frame.exitIf = false;
    frame.exitType = "";
    frame.exitPressureOver = 0.0;
    frame.exitPressureUnder = 0.0;
    frame.exitFlowOver = 0.0;
    frame.exitFlowUnder = 0.0;

    return frame;
}
