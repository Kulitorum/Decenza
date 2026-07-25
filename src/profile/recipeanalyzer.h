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
 * - Complex profiles: More than 9 frames or non-matching patterns → not convertible
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
     *
     * For a D-Flow or A-Flow profile this dispatches to the matching transcription
     * of the plugin's own `prep` below. Everything else falls back to the pattern
     * detection further down, which exists for arbitrary profiles being converted
     * into recipe mode and has no plugin to be faithful to.
     *
     * @param profile The profile to analyze
     * @return RecipeParams extracted from the frames (defaults if not convertible)
     */
    static RecipeParams extractRecipeParams(const Profile& profile);

    /**
     * D-Flow's `proc prep` (plugin.tcl:195-210), transcribed.
     *
     * Frame roles are FIXED INDICES 0/1/2 — the plugin does not pattern-match and
     * neither does this. Returns the profile's params untouched if the profile is
     * too short to have those frames.
     */
    static RecipeParams prepDFlow(const Profile& profile);

    /**
     * A-Flow's `proc prep` (code.tcl:194-240), transcribed, with frame roles from
     * `proc set_profile_index` (code.tcl:171-190).
     *
     * Roles are POSITIONAL and layout-dependent: a 9-frame profile numbers them
     * Pre Fill / Filling / Soaking / 2nd Fill / Pause / Ramp Up / Ramp Down /
     * Pouring Start / Pouring, a legacy 6-frame one drops Pre Fill, 2nd Fill and
     * Pause. The three structural toggles are DERIVED from the frames — nothing
     * stores them, which is the property that makes the frames sufficient.
     */
    static RecipeParams prepAFlow(const Profile& profile);

    /**
     * Convert a profile to recipe mode if possible.
     * Populates recipeParams if successful. editorType() is derived at runtime.
     * @param profile The profile to convert (modified in place)
     * @return true if conversion was successful
     */
    static bool convertToRecipeMode(Profile& profile);

    /**
     * Force convert any profile to recipe mode.
     * Even complex profiles will be simplified to fit the D-Flow pattern.
     * This may result in loss of advanced settings.
     * @param profile The profile to convert (modified in place)
     */
    static void forceConvertToRecipe(Profile& profile);

private:
    // Frame pattern detection
    static bool isFillFrame(const ProfileFrame& frame);
    static bool isInfuseFrame(const ProfileFrame& frame);
    static bool isRampFrame(const ProfileFrame& frame);
    static bool isPourFrame(const ProfileFrame& frame);

    // Parameter extraction from frames
    static double extractInfusePressure(const ProfileFrame& frame);
    static double extractInfuseTime(const ProfileFrame& frame);
    static double extractPourPressure(const ProfileFrame& frame);
    static double extractPourFlow(const ProfileFrame& frame);
    static double extractFlowLimit(const ProfileFrame& frame);
    static double extractPressureLimit(const ProfileFrame& frame);
};
