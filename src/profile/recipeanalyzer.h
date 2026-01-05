#pragma once

#include "profile.h"
#include "recipeparams.h"
#include <QList>

/**
 * RecipeAnalyzer attempts to extract RecipeParams from existing frame-based profiles.
 *
 * This enables the Recipe Editor to work with imported D-Flow-style profiles
 * that were created with the D-Flow plugin but only have the generated frames.
 *
 * Detection patterns:
 * - Simple D-Flow: Fill → Infuse → Pour (3 frames)
 * - D-Flow with decline: Fill → Infuse → Pour → Decline (4 frames)
 * - Complex profiles: More than 4 frames or non-matching patterns → not convertible
 */
class RecipeAnalyzer {
public:
    /**
     * Analyze a profile and determine if it can be represented as a Recipe.
     * @param profile The profile to analyze
     * @return true if the profile matches a Recipe-compatible pattern
     */
    static bool canConvertToRecipe(const Profile& profile);

    /**
     * Extract RecipeParams from a frame-based profile.
     * @param profile The profile to analyze
     * @return RecipeParams extracted from the frames (defaults if not convertible)
     */
    static RecipeParams extractRecipeParams(const Profile& profile);

    /**
     * Convert a profile to recipe mode if possible.
     * Sets isRecipeMode=true and populates recipeParams if successful.
     * @param profile The profile to convert (modified in place)
     * @return true if conversion was successful
     */
    static bool convertToRecipeMode(Profile& profile);

private:
    // Frame pattern detection
    static bool isFillFrame(const ProfileFrame& frame);
    static bool isBloomFrame(const ProfileFrame& frame);
    static bool isInfuseFrame(const ProfileFrame& frame);
    static bool isRampFrame(const ProfileFrame& frame);
    static bool isPourFrame(const ProfileFrame& frame);
    static bool isDeclineFrame(const ProfileFrame& frame, const ProfileFrame* previousFrame);

    // Parameter extraction from frames
    static double extractFillPressure(const ProfileFrame& frame);
    static double extractInfusePressure(const ProfileFrame& frame);
    static double extractInfuseTime(const ProfileFrame& frame);
    static double extractPourPressure(const ProfileFrame& frame);
    static double extractPourFlow(const ProfileFrame& frame);
    static double extractFlowLimit(const ProfileFrame& frame);
    static double extractPressureLimit(const ProfileFrame& frame);
    static double extractDeclinePressure(const ProfileFrame& frame);
    static double extractDeclineTime(const ProfileFrame& frame);
};
