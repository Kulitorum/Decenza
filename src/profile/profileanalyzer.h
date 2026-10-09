#pragma once

#include "profile.h"
#include "profileparams.h"
#include <QList>

/**
 * ProfileAnalyzer attempts to extract ProfileParams from existing frame-based profiles.
 *
 * This enables the D-Flow/A-Flow editor to work with imported D-Flow-style profiles
 * that were created with the D-Flow plugin but only have the generated frames.
 *
 * Detection patterns:
 * - Simple D-Flow: Fill → Infuse → Pour (3 frames)
 * - Complex profiles: More than 9 frames or non-matching patterns → not convertible
 */
class ProfileAnalyzer {
public:
    /**
     * Analyze a profile and determine if it can be represented by a parameter editor.
     * @param profile The profile to analyze
     * @return true if the profile matches a pattern a parameter editor can represent
     */
    static bool canConvertToParams(const Profile& profile);

    /**
     * Extract ProfileParams from a frame-based profile.
     *
     * For a D-Flow or A-Flow profile this dispatches to the matching transcription
     * of the plugin's own `prep` below. Everything else falls back to the pattern
     * detection further down, which exists for arbitrary profiles being converted
     * into params mode and has no plugin to be faithful to.
     *
     * @param profile The profile to analyze
     * @return ProfileParams extracted from the frames (defaults if not convertible)
     */
    static ProfileParams extractProfileParams(const Profile& profile, bool* derived = nullptr);

    /**
     * Do this profile's frames fit the layout its editor indexes?
     *
     * D-Flow needs 3; A-Flow needs 6 (legacy) or 9. Both plugins index frame
     * roles POSITIONALLY with no validation, so a profile that does not fit
     * cannot have its parameters read — `prep` would be reading whatever
     * happens to sit at those indices.
     *
     * Callers use this to refuse to REGENERATE such a profile. Rebuilding its
     * frames from parameters that were never derived from it replaces real
     * frames with fabricated ones, which is finding REC-1 in a different guise.
     */
    static bool framesFitEditorLayout(const Profile& profile);

    /**
     * D-Flow's `proc prep` (plugin.tcl:195-210), transcribed.
     *
     * Frame roles are FIXED INDICES 0/1/2 — the plugin does not pattern-match and
     * neither does this. Returns the profile's params untouched if the profile is
     * too short to have those frames.
     */
    static ProfileParams prepDFlow(const Profile& profile, bool* derived = nullptr);

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
    static ProfileParams prepAFlow(const Profile& profile, bool* derived = nullptr);

    /**
     * Convert a profile to params mode if possible.
     * Populates profileParams if successful. editorType() is derived at runtime.
     * @param profile The profile to convert (modified in place)
     * @return true if conversion was successful
     */
    static bool convertToParamsMode(Profile& profile);

    /**
     * Force convert any profile to params mode.
     * Even complex profiles will be simplified to fit the D-Flow pattern.
     * This may result in loss of advanced settings.
     * @param profile The profile to convert (modified in place)
     */
    static void forceConvertToParams(Profile& profile);

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
