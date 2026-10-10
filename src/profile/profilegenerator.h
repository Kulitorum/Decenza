#pragma once

#include "profileframe.h"
#include "profileparams.h"
#include <QList>

class Profile;

/**
 * ProfileGenerator converts high-level ProfileParams into DE1 frames.
 *
 * Supports four editor types:
 *
 * D-Flow (Damian Brakel):
 *   Filling -> [Infusing] -> Pouring
 *   Pressure preinfusion, flow-driven extraction with pressure limiter.
 *   Always 3 core frames matching de1app's update_D-Flow.
 *
 * A-Flow (Janek, forked from D-Flow):
 *   Pre Fill -> Fill -> [Infuse] -> [2nd Fill] -> [Pause] -> Pressure Up -> Pressure Decline -> Flow Start -> Flow Extraction
 *   Hybrid pressure-then-flow extraction. All 9 frames built inline (not shared with D-Flow).
 *
 * Pressure (settings_2a):
 *   Preinfusion -> [Forced Rise] -> Hold -> Decline
 *   Matches de1app's pressure_to_advanced_list().
 *
 * Flow (settings_2b):
 *   Preinfusion -> Hold -> Decline
 *   Matches de1app's flow_to_advanced_list().
 */
class ProfileGenerator {
public:
    static QList<ProfileFrame> generateFrames(const ProfileParams& params);

    static Profile createProfile(const ProfileParams& params,
                                  const QString& title = "New Profile");

private:
    // D-Flow frame generators
    static ProfileFrame createFillFrame(const ProfileParams& params);
    static ProfileFrame createInfuseFrame(const ProfileParams& params);
    static ProfileFrame createPourFrame(const ProfileParams& params);

    // A-Flow frame generation
    static QList<ProfileFrame> generateAFlowFrames(const ProfileParams& params);

    // Simple pressure/flow profile generators
    static QList<ProfileFrame> generatePressureFrames(const ProfileParams& params);
    static QList<ProfileFrame> generateFlowFrames(const ProfileParams& params);
};
