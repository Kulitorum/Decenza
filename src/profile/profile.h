#pragma once

#include <QString>
#include <QList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QJsonValue>
#include <QByteArray>
#include <QDebug>
#include "profileframe.h"
#include "recipeparams.h"

// Convert a JSON value that may be string OR number to double — de1app /
// Visualizer profile JSON encodes numbers as strings ("92.00"), so a raw
// QJsonValue::toDouble() silently yields 0 for them. The one tolerant parse
// shared by every profile-JSON reader (Profile::fromJson, the ProfileManager
// catalog scan).

/**
 * Profile represents a complete espresso profile for the DE1.
 *
 * Supports two execution modes:
 * 1. Frame-Based Mode: Profile is uploaded to machine, which executes it autonomously
 * 2. Direct Setpoint Control: App sends live setpoints frame-by-frame during extraction
 *
 * Profile File Formats:
 * - .json: Unified format (compatible with de1app v2)
 * - .tcl: de1app format (Tcl list syntax, importable)
 */
double profileJsonToDouble(const QJsonValue& val, double defaultVal = 0.0);

// Tolerant boolean reader. QJsonValue::toBool() returns its default for a
// non-bool, so de1app/Decaid's "1"/"0" string flags read as false and are
// destroyed rather than misread. Pass `ok` when comparing two values — see the
// definition for why a silent default is dangerous there.
bool profileJsonToBool(const QJsonValue& val, bool defaultVal = false, bool* ok = nullptr);

// One field on which two profiles disagree, produced by Profile::fieldDeltas().
//
// TWO AUDIENCES, ONE WALK. The developer-facing frameDiffReport() (the TCL
// import parity gate, profile_sync) and the user-facing dial-in block want
// overlapping but different field sets, and the overlap is where the subtle
// rules live: which setpoint axis is active, which of the four exit thresholds
// counts. Those rules had already been three hand-maintained copies once; a
// second walk here would be the fourth. So one traversal emits every row and
// each row declares who it is for.
struct ProfileFieldDelta {
    // Stable identifier. For a row in the developer report this IS the label
    // that report prints, which is what keeps its text byte-identical to the
    // hand-written version it replaced. QML switches on it to build a
    // translated label for the user-facing rows.
    QString kind;

    // Index of the frame this row is about, or -1 for a profile-level row and
    // for a row the dial-in filter collapsed across every frame.
    int frameIndex = -1;

    // The frame's display name in profile `a` — the BASE, for dialInDeltas().
    // Empty when frameIndex < 0. Carried so a surface can say "Pouring" instead
    // of "frame 3".
    QString frameName;

    // Unit token for a numeric row: "celsius", "bar", "mlPerSec", "g", "ml",
    // plus "s" and "count" which are developer-only today. Empty for a string
    // row.
    //
    // Set HERE rather than inferred by the surface, because one row cannot be
    // inferred at all: the frame limiter is a max FLOW on a pressure-driven
    // frame and a max PRESSURE on a flow-driven one, and only this walk knows
    // which. A token, not a suffix — the suffix is user-visible text and the
    // temperature one depends on a user setting.
    QString unit;

    // How far apart two values must be to count as different, derived from the
    // field's SERIALIZATION precision (ProfileJson) rather than picked: half of
    // the last decimal place that survives a write, so a round-trip cannot
    // manufacture a difference and one editor step always shows.
    //
    // This is deliberately TIGHTER than the flat 0.1 frameDiffReport() has
    // always applied. That 0.1 is right for an import-parity check absorbing
    // TCL-vs-JSON noise, and wrong for a user-facing diff: target weight's
    // editor step IS 0.1 g, so a 36.0 -> 36.1 g edit landed exactly on the
    // boundary and was reported as "unchanged". Rows are emitted at this
    // tolerance and frameDiffReport() re-filters at its own 0.1, which is what
    // keeps its output byte-identical while the dial-in block gets the truth.
    double tolerance = 0.1;

    // numeric rows carry oldValue/newValue; the rest carry oldText/newText.
    // Defaults to the string branch: a delta that somehow skipped the emitters
    // then renders as missing rather than as a real value of zero.
    bool numeric = false;
    double oldValue = 0.0;
    double newValue = 0.0;
    QString oldText;
    QString newText;

    // Who this row is for. A row can be in both, in either, or (for the frame
    // display name) in only the user-facing one — adding it to the developer
    // report would make a renamed frame fail the TCL parity gate, which is not
    // a portability defect.
    bool inDeveloperReport = false;
    bool inDialIn = false;
};

class Profile {
public:
    // Execution modes
    enum class Mode {
        FrameBased,     // Upload to machine, execute autonomously
        DirectControl   // App sends live setpoints during shot
    };

    Profile() = default;

    // === Metadata ===
    QString title() const { return m_title; }
    void setTitle(const QString& title) {
        // Strip leading "*" (de1app modified indicator)
        m_title = title.startsWith(QLatin1Char('*')) ? title.mid(1).trimmed() : title;
    }

    QString author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }

    QString profileNotes() const { return m_profileNotes; }
    void setProfileNotes(const QString& notes) { m_profileNotes = notes; }

    QString beverageType() const { return m_beverageType; }
    void setBeverageType(const QString& type) { m_beverageType = type; }

    // The no-coffee maintenance tier: cleaning/descale/calibrate. Normalizes
    // internally (trim + lowercase) so odd-cased community-authored values match.
    // Single source for the QML Shot Plan warning (via ProfileManager), the
    // shot-history exclusion (maincontroller), the Visualizer upload gate, and the
    // MCP upload gate. Add any new maintenance beverage_type HERE — plus the
    // selector category in profilemanager.cpp and the beverage-type list in
    // PostShotReviewPage.qml if it should be user-visible.
    static bool isMaintenanceBeverageType(const QString& beverageType) {
        const QString t = beverageType.trimmed().toLower();
        return t == QLatin1String("cleaning") || t == QLatin1String("descale")
            || t == QLatin1String("calibrate");
    }

    // Profile type for compatibility with de1app settings
    // "settings_2a" = simple pressure, "settings_2b" = simple flow,
    // "settings_2c" = advanced (our default), "settings_2c2" = advanced with limiter
    QString profileType() const { return m_profileType; }
    void setProfileType(const QString& type) { m_profileType = type; }

    // === Target Values ===
    double targetWeight() const { return m_targetWeight; }
    void setTargetWeight(double weight) { m_targetWeight = weight; }

    double targetVolume() const { return m_targetVolume; }
    void setTargetVolume(double volume) { m_targetVolume = volume; }

    // === Temperature Settings ===
    // Primary espresso temperature (often mirrors first frame temp)
    double espressoTemperature() const { return m_espressoTemperature; }
    void setEspressoTemperature(double temp) { m_espressoTemperature = temp; }

    // True when fromJson() re-derived espresso_temperature from the first frame
    // because the stored value was missing, or was the bare 93.0 default sitting
    // outside the frame range (a stale Visualizer import). Transient (not
    // serialized) — lets callers persist the repair once. See Profile::fromJson /
    // ProfileManager::loadProfile.
    bool espressoTemperatureHealed() const { return m_espressoTemperatureHealed; }

    // Temperature presets for quick adjustment (de1app feature)
    QList<double> temperaturePresets() const { return m_temperaturePresets; }
    void setTemperaturePresets(const QList<double>& presets) { m_temperaturePresets = presets; }

    // === Recommended Dose ===
    // The read default, shared by the member initialiser, fromJson's fallback and the
    // recipe-block promotion — which needs it to tell "the user set 18" (indistinguishable,
    // and harmless) from "this is just the default" (must not enable a recommendation).
    static constexpr double kDefaultRecommendedDose = 18.0;

    bool hasRecommendedDose() const { return m_hasRecommendedDose; }
    void setHasRecommendedDose(bool enabled) { m_hasRecommendedDose = enabled; }

    double recommendedDose() const { return m_recommendedDose; }
    void setRecommendedDose(double dose) { m_recommendedDose = dose; }

    // === Flow/Pressure Limits ===
    double maximumPressure() const { return m_maximumPressure; }
    void setMaximumPressure(double pressure) { m_maximumPressure = pressure; }

    double maximumFlow() const { return m_maximumFlow; }
    void setMaximumFlow(double flow) { m_maximumFlow = flow; }

    double minimumPressure() const { return m_minimumPressure; }
    void setMinimumPressure(double pressure) { m_minimumPressure = pressure; }

    // === Simple Profile Parameters (settings_2a/2b) ===
    // Used by Visualizer to reconstruct simple profiles
    double preinfusionTime() const { return m_preinfusionTime; }
    void setPreinfusionTime(double t) { m_preinfusionTime = t; }
    double preinfusionFlowRate() const { return m_preinfusionFlowRate; }
    void setPreinfusionFlowRate(double f) { m_preinfusionFlowRate = f; }
    double preinfusionStopPressure() const { return m_preinfusionStopPressure; }
    void setPreinfusionStopPressure(double p) { m_preinfusionStopPressure = p; }
    double espressoPressure() const { return m_espressoPressure; }
    void setEspressoPressure(double p) { m_espressoPressure = p; }
    double espressoHoldTime() const { return m_espressoHoldTime; }
    void setEspressoHoldTime(double t) { m_espressoHoldTime = t; }
    double espressoDeclineTime() const { return m_espressoDeclineTime; }
    void setEspressoDeclineTime(double t) { m_espressoDeclineTime = t; }
    double pressureEnd() const { return m_pressureEnd; }
    void setPressureEnd(double p) { m_pressureEnd = p; }
    double flowProfileHold() const { return m_flowProfileHold; }
    void setFlowProfileHold(double f) { m_flowProfileHold = f; }
    double flowProfileHoldTime() const { return m_flowProfileHoldTime; }
    void setFlowProfileHoldTime(double t) { m_flowProfileHoldTime = t; }
    double flowProfileDecline() const { return m_flowProfileDecline; }
    void setFlowProfileDecline(double f) { m_flowProfileDecline = f; }
    double flowProfileDeclineTime() const { return m_flowProfileDeclineTime; }
    void setFlowProfileDeclineTime(double t) { m_flowProfileDeclineTime = t; }
    double maximumFlowRangeDefault() const { return m_maximumFlowRangeDefault; }
    void setMaximumFlowRangeDefault(double r) { m_maximumFlowRangeDefault = r; }
    double maximumPressureRangeDefault() const { return m_maximumPressureRangeDefault; }
    void setMaximumPressureRangeDefault(double r) { m_maximumPressureRangeDefault = r; }

    bool tempStepsEnabled() const { return m_tempStepsEnabled; }
    void setTempStepsEnabled(bool e) { m_tempStepsEnabled = e; }

    // === Advanced Limits (de1app settings_2c2) ===
    double tankDesiredWaterTemperature() const { return m_tankDesiredWaterTemperature; }
    void setTankDesiredWaterTemperature(double temp) { m_tankDesiredWaterTemperature = temp; }

    double maximumFlowRangeAdvanced() const { return m_maximumFlowRangeAdvanced; }
    void setMaximumFlowRangeAdvanced(double range) { m_maximumFlowRangeAdvanced = range; }

    double maximumPressureRangeAdvanced() const { return m_maximumPressureRangeAdvanced; }
    void setMaximumPressureRangeAdvanced(double range) { m_maximumPressureRangeAdvanced = range; }

    // === Steps/Frames ===
    const QList<ProfileFrame>& steps() const { return m_steps; }
    void setSteps(const QList<ProfileFrame>& steps) {
        if (steps.size() > MAX_FRAMES) {
            qWarning() << "Profile::setSteps: truncating" << steps.size() << "frames to MAX_FRAMES" << MAX_FRAMES;
            m_steps = steps.mid(0, MAX_FRAMES);
        } else {
            m_steps = steps;
        }
    }
    bool addStep(const ProfileFrame& step) {
        if (m_steps.size() >= MAX_FRAMES) {
            qWarning() << "Profile::addStep: already at MAX_FRAMES" << MAX_FRAMES;
            return false;
        }
        m_steps.append(step);
        return true;
    }
    bool insertStep(int index, const ProfileFrame& step) {
        if (m_steps.size() >= MAX_FRAMES) {
            qWarning() << "Profile::insertStep: already at MAX_FRAMES" << MAX_FRAMES;
            return false;
        }
        if (index < 0 || index > m_steps.size()) {
            qWarning() << "Profile::insertStep: index" << index << "out of range [0," << m_steps.size() << "]";
            return false;
        }
        m_steps.insert(index, step);
        return true;
    }
    bool removeStep(int index) {
        if (index < 0 || index >= m_steps.size()) {
            qWarning() << "Profile::removeStep: index" << index << "out of range [0," << m_steps.size() << ")";
            return false;
        }
        m_steps.removeAt(index);
        return true;
    }
    void moveStep(int from, int to);
    void setStepAt(int index, const ProfileFrame& step) {
        if (index < 0 || index >= m_steps.size()) {
            qWarning() << "Profile::setStepAt: index" << index << "out of range [0," << m_steps.size() << ")";
            return;
        }
        m_steps[index] = step;
    }

    int preinfuseFrameCount() const { return m_preinfuseFrameCount; }
    void setPreinfuseFrameCount(int count) { m_preinfuseFrameCount = count; }

    // Max frames the DE1 accepts (hardware limit)
    static constexpr int MAX_FRAMES = 20;

    // === Execution Mode ===
    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    // === Editor Type ===
    // Derived from title + profileType at runtime — never stored in JSON.
    // Returns: "dflow", "aflow", "pressure", "flow", "advanced"
    QString editorType() const;

    // === Recipe Parameters ===
    RecipeParams recipeParams() const { return m_recipeParams; }
    void setRecipeParams(const RecipeParams& params) {
        m_recipeParams = params;
        m_hasRecipeParams = true;
    }

    // Have this profile's recipe parameters been ESTABLISHED — derived from its
    // frames, read from a stored recipe block, or set by a D-Flow/A-Flow edit —
    // as opposed to being the member initialisers of a default-constructed
    // RecipeParams?
    //
    // NOT set by editing a pressure/flow (settings_2a/2b) profile:
    // uploadRecipeProfile routes those through applyRecipeToScalarFields, which
    // writes the profile's own scalar fields and never touches m_recipeParams.
    // That is harmless — those parameters round-trip through the scalars
    // independently of any recipe block — but it means this flag answers
    // "does a recipe BLOCK belong on this profile", not "has anyone edited it".
    //
    // This is not a nicety. RecipeParams' defaults are live values, not sentinels
    // (targetWeight 36.0, fillTemperature 88.0), so "did anyone set these?" cannot
    // be answered by inspecting them: a fresh struct is indistinguishable from a
    // deliberate 88 °C. Writing a recipe block for a profile that merely has a
    // recipe-shaped TITLE is what fabricated the five identical blocks in the
    // A-Flow built-ins, none of which matches its own frames — and what made
    // editing any one parameter reset the fill temperature to 88 °C
    // (finding REC-1). The plugins have no such notion: both reconstruct their
    // editor state from the frames on every load. A stored block is a cache.
    // True when the profile was loaded from a source that still carried a `recipe`
    // block. See ProfileManager::loadProfile, which persists the stripped form once.
    bool recipeBlockStripped() const { return m_recipeBlockStripped; }
    void clearRecipeBlockStripped() { m_recipeBlockStripped = false; }

    bool hasRecipeParams() const { return m_hasRecipeParams; }

    // Regenerate frames from stored recipe parameters
    void regenerateFromRecipe();

private:
    // Reinstate the in-place-mutation semantics the plugins have and Decenza's
    // build-from-constants generator does not: every frame field the plugin's
    // update_* proc never assigns keeps the value it had. Called by
    // regenerateFromRecipe with the pre-regeneration frames.
    void restoreFieldsThePluginNeverWrites(const QList<ProfileFrame>& oldSteps);

public:

    // Regenerate frames from scalar fields for simple profiles (settings_2a/2b)
    void regenerateSimpleFrames();

    // === Serialization ===
    // toJsonObject() is the single canonical profile serializer: string-encoded
    // values, ecosystem-required keys, standard DE1 v2 metadata, non-empty steps.
    // toJson() wraps it in a document; the Visualizer upload delegates to it too,
    // so there is exactly one format validated across Decenza, Decaid, and Visualizer.
    QJsonObject toJsonObject() const;
    QJsonDocument toJson() const;
    static Profile fromJson(const QJsonDocument& doc);

    // Validate a serialized profile object against Decaid's Profile.fromJson
    // contract (the strictest reader in the DE1 ecosystem): non-empty title and
    // steps, the required tank_temperature / target_volume_count_start keys, and
    // in-vocabulary enum values (pump/sensor/transition, exit type/condition).
    // Returns an empty list when the object is Decaid-readable; otherwise one
    // human-readable message per violation. Reusable from tests and profile_sync.
    static QStringList decaidReadabilityErrors(const QJsonObject& obj);

    // Deep semantic parity check between two serialized profiles: every key and
    // value in `before` must survive into `after`. Encoding differences are
    // normalized (numeric 9.0 == string "9.00"), so this compares MEANING, not
    // bytes — the exact invariant a format-only change must satisfy.
    //
    // Additions are allowed (a canonical serializer may add keys); LOSSES are not.
    // A dropped object/array, or a dropped or changed non-zero scalar, is an error;
    // a dropped zero/empty scalar is inert (absent and 0 mean the same to every
    // reader in this format). Returns one message per violation, empty when parity
    // holds.
    //
    // Because additions are allowed, this CANNOT detect content the reader derived
    // rather than read — a key absent from `before` is never visited. A caller that
    // needs "nothing changed", not merely "nothing was lost", has to exclude the
    // deriving cases itself; ProfileManager::upgradeStoredEncoding does that with
    // Profile::espressoTemperatureHealed().
    //
    // Callers: profile_sync's rewrite audit, the built-in parity tests,
    // ProfileManager::upgradeStoredEncoding and ProfileManager::migrateProfileFormat
    // — the last two run against real user profiles, so a format change that
    // silently drops data fails all four.
    static QStringList jsonParityErrors(const QJsonObject& before, const QJsonObject& after);

    // === File I/O ===
    static Profile loadFromFile(const QString& filePath);
    static Profile loadFromJsonString(const QString& jsonContent);
    bool saveToFile(const QString& filePath) const;
    QString toJsonString() const;

    // Import from de1app .tcl file
    static Profile loadFromTclFile(const QString& filePath);

    // Import from de1app TCL format string (used by Visualizer API)
    static Profile loadFromTclString(const QString& tclContent);

    // === BLE Data Generation ===
    // Generate profile header for upload (5 bytes)
    QByteArray toHeaderBytes() const;

    // Generate all frame data for upload (8 bytes each + extension frames + tail)
    QList<QByteArray> toFrameBytes() const;

    // Generate a single frame for direct control mode
    QByteArray toDirectControlFrame(int frameIndex, const ProfileFrame& frame) const;

    // === Read-Only Flag ===
    // Matches de1app's read_only field (integer):
    //   0 = not read-only (user-created or edited copy)
    //   1 = read-only (built-in / author-protected)
    //   2 = reset to original requested
    // Built-in profiles are always treated as read-only regardless of this flag
    // (enforced by ProfileManager).
    int readOnly() const { return m_readOnly; }
    void setReadOnly(int readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly == 1; }


    // === AI Description ===
    // Generate a compact text description of the frame sequence for AI analysis
    QString describeFrames() const;
    // Convenience: deserialize JSON then call describeFrames()
    static QString describeFramesFromJson(const QString& json);

    // === Validation ===
    bool isValid() const;
    QStringList validationErrors() const;

    // Step keys encountered at load that this build does not understand. When
    // non-empty the profile is deliberately invalid: we cannot promise it brews
    // the same shot, so it must not be imported. Exposed so callers can name the
    // offending key in a message the user can turn into a bug report.
    QStringList unsupportedStepKeys() const { return m_unsupportedStepKeys; }

    // Values encountered at load that this build cannot interpret, as
    // "key=raw". The value-level twin of unsupportedStepKeys(), and invalidating
    // for the same reason: a number we cannot read becomes 0.0, and 0 is legal
    // for every field it can happen to, so substituting it silently pours
    // something the file did not describe. `maximum_pressure 9,5` would read as
    // "no pressure limit"; `final_desired_shot_weight n/a` would switch
    // stop-at-weight on at the 36 g default.
    QStringList malformedValues() const { return m_malformedValues; }

    // Count consecutive leading frames with exit conditions (preinfusion frames)
    static int countPreinfuseFrames(const QList<ProfileFrame>& steps);

    // countPreinfuseFrames() plus every forced-rise frame anywhere in steps (either
    // spelling — see ProfileFrame::isForcedRiseName). A Pressure-type profile's
    // forced-rise frame(s) drive pressure up before any coffee pours — filling headspace,
    // not pouring — so the DE1 must treat them as preinfusion too, or Stop-at-Volume
    // starts counting before the pour begins (matching de1app commit 13a30463). The
    // frame(s) keep exitIf==false: that flag also sets the DE1's real DoCompare exit
    // condition, and setting it would end the ramp early.
    // A no-op add for Flow-type steps, which never generate a frame with this name.
    static int countPreinfuseFramesWithForcedRise(const QList<ProfileFrame>& steps);

    // === Flow limits ===
    // The flow limit (mL/s) every pressure step gets when it carries none. Decent's
    // second machine reaches ~20 mL/s where the DE1 manages 7-8, so an unlimited
    // pressure step pours differently on the two; capping at 8 makes a shared profile
    // behave the same on both. de1app applies the identical cap in select_profile and
    // at startup (skialpine/de1app@fdd091f3), so this number is a compatibility
    // constant, not a tuning knob — changing it here alone reopens the divergence.
    static constexpr double kDefaultPressureFlowLimit = 8.0;

    // Ceiling for an editable flow goal or flow limit. Above the DE1's own maximum on
    // purpose: it is what lets a profile authored on a higher-flow machine open and save
    // here without being clamped to a value its author never chose. The DE1 runs at its
    // own maximum when asked for more. Note the BLE frame encoding (U8P4) saturates at
    // 15.9375 mL/s — a wire property shared with de1app, not a Decenza limit.
    static constexpr double kMaxSettableFlow = 20.0;

    // Give every unlimited pressure step the default flow limit, in memory. Returns
    // true if anything changed.
    //
    // Call it where a profile becomes the CURRENT profile, never inside a parse: a
    // stored profile — a shot's record of what it was pulled with, an import being
    // compared for de-duplication — must keep reading back exactly what it says.
    bool applyDefaultPressureFlowLimit();

    // Compare two profiles for functional equality (frame sequence only).
    // Profile-level limits (maximumPressure, maximumFlow, etc.) are excluded.
    // Returns false if either profile has no steps.
    static bool functionallyEqual(const Profile& a, const Profile& b);

    // === Shape ===
    // A profile's SHAPE is its structure and frame durations, with every
    // magnitude a user changes while dialling in a coffee excluded:
    // temperature, pressure/flow setpoints, volume, exit weight, exit
    // THRESHOLDS, the limiter, and frame names all differ freely. What
    // remains is frame count, preinfuse frame count, beverage type, and per
    // frame the pump mode, sensor, transition, and exit type/direction plus
    // its duration.
    //
    // The split is drawn there because the KB's per-profile suppression flags
    // (flow_trend_ok, channeling_expected) are claims about the SHAPE — "this
    // structure legitimately produces declining flow" — and remain true of a
    // profile someone re-tuned. See the profile-shape-equivalence capability.
    //
    // NOT a relative of functionallyEqual(): that one answers "is this the
    // same profile" for import de-duplication and is deliberately stricter.
    // It is also inherently PAIRWISE — its inactive-axis rule reads both
    // frames at once (skip when EITHER side is zero, because de1app writes a
    // default our writer omits) — so it cannot be expressed as a per-frame
    // signature and the two do not share a traversal. Nor do they share the
    // 0.1 convention: functionallyEqual compares durations with a ±0.1 s
    // TOLERANCE, this signature ROUNDS them to 0.1 s. Neither predicate
    // implies the other. functionallyEqual does not imply same shape — it
    // never compares beverage type, which the signature includes, and two
    // frames 0.02 s apart can straddle a rounding bucket. What does hold:
    // literal equality of every frame field the signature reads implies the
    // same signature, by construction.
    //
    // Duration caveat, stated because it is a real edge: rounding and a
    // tolerance differ at bucket boundaries — 2.04 s and 2.06 s are 0.02 s
    // apart but round to 2.0 and 2.1. A tolerance is not transitive and so
    // cannot produce a stable grouping key; rounding can, which is why this
    // rounds. Profile durations are authored as whole or half seconds, so the
    // boundary is not reachable from the shipped set, but a generated profile
    // could sit on one.
    //
    // EMPTY for a profile with no frames — no frames, no shape. Callers must
    // treat an empty signature as "matches nothing", never as a value two
    // frameless profiles have in common; ProfileShapeIndex does this at both
    // its entry points. There is deliberately no sameShape(a, b) helper: it
    // existed, had no production caller, and its tests only re-asserted that
    // string comparison is reflexive and symmetric.
    QString shapeSignature() const;

    // Human-readable account of WHY functionallyEqual() said no: one line per
    // differing field, labelled with the frame index. Empty exactly when
    // functionallyEqual() is true.
    //
    // It lives next to functionallyEqual() because it has to apply the same
    // rules — the same skipped inactive axis, the same active-exit-only
    // threshold check. It previously existed as two hand-maintained copies, in
    // profile_sync and in tst_tclimport, and a rule added to one of them did not
    // reach the other.
    static QString frameDiffReport(const Profile& a, const Profile& b);

    // Every field on which `a` and `b` disagree, both audiences, in a fixed
    // order: profile-level rows first, then each frame's rows in frame order.
    // Doubles compare PER UNIT, at half the last decimal ProfileJson writes for
    // that unit — 0.005 for the two-decimal fields, 0.05 for the one-decimal
    // ones (g, ml, tank temperature), 0.5 for integral counts. Each row carries
    // the tolerance it was judged at, so a consumer comparing two delta lists
    // does not have to re-derive it. This is TIGHTER than frameDiffReport(),
    // which re-filters the same rows at a flat 0.1 of its own — it is a parity
    // gate for TCL import, not a dial-in view, and loosening it here would
    // silently weaken that gate.
    //
    // Frames are walked to min(a.steps(), b.steps()); a differing step count is
    // reported as its own row rather than by inventing rows for frames one side
    // does not have.
    static QVector<ProfileFieldDelta> fieldDeltas(const Profile& a, const Profile& b);

    // The user-facing subset: `base` is the bundled profile a knowledge entry
    // was authored against, `user` the profile being looked at, so oldValue is
    // what the documentation describes and newValue is what will actually brew.
    //
    // Filters fieldDeltas() to the dial-in rows and then COLLAPSES a row that
    // changed identically on every frame into a single frameIndex == -1 row —
    // raising a three-frame profile's temperature is one edit, and listing it
    // three times is both longer and less true.
    static QVector<ProfileFieldDelta> dialInDeltas(const Profile& base, const Profile& user);

    // Canonical profile-title → base filename mapping (no extension). Accents
    // are decomposed and stripped, every other non-alphanumeric becomes '_',
    // runs collapse, leading/trailing '_' are trimmed.
    //
    // ONE implementation on purpose: this used to be three (ProfileManager, the
    // profile_sync tool, tst_tclimport) that had already drifted apart on both
    // accent handling and a length cap. A tool that computes a different
    // filename than the app looks in the wrong place and reports a built-in as
    // missing.
    static QString titleToFilename(const QString& title);

private:
    // Frames for serialization, materializing simple (settings_2a/2b) profiles
    // that carry their frames implicitly. const-safe (no mutation of m_steps).
    QVector<ProfileFrame> materializedSteps() const;

    // Log a portability failure without blocking the write. Called by BOTH write
    // paths — saveToFile() and toJsonString() — because the latter is every write
    // on Android and previously produced no diagnostic. `target` may be empty when
    // the caller has no file path to name. See the definition for why this warns
    // rather than refuses.
    static void warnIfNotPortable(const QJsonObject& canonical, const QString& title,
                                  const QString& context, const QString& target);

    // Metadata
    QString m_title = "Default";
    QString m_author;
    QString m_profileNotes;
    QString m_beverageType = "espresso";
    QString m_profileType = "settings_2c";  // Advanced by default

    // Targets
    double m_targetWeight = 36.0;
    double m_targetVolume = 0.0;

    // Temperature
    double m_espressoTemperature = 93.0;
    bool m_espressoTemperatureHealed = false;  // transient; set by fromJson() when re-derived from frames, not serialized
    QList<double> m_temperaturePresets = {88.0, 90.0, 93.0, 96.0};

    // Recommended dose
    bool m_hasRecommendedDose = false;
    double m_recommendedDose = kDefaultRecommendedDose;

    // Transient; set by fromJson() when the source carried a `recipe` block, so
    // ProfileManager::loadProfile can write the stripped profile back once. Not
    // serialized — the block is gone from the output either way, and this only
    // says whether the copy ON DISK still has one.
    bool m_recipeBlockStripped = false;

    // Limits
    double m_maximumPressure = 12.0;
    double m_maximumFlow = 6.0;
    double m_minimumPressure = 0.0;

    // Advanced limits (de1app settings_2c2)
    double m_tankDesiredWaterTemperature = 0.0;
    double m_maximumFlowRangeAdvanced = 0.6;
    double m_maximumPressureRangeAdvanced = 0.6;

    // Simple profile parameters (settings_2a/2b scalar params for frame generation)
    // Stored in JSON so frames can be regenerated when loading profiles with empty steps
    double m_preinfusionTime = 5.0;
    double m_preinfusionFlowRate = 4.0;
    double m_preinfusionStopPressure = 4.0;
    // "used BY the settings_2a editor" — NOT "only present on settings_2a
    // profiles". de1app writes this whole block on every profile of every type,
    // and reading it only on the matching type is what lost the flow-editor
    // values on 2a/2c profiles and the pressure-editor ones on 2b. The
    // annotations below used to read "settings_2a only", which is how that
    // mistake reads as correct.
    double m_espressoPressure = 9.2;        // settings_2a editor
    double m_espressoHoldTime = 10.0;
    double m_espressoDeclineTime = 25.0;
    double m_pressureEnd = 4.0;             // settings_2a editor
    double m_flowProfileHold = 2.0;         // settings_2b editor
    double m_flowProfileHoldTime = 8.0;     // settings_2b editor
    double m_flowProfileDecline = 1.2;      // settings_2b editor
    double m_flowProfileDeclineTime = 17.0; // settings_2b editor
    double m_maximumFlowRangeDefault = 1.0; // settings_2a limiter range
    double m_maximumPressureRangeDefault = 0.9; // settings_2b limiter range
    bool m_tempStepsEnabled = false;

    // Frames
    int m_preinfuseFrameCount = 0;
    QList<ProfileFrame> m_steps;

    // Mode
    Mode m_mode = Mode::FrameBased;

    // Recipe parameters (for D-Flow/A-Flow/Pressure/Flow editors)
    // Top-level keys from the source JSON that Decenza does not model. Re-emitted
    // verbatim on serialize so a profile authored in another DE1 app survives a
    // Decenza load→save round trip instead of being silently stripped (de1app
    // writes several simple-editor keys we never read). Canonical keys always win.
    QJsonObject m_unknownKeys;

    // Step keys seen at load that we do not understand. Non-empty makes the
    // profile invalid — see ProfileFrame::knownJsonKeys() for why an unknown
    // key inside a step is refused rather than carried along.
    QStringList m_unsupportedStepKeys;
    QStringList m_malformedValues;

    RecipeParams m_recipeParams;
    bool m_hasRecipeParams = false;  // see hasRecipeParams()

    // Read-only flag (de1app compatibility: 0=editable, 1=read-only, 2=reset)
    int m_readOnly = 0;
};

Q_DECLARE_METATYPE(Profile)
