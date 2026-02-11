#pragma once

#include "profileframe.h"
#include "recipeparams.h"
#include <QList>

class Profile;

/**
 * RecipeGenerator converts high-level RecipeParams into DE1 frames.
 *
 * Supports two editor types:
 *
 * D-Flow (Damian Brakel):
 *   Frame 0: Fill      - Flow mode fill to saturate puck
 *   Frame 1: Bloom     - Optional pause for CO2 release
 *   Frame 2: Infuse    - Hold at low pressure (preinfusion/soak)
 *   Frame 3: Ramp      - Smooth transition to pour setpoint
 *   Frame 4: Pour      - Flow-driven extraction with pressure limit
 *   Frame 5: Decline   - Optional flow decline
 *
 * A-Flow (Janek, forked from D-Flow):
 *   Frame 0: Fill              - Same as D-Flow
 *   Frame 1: Infuse            - Same as D-Flow
 *   Frame 2: Pressure Up       - Smooth ramp to pour pressure
 *   Frame 3: Pressure Decline  - Decline to 1 bar, exit on flow_under
 *   Frame 4: Flow Start        - Transition to flow control
 *   Frame 5: Flow Extraction   - Flow-driven extraction with pressure limiter
 */
class RecipeGenerator {
public:
    static QList<ProfileFrame> generateFrames(const RecipeParams& recipe);

    static Profile createProfile(const RecipeParams& recipe,
                                  const QString& title = "Recipe Profile");

private:
    // D-Flow frame generators
    static ProfileFrame createFillFrame(const RecipeParams& recipe);
    static ProfileFrame createBloomFrame(const RecipeParams& recipe);
    static ProfileFrame createInfuseFrame(const RecipeParams& recipe);
    static ProfileFrame createRampFrame(const RecipeParams& recipe);
    static ProfileFrame createPourFrame(const RecipeParams& recipe);
    static ProfileFrame createDeclineFrame(const RecipeParams& recipe);

    // A-Flow frame generation
    static QList<ProfileFrame> generateAFlowFrames(const RecipeParams& recipe);

    // Simple pressure/flow profile generators
    static QList<ProfileFrame> generatePressureFrames(const RecipeParams& recipe);
    static QList<ProfileFrame> generateFlowFrames(const RecipeParams& recipe);
};
