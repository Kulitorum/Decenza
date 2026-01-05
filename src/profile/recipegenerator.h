#pragma once

#include "profileframe.h"
#include "recipeparams.h"
#include <QList>

class Profile;

/**
 * RecipeGenerator converts high-level RecipeParams into DE1 frames.
 *
 * This is the core of the Recipe Editor functionality, providing
 * a D-Flow-style simplified interface. Users edit intuitive parameters
 * like "infuse pressure" and "pour flow", and this class generates
 * the underlying machine frames.
 *
 * Generated frame structure:
 *   Frame 0: Fill      - Flow mode fill to saturate puck
 *   Frame 1: Bloom     - Optional pause for CO2 release
 *   Frame 2: Infuse    - Hold at low pressure (preinfusion/soak)
 *   Frame 3: Ramp      - Smooth transition to pour setpoint
 *   Frame 4: Pour      - Main extraction phase
 *   Frame 5: Decline   - Optional pressure/flow ramp-down
 *
 * Based on the D-Flow plugin by Damian Brakel.
 */
class RecipeGenerator {
public:
    /**
     * Generate frames from recipe parameters.
     * @param recipe The recipe parameters to convert
     * @return List of ProfileFrame objects ready for upload
     */
    static QList<ProfileFrame> generateFrames(const RecipeParams& recipe);

    /**
     * Create a complete Profile from recipe parameters.
     * @param recipe The recipe parameters
     * @param title Profile title (default: "Recipe Profile")
     * @return Complete Profile object with frames and metadata
     */
    static Profile createProfile(const RecipeParams& recipe,
                                  const QString& title = "Recipe Profile");

private:
    // Individual frame generators
    static ProfileFrame createFillFrame(const RecipeParams& recipe);
    static ProfileFrame createBloomFrame(const RecipeParams& recipe);
    static ProfileFrame createInfuseFrame(const RecipeParams& recipe);
    static ProfileFrame createRampFrame(const RecipeParams& recipe);
    static ProfileFrame createPourFrame(const RecipeParams& recipe);
    static ProfileFrame createDeclineFrame(const RecipeParams& recipe);
};
