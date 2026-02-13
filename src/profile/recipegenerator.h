#pragma once

#include "profileframe.h"
#include "recipeparams.h"
#include <QList>

class Profile;

/**
 * RecipeGenerator converts high-level RecipeParams into DE1 frames.
 *
 * Supports four editor types:
 *
 * D-Flow (Damian Brakel):
 *   Fill -> [Bloom] -> [Infuse] -> [Ramp] -> Pour -> [Decline]
 *   Flow-driven extraction with pressure limit.
 *
 * A-Flow (Janek, forked from D-Flow):
 *   Fill -> [Infuse] -> Pressure Up -> Pressure Decline -> Flow Start -> Flow Extraction
 *   Hybrid pressure-then-flow extraction.
 *
 * Pressure (settings_2a):
 *   Preinfusion -> [Forced Rise] -> Hold -> Decline
 *   Matches de1app's pressure_to_advanced_list().
 *
 * Flow (settings_2b):
 *   Preinfusion -> Hold -> Decline
 *   Matches de1app's flow_to_advanced_list().
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
