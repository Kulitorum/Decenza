#include "profile.h"
#include "de1apptclfields.h"
#include "profilejson.h"
#include "recipegenerator.h"
#include "recipeanalyzer.h"
#include "../ble/protocol/binarycodec.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

// Convert a JSON value that may be string or number to double (de1app encodes
// numbers as strings). Public — the ProfileManager catalog scan shares it.
double profileJsonToDouble(const QJsonValue& val, double defaultVal) {
    if (val.isString()) {
        bool ok;
        double d = val.toString().toDouble(&ok);
        if (!ok) {
            qWarning() << "profileJsonToDouble: failed to parse string" << val.toString() << "- using default" << defaultVal;
        }
        return ok ? d : defaultVal;
    }
    return val.toDouble(defaultVal);
}

bool profileJsonToBool(const QJsonValue& val, bool defaultVal, bool* ok) {
    // The boolean twin of profileJsonToDouble, and needed for the same reason:
    // de1app and reaprime encode flags as "1"/"0" strings, and
    // QJsonValue::toBool() returns its DEFAULT for anything that is not a real
    // JSON bool. So `"1"` read with .toBool(false) silently yields false — the
    // flag is not misread, it is destroyed, and the value that replaces it is
    // indistinguishable from the profile never having set it.
    //
    // `ok` reports whether the value was interpretable as a truth value at all.
    // Callers that are COMPARING two values must check it: letting an
    // uninterpretable value fall back to a default is precisely how the parity
    // audit compared "1" against false and certified them equal.
    if (ok) *ok = true;
    if (val.isBool()) return val.toBool();
    if (val.isDouble()) return !qFuzzyIsNull(val.toDouble());
    if (val.isString()) {
        const QString s = val.toString().trimmed().toLower();
        if (s == QStringLiteral("true")) return true;
        if (s == QStringLiteral("false")) return false;
        bool numOk = false;
        const double n = s.toDouble(&numOk);
        if (numOk) return !qFuzzyIsNull(n);
    }
    if (ok) *ok = false;
    return defaultVal;
}

static double jsonToDouble(const QJsonValue& val, double defaultVal = 0.0) {
    return profileJsonToDouble(val, defaultVal);
}

// Every top-level key Profile models (reads and/or writes). Anything outside this
// set is retained verbatim in m_unknownKeys and re-emitted, so a Decenza save does
// not strip keys another DE1 app authored. Add a key here when Profile starts
// modelling it — otherwise it would be both parsed AND echoed from the passthrough.
static const QSet<QString> kKnownProfileKeys = {
    QStringLiteral("title"), QStringLiteral("author"), QStringLiteral("notes"),
    QStringLiteral("profile_notes"), QStringLiteral("beverage_type"), QStringLiteral("version"),
    QStringLiteral("legacy_profile_type"), QStringLiteral("profile_type"), QStringLiteral("type"),
    QStringLiteral("target_weight"), QStringLiteral("target_volume"),
    QStringLiteral("target_volume_count_start"), QStringLiteral("espresso_temperature"),
    QStringLiteral("maximum_pressure"), QStringLiteral("maximum_flow"),
    QStringLiteral("minimum_pressure"), QStringLiteral("tank_desired_water_temperature"),
    QStringLiteral("tank_temperature"), QStringLiteral("maximum_flow_range_advanced"),
    QStringLiteral("maximum_pressure_range_advanced"), QStringLiteral("maximum_flow_range_default"),
    QStringLiteral("maximum_pressure_range_default"), QStringLiteral("number_of_preinfuse_frames"),
    QStringLiteral("preinfuse_frame_count"), QStringLiteral("has_recommended_dose"),
    QStringLiteral("recommended_dose"), QStringLiteral("mode"), QStringLiteral("steps"),
    QStringLiteral("temperature_presets"), QStringLiteral("temp_steps_enabled"),
    QStringLiteral("recipe"), QStringLiteral("read_only"),
    QStringLiteral("preinfusion_time"), QStringLiteral("preinfusion_flow_rate"),
    QStringLiteral("preinfusion_stop_pressure"), QStringLiteral("espresso_pressure"),
    QStringLiteral("espresso_hold_time"), QStringLiteral("espresso_decline_time"),
    QStringLiteral("pressure_end"), QStringLiteral("flow_profile_hold"),
    QStringLiteral("flow_profile_hold_time"), QStringLiteral("flow_profile_decline"),
    QStringLiteral("flow_profile_decline_time"),
    // NOTE: "reference_file", "lang", "hidden", "changes_since_last_espresso" and
    // "is_recipe_mode" are all deliberately ABSENT from this set. Profile does not
    // model any of them, so listing them here would exclude them from the
    // m_unknownKeys passthrough while toJsonObject() overwrote them with constants
    // — silently destroying a foreign profile's state on every save (a de1app
    // profile marked "hidden":"1" came back visible). Left unlisted, they survive
    // verbatim and toJsonObject() only supplies a default when the source had none.
    // "version" IS listed on purpose: we genuinely author v2 and must not echo back
    // a version claim for semantics we did not write.
};

// Generate frames for simple pressure profile (settings_2a)
// Based on de1app's pressure_to_advanced_list()
static QVector<ProfileFrame> generatePressureProfileFrames(
    double preinfusionTime, double preinfusionFlowRate, double preinfusionStopPressure,
    double holdTime, double espressoPressure,
    double declineTime, double pressureEnd,
    double maximumFlow, double maximumFlowRange,
    double temp0, double temp1, double temp2, double temp3,
    bool tempStepsEnabled)
{
    QVector<ProfileFrame> frames;

    // Caller supplies the four temperatures already resolved — including
    // de1app's rule that temp stepping OFF means all four equal
    // espresso_temperature. See regenerateSimpleFrames(), which is the one place
    // that rule lives.

    // Preinfusion frame(s) (flow pump, exit on pressure_over).
    //
    // de1app computes both frame lengths BEFORE testing anything, and emits each
    // frame on its own `> 0` check (profile.tcl:19-56). With temp stepping on it
    // sets first_frame_len to temp_bump_time_seconds UNCONDITIONALLY, so a
    // profile with preinfusion_time 0 still gets a 2-second boost frame.
    //
    // Gating the whole block on `preinfusionTime > 0` therefore dropped that
    // frame: Steam_only and "e61 classic at 9 bar" both carry preinfusion_time 0
    // with stepping on, and de1app brews 3 frames there where we brewed 2 —
    // skipping a 2-second flow phase entirely.
    double firstFrameLen = 0.0;
    double secondFrameLen = preinfusionTime;
    if (tempStepsEnabled) {
        firstFrameLen = 2.0;  // de1app: temp_bump_time_seconds
        secondFrameLen = preinfusionTime - firstFrameLen;
        if (secondFrameLen < 0) secondFrameLen = 0;
    }

    // Temp boost frame at temp0 (no flow exit)
    if (firstFrameLen > 0) {
        ProfileFrame boost;
        boost.name = "preinfusion temp boost";
        boost.temperature = temp0;
        boost.sensor = "coffee";
        boost.pump = "flow";
        boost.transition = "fast";
        boost.pressure = 1.0;
        boost.flow = preinfusionFlowRate;
        boost.seconds = firstFrameLen;
        boost.volume = 0;
        boost.exitIf = true;
        boost.exitType = "pressure_over";
        boost.exitPressureOver = preinfusionStopPressure;
        // exitFlowOver = 0 (default) - no flow exit during temp boost
        frames.append(boost);
    }

    // Preinfusion frame at temp1 (with flow exit)
    if (secondFrameLen > 0) {
        ProfileFrame preinfusion;
        preinfusion.name = "preinfusion";
        preinfusion.temperature = temp1;
        preinfusion.sensor = "coffee";
        preinfusion.pump = "flow";
        preinfusion.transition = "fast";
        preinfusion.pressure = 1.0;
        preinfusion.flow = preinfusionFlowRate;
        preinfusion.seconds = secondFrameLen;
        preinfusion.volume = 0;
        preinfusion.exitIf = true;
        preinfusion.exitType = "pressure_over";
        preinfusion.exitPressureOver = preinfusionStopPressure;
        preinfusion.exitFlowOver = 6.0;
        frames.append(preinfusion);
    }

    // Rise and hold frame (pressure pump)
    if (holdTime > 0) {
        // If hold time > 3s, add a forced rise frame without limiter first
        if (holdTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = temp2;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.flow = 0.0;  // de1app writes no flow on a pressure frame
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            holdTime -= 3;
        }

        ProfileFrame hold;
        hold.name = "rise and hold";
        hold.temperature = temp2;
        hold.sensor = "coffee";
        hold.pump = "pressure";
        hold.flow = 0.0;  // de1app writes no flow on a pressure frame
        hold.transition = "fast";
        hold.pressure = espressoPressure;
        hold.seconds = holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        if (maximumFlow > 0) {
            hold.maxFlowOrPressure = maximumFlow;
            hold.maxFlowOrPressureRange = maximumFlowRange;
        }
        frames.append(hold);
    }

    // Decline frame (pressure pump, smooth transition)
    if (declineTime > 0) {
        // Match de1app: add forced rise before decline when hold was short (< 3s after
        // possible decrement) and decline is long enough to split off 3s
        if (holdTime < 3 && declineTime > 3) {
            ProfileFrame riseNoLimit;
            riseNoLimit.name = "forced rise without limit";
            riseNoLimit.temperature = temp3;
            riseNoLimit.sensor = "coffee";
            riseNoLimit.pump = "pressure";
            riseNoLimit.flow = 0.0;  // de1app writes no flow on a pressure frame
            riseNoLimit.transition = "fast";
            riseNoLimit.pressure = espressoPressure;
            riseNoLimit.seconds = 3.0;
            riseNoLimit.volume = 0;
            riseNoLimit.exitIf = false;
            frames.append(riseNoLimit);
            declineTime -= 3;
        }

        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = temp3;
        decline.sensor = "coffee";
        decline.pump = "pressure";
        decline.flow = 0.0;  // de1app writes no flow on a pressure frame
        decline.transition = "smooth";
        decline.pressure = pressureEnd;
        decline.seconds = declineTime;
        decline.volume = 0;
        decline.exitIf = false;
        decline.exitFlowOver = 6.0;
        if (maximumFlow > 0) {
            decline.maxFlowOrPressure = maximumFlow;
            decline.maxFlowOrPressureRange = maximumFlowRange;
        }
        frames.append(decline);
    }

    // Add empty frame if no frames were created
    if (frames.isEmpty()) {
        qWarning() << "generatePressureProfileFrames: all time parameters are zero, adding empty fallback frame";
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}

// Generate frames for simple flow profile (settings_2b)
// Based on de1app's flow_to_advanced_list()
static QVector<ProfileFrame> generateFlowProfileFrames(
    double preinfusionTime, double preinfusionFlowRate, double preinfusionStopPressure,
    double holdTime, double flowHold,
    double declineTime, double flowDecline,
    double maximumPressure, double maximumPressureRange,
    double temp0, double temp1, double temp2, double temp3,
    bool tempStepsEnabled)
{
    QVector<ProfileFrame> frames;

    // Temperatures arrive already resolved — see the note in
    // generatePressureProfileFrames() and regenerateSimpleFrames().

    // Preinfusion frame(s). Identical structure to the pressure builder, and
    // identical de1app source (flow_to_advanced_list, profile.tcl:212-275):
    // both lengths are computed before any test, and each frame has its own
    // `> 0` check, so temp stepping produces a boost frame even at
    // preinfusion_time 0.
    double firstFrameLen = 0.0;
    double secondFrameLen = preinfusionTime;
    if (tempStepsEnabled) {
        firstFrameLen = 2.0;  // de1app: temp_bump_time_seconds
        secondFrameLen = preinfusionTime - firstFrameLen;
        if (secondFrameLen < 0) secondFrameLen = 0;
    }

    // Temp boost frame at temp0 (no flow exit)
    if (firstFrameLen > 0) {
        ProfileFrame boost;
        // "preinfusion boost", NOT the pressure builder's "preinfusion temp
        // boost" — de1app really does name this frame differently in its two
        // builders (profile.tcl:38 vs :233). Unifying them looks like tidying
        // and silently renamed the frame on 5 shipped flow built-ins.
        boost.name = "preinfusion boost";
        boost.temperature = temp0;
        boost.sensor = "coffee";
        boost.pump = "flow";
        boost.transition = "fast";
        boost.pressure = 1.0;
        boost.flow = preinfusionFlowRate;
        boost.seconds = firstFrameLen;
        boost.volume = 0;
        boost.exitIf = true;
        boost.exitType = "pressure_over";
        boost.exitPressureOver = preinfusionStopPressure;
        // exitFlowOver = 0 (default) - no flow exit during temp boost
        frames.append(boost);
    }

    // Preinfusion frame at temp1 (no flow exit for flow profiles)
    if (secondFrameLen > 0) {
        ProfileFrame preinfusion;
        preinfusion.name = "preinfusion";
        preinfusion.temperature = temp1;
        preinfusion.sensor = "coffee";
        preinfusion.pump = "flow";
        preinfusion.transition = "fast";
        preinfusion.pressure = 1.0;
        preinfusion.flow = preinfusionFlowRate;
        preinfusion.seconds = secondFrameLen;
        preinfusion.volume = 0;
        preinfusion.exitIf = true;
        preinfusion.exitType = "pressure_over";
        preinfusion.exitPressureOver = preinfusionStopPressure;
        // exitFlowOver = 0 (default) - flow profiles don't use flow exit
        frames.append(preinfusion);
    }

    // Hold frame (flow pump)
    if (holdTime > 0) {
        ProfileFrame hold;
        hold.name = "hold";
        hold.temperature = temp2;
        hold.sensor = "coffee";
        hold.pump = "flow";
        hold.pressure = 0.0;  // de1app writes no pressure on a flow frame
        hold.transition = "fast";
        hold.flow = flowHold;
        hold.seconds = holdTime;
        hold.volume = 0;
        hold.exitIf = false;
        hold.exitFlowOver = 6.0;
        if (maximumPressure > 0) {
            hold.maxFlowOrPressure = maximumPressure;
            hold.maxFlowOrPressureRange = maximumPressureRange;
        }
        frames.append(hold);
    }

    // Decline frame (flow pump, smooth transition)
    // de1app: decline is only generated when holdTime > 0 (not declineTime > 0)
    if (holdTime > 0) {
        ProfileFrame decline;
        decline.name = "decline";
        decline.temperature = temp3;
        decline.sensor = "coffee";
        decline.pump = "flow";
        decline.pressure = 0.0;  // de1app writes no pressure on a flow frame
        decline.transition = "smooth";
        decline.flow = flowDecline;
        decline.seconds = declineTime;
        decline.volume = 0;
        decline.exitIf = false;
        if (maximumPressure > 0) {
            decline.maxFlowOrPressure = maximumPressure;
            decline.maxFlowOrPressureRange = maximumPressureRange;
        }
        frames.append(decline);
    }

    // Add empty frame if no frames were created
    if (frames.isEmpty()) {
        qWarning() << "generateFlowProfileFrames: all time parameters are zero, adding empty fallback frame";
        ProfileFrame empty;
        empty.name = "empty";
        empty.temperature = 90.0;
        empty.sensor = "coffee";
        empty.pump = "flow";
        empty.transition = "smooth";
        empty.flow = 0;
        empty.seconds = 0;
        empty.volume = 0;
        empty.exitIf = false;
        frames.append(empty);
    }

    return frames;
}

QString Profile::editorType() const {
    // Derived from title + profileType — matches de1app behavior.
    // Title check first (D-Flow/A-Flow), then profileType, then advanced fallback.
    QString t = m_title.startsWith(QLatin1Char('*')) ? m_title.mid(1) : m_title;
    if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("dflow");
    if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("aflow");
    if (m_profileType == QLatin1String("settings_2a"))
        return QStringLiteral("pressure");
    if (m_profileType == QLatin1String("settings_2b"))
        return QStringLiteral("flow");
    return QStringLiteral("advanced");
}

// Canonical DE1 v2 profile serialization. Single source of truth for every
// Decenza profile JSON — on-disk, exported, share-code, AND the Visualizer
// upload (which delegates here). One format, validated across Decenza, reaprime,
// and Visualizer:
//   - numeric values are string-encoded (matches de1app / tablet / Visualizer / reaprime);
//   - the ecosystem-required keys tank_temperature + target_volume_count_start are
//     always present (reaprime's Profile.fromJson hard-rejects a profile without them);
//   - the standard DE1 v2 metadata (type/lang/hidden/reference_file/changes_since_last_espresso);
//   - steps are never empty (simple settings_2a/2b profiles are materialized before emit).
// Decenza-only keys (mode, temperature_presets, the settings_2a block, recipe,
// read_only) are additive and ignored by apps that don't understand them.
QJsonObject Profile::toJsonObject() const {
    // Precision policy lives in profilejson.h — do not inline magic decimal counts
    // here; a field encoded below its editor's resolution silently changes the shot.
    auto num = [](double v, int prec) { return QJsonValue(ProfileJson::enc(v, prec)); };

    // Unmodelled keys from the source profile go in FIRST so every canonical key
    // below overwrites them — the passthrough preserves foreign data without ever
    // letting a stale value shadow one Decenza actually manages.
    QJsonObject obj = m_unknownKeys;
    obj["title"] = m_title;
    obj["author"] = m_author;
    obj["notes"] = m_profileNotes;
    obj["beverage_type"] = m_beverageType;
    obj["version"] = QStringLiteral("2");
    obj["legacy_profile_type"] = m_profileType;
    // Precision must not be below what the editors let a user set, or a
    // save→reload cycle silently changes the shot. target weight/volume are
    // 0.1-resolution in ProfileEditorPage; limiter ranges are 0.01.
    obj["target_weight"] = num(m_targetWeight, ProfileJson::TargetMass);
    obj["target_volume"] = num(m_targetVolume, ProfileJson::TargetMass);
    obj["espresso_temperature"] = num(m_espressoTemperature, ProfileJson::Temperature);
    obj["maximum_pressure"] = num(m_maximumPressure, ProfileJson::Pressure);
    obj["maximum_flow"] = num(m_maximumFlow, ProfileJson::Flow);
    obj["minimum_pressure"] = num(m_minimumPressure, ProfileJson::Pressure);
    // flow_profile_minimum_pressure is de1app's spelling of the SAME field, and
    // it is NOT re-derived from m_minimumPressure here. Writing the canonical
    // value over the alias destroyed de1app's real setting in 3 shipped
    // built-ins (6 bar overwritten with 0), because in those the ALIAS is the
    // populated side. It rides through as a passthrough key instead.
    //
    // (An earlier revision of this comment also described re-deriving it "only
    // when the source actually had it". No code ever did that, and the sentence
    // read as a prescription to restore — hence this note.)
    //
    // flow_profile_preinfusion / _preinfusion_time get the same treatment for a
    // different reason. They LOOK like aliases of preinfusion_flow_rate /
    // preinfusion_time and are not: they are de1app's flow-editor (settings_2b)
    // values versus the pressure-editor (settings_2a) ones, which is why the Tcl
    // reader's regex carries an explicit \b guard to stop one matching the
    // other. Of the 62 built-ins carrying both pairs, flow_profile_preinfusion_time
    // differs from preinfusion_time in 61 and flow_profile_preinfusion differs
    // from preinfusion_flow_rate in 19 — so at least one of the pair differs
    // almost everywhere, correctly. Re-deriving them would overwrite the flow
    // editor's settings with the pressure editor's across the corpus. Left as
    // passthrough until the direction is settled.
    obj["tank_desired_water_temperature"] = num(m_tankDesiredWaterTemperature, ProfileJson::TankTemp);
    // tank_temperature: ecosystem-standard alias required by reaprime.
    obj["tank_temperature"] = obj["tank_desired_water_temperature"];
    obj["maximum_flow_range_advanced"] = num(m_maximumFlowRangeAdvanced, ProfileJson::Limiter);
    obj["maximum_pressure_range_advanced"] = num(m_maximumPressureRangeAdvanced, ProfileJson::Limiter);
    obj["number_of_preinfuse_frames"] = QString::number(m_preinfuseFrameCount);
    // target_volume_count_start: ecosystem-standard alias required by reaprime.
    obj["target_volume_count_start"] = obj["number_of_preinfuse_frames"];
    obj["has_recommended_dose"] = m_hasRecommendedDose;
    obj["recommended_dose"] = num(m_recommendedDose, ProfileJson::Weight);
    obj["mode"] = (m_mode == Mode::DirectControl) ? "direct" : "frame_based";

    // Standard DE1 v2 metadata (de1app / tablet / Visualizer). `type` is derived
    // from the profile type, matching de1app's convention.
    if (m_profileType == "settings_2a")
        obj["type"] = QStringLiteral("pressure");
    else if (m_profileType == "settings_2b")
        obj["type"] = QStringLiteral("flow");
    else
        obj["type"] = QStringLiteral("advanced");
    // Preserve the source profile's language (via m_unknownKeys); only default it
    // when absent. A Korean-authored profile must not come back tagged "en".
    if (!obj.contains("lang")) obj["lang"] = QStringLiteral("en");
    if (!obj.contains("hidden")) obj["hidden"] = QStringLiteral("0");
    // Only synthesize when absent — an imported profile's own reference_file
    // (preserved via m_unknownKeys) is more accurate than our title.
    if (!obj.contains("reference_file")) obj["reference_file"] = m_title;
    if (!obj.contains("changes_since_last_espresso")) obj["changes_since_last_espresso"] = QString();

    // Simple profile parameters. Emitted for EVERY profile type, matching de1app
    // (which writes them unconditionally) and symmetric with fromJson, which also
    // reads them unconditionally. Gating this on settings_2a/2b destroyed these
    // keys on 58+ advanced built-ins the first time they were re-serialized.
    {
        obj["preinfusion_time"] = num(m_preinfusionTime, ProfileJson::Seconds);
        obj["preinfusion_flow_rate"] = num(m_preinfusionFlowRate, ProfileJson::Flow);
        obj["preinfusion_stop_pressure"] = num(m_preinfusionStopPressure, ProfileJson::Pressure);
        obj["espresso_pressure"] = num(m_espressoPressure, ProfileJson::Pressure);
        obj["espresso_hold_time"] = num(m_espressoHoldTime, ProfileJson::Seconds);
        obj["espresso_decline_time"] = num(m_espressoDeclineTime, ProfileJson::Seconds);
        obj["pressure_end"] = num(m_pressureEnd, ProfileJson::Pressure);
        obj["flow_profile_hold"] = num(m_flowProfileHold, ProfileJson::Flow);
        obj["flow_profile_hold_time"] = num(m_flowProfileHoldTime, ProfileJson::Seconds);
        obj["flow_profile_decline"] = num(m_flowProfileDecline, ProfileJson::Flow);
        obj["flow_profile_decline_time"] = num(m_flowProfileDeclineTime, ProfileJson::Seconds);
        obj["maximum_flow_range_default"] = num(m_maximumFlowRangeDefault, ProfileJson::Limiter);
        obj["maximum_pressure_range_default"] = num(m_maximumPressureRangeDefault, ProfileJson::Limiter);
        obj["temp_steps_enabled"] = m_tempStepsEnabled;
    }

    QJsonArray tempsArray;
    for (double temp : m_temperaturePresets) {
        tempsArray.append(num(temp, ProfileJson::Temperature));
    }
    obj["temperature_presets"] = tempsArray;

    // Steps: never emit an empty array. Simple settings_2a/2b profiles carry their
    // frames implicitly (regenerated at activation); materialize them here so any
    // file Decenza emits is a valid, runnable profile in any DE1 app. In-memory
    // profiles loaded via fromJson() already have frames; this covers the rest.
    const QVector<ProfileFrame> steps = materializedSteps();
    QJsonArray stepsArray;
    for (const auto& step : steps) {
        stepsArray.append(step.toJson());
    }
    obj["steps"] = stepsArray;

    // When frames were materialized here (they were not stored on the object), the
    // preinfuse count must be derived from them exactly as fromJson() and
    // regenerateSimpleFrames() do — otherwise a profile built through setters emits
    // generated preinfusion frames alongside a stale target_volume_count_start, and
    // the machine counts target volume from the wrong frame.
    if (m_steps.isEmpty() && !steps.isEmpty()) {
        const QString derived = QString::number(countPreinfuseFrames(steps));
        obj["number_of_preinfuse_frames"] = derived;
        obj["target_volume_count_start"] = derived;
    }

    // Recipe params — only write when explicitly populated (not default).
    // D-Flow/A-Flow profiles always have recipe data. Simple profiles (settings_2a/2b)
    // only have recipe data if they were edited through the recipe editor.
    // The recipe block is written from RecipeParams ALONE — never overlaid on the
    // block as it arrived. An earlier revision did overlay it, to stop
    // `recipe.editorType` being dropped, and that was wrong twice over:
    //
    //   1. RecipeParams::fromJson consumes `pourStyle` / `flowLimit` /
    //      `pressureLimit` as a one-shot MIGRATION SOURCE — migratePourStyle()
    //      runs after pourPressure/pourFlow are read and overrides them. Echoing
    //      those keys back made the migration re-fire on every load, so a user's
    //      edited pour pressure silently reverted to the legacy limit forever.
    //   2. It defeated ProfileManager's editorType self-repair
    //      (profilemanager.cpp): the corrected value was applied in memory and
    //      then discarded on save, so the repair re-ran every load and never
    //      stuck — and editorType drives frame generation.
    //
    // Dropping `editorType` is correct: fromJson infers it for every case where a
    // recipe block is written at all (dflow/aflow from the title, pressure/flow
    // from settings_2a/2b), so nothing is lost.
    //
    // RULE: a key the reader consumes as a migration source must never be echoed
    // back. Preserving it converts a one-shot migration into a permanent override.
    auto recipeJson = [this]() { return m_recipeParams.toJson(); };

    // NOTE: an ADVANCED profile deliberately gets no recipe block. Editor type is
    // derived from the title ("D-Flow /", "A-Flow /"), so an advanced profile has
    // no recipe editor and its recipe params are meaningless. Dropping a stale
    // recipe block from an advanced profile is correct cleanup, not data loss —
    // several built-ins carried one purely as cruft. Do not "preserve" it here.
    //
    // A block is written only when the parameters were ESTABLISHED — see
    // Profile::hasRecipeParams(). A recipe-shaped TITLE is not evidence that a
    // profile has recipe parameters. Every `.tcl` import, every Visualizer
    // download and every profile shared from another app arrives without a
    // recipe block, by design: both upstream plugins reconstruct their editor
    // state from the frames on load, so nothing persists it. Writing one anyway
    // from a default-constructed struct is finding REC-1 — it put five identical
    // 88 °C / 25 s / 4 g blocks into the A-Flow built-ins, matching none of their
    // frames, and made editing one parameter silently reset the others.
    QString et = editorType();
    if (m_hasRecipeParams
        && (et == QLatin1String("dflow") || et == QLatin1String("aflow")
            || et == QLatin1String("pressure") || et == QLatin1String("flow"))) {
        obj["recipe"] = recipeJson();
    }

    // Read-only flag (de1app compatibility: integer 0/1/2)
    if (m_readOnly != 0) {
        obj["read_only"] = m_readOnly;
    }

    return obj;
}

QJsonDocument Profile::toJson() const {
    return QJsonDocument(toJsonObject());
}

// Return this profile's frames, materializing them for simple (settings_2a/2b)
// profiles that carry their frames implicitly. const-safe: builds a local vector
// rather than mutating m_steps (mirrors the generation fromJson() does on load).
QVector<ProfileFrame> Profile::materializedSteps() const {
    if (!m_steps.isEmpty())
        return m_steps;
    if (m_profileType != "settings_2a" && m_profileType != "settings_2b")
        return m_steps;

    double temp0 = m_temperaturePresets.value(0, m_espressoTemperature);
    double temp1 = m_temperaturePresets.value(1, m_espressoTemperature);
    double temp2 = m_temperaturePresets.value(2, m_espressoTemperature);
    double temp3 = m_temperaturePresets.value(3, m_espressoTemperature);

    // Temp stepping off means EVERY frame runs at espresso_temperature — de1app
    // overwrites all four presets with it before building (profile.tcl:28-33).
    //
    // This function is THE simple-profile generator: regenerateSimpleFrames()
    // and fromJson() both route through it. They used to carry their own copies
    // of this dispatch, and when the rule moved out of the generators only one
    // copy got it — so the same profile produced stepped frames on load and
    // flat frames on re-activation, which is a difference in what the DE1 is
    // handed. One implementation is the fix; adding the rule three times is not.
    if (!m_tempStepsEnabled)
        temp0 = temp1 = temp2 = temp3 = m_espressoTemperature;

    if (m_profileType == "settings_2a") {
        return generatePressureProfileFrames(
            m_preinfusionTime, m_preinfusionFlowRate, m_preinfusionStopPressure,
            m_espressoHoldTime, m_espressoPressure,
            m_espressoDeclineTime, m_pressureEnd,
            m_maximumFlow, m_maximumFlowRangeDefault,
            temp0, temp1, temp2, temp3,
            m_tempStepsEnabled);
    }
    return generateFlowProfileFrames(
        m_preinfusionTime, m_preinfusionFlowRate, m_preinfusionStopPressure,
        m_espressoHoldTime, m_flowProfileHold,
        m_espressoDeclineTime, m_flowProfileDecline,
        m_maximumPressure, m_maximumPressureRangeDefault,
        temp0, temp1, temp2, temp3,
        m_tempStepsEnabled);
}

namespace {

// Keys whose READ default is non-zero, so "absent" and "0" mean different things.
// Dropping an explicit zero for one of these is real loss, never inert.
const QSet<QString>& nonZeroDefaultKeys() {
    static const QSet<QString> k = {
        QStringLiteral("target_weight"), QStringLiteral("espresso_temperature"),
        QStringLiteral("maximum_pressure"), QStringLiteral("maximum_flow"),
        QStringLiteral("recommended_dose"), QStringLiteral("espresso_pressure"),
        QStringLiteral("preinfusion_time"), QStringLiteral("preinfusion_flow_rate"),
        QStringLiteral("preinfusion_stop_pressure"), QStringLiteral("espresso_hold_time"),
        QStringLiteral("espresso_decline_time"), QStringLiteral("pressure_end"),
        QStringLiteral("flow_profile_hold"), QStringLiteral("flow_profile_hold_time"),
        QStringLiteral("flow_profile_decline"), QStringLiteral("flow_profile_decline_time"),
        QStringLiteral("maximum_flow_range_advanced"), QStringLiteral("maximum_pressure_range_advanced"),
        QStringLiteral("maximum_flow_range_default"), QStringLiteral("maximum_pressure_range_default"),
        // per-step
        QStringLiteral("temperature"), QStringLiteral("pressure"), QStringLiteral("flow"),
        QStringLiteral("seconds"), QStringLiteral("range"),
    };
    return k;
}

// True when a value carries no information — absent, null, empty string, or a
// number that parses to 0, AND therefore safe to drop.
//
// Careful with the justification here: it is NOT "every reader defaults an absent
// key to 0" — Decenza's own fromJson does the opposite for many keys
// (target_weight 36.0, espresso_temperature 93.0, maximum_pressure 12.0,
// espresso_pressure 9.2 …). Dropping an explicit "target_weight":"0" would reload
// as 36 g and silently switch stop-at-weight ON.
//
// The narrow reason this is safe: every key with a non-zero read default is one the
// canonical serializer ALWAYS re-emits, so the checker never actually sees it
// dropped. Inertness is therefore only ever applied to foreign keys, which by
// definition Decenza does not model and cannot default. kNonZeroDefaultKeys below
// enforces that rather than leaving it to the reader to notice.
bool isInertValue(const QJsonValue& v) {
    if (v.isUndefined() || v.isNull()) return true;
    if (v.isObject() || v.isArray()) return false;   // structure is never inert
    if (v.isBool()) return v.toBool() == false;
    if (v.isString()) {
        const QString s = v.toString();
        if (s.isEmpty()) return true;
        bool ok = false;
        const double d = s.toDouble(&ok);
        return ok && qFuzzyIsNull(d);
    }
    return qFuzzyIsNull(v.toDouble());
}

// Compare two scalars for MEANING, normalizing the numeric/string encoding split
// (9.0 vs "9.00" are equal). Falls back to string comparison for non-numerics.
bool scalarsEqual(const QJsonValue& a, const QJsonValue& b) {
    auto asNumber = [](const QJsonValue& v, bool* ok) -> double {
        if (v.isDouble()) { *ok = true; return v.toDouble(); }
        if (v.isString()) return v.toString().toDouble(ok);
        *ok = false; return 0.0;
    };
    // A source value carrying no information — null, or "" (de1app writes "" for
    // fields a frame does not use, e.g. flow on a pressure step; reaprime writes
    // null for version/hidden). Filling one in is normally an addition, not a loss.
    //
    // BUT not when the replacement is a non-zero number: ProfileFrame::fromJson
    // defaults are non-zero (pressure 9.0, flow 2.0, seconds 30.0) and
    // profileJsonToDouble returns the DEFAULT for an unparseable string. So
    // "seconds":"" silently becomes a fabricated 30-second frame. Treating that as
    // equal certified a fabrication as lossless; the caller reports it instead.
    const bool aEmpty = a.isNull() || a.isUndefined()
                        || (a.isString() && a.toString().isEmpty());
    if (aEmpty) {
        bool bNumOk = false;
        double bNum = 0.0;
        if (b.isDouble()) {
            bNum = b.toDouble();
            bNumOk = true;
        } else if (b.isString()) {
            bNum = b.toString().toDouble(&bNumOk);
        }
        return !(bNumOk && !qFuzzyIsNull(bNum));   // non-zero fill-in => report it
    }
    bool aNum = false, bNum = false;
    const double av = asNumber(a, &aNum);
    const double bv = asNumber(b, &bNum);
    if (aNum && bNum) return qAbs(av - bv) < 0.0005;
    if (a.isBool() || b.isBool()) {
        // NOT a.toBool() == b.toBool(). toBool() returns its default for a
        // non-bool, so comparing de1app's "1" against our serialized `false`
        // took both sides to false and reported them EQUAL — the audit
        // certified a destroyed flag as lossless, and the test written to catch
        // exactly that passed straight through it.
        bool aOk = false, bOk = false;
        const bool at = profileJsonToBool(a, false, &aOk);
        const bool bt = profileJsonToBool(b, false, &bOk);
        return aOk && bOk && at == bt;
    }
    const QString as = a.isString() ? a.toString() : QString();
    const QString bs = b.isString() ? b.toString() : QString();
    return as == bs;
}

void collectParityErrors(const QJsonObject& before, const QJsonObject& after,
                         const QString& path, QStringList& errors);

void compareParityValue(const QJsonValue& b, const QJsonValue& a,
                        const QString& path, QStringList& errors) {
    if (b.isObject()) {
        if (!a.isObject()) { errors << path + QStringLiteral(": object lost"); return; }
        collectParityErrors(b.toObject(), a.toObject(), path, errors);
        return;
    }
    if (b.isArray()) {
        if (!a.isArray()) { errors << path + QStringLiteral(": array lost"); return; }
        const QJsonArray ba = b.toArray(), aa = a.toArray();
        if (ba.size() != aa.size()) {
            errors << QStringLiteral("%1: array size %2 -> %3").arg(path).arg(ba.size()).arg(aa.size());
            return;
        }
        for (qsizetype i = 0; i < ba.size(); ++i)
            compareParityValue(ba[i], aa[i], QStringLiteral("%1[%2]").arg(path).arg(i), errors);
        return;
    }
    if (!scalarsEqual(b, a)) {
        errors << QStringLiteral("%1: %2 -> %3").arg(path,
                    b.toVariant().toString(), a.toVariant().toString());
    }
}

void collectParityErrors(const QJsonObject& before, const QJsonObject& after,
                         const QString& path, QStringList& errors) {
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        const QString key = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
        if (!after.contains(it.key())) {
            // Only an informative value going missing is a loss — EXCEPT for keys
            // whose read default is non-zero, where dropping an explicit "0" flips
            // behaviour on reload (a dropped "target_weight":"0" comes back as 36 g
            // and silently enables stop-at-weight). For those, any drop is a loss.
            if (!isInertValue(it.value()) || nonZeroDefaultKeys().contains(it.key()))
                errors << key + QStringLiteral(": KEY LOST");
            continue;
        }
        compareParityValue(it.value(), after.value(it.key()), key, errors);
    }
}

}  // namespace

QStringList Profile::jsonParityErrors(const QJsonObject& before, const QJsonObject& after) {
    QStringList errors;
    collectParityErrors(before, after, QString(), errors);
    return errors;
}

QStringList Profile::reaprimeReadabilityErrors(const QJsonObject& obj) {
    QStringList errors;

    if (obj.value("title").toString().isEmpty())
        errors << QStringLiteral("missing/empty 'title'");

    // reaprime hard-requires these two keys (its Profile.fromJson throws otherwise).
    // An empty string counts as missing: reaprime parses them with double.parse /
    // int.parse, which throw on "" just as they do on an absent key.
    auto missingRequired = [&obj](const char* key) {
        const QJsonValue v = obj.value(QLatin1String(key));
        return v.isUndefined() || v.isNull() || (v.isString() && v.toString().isEmpty());
    };
    if (missingRequired("tank_temperature"))
        errors << QStringLiteral("missing 'tank_temperature'");
    if (missingRequired("target_volume_count_start"))
        errors << QStringLiteral("missing 'target_volume_count_start'");

    const QJsonValue stepsVal = obj.value("steps");
    if (!stepsVal.isArray() || stepsVal.toArray().isEmpty()) {
        errors << QStringLiteral("'steps' missing or empty");
        return errors;  // nothing further to validate
    }

    static const QStringList pumps      = {QStringLiteral("pressure"), QStringLiteral("flow")};
    static const QStringList sensors    = {QStringLiteral("coffee"),   QStringLiteral("water")};
    static const QStringList transitions= {QStringLiteral("fast"),     QStringLiteral("smooth")};
    static const QStringList exitTypes  = {QStringLiteral("pressure"), QStringLiteral("flow")};
    static const QStringList exitConds  = {QStringLiteral("over"),     QStringLiteral("under")};

    const QJsonArray steps = stepsVal.toArray();
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const QJsonObject s = steps[i].toObject();
        const QString where = QStringLiteral("step[%1] ").arg(i);
        if (s.value("name").toString().isEmpty())
            errors << where + QStringLiteral("missing 'name'");
        if (!pumps.contains(s.value("pump").toString()))
            errors << where + QStringLiteral("invalid 'pump' '%1'").arg(s.value("pump").toString());
        if (!sensors.contains(s.value("sensor").toString()))
            errors << where + QStringLiteral("invalid 'sensor' '%1'").arg(s.value("sensor").toString());
        if (!transitions.contains(s.value("transition").toString()))
            errors << where + QStringLiteral("invalid 'transition' '%1'").arg(s.value("transition").toString());
        if (s.contains("exit")) {
            const QJsonObject e = s.value("exit").toObject();
            if (!exitTypes.contains(e.value("type").toString()))
                errors << where + QStringLiteral("invalid exit 'type' '%1'").arg(e.value("type").toString());
            if (!exitConds.contains(e.value("condition").toString()))
                errors << where + QStringLiteral("invalid exit 'condition' '%1'").arg(e.value("condition").toString());
        }
    }

    return errors;
}

Profile Profile::fromJson(const QJsonDocument& doc) {
    Profile profile;
    QJsonObject obj = doc.object();

    // Snapshot key presence BEFORE any obj[...] access. QJsonObject's non-const
    // operator[] inserts a null member as a side effect of the reads below, after
    // which obj.contains("espresso_temperature") is always true — so the
    // absent-key reconciliation further down must consult this snapshot, not
    // obj.contains(). (This insertion is exactly what silently broke Visualizer
    // imports: their JSON omits espresso_temperature, but the dead contains-guard
    // let the 93.0 default leak through and get saved. See below.)
    const bool hadEspressoTemperature = obj.contains(QStringLiteral("espresso_temperature"));
    // Same reason, for the alias fallbacks below: these must be snapshotted here,
    // BEFORE any obj[...] access inserts a null member and makes contains() true.
    // They are correct today only because nothing above touches them — one added
    // read would silently break the reaprime tank-temperature/preinfuse-count
    // import with no test failure.
    const bool hadTankDesired = obj.contains(QStringLiteral("tank_desired_water_temperature"));
    const bool hadPreinfuseFrames = obj.contains(QStringLiteral("number_of_preinfuse_frames"));
    const bool hadVolumeCountStart = obj.contains(QStringLiteral("target_volume_count_start"));

    profile.setTitle(obj["title"].toString("Default"));
    profile.m_author = obj["author"].toString();
    // Support both legacy "profile_notes" (old Decenza) and current "notes" (de1app) keys
    profile.m_profileNotes = obj["notes"].toString();
    if (profile.m_profileNotes.isEmpty()) {
        profile.m_profileNotes = obj["profile_notes"].toString();
    }
    profile.m_beverageType = obj["beverage_type"].toString("espresso");

    // Read profile type: prefer legacy_profile_type (de1app), fall back to profile_type (Decenza)
    QString profileType = obj["legacy_profile_type"].toString();
    if (profileType.isEmpty()) profileType = obj["profile_type"].toString("settings_2c");
    profile.m_profileType = profileType;

    profile.m_targetWeight = jsonToDouble(obj["target_weight"], 36.0);
    profile.m_targetVolume = jsonToDouble(obj["target_volume"], 0.0);
    profile.m_espressoTemperature = jsonToDouble(obj["espresso_temperature"], 93.0);
    profile.m_maximumPressure = jsonToDouble(obj["maximum_pressure"], 12.0);
    profile.m_maximumFlow = jsonToDouble(obj["maximum_flow"], 6.0);
    // Fall back to de1app's spelling when ours is absent. Without this a de1app
    // JSON that carries only `flow_profile_minimum_pressure` loaded as 0 — the
    // same import loss already fixed for tank_temperature and
    // target_volume_count_start, in the same shape.
    profile.m_minimumPressure =
        obj.contains(QStringLiteral("minimum_pressure"))
            ? jsonToDouble(obj["minimum_pressure"], 0.0)
            : jsonToDouble(obj["flow_profile_minimum_pressure"], 0.0);

    // Both present and disagreeing means some app in the chain edited one and
    // not the other, so the file is already self-contradictory before we touch
    // it. Rare — 3 of 93 shipped built-ins — and it only ever surfaces as
    // "this profile behaves differently in de1app", which is precisely the
    // report nobody can reproduce without a log line naming both values.
    if (obj.contains(QStringLiteral("minimum_pressure"))
        && obj.contains(QStringLiteral("flow_profile_minimum_pressure"))) {
        const double alias = jsonToDouble(obj["flow_profile_minimum_pressure"], 0.0);
        if (qAbs(alias - profile.m_minimumPressure) > 0.0005) {
            // qDebug, not qWarning: this is a known-open data question, not a
            // fault, and 3 shipped built-ins trip it on every load. A warning
            // here fails the suite (QTest::failOnWarning) on files we ship.
            //
            // Which side wins is UNRESOLVED and must not be guessed. In those 3
            // the alias is populated (6 bar) and minimum_pressure is 0, so
            // Decenza currently brews 0 where de1app brews 6 — the apps already
            // disagree. Re-deriving the alias from the canonical field destroys
            // de1app's value; adopting the alias changes what Decenza brews.
            // Both are behaviour changes on shipped profiles.
            qDebug().noquote()
                << "Profile: de1app alias disagrees with the canonical field in"
                << obj.value(QStringLiteral("title")).toString()
                << "— flow_profile_minimum_pressure=" << alias
                << "vs minimum_pressure=" << profile.m_minimumPressure
                << "| Decenza brews minimum_pressure. If this profile brews"
                   " differently in de1app, this is why.";
        }
    }
    // Tank temperature: Decenza's key first, then the ecosystem-standard
    // `tank_temperature` that reaprime and de1app actually write. Reading only the
    // Decenza spelling silently zeroed the tank target on every reaprime import.
    profile.m_tankDesiredWaterTemperature =
        hadTankDesired
            ? jsonToDouble(obj["tank_desired_water_temperature"], 0.0)
            : jsonToDouble(obj["tank_temperature"], 0.0);
    profile.m_maximumFlowRangeAdvanced = jsonToDouble(obj["maximum_flow_range_advanced"], 0.6);
    profile.m_maximumPressureRangeAdvanced = jsonToDouble(obj["maximum_pressure_range_advanced"], 0.6);

    // Preinfuse frame count. All three spellings are in the wild: de1app/Decenza
    // write `number_of_preinfuse_frames`, reaprime and the Visualizer write
    // `target_volume_count_start`, and old Decenza files use `preinfuse_frame_count`.
    // Missing the second one reset the count to 0 on import, so target volume was
    // counted from frame 0 — including preinfusion — and the shot ran long.
    if (hadPreinfuseFrames) {
        profile.m_preinfuseFrameCount = static_cast<int>(jsonToDouble(obj["number_of_preinfuse_frames"], 0));
    } else if (hadVolumeCountStart) {
        profile.m_preinfuseFrameCount = static_cast<int>(jsonToDouble(obj["target_volume_count_start"], 0));
    } else {
        profile.m_preinfuseFrameCount = obj["preinfuse_frame_count"].toInt(0);
    }

    profile.m_hasRecommendedDose = profileJsonToBool(obj["has_recommended_dose"], false);
    profile.m_recommendedDose = jsonToDouble(obj["recommended_dose"], 18.0);

    // Simple profile parameters (settings_2a/2b)
    profile.m_preinfusionTime = jsonToDouble(obj["preinfusion_time"], 5.0);
    profile.m_preinfusionFlowRate = jsonToDouble(obj["preinfusion_flow_rate"], 4.0);
    profile.m_preinfusionStopPressure = jsonToDouble(obj["preinfusion_stop_pressure"], 4.0);
    profile.m_espressoPressure = jsonToDouble(obj["espresso_pressure"], 9.2);
    profile.m_espressoHoldTime = jsonToDouble(obj["espresso_hold_time"], 10.0);
    profile.m_espressoDeclineTime = jsonToDouble(obj["espresso_decline_time"], 25.0);
    profile.m_pressureEnd = jsonToDouble(obj["pressure_end"], 4.0);
    profile.m_flowProfileHold = jsonToDouble(obj["flow_profile_hold"], 2.0);
    profile.m_flowProfileHoldTime = jsonToDouble(obj["flow_profile_hold_time"], 8.0);
    profile.m_flowProfileDeclineTime = jsonToDouble(obj["flow_profile_decline_time"], 17.0);
    profile.m_flowProfileDecline = jsonToDouble(obj["flow_profile_decline"], 1.2);
    profile.m_maximumFlowRangeDefault = jsonToDouble(obj["maximum_flow_range_default"], 1.0);
    profile.m_maximumPressureRangeDefault = jsonToDouble(obj["maximum_pressure_range_default"], 0.9);
    profile.m_tempStepsEnabled = profileJsonToBool(obj["temp_steps_enabled"], false);

    QString modeStr = obj["mode"].toString("frame_based");
    profile.m_mode = (modeStr == "direct") ? Mode::DirectControl : Mode::FrameBased;

    QJsonArray tempsArray = obj["temperature_presets"].toArray();
    profile.m_temperaturePresets.clear();
    for (const auto& temp : tempsArray) {
        profile.m_temperaturePresets.append(jsonToDouble(temp));
    }
    if (profile.m_temperaturePresets.isEmpty()) {
        // Same rule as the Tcl path: absent presets mean espresso_temperature,
        // not a house ladder. A foreign DE1 profile carries no
        // temperature_presets at all (it is a Decenza extension), so inventing
        // one here would change what a simple profile regenerates to.
        profile.m_temperaturePresets = {profile.m_espressoTemperature, profile.m_espressoTemperature,
                                        profile.m_espressoTemperature, profile.m_espressoTemperature};
    }

    QJsonArray stepsArray = obj["steps"].toArray();
    for (const auto& stepVal : stepsArray) {
        const QJsonObject stepObj = stepVal.toObject();
        // Collect before parsing: a key we do not model is silently absent from
        // the resulting frame, so this is the only point at which it is visible.
        for (const QString& key : ProfileFrame::unknownJsonKeys(stepObj)) {
            if (!profile.m_unsupportedStepKeys.contains(key))
                profile.m_unsupportedStepKeys << key;
        }
        profile.m_steps.append(ProfileFrame::fromJson(stepObj));
    }

    // Generate frames for simple profiles when steps are empty — built-in and
    // legacy profiles store scalar parameters instead of pre-generated frames.
    //
    // Goes through regenerateSimpleFrames() so this path cannot drift from the
    // one the app runs at activation. It used to be a third copy of the
    // generator dispatch, and a rule added to only one copy meant the same
    // profile loaded with different frames than it re-activated with.
    if (profile.m_steps.isEmpty() &&
        (profile.m_profileType == "settings_2a" || profile.m_profileType == "settings_2b")) {
        profile.regenerateSimpleFrames();
        qDebug() << "Generated" << profile.m_steps.size() << "frames from simple"
                 << profile.m_profileType << "profile (JSON)";
    }

    // Read-only flag (de1app compatibility: integer 0/1/2)
    profile.m_readOnly = obj["read_only"].toInt(0);

    // Load recipe params if present. A block that is present was established by
    // whoever wrote it, so it round-trips; its ABSENCE is meaningful and must not
    // be papered over with defaults (REC-1).
    if (obj.contains("recipe")) {
        profile.m_recipeParams = RecipeParams::fromJson(obj["recipe"].toObject());
        profile.m_hasRecipeParams = true;
        // Infer RecipeParams.editorType from profileType/title when the recipe
        // block does not include an explicit editorType enum value
        if (!obj["recipe"].toObject().contains("editorType")) {
            if (profile.m_profileType == "settings_2a") {
                profile.m_recipeParams.editorType = EditorType::Pressure;
            } else if (profile.m_profileType == "settings_2b") {
                profile.m_recipeParams.editorType = EditorType::Flow;
            } else if (profile.m_title.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive) ||
                       (profile.m_title.startsWith(QLatin1Char('*')) &&
                        profile.m_title.mid(1).startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))) {
                profile.m_recipeParams.editorType = EditorType::AFlow;
            }
        }
    }

    // Reconcile espresso_temperature against the frames, which are the source of
    // truth for what the machine actually brews. The top-level scalar stays
    // authoritative when the author set it — it may legitimately differ from
    // steps[0] (a cooler group preheat target paired with a hotter preinfusion ramp,
    // as on the D-Flow / A-Flow built-ins; see PR #961). Two cases need repair:
    //
    //   1. Key absent → derive from the first frame. Visualizer's
    //      /profile?format=json omits espresso_temperature entirely (per-step temps
    //      only). Before this, the obj[...] side-effect insertion above defeated the
    //      old `!obj.contains()` guard, so the 93.0 default leaked through and was
    //      saved to disk — the actual Visualizer-import bug.
    //   2. Key present but equal to the bare 93.0 default AND outside the frame
    //      range → the fingerprint of a profile imported (and saved) while case 1
    //      was broken. Re-derive from the first frame so already-stored victims are
    //      repaired on load. A genuinely authored scalar is a real brew temp inside
    //      the frames, so this never touches the intentional-divergence case above.
    //
    // m_espressoTemperatureHealed flags either repair so callers can persist it once
    // (see ProfileManager::loadProfile). (regenerateFromRecipe resyncs from frames
    // after recipe regeneration.)
    if (!profile.m_steps.isEmpty()) {
        double minTemp = profile.m_steps.first().temperature;
        double maxTemp = minTemp;
        for (const ProfileFrame& step : profile.m_steps) {
            minTemp = qMin(minTemp, step.temperature);
            maxTemp = qMax(maxTemp, step.temperature);
        }
        constexpr double kDefaultTemp = 93.0;  // matches m_espressoTemperature / jsonToDouble fallback
        const bool leakedDefault = hadEspressoTemperature
            && qFuzzyCompare(profile.m_espressoTemperature, kDefaultTemp)
            && (kDefaultTemp > maxTemp + 0.1 || kDefaultTemp < minTemp - 0.1);
        if (!hadEspressoTemperature || leakedDefault) {
            if (leakedDefault) {
                qDebug() << "Profile::fromJson: replacing leaked 93.0 default espresso_temperature"
                         << "(frames" << minTemp << ".." << maxTemp << ") with first-frame temp for"
                         << profile.m_title;
            }
            profile.m_espressoTemperature = profile.m_steps.first().temperature;
            profile.m_espressoTemperatureHealed = true;
        }
    }

    // De1app defaults NumberOfPreinfuseFrames to 0 when the field is missing
    // (binary.tcl line 990: ifexists returns empty → 0). For simple profiles
    // (settings_2a/2b), de1app calculates it during frame generation
    // (pressure_to_advanced_list / flow_to_advanced_list in profile.tcl),
    // which Decenza already handles via countPreinfuseFrames() in the simple
    // profile generation block above (~line 540). Do NOT auto-calculate here
    // for advanced profiles — the profile author sets
    // final_desired_shot_volume_advanced_count_start explicitly, and we must
    // match de1app behavior for the same profile.

    // Retain any top-level key Decenza does not model, so serializing this
    // profile does not strip data another DE1 app authored (see m_unknownKeys).
    // Note this reads `doc.object()`, not `obj` — obj's non-const operator[]
    // has been inserting null members throughout this function, and those
    // phantom keys must not be captured as if the author had written them.
    const QJsonObject sourceObj = doc.object();
    for (auto it = sourceObj.constBegin(); it != sourceObj.constEnd(); ++it) {
        if (!kKnownProfileKeys.contains(it.key()))
            profile.m_unknownKeys.insert(it.key(), it.value());
    }

    return profile;
}

Profile Profile::loadFromFile(const QString& filePath) {
    // Check file extension
    if (filePath.endsWith(".tcl", Qt::CaseInsensitive)) {
        return loadFromTclFile(filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Profile::loadFromFile: Failed to open file:" << filePath
                   << "- Error:" << file.errorString();
        return Profile();
    }

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        qWarning() << "Profile::loadFromFile: JSON parse error:" << parseError.errorString()
                   << "at offset" << parseError.offset << "in file:" << filePath;
        return Profile();
    }

    return fromJson(doc);
}

bool Profile::saveToFile(const QString& filePath) const {
    // QSaveFile, not QFile: it writes to a temporary alongside the target and
    // renames on commit(), so an interrupted write leaves the previous file
    // intact instead of a truncated one. A plain QIODevice::WriteOnly truncates
    // the moment it opens — survivable for a git-tracked built-in, not for a
    // user's own profile, which the encoding upgrade in ProfileManager rewrites
    // purely to reformat it. Losing a file that was fine, in order to tidy it,
    // is the one outcome that upgrade must not have.
    QSaveFile file(filePath);
    // QSaveFile puts its temporary alongside the target, so open() fails outright
    // when the DIRECTORY forbids creating files even though the target itself is
    // writable — a regression the plain QFile write did not have. Qt recommends the
    // fallback for exactly this case ("to save documents edited by the user"): it
    // keeps the atomic temp+rename wherever that is possible and degrades to a
    // direct write rather than refusing where it is not.
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Profile::saveToFile: Failed to open file for writing:" << filePath
                   << "- Error:" << file.errorString();
        return false;
    }

    const QJsonObject canonical = toJsonObject();

    // The cross-app contract, checked on the real output rather than only in
    // tests. Every profile we write is one a user may hand to another app, and
    // reaprime hard-rejects a profile missing tank_temperature /
    // target_volume_count_start or carrying an empty steps array.
    //
    warnIfNotPortable(canonical, m_title, QStringLiteral("saveToFile"), filePath);

    QByteArray data = QJsonDocument(canonical).toJson(QJsonDocument::Indented);
    qint64 bytesWritten = file.write(data);
    if (bytesWritten != data.size()) {
        qWarning() << "Profile::saveToFile: Failed to write all data to:" << filePath
                   << "- Expected:" << data.size() << "bytes, wrote:" << bytesWritten
                   << "- Error:" << file.errorString();
        return false;   // ~QSaveFile discards the temporary; the original survives
    }

    // commit() is what makes the write visible. Without it QSaveFile destructs
    // into cancelWriteFile() and the target is never touched — a silent no-op
    // that would read as success.
    if (!file.commit()) {
        qWarning() << "Profile::saveToFile: Failed to commit:" << filePath
                   << "- Error:" << file.errorString();
        return false;
    }

    return true;
}

Profile Profile::loadFromJsonString(const QString& jsonContent) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseError);
    if (doc.isNull()) {
        qWarning() << "Profile::loadFromJsonString: JSON parse error:" << parseError.errorString()
                   << "at offset" << parseError.offset;
        return Profile();
    }
    return fromJson(doc);
}

// The cross-app contract, checked on the real output rather than only in tests.
// Every profile we write is one a user may hand to another app, and reaprime
// hard-rejects a profile missing tank_temperature / target_volume_count_start or
// carrying an empty steps array.
//
// This warns and still writes, deliberately. A profile that fails here is OUR
// serializer's bug, not the user's mistake — refusing the write would destroy
// their work to punish our defect, and they would have no way to fix it. The
// warning names the exact failures so it is actionable from the debug log (which
// is how field reports reach us), while their profile stays safe.
//
// Contrast the import direction, which DOES refuse: there the input is someone
// else's file and we cannot promise the shot. Here the shot is already correct
// locally; only its portability is in question.
//
// Shared by both write paths on purpose. It lived only in saveToFile(), so the
// toJsonString() route — which is every write on Android, where ProfileStorage
// writes through the SAF folder rather than QFile — produced no diagnostic at
// all. Same serializer, same contract, same bug class; only the platform decided
// whether anyone found out.
void Profile::warnIfNotPortable(const QJsonObject& canonical, const QString& title,
                                const QString& context, const QString& target)
{
    const QStringList contractErrors = reaprimeReadabilityErrors(canonical);
    if (contractErrors.isEmpty())
        return;
    qWarning().noquote()
        << QStringLiteral("Profile::%1: SAVED, but this profile is not readable by "
                          "other DE1 apps (reaprime) —").arg(context)
        << contractErrors.join(QStringLiteral("; "))
        << "| profile:" << title
        << (target.isEmpty() ? QString() : QStringLiteral("| file: %1").arg(target))
        << "| This is a Decenza serializer bug, not a problem with your profile. "
           "Please report it; the profile itself is saved and usable here.";
}

QString Profile::toJsonString() const {
    const QJsonObject canonical = toJsonObject();
    warnIfNotPortable(canonical, m_title, QStringLiteral("toJsonString"), QString());
    return QString::fromUtf8(QJsonDocument(canonical).toJson(QJsonDocument::Indented));
}

Profile Profile::loadFromTclFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open Tcl profile:" << filePath;
        return Profile();
    }

    QString content = QTextStream(&file).readAll();
    return loadFromTclString(content);
}

Profile Profile::loadFromTclString(const QString& content) {
    // Parse de1app .tcl profile format
    // Format: Tcl script with "array set" commands setting profile variables

    Profile profile;

    // Scalar extraction and the type-dependent key rule both live in
    // De1AppTcl — the drift gate in profile_sync reads the same .tcl through
    // the same functions, so the reader and the gate cannot disagree about
    // which spelling is authoritative.
    auto extractValue = [&content](const QString& varName) -> QString {
        return De1AppTcl::extractValue(content, varName);
    };

    // Extract metadata
    profile.setTitle(extractValue("profile_title"));
    profile.m_author = extractValue("author");
    profile.m_profileNotes = extractValue("profile_notes");
    profile.m_profileType = extractValue("settings_profile_type");
    profile.m_beverageType = extractValue("beverage_type");
    if (profile.m_beverageType.isEmpty()) {
        profile.m_beverageType = "espresso";
    }

    // Determine if this is an advanced profile (settings_2c or settings_2c2)
    bool isAdvancedProfile = De1AppTcl::isAdvancedType(profile.m_profileType);

    // Every numeric scalar goes through De1AppTcl by its CANONICAL name. That
    // one call resolves the dual-spelled fields by profile type and applies
    // de1app's absent-key values, so neither rule has a second copy here.
    QString val;

    // A value we cannot interpret is recorded and the profile refused, never
    // substituted. toDouble() yields 0.0 on failure and the member default
    // otherwise, and both are legal values here — so a substitution is
    // undetectable downstream and pours something the file never described.
    auto recordMalformed = [&profile](const QString& key, const QString& raw) {
        const QString entry = key + QStringLiteral("=") + raw;
        if (!profile.m_malformedValues.contains(entry))
            profile.m_malformedValues << entry;
        qWarning() << "Profile::loadFromTclString: cannot interpret" << key << "value" << raw
                   << "in profile" << profile.m_title << "— refusing rather than substituting";
    };

    auto readNum = [&](const QString& canonical, double& target) {
        const De1AppTcl::ScalarRead r =
            De1AppTcl::readScalar(content, canonical, profile.m_profileType, target);
        if (r.status == De1AppTcl::ReadStatus::Malformed) {
            recordMalformed(r.tclKey, r.raw);
            return;   // leave the member alone; the profile is invalid anyway
        }
        target = r.value;
    };

    // The integer/boolean scalars need the same treatment — each one silently
    // becomes 0 on a garbled value, and 0 means something valid for all of them
    // (read_only 0 = editable, stepping off, no preinfuse frames).
    auto readIntLike = [&](const QString& tclKey, auto&& apply) {
        const QString raw = extractValue(tclKey);
        if (raw.isEmpty()) return;
        bool ok = false;
        const int v = raw.toInt(&ok);
        if (!ok) { recordMalformed(tclKey, raw); return; }
        apply(v);
    };

    // Read-only flag (de1app: read_only 0/1/2). A garbled value would read as 0
    // — "editable" — quietly unlocking a profile its author marked protected.
    readIntLike(QStringLiteral("read_only"), [&](int v) { profile.m_readOnly = v; });

    readNum(QStringLiteral("target_weight"),                   profile.m_targetWeight);
    readNum(QStringLiteral("target_volume"),                   profile.m_targetVolume);
    readNum(QStringLiteral("maximum_flow_range_advanced"),     profile.m_maximumFlowRangeAdvanced);
    readNum(QStringLiteral("maximum_pressure_range_advanced"), profile.m_maximumPressureRangeAdvanced);

    readNum(QStringLiteral("espresso_temperature"),           profile.m_espressoTemperature);
    readNum(QStringLiteral("maximum_flow"),                   profile.m_maximumFlow);
    readNum(QStringLiteral("maximum_pressure"),               profile.m_maximumPressure);
    readNum(QStringLiteral("minimum_pressure"),               profile.m_minimumPressure);
    readNum(QStringLiteral("tank_desired_water_temperature"), profile.m_tankDesiredWaterTemperature);

    // de1app's simple-editor scalars, read for EVERY profile type. de1app writes
    // the whole block on every profile regardless of type, so gating them on the
    // matching settings_2a/2b branch loses the flow-editor values on 2a and 2c
    // profiles and the pressure-editor ones on 2b. This is the same mistake the
    // WRITER made, where it "destroyed those keys on 58+ advanced built-ins".
    readNum(QStringLiteral("preinfusion_time"),               profile.m_preinfusionTime);
    readNum(QStringLiteral("preinfusion_flow_rate"),          profile.m_preinfusionFlowRate);
    readNum(QStringLiteral("preinfusion_stop_pressure"),      profile.m_preinfusionStopPressure);
    readNum(QStringLiteral("espresso_pressure"),              profile.m_espressoPressure);
    readNum(QStringLiteral("espresso_hold_time"),             profile.m_espressoHoldTime);
    readNum(QStringLiteral("espresso_decline_time"),          profile.m_espressoDeclineTime);
    readNum(QStringLiteral("pressure_end"),                   profile.m_pressureEnd);
    readNum(QStringLiteral("flow_profile_hold"),              profile.m_flowProfileHold);
    readNum(QStringLiteral("flow_profile_hold_time"),         profile.m_flowProfileHoldTime);
    readNum(QStringLiteral("flow_profile_decline"),           profile.m_flowProfileDecline);
    readNum(QStringLiteral("flow_profile_decline_time"),      profile.m_flowProfileDeclineTime);
    readNum(QStringLiteral("maximum_flow_range_default"),     profile.m_maximumFlowRangeDefault);
    readNum(QStringLiteral("maximum_pressure_range_default"), profile.m_maximumPressureRangeDefault);

    readIntLike(QStringLiteral("espresso_temperature_steps_enabled"),
                [&](int v) { profile.m_tempStepsEnabled = (v == 1); });

    // de1app scalars Profile does not model, kept verbatim under de1app's own
    // spelling so a Decenza-written file still means the same thing to de1app.
    //   - profile_hide → hidden: de1app and reaprime read this to filter their
    //     profile lists. Decenza's own list uses SettingsApp::isHiddenProfile(),
    //     a separate per-user filename list, so this is inert locally.
    //   - flow_profile_preinfusion / _preinfusion_time: NOT aliases of
    //     preinfusion_flow_rate / preinfusion_time. They are de1app's flow-editor
    //     (settings_2b) values against the pressure-editor (settings_2a) ones,
    //     and they legitimately disagree in 61 of 62 shipped built-ins.
    //   - flow_profile_minimum_pressure: de1app's spelling of minimum_pressure,
    //     read into m_minimumPressure above and echoed here for de1app's editor.
    //   - profile_language → lang.
    auto passThrough = [&](const QString& tclKey, const QString& jsonKey) {
        const QString v = extractValue(tclKey);
        if (!v.isEmpty()) profile.m_unknownKeys.insert(jsonKey, v);
    };
    passThrough(QStringLiteral("profile_hide"),                  QStringLiteral("hidden"));
    // Shot-affecting in de1app (binary.tcl:880 prepends a 2-second pause frame
    // when set), so it must survive the round-trip even though Decenza does not
    // implement the pause itself — dropping it would make de1app brew a
    // different shot from our copy of the same profile.
    passThrough(QStringLiteral("insert_preinfusion_pause"),      QStringLiteral("insert_preinfusion_pause"));
    passThrough(QStringLiteral("profile_language"),              QStringLiteral("lang"));
    passThrough(QStringLiteral("flow_profile_preinfusion"),      QStringLiteral("flow_profile_preinfusion"));
    passThrough(QStringLiteral("flow_profile_preinfusion_time"), QStringLiteral("flow_profile_preinfusion_time"));
    passThrough(QStringLiteral("flow_profile_minimum_pressure"), QStringLiteral("flow_profile_minimum_pressure"));

    // Extract temperature presets.
    //
    // This one FABRICATES rather than falling back: an unparseable value would
    // append a 0 °C preset into the ladder regenerateSimpleFrames() builds
    // frames from, and because the list is then non-empty the
    // espresso_temperature backfill below never runs to correct it.
    profile.m_temperaturePresets.clear();
    for (int i = 0; i <= 3; i++) {
        const QString key = QString("espresso_temperature_%1").arg(i);
        val = extractValue(key);
        if (val.isEmpty())
            continue;
        bool ok = false;
        const double preset = val.toDouble(&ok);
        if (!ok) { recordMalformed(key, val); continue; }
        profile.m_temperaturePresets.append(preset);
    }
    if (profile.m_temperaturePresets.isEmpty()) {
        // de1app's value for a profile with no espresso_temperature_0..3 is
        // espresso_temperature, in all four slots — NOT a house preset ladder.
        // 7 of the 89 stock .tcl files omit these keys, and a hardcoded
        // {88, 90, 93, 96} brewed two of them 4-6 °C off de1app.
        profile.m_temperaturePresets = {profile.m_espressoTemperature, profile.m_espressoTemperature,
                                        profile.m_espressoTemperature, profile.m_espressoTemperature};
    }

    // Extract advanced_shot steps
    // Format: advanced_shot {{step1 props} {step2 props} ...}
    // A simple profile discards its stored advanced_shot below, so its frames
    // decide nothing and are not inspected at all. Scanning them would refuse a
    // profile — or log a warning about it — over an array de1app never reads.
    const bool simpleType = profile.m_profileType == QLatin1String("settings_2a")
                         || profile.m_profileType == QLatin1String("settings_2b");

    QRegularExpression shotRe("advanced_shot\\s+\\{(.*?)\\}\\s*$",
        QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch shotMatch = shotRe.match(content);

    if (shotMatch.hasMatch()) {
        QString stepsContent = shotMatch.captured(1);

        // Parse each step (nested braces)
        int depth = 0;
        int stepStart = -1;

        for (int i = 0; i < stepsContent.length(); i++) {
            QChar c = stepsContent[i];
            if (c == '{') {
                if (depth == 0) stepStart = i;
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0 && stepStart >= 0) {
                    QString stepStr = stepsContent.mid(stepStart, i - stepStart + 1);
                    // Same rule as the JSON path: a frame setting we do not
                    // model would be dropped here and never brewed, so refuse
                    // the profile rather than pour something else silently.
                    if (!simpleType) {
                        for (const QString& key : ProfileFrame::unknownTclKeys(stepStr)) {
                            if (!profile.m_unsupportedStepKeys.contains(key))
                                profile.m_unsupportedStepKeys << key;
                        }
                        // Same rule one level down: a known key whose VALUE we
                        // cannot read would become 0.0, and 0 is legal for every
                        // frame number, so the frame would run silently wrong.
                        for (const QString& bad : ProfileFrame::malformedTclValues(stepStr)) {
                            if (!profile.m_malformedValues.contains(bad))
                                profile.m_malformedValues << bad;
                            qWarning() << "Profile::loadFromTclString: cannot interpret frame value"
                                       << bad << "in profile" << profile.m_title;
                        }
                    }
                    ProfileFrame frame = ProfileFrame::fromTclList(stepStr);
                    if (!frame.name.isEmpty() || frame.seconds > 0) {
                        profile.m_steps.append(frame);
                    }
                    stepStart = -1;
                }
            }
        }
    }

    // Simple profiles (settings_2a = pressure, settings_2b = flow) derive their
    // frames from the scalars read above — ALWAYS, discarding any advanced_shot
    // the .tcl stored, because de1app discards it too. Its builders open with
    // `set temp_advanced(advanced_shot) {}` (profile.tcl:16, :212) and rebuild
    // the list from the scalars, so the stored array is dead data: a simple
    // .tcl can carry frames that contradict its own scalars and de1app runs the
    // scalars. Steam_only.tcl stores frames at 82/80/72 °C while
    // espresso_temperature is 0 — de1app brews the 0, and so must we.
    //
    // Frame synthesis is the ONLY thing conditional here; the scalars above are
    // read for every profile of every type. Conflating the two questions ("do I
    // need to synthesize frames?" and "should I read the scalars?") is what made
    // most de1app scalars unreadable on any profile carrying a stored array.
    if (profile.m_profileType == QLatin1String("settings_2a")
        || profile.m_profileType == QLatin1String("settings_2b")) {
        // Same generator the app runs at activation time — an imported profile
        // and a re-activated one must not produce different frames.
        profile.regenerateSimpleFrames();
        qDebug() << "Generated" << profile.m_steps.size() << "frames from simple"
                 << profile.m_profileType << "profile";
    }

    // Back-fill espresso temperature from the first frame — for ADVANCED
    // profiles only, where the frames are the source of truth and the scalar is
    // just a summary.
    //
    // Never for a simple profile: there the scalar generated the frames, so a 0
    // is the authored value rather than a gap. Doing it unconditionally is what
    // made Steam_only import at 82 °C (the first stored frame) when its
    // espresso_temperature says 0 and de1app brews 0.
    if (isAdvancedProfile && qFuzzyIsNull(profile.m_espressoTemperature)
        && !profile.m_steps.isEmpty()) {
        profile.m_espressoTemperature = profile.m_steps.first().temperature;
    }

    // Preinfuse frame count — de1app's NumberOfPreinfuseFrames, sent in the BLE
    // header, where it tells the firmware where preinfusion ends. It is stored
    // as "final_desired_shot_volume_advanced_count_start" (binary.tcl:990).
    //
    // For a SIMPLE profile the stored number is not the answer: de1app's
    // builders reset it to 0 and `incr` it once per preinfusion frame they
    // generate (profile.tcl:17/56/79 for settings_2a, :212/251/274 for 2b), so
    // the value follows the frames they just built. Four built-ins carry a
    // literal 0 while generating two preinfusion frames — taking the literal
    // would hand the DE1 a 0 and change the shot. regenerateSimpleFrames() has
    // already derived the right count, so leave it alone.
    //
    // Advanced profiles keep the stored value: de1app does not recompute it
    // there, and D-Flow/A-Flow depend on the authored number surviving.
    if (isAdvancedProfile) {
        // Garbled would read as 0, and 0 is a legal preinfuse count — so the
        // DE1 would get a wrong NumberOfPreinfuseFrames in its BLE header with
        // nothing to show for it.
        profile.m_preinfuseFrameCount = 0;
        readIntLike(QStringLiteral("final_desired_shot_volume_advanced_count_start"),
                    [&](int v) { profile.m_preinfuseFrameCount = v; });
    } else {
        // Derived from the frames, whether they were generated here or read
        // from a stored advanced_shot — de1app's count always describes the
        // frame list it is sent with.
        profile.m_preinfuseFrameCount = countPreinfuseFrames(profile.m_steps);
    }

    qDebug() << "Loaded Tcl profile:" << profile.m_title
             << "with" << profile.m_steps.size() << "steps";

    return profile;
}

void Profile::moveStep(int from, int to) {
    if (from < 0 || from >= m_steps.size() || to < 0 || to >= m_steps.size()) {
        qWarning() << "Cannot move step: invalid indices from" << from << "to" << to << "(size:" << m_steps.size() << ")";
        return;
    }
    m_steps.move(from, to);
}

bool Profile::isValid() const {
    // An unrecognised step key makes the profile invalid on purpose, so that
    // every existing import path refuses it without each one needing its own
    // check. We would execute such a frame without whatever the key asked for —
    // a different shot than the file describes, with nothing to show for it.
    return !m_steps.isEmpty() && m_steps.size() <= MAX_FRAMES
           && m_unsupportedStepKeys.isEmpty() && m_malformedValues.isEmpty();
}

bool Profile::functionallyEqual(const Profile& a, const Profile& b)
{
    if (a.steps().isEmpty() || b.steps().isEmpty())
        return false;

    // Profile-level limits (maximumPressure, maximumFlow, etc.) are intentionally
    // NOT compared here. The built-in JSONs may have stale/zero values for these
    // fields from older writes, while TCL files carry the original de1app defaults.
    // Import identity is determined by the extraction frame sequence, not global limits.
    //
    // preinfuseFrameCount IS compared because it is sent to the DE1 as
    // NumberOfPreinfuseFrames in the BLE header and directly affects machine behavior.
    if (a.preinfuseFrameCount() != b.preinfuseFrameCount()) return false;

    const auto& stepsA = a.steps();
    const auto& stepsB = b.steps();
    if (stepsA.size() != stepsB.size()) return false;

    for (qsizetype i = 0; i < stepsA.size(); i++) {
        const ProfileFrame& fa = stepsA[i];
        const ProfileFrame& fb = stepsB[i];

        if (qAbs(fa.temperature - fb.temperature) > 0.1) return false;
        if (fa.sensor     != fb.sensor)     return false;
        if (fa.pump       != fb.pump)       return false;
        if (fa.transition != fb.transition) return false;
        // Always compare the active axis. For the inactive axis, skip comparison
        // when either side is 0 — de1app TCL stores a default value (e.g. flow=2
        // on pressure frames) that our JSON writer omits (stored as 0). If both
        // sides are non-zero the value was explicitly set and must match.
        if (fa.pump == "pressure") {
            if (qAbs(fa.pressure - fb.pressure) > 0.1) return false;
            if (fa.flow > 0.1 && fb.flow > 0.1 && qAbs(fa.flow - fb.flow) > 0.1) return false;
        } else {
            if (qAbs(fa.flow - fb.flow) > 0.1) return false;
            if (fa.pressure > 0.1 && fb.pressure > 0.1 && qAbs(fa.pressure - fb.pressure) > 0.1) return false;
        }
        if (qAbs(fa.seconds  - fb.seconds)  > 0.1) return false;
        if (qAbs(fa.volume   - fb.volume)   > 0.1) return false;
        if (fa.popup != fb.popup) return false;

        if (fa.exitIf != fb.exitIf) return false;
        if (fa.exitIf) {
            if (fa.exitType != fb.exitType) return false;
            // Only compare the threshold field for the active exit type.
            // TCL sets exit_flow_over=6 as a safety cap on pressure frames; JSON only
            // serialises the relevant field — comparing all four causes false mismatches.
            if (fa.exitType == "pressure_over")  { if (qAbs(fa.exitPressureOver  - fb.exitPressureOver)  > 0.1) return false; }
            else if (fa.exitType == "pressure_under") { if (qAbs(fa.exitPressureUnder - fb.exitPressureUnder) > 0.1) return false; }
            else if (fa.exitType == "flow_over")  { if (qAbs(fa.exitFlowOver     - fb.exitFlowOver)     > 0.1) return false; }
            else if (fa.exitType == "flow_under") { if (qAbs(fa.exitFlowUnder    - fb.exitFlowUnder)    > 0.1) return false; }
        }

        if (qAbs(fa.exitWeight          - fb.exitWeight)          > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressure   - fb.maxFlowOrPressure)   > 0.1) return false;
        if (qAbs(fa.maxFlowOrPressureRange - fb.maxFlowOrPressureRange) > 0.1) return false;
    }

    return true;
}

QString Profile::frameDiffReport(const Profile& a, const Profile& b)
{
    QString report;

    // Header-level mismatches print even when one side has no frames at all —
    // otherwise a simple-profile diff renders as an empty body.
    if (a.steps().size() != b.steps().size())
        report += QString("  step count: A=%1 B=%2\n")
                      .arg(a.steps().size()).arg(b.steps().size());
    if (a.preinfuseFrameCount() != b.preinfuseFrameCount())
        report += QString("  preinfuseFrameCount: A=%1 B=%2\n")
                      .arg(a.preinfuseFrameCount()).arg(b.preinfuseFrameCount());

    const qsizetype n = qMin(a.steps().size(), b.steps().size());
    for (qsizetype i = 0; i < n; ++i) {
        const ProfileFrame& fa = a.steps()[i];
        const ProfileFrame& fb = b.steps()[i];
        const QString p = QString("  FRAME[%1] ").arg(i);

        auto chkS = [&](const QString& lbl, const QString& va, const QString& vb) {
            if (va != vb) report += p + lbl + ": A=" + va + " B=" + vb + "\n";
        };
        auto chkF = [&](const QString& lbl, double va, double vb) {
            if (qAbs(va - vb) > 0.1)
                report += p + lbl + ": A=" + QString::number(va) + " B=" + QString::number(vb) + "\n";
        };

        chkS("pump", fa.pump, fb.pump);
        chkS("sensor", fa.sensor, fb.sensor);
        chkS("transition", fa.transition, fb.transition);
        chkS("popup", fa.popup, fb.popup);
        if (fa.exitIf != fb.exitIf)
            report += p + "exitIf: A=" + QString::number(fa.exitIf) + " B=" + QString::number(fb.exitIf) + "\n";
        if (fa.exitIf) chkS("exitType", fa.exitType, fb.exitType);

        chkF("temperature", fa.temperature, fb.temperature);
        // Same asymmetry as functionallyEqual(): the inactive axis carries a
        // de1app default our writer omits, so it only counts when both sides set it.
        if (fa.pump == "pressure") {
            chkF("pressure", fa.pressure, fb.pressure);
            if (fa.flow > 0.1 && fb.flow > 0.1) chkF("flow", fa.flow, fb.flow);
        } else {
            chkF("flow", fa.flow, fb.flow);
            if (fa.pressure > 0.1 && fb.pressure > 0.1) chkF("pressure", fa.pressure, fb.pressure);
        }
        chkF("seconds", fa.seconds, fb.seconds);
        chkF("volume",  fa.volume,  fb.volume);

        // Only the active exit threshold; the other three are noise from de1app TCL.
        if (fa.exitIf) {
            if      (fa.exitType == "pressure_over")  chkF("exitPressureOver",  fa.exitPressureOver,  fb.exitPressureOver);
            else if (fa.exitType == "pressure_under") chkF("exitPressureUnder", fa.exitPressureUnder, fb.exitPressureUnder);
            else if (fa.exitType == "flow_over")      chkF("exitFlowOver",      fa.exitFlowOver,      fb.exitFlowOver);
            else if (fa.exitType == "flow_under")     chkF("exitFlowUnder",     fa.exitFlowUnder,     fb.exitFlowUnder);
        }

        chkF("exitWeight",             fa.exitWeight,             fb.exitWeight);
        chkF("maxFlowOrPressure",      fa.maxFlowOrPressure,      fb.maxFlowOrPressure);
        chkF("maxFlowOrPressureRange", fa.maxFlowOrPressureRange, fb.maxFlowOrPressureRange);
    }

    return report;
}

QString Profile::titleToFilename(const QString& title)
{
    // NFD decomposition splits an accented char into base + combining mark, so
    // stripping the marks leaves the base letter. This covers every accent,
    // where the explicit per-character table it replaced covered 22 of them.
    static const QRegularExpression reCombining(QStringLiteral("[\\x{0300}-\\x{036f}]"));
    QString decomposed = title.normalized(QString::NormalizationForm_D);
    decomposed.remove(reCombining);

    QString sanitized;
    sanitized.reserve(decomposed.size());
    for (const QChar& c : decomposed)
        sanitized += c.isLetterOrNumber() ? c.toLower() : QLatin1Char('_');

    while (sanitized.contains(QLatin1String("__")))
        sanitized.replace(QLatin1String("__"), QLatin1String("_"));
    while (sanitized.startsWith(QLatin1Char('_'))) sanitized.remove(0, 1);
    while (sanitized.endsWith(QLatin1Char('_')))   sanitized.chop(1);

    return sanitized;
}

QStringList Profile::validationErrors() const {
    QStringList errors;

    if (m_steps.isEmpty()) {
        errors << "Profile has no steps";
    }

    // Named explicitly, and phrased so the message is worth pasting into a bug
    // report: the key is the whole diagnosis, and a user who only sees "invalid
    // profile" has nothing to report and no reason to trust the refusal.
    if (!m_unsupportedStepKeys.isEmpty()) {
        errors << QStringLiteral(
                      "Profile uses step settings this version does not understand "
                      "(%1). It was not imported, because ignoring them would brew "
                      "a different shot than the profile describes. Please report this.")
                      .arg(m_unsupportedStepKeys.join(QStringLiteral(", ")));
    }

    if (!m_malformedValues.isEmpty()) {
        errors << QStringLiteral(
                      "Profile contains values this version cannot read (%1). It was not "
                      "imported, because guessing a number would brew a different shot "
                      "than the profile describes. A decimal comma (\"9,5\") is the "
                      "usual cause. Please report this.")
                      .arg(m_malformedValues.join(QStringLiteral(", ")));
    }

    if (m_steps.size() > MAX_FRAMES) {
        errors << QString("Profile has %1 steps, maximum is %2").arg(m_steps.size()).arg(MAX_FRAMES);
    }

    for (int i = 0; i < m_steps.size(); i++) {
        const auto& step = m_steps[i];
        if (step.seconds < 0) {
            errors << QString("Step %1 has negative duration").arg(i + 1);
        }
        if (step.temperature < 70 || step.temperature > 100) {
            errors << QString("Step %1 temperature out of range (70-100°C)").arg(i + 1);
        }
    }

    return errors;
}

QString Profile::describeFrames() const
{
    if (m_steps.isEmpty()) return QString();

    // Compact format: one dense line per frame to minimise AI token usage
    // while keeping diagnostically useful info (control mode, setpoint, temp, transitions, exits, limiters).
    QString result;
    QTextStream out(&result);
    out << "## Profile Recipe (" << m_steps.size() << " frames)\n\n";

    for (int i = 0; i < m_steps.size(); i++) {
        const auto& f = m_steps[i];
        bool isFlow = f.isFlowControl();

        out << (i + 1) << ". ";
        if (!f.name.isEmpty())
            out << f.name << " ";
        out << "(" << QString::number(f.seconds, 'f', 0) << "s) ";
        if (isFlow)
            out << "FLOW " << QString::number(f.flow, 'f', 1) << "ml/s";
        else
            out << "PRESSURE " << QString::number(f.pressure, 'f', 1) << "bar";
        out << " " << QString::number(f.temperature, 'f', 0) << "\u00B0C";

        // Smooth transition from previous frame — shows intended ramp direction.
        // Critical for lever/d-flow profiles where control mode switches (e.g. pressure→flow).
        if (i > 0 && f.transition == "smooth") {
            const auto& prev = m_steps[i - 1];
            bool prevIsFlow = prev.isFlowControl();
            if (prevIsFlow != isFlow) {
                // Control mode switch: show what we're transitioning from
                if (prevIsFlow)
                    out << " (from FLOW " << QString::number(prev.flow, 'f', 1) << "ml/s)";
                else
                    out << " (from PRESSURE " << QString::number(prev.pressure, 'f', 1) << "bar)";
            } else {
                // Same control mode but ramping value
                double prevVal = isFlow ? prev.flow : prev.pressure;
                double curVal = isFlow ? f.flow : f.pressure;
                if (std::abs(prevVal - curVal) > 0.1) {
                    out << " (ramp from " << QString::number(prevVal, 'f', 1) << ")";
                }
            }
        }

        // Exit conditions — compact
        if (f.exitIf) {
            if (f.exitType == "pressure_over" && f.exitPressureOver > 0)
                out << " exit:p>" << QString::number(f.exitPressureOver, 'f', 1);
            else if (f.exitType == "pressure_under" && f.exitPressureUnder > 0)
                out << " exit:p<" << QString::number(f.exitPressureUnder, 'f', 1);
            else if (f.exitType == "flow_over" && f.exitFlowOver > 0)
                out << " exit:f>" << QString::number(f.exitFlowOver, 'f', 1);
            else if (f.exitType == "flow_under" && f.exitFlowUnder > 0)
                out << " exit:f<" << QString::number(f.exitFlowUnder, 'f', 1);
        }
        if (f.exitWeight > 0)
            out << " exit:w" << QString::number(f.exitWeight, 'f', 1) << "g";

        // Limiter — just the value, skip range
        if (f.maxFlowOrPressure > 0)
            out << " lim:" << QString::number(f.maxFlowOrPressure, 'f', 1)
                << (isFlow ? "bar" : "ml/s");

        out << "\n";
    }

    return result;
}

QString Profile::describeFramesFromJson(const QString& json)
{
    if (json.isEmpty()) return QString();
    Profile p = Profile::loadFromJsonString(json);
    if (p.steps().isEmpty()) {
        // Distinguish between valid profile with no steps vs parse failure
        if (p.title().isEmpty()) {
            qWarning() << "Profile::describeFramesFromJson: Could not parse profile JSON";
            return QStringLiteral("(Profile recipe not available — stored profile data could not be parsed)\n");
        }
        return QString();
    }
    return p.describeFrames();
}

int Profile::countPreinfuseFrames(const QList<ProfileFrame>& steps) {
    int count = 0;
    for (const auto& step : steps) {
        if (step.exitIf) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

QByteArray Profile::toDirectControlFrame(int frameIndex, const ProfileFrame& frame) const {
    // Generate a single frame for direct control mode
    // Same format as toFrameBytes but for live updates

    QByteArray frameData(8, 0);
    frameData[0] = static_cast<char>(frameIndex);  // FrameToWrite
    frameData[1] = static_cast<char>(frame.computeFlags());  // Flag
    frameData[2] = BinaryCodec::encodeU8P4(frame.getSetVal());  // SetVal
    frameData[3] = BinaryCodec::encodeU8P1(frame.temperature);  // Temp
    frameData[4] = BinaryCodec::encodeF8_1_7(frame.seconds);  // FrameLen
    frameData[5] = BinaryCodec::encodeU8P4(frame.getTriggerVal());  // TriggerVal

    uint16_t maxVol = BinaryCodec::encodeU10P0(frame.volume);
    frameData[6] = static_cast<char>((maxVol >> 8) & 0xFF);
    frameData[7] = static_cast<char>(maxVol & 0xFF);

    return frameData;
}

QByteArray Profile::toHeaderBytes() const {
    // Profile header: 5 bytes
    // HeaderV (1), NumberOfFrames (1), NumberOfPreinfuseFrames (1),
    // MinimumPressure (U8P4, 1), MaximumFlow (U8P4, 1)

    QByteArray header(5, 0);
    header[0] = 1;  // HeaderV
    header[1] = static_cast<char>(m_steps.size());  // NumberOfFrames
    header[2] = static_cast<char>(m_preinfuseFrameCount);  // NumberOfPreinfuseFrames
    // De1app defaults to IgnoreLimit, so MinimumPressure and MaximumFlow are not
    // used as constraints. Hardcode to match de1app (binary.tcl:867-868).
    header[3] = 0;  // MinimumPressure
    header[4] = BinaryCodec::encodeU8P4(6.0);  // MaximumFlow

    return header;
}

QList<QByteArray> Profile::toFrameBytes() const {
    QList<QByteArray> frames;

    // Regular frames
    for (int i = 0; i < m_steps.size(); i++) {
        const ProfileFrame& step = m_steps[i];

        // Frame: 8 bytes
        // FrameToWrite (1), Flag (1), SetVal (U8P4, 1), Temp (U8P1, 1),
        // FrameLen (F8_1_7, 1), TriggerVal (U8P4, 1), MaxVol (U10P0, 2)

        QByteArray frame(8, 0);
        frame[0] = static_cast<char>(i);  // FrameToWrite
        frame[1] = static_cast<char>(step.computeFlags());  // Flag
        frame[2] = BinaryCodec::encodeU8P4(step.getSetVal());  // SetVal
        frame[3] = BinaryCodec::encodeU8P1(step.temperature);  // Temp
        frame[4] = BinaryCodec::encodeF8_1_7(step.seconds);  // FrameLen
        frame[5] = BinaryCodec::encodeU8P4(step.getTriggerVal());  // TriggerVal

        uint16_t maxVol = BinaryCodec::encodeU10P0(step.volume);
        frame[6] = static_cast<char>((maxVol >> 8) & 0xFF);
        frame[7] = static_cast<char>(maxVol & 0xFF);

        frames.append(frame);
    }

    // Extension frames (for max flow/pressure limits)
    for (int i = 0; i < m_steps.size(); i++) {
        const ProfileFrame& step = m_steps[i];

        if (step.maxFlowOrPressure > 0) {
            QByteArray extFrame(8, 0);
            extFrame[0] = static_cast<char>(i + 32);  // FrameToWrite + 32 for extension
            extFrame[1] = BinaryCodec::encodeU8P4(step.maxFlowOrPressure);
            extFrame[2] = BinaryCodec::encodeU8P4(step.maxFlowOrPressureRange);
            // Bytes 3-7 are padding (zeros)

            frames.append(extFrame);
        }
    }

    // Tail frame
    QByteArray tailFrame(8, 0);
    tailFrame[0] = static_cast<char>(m_steps.size());  // FrameToWrite = number of frames

    // MaxTotalVolume, sent as a bare 16-bit 0 — NOT through encodeU10P0, which
    // ORs in the bit-10 marker (0x0400). de1app sets `tail(MaxTotalVolume) 0`
    // literally (binary.tcl:1001) and packs the low ten bits, so it sends
    // 0x0000; Decenza sent 0x0400 on every profile (finding WIRE-1).
    //
    // Not cosmetic padding. de1app's own comment above that field reads "Unused.
    // Use highest bit to enable / disable preinfusion tracking" — so bit 10 is a
    // flag the firmware may act on, and Decenza was asserting it unconditionally
    // while de1app never does. The per-frame MaxVol keeps the marker; only the
    // tail differs, which is why it survived a field-level comparison.
    tailFrame[1] = 0;
    tailFrame[2] = 0;
    // Bytes 3-7 are padding (zeros)

    frames.append(tailFrame);

    return frames;
}

void Profile::regenerateSimpleFrames() {
    if (m_profileType != QLatin1String("settings_2a")
        && m_profileType != QLatin1String("settings_2b")) {
        qWarning() << "regenerateSimpleFrames called on non-simple profile type:" << m_profileType;
        return;
    }

    // Delegate to materializedSteps(), which owns the generation — including
    // de1app's temp-stepping rule. Clearing first is what makes it regenerate
    // rather than hand back what is already there.
    m_steps.clear();
    m_steps = materializedSteps();

    m_preinfuseFrameCount = countPreinfuseFrames(m_steps);

    // Do NOT sync m_espressoTemperature from first frame here.
    // The caller (applyRecipeToScalarFields) already set it from tempStart.
    // Syncing from the first frame is wrong when preinfusionTime=0 and
    // tempStepsEnabled=true — the first frame would be the hold frame at
    // temp2, not temp0.
}

// The plugins mutate frames IN PLACE — `array set filling [lindex ... 0]`,
// overwrite a named handful of fields, write the array back — so every field
// they do not name survives untouched. Decenza builds frames from constants, so
// each unnamed field is a candidate divergence: findings DF-1, DF-2, DF-5 and
// AF-6 are all one shape, "a field the plugin never writes, rewritten".
//
// This restores that semantics rather than patching the fields we happened to
// notice. It was previously a name-matched restore of volume and exitWeight only
// (issue #331), which fixed two of the four and left `filling(seconds)`,
// `filling(pressure)` and the rest to keep drifting.
//
// Roles are POSITIONAL, matching the plugins. Old and new frames are matched by
// role, not by index, so a legacy 6-frame profile being upgraded to 9 restores
// its Filling fields onto the new Filling frame rather than onto Pre Fill. A role
// with no old counterpart (the frames an upgrade inserts) keeps the generator's
// values, which are the plugin's own template literals.
void Profile::restoreFieldsThePluginNeverWrites(const QList<ProfileFrame>& oldSteps) {
    if (oldSteps.isEmpty() || m_steps.isEmpty()) return;

    enum Role { Filling, Soaking, RampUp, RampDown, PouringStart, Pouring, RoleCount };

    // set_profile_index (A_Flow/code.tcl:171-190) for A-Flow; D-Flow is always
    // the three frames its `prep` indexes directly. Returns -1 for a role the
    // layout does not have.
    const bool aflow = editorType() == QLatin1String("aflow");
    auto roleIndex = [aflow](qsizetype n, Role r) -> qsizetype {
        if (!aflow) {
            if (n < 3) return -1;
            switch (r) {
            case Filling: return 0;
            case Soaking: return 1;
            case Pouring: return 2;
            default:      return -1;   // D-Flow has no ramp or pouring-start frames
            }
        }
        const bool nine = n > 8;
        if (n < (nine ? 9 : 6)) return -1;
        switch (r) {
        case Filling:      return nine ? 1 : 0;
        case Soaking:      return nine ? 2 : 1;
        case RampUp:       return nine ? 5 : 2;
        case RampDown:     return nine ? 6 : 3;
        case PouringStart: return nine ? 7 : 4;
        case Pouring:      return nine ? 8 : 5;
        default:           return -1;
        }
    };

    // Exactly what each plugin's update_* proc assigns. Everything absent here is
    // restored from the old frame.
    //   D-Flow — plugin.tcl:338-353
    //   A-Flow — code.tcl:251-296
    struct Written {
        bool temperature = false, pressure = false, flow = false, seconds = false;
        bool volume = false, weight = false, limiter = false;
        bool exitPressureOver = false, exitFlowOver = false, exitFlowUnder = false;
    };
    auto written = [aflow](Role r) {
        Written w;
        switch (r) {
        case Filling:
            w.temperature = true;
            // D-Flow also DERIVES the fill pressure and its pressure-over exit
            // from the soak pressure; A-Flow writes neither.
            w.pressure = w.exitPressureOver = !aflow;
            break;
        case Soaking:
            w.temperature = w.pressure = w.seconds = w.volume = w.weight = true;
            break;
        case RampUp:
            w.temperature = w.pressure = w.seconds = w.exitFlowOver = true;
            break;
        case RampDown:
            w.temperature = w.seconds = w.exitFlowUnder = true;
            break;
        case PouringStart:
            // exit_type/exit_if are written too, but only on the short-ramp
            // branch; the generator sets them consistently with seconds, so they
            // travel with it.
            w.temperature = w.flow = w.seconds = w.exitFlowOver = true;
            break;
        case Pouring:
            w.temperature = w.flow = w.limiter = true;
            break;
        default:
            break;
        }
        return w;
    };

    for (int r = 0; r < RoleCount; ++r) {
        const qsizetype oldIdx = roleIndex(oldSteps.size(), Role(r));
        const qsizetype newIdx = roleIndex(m_steps.size(), Role(r));
        if (oldIdx < 0 || newIdx < 0) continue;

        const ProfileFrame& o = oldSteps[oldIdx];
        ProfileFrame& n = m_steps[newIdx];
        const Written w = written(Role(r));

        if (!w.temperature)      n.temperature = o.temperature;
        if (!w.pressure)         n.pressure = o.pressure;
        if (!w.flow)             n.flow = o.flow;
        if (!w.seconds)          n.seconds = o.seconds;
        if (!w.volume)           n.volume = o.volume;
        if (!w.weight)           n.exitWeight = o.exitWeight;
        if (!w.limiter)          n.maxFlowOrPressure = o.maxFlowOrPressure;
        if (!w.exitPressureOver) n.exitPressureOver = o.exitPressureOver;
        if (!w.exitFlowOver)     n.exitFlowOver = o.exitFlowOver;
        if (!w.exitFlowUnder)    n.exitFlowUnder = o.exitFlowUnder;
    }
}

void Profile::regenerateFromRecipe() {
    if (editorType() == QLatin1String("advanced")) {
        return;
    }

    // Never regenerate from parameters nobody established. RecipeParams' defaults
    // are live values rather than sentinels, so a default-constructed struct
    // generates a complete, plausible-looking profile that brews something else
    // entirely — the expensive failure. Keeping the frames and saying so is the
    // correct outcome (REC-1; design D7).
    if (!m_hasRecipeParams) {
        qWarning() << "regenerateFromRecipe: no established recipe parameters for" << m_title
                   << "— keeping its frames rather than generating from defaults";
        return;
    }

    // Save old frames so we can preserve passthrough fields after regeneration
    QList<ProfileFrame> oldSteps = m_steps;

    // Regenerate frames from recipe parameters
    m_steps = RecipeGenerator::generateFrames(m_recipeParams);

    if (m_steps.size() == 1 && m_steps[0].name == "empty") {
        qWarning() << "regenerateFromRecipe: recipe produced fallback empty frame"
                   << "- check recipe parameters for" << m_title;
    }

    restoreFieldsThePluginNeverWrites(oldSteps);

    // Update profile metadata from recipe
    m_targetWeight = m_recipeParams.targetWeight;
    m_targetVolume = m_recipeParams.targetVolume;
    // Use first frame temperature (matches de1app behavior)
    if (!m_steps.isEmpty()) {
        m_espressoTemperature = m_steps.first().temperature;
    }

    // De1app recomputes preinfuseFrameCount for simple profiles (pressure_to_advanced_list,
    // flow_to_advanced_list rebuild frames and count each time) but preserves it for advanced
    // profiles (settings_to_advanced_list copies as-is). D-Flow/A-Flow are advanced profiles.
    if (m_recipeParams.editorType == EditorType::Pressure
        || m_recipeParams.editorType == EditorType::Flow) {
        m_preinfuseFrameCount = countPreinfuseFrames(m_steps);
    }
}
