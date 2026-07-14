#pragma once

#include "dialing_helpers.h"  // buildBeanFreshness — composed inside buildCurrentBeanBlock
#include "aiconversation.h"   // HistoricalAssistantTurn — input to buildRecentAdviceBlock
#include "../core/basketaliases.h"  // basket spec derivation inside buildCurrentBeanBlock
#include "../core/puckprep.h"       // puck-prep flags + distribution inside buildCurrentBeanBlock
#include "../history/shotprojection.h"  // source type for beanInputsFromProjection (inline)

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>

class QSqlDatabase;
class Settings;
class ProfileManager;

// Shared block builders for the dialing-context payload. Both
// `dialing_get_context` (MCP tool) and the in-app advisor's user-prompt
// enrichment path call these helpers, so the shape stays in one place
// and the two surfaces cannot drift. See openspec change
// `add-dialing-blocks-to-advisor`.
//
// Threading:
//   - `buildDialInSessionsBlock`, `buildBestRecentShotBlock`, and
//     `buildGrinderContextBlock` accept a `QSqlDatabase&` and run SQL on
//     the caller's thread. Callers MUST own a background-thread DB
//     connection (see `withTempDb` in `core/dbutils.h`).
//   - `buildSawPredictionBlock` is main-thread only — it dereferences
//     `Settings*` (calibration sub-object) and `ProfileManager*` which
//     are not safe to touch from a worker thread.
//
// Empty-return contract: each builder returns an empty `QJsonObject`
// (or empty `QJsonArray`) when its preconditions don't hold, so callers
// can suppress the corresponding key entirely (no `null` placeholders).

namespace DialingBlocks {

// Window for the "best recent shot on this profile" anchor. Bounded
// so the anchor reflects the user's *current* setup era — same grinder
// family, same beans family, same recent preferences. An all-time-best
// from years ago runs on different beans, possibly worn burrs, and the
// parameters don't transfer.
constexpr qint64 kBestRecentShotWindowDays = 90;

// Issue #1158: derive the pour's control mode from the profile recipe.
// The profile JSON's `steps` array holds the frames; the dominant
// extraction frame (the one with the largest `seconds`) is the pour,
// and its `pump` field ("flow" / "pressure") is what the user targets
// during it. We read this from the recipe — NOT the runtime phase
// markers — because the markers are recorded frame transitions
// (merged/truncated at runtime) and do not reliably identify the flow
// pour: an earlier phase-marker heuristic returned "pressure" for a
// flow-controlled D-Flow / Q pour (issue #1147 follow-up). profileJson
// is available on every path that emits a shot — the dialInSessions
// loader (`loadRecentShotsByKbIdStatic` selects `profile_json`) and the
// full bestRecentShot load both carry it.
//
// Returns "flow" / "pressure", or "" when profileJson is empty,
// unparseable, has no steps, or the chosen frame has no `pump` (de1app
// / visualizer imports may lack a usable recipe) so callers keep the
// field sparse like the rest of the per-shot envelope. Sparse-omit is
// deliberate: a confidently-wrong value is worse than an absent one.
//
// This is the structured signal that lets the LLM apply the
// stop-at-weight rule in ShotSummarizer::shotAnalysisSystemPrompt(): a
// flow-controlled pour that stops at a weight target pins yield and
// makes duration ≈ stopWeight ÷ flowTarget, so neither is grind
// feedback. Without it the model only sees yieldG / durationSec on
// historical shots and reads them as dial-in outcomes (issue #1147).
//
// Defined inline (same rationale as buildCurrentBeanBlock) so test
// binaries can exercise the derivation without linking the DB-dependent
// block builders.
inline QString pourControlFromProfileJson(const QString& profileJson)
{
    if (profileJson.isEmpty()) return QString();
    QJsonParseError err{};
    const QJsonObject obj =
        QJsonDocument::fromJson(profileJson.toUtf8(), &err).object();
    if (err.error != QJsonParseError::NoError) return QString();
    const QJsonArray steps = obj.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty()) return QString();

    // Pick the longest frame — the dominant extraction (pour) frame —
    // rather than the last frame, so a short trailing decline/ending
    // frame can't flip the classification.
    QString pump;
    double maxSeconds = -1.0;
    for (const QJsonValue& sv : steps) {
        const QJsonObject step = sv.toObject();
        const QJsonValue secVal = step.value(QStringLiteral("seconds"));
        const double sec = secVal.isString()
            ? secVal.toString().toDouble()
            : secVal.toDouble();
        if (sec > maxSeconds) {
            maxSeconds = sec;
            pump = step.value(QStringLiteral("pump")).toString();
        }
    }
    if (pump.isEmpty()) return QString();
    return pump == QStringLiteral("flow")
        ? QStringLiteral("flow") : QStringLiteral("pressure");
}

// Issue #1158: append the stop-at-weight clarification to a rendered
// recipe string. The frame durations in the recipe are maximums; under
// stop-at-weight the shot truncates when the scale hits the target,
// typically long before those frame times elapse (e.g. a
// "Pouring (127s)" frame that really runs ~25s). Stating this inline,
// next to the frames where the misread happens, stops the model
// reading the frame ceiling as the intended shot length (issue #1147,
// "the concern is duration — you're pulling in 32–34s").
//
// Both *structured profile-block* recipe-assembly sites call this —
// `dialing_get_context`'s profile block (mcptools_dialing.cpp) and the
// in-app advisor's ShotSummarizer::buildCurrentProfileBlock — so those
// two surfaces cannot drift. NOTE: this does not cover every place a
// recipe is rendered: the prose multi-shot *history* blocks
// (AIManager / ShotSummarizer history context) call
// `Profile::describeFramesFromJson` directly without this note. That is
// intentional — those blocks explicitly tell the model not to comment
// on frame-level recipe detail, so the stop-at-weight clarification is
// only needed where the recipe is presented as the current shot's spec.
// No-op when the recipe is empty or no target weight is set; phrased
// conditionally so it stays correct even if targetWeightG is a
// volume/timer fallback rather than a real SAW target. Callers must
// pass the target weight from the SAME source as the recipe string
// (the analyzed shot), or the two structured surfaces will diverge.
inline QString withStopAtWeightNote(QString recipe, double targetWeightG)
{
    if (recipe.isEmpty() || targetWeightG <= 0) return recipe;
    recipe += QStringLiteral(
        "\nStop-at-weight: if a target weight is set (see targetWeightG), "
        "the shot ends when the scale reaches it — usually well before "
        "the frame durations above elapse — so the actual shot time and "
        "final yield follow the weight cutoff, not the frame timers.\n");
    return recipe;
}

// Dial-in history grouped into sessions (runs of shots on the same
// profile within ~60 minutes of each other). Returns `[]`-shaped
// QJsonArray; the array is empty when the profile has no prior shots.
QJsonArray buildDialInSessionsBlock(QSqlDatabase& db,
                                    const QString& profileKbId,
                                    qint64 resolvedShotId,
                                    int historyLimit);

// Highest-rated past shot on the same profile within the last
// `kBestRecentShotWindowDays`, with a `changeFromBest` diff against
// `currentShot`. Returns an empty `QJsonObject` (caller suppresses the
// assignment) when no rated shot exists in the window or when
// `profileKbId` is empty.
QJsonObject buildBestRecentShotBlock(QSqlDatabase& db,
                                     const QString& profileKbId,
                                     qint64 resolvedShotId,
                                     const ShotProjection& currentShot);

// Observed grinder settings range, step size, burr-swappable flag, with
// bean-scoped → cross-bean fallback. Returns an empty `QJsonObject`
// when `grinderModel` is empty OR when both queries return no rows.
QJsonObject buildGrinderContextBlock(QSqlDatabase& db,
                                     const QString& grinderModel,
                                     const QString& beverageType,
                                     const QString& beanBrand);

// `currentBean` block for the resolved shot. Bean / grinder / dose /
// roastDate fields come from the shot's saved metadata only — never
// from `Settings::dye()` or any other live-machine-state source. Both
// `dialing_get_context.currentBean` and the in-app advisor's user-prompt
// `currentBean` build through this helper so the two surfaces produce
// byte-equivalent JSON for the same shot. Composes
// `currentBean.beanFreshness` from the shot's `roastDate` via
// `DialingHelpers::buildBeanFreshness`. Pure: no Qt object
// dependencies beyond the projection and JSON value types.
//
// Inputs that capture every field this block reads from the shot. Kept
// as a small struct (not the full `ShotProjection`) so the in-app
// advisor — which carries a `ShotSummary`, not a `ShotProjection` —
// can call the helper without round-tripping through the heavyweight
// projection type.
struct CurrentBeanBlockInputs {
    QString beanBrand;
    QString beanType;
    QString roastLevel;
    QString roastDate;
    // Bean storage lifecycle (bean-bag-inventory), snapshotted at shot time.
    // When either is set, buildBeanFreshness reports storage as KNOWN and ages
    // the beans from the thaw date instead of asking the user. Empty = no
    // freeze history recorded for this shot.
    QString frozenDate;
    QString defrostDate;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    // Basket identity (resolved via the shot's equipment_id; empty = no basket).
    // Specs (wall/flow/precision/dose range) are DERIVED here from BasketAliases,
    // not passed in — the caller supplies identity only (add-basket-equipment).
    QString basketBrand;
    QString basketModel;
    // Puck-prep canonical flag string (resolved via the shot's equipment_id; empty
    // = no puck prep). The individual flags + the derived `distribution` rollup are
    // computed here from PuckPrep, not passed in (add-puckprep-equipment).
    QString puckPrep;
    QString grinderSetting;
    // Grinder RPM the shot was ground at (0 = unset / not an adjustable-RPM
    // grinder). A second grind axis alongside grinderSetting on variable-RPM
    // grinders — surfaced so the advisor can qualify numeric recommendations.
    int rpm = 0;
    double doseWeightG = 0;
    // Compact-JSON linked-bean snapshot ("" = unlinked, Visualizer canonical
    // or Bean Base sourced). Parsed into a
    // `beanBase` sub-object so the advisor sees structured origin/variety/
    // process/tasting data instead of just free-text brand+name strings.
    QString beanBaseJson;
};

// Map a persisted shot record to the currentBean inputs. THE single source of
// truth for that mapping: both `dialing_get_context` (MCP) and the advisor's
// `summarizeFromHistory` path build `CurrentBeanBlockInputs` from a
// ShotProjection through here, so the two surfaces cannot drift — a field
// added to currentBean is wired in exactly one place. Inline for the same
// reason as buildCurrentBeanBlock below (test binaries that link only
// shotsummarizer.cpp pick it up without dialing_blocks.cpp).
inline CurrentBeanBlockInputs beanInputsFromProjection(const ShotProjection& sd)
{
    CurrentBeanBlockInputs in;
    in.beanBrand = sd.beanBrand;
    in.beanType = sd.beanType;
    in.roastLevel = sd.roastLevel;
    in.roastDate = sd.roastDate;
    in.frozenDate = sd.frozenDate;
    in.defrostDate = sd.defrostDate;
    in.grinderBrand = sd.grinderBrand;
    in.grinderModel = sd.grinderModel;
    in.grinderBurrs = sd.grinderBurrs;
    in.basketBrand = sd.basketBrand;
    in.basketModel = sd.basketModel;
    in.puckPrep = sd.puckPrep;
    in.grinderSetting = sd.grinderSetting;
    in.rpm = static_cast<int>(sd.rpm);
    in.doseWeightG = sd.doseWeightG;
    in.beanBaseJson = sd.beanBaseJson;
    return in;
}

// Defined inline so test binaries that link only `shotsummarizer.cpp`
// (and not the rest of `dialing_blocks.cpp`'s DB-dependent
// builders) can pick up this single helper without pulling in
// `loadShotRecordStatic` / `loadRecentShotsByKbIdStatic` symbols.
inline QJsonObject buildCurrentBeanBlock(const CurrentBeanBlockInputs& in)
{
    QJsonObject bean;
    bean["brand"] = in.beanBrand;
    bean["type"] = in.beanType;
    bean["roastLevel"] = in.roastLevel;
    bean["grinderBrand"] = in.grinderBrand;
    bean["grinderModel"] = in.grinderModel;
    bean["grinderBurrs"] = in.grinderBurrs;
    bean["grinderSetting"] = in.grinderSetting;
    // Only emit rpm when set, so non-adjustable grinders don't carry a noisy 0.
    if (in.rpm > 0)
        bean["rpm"] = in.rpm;
    bean["doseWeightG"] = in.doseWeightG;

    // Basket sub-object (add-basket-equipment). Identity always when present;
    // registry-derived specs only when the basket matches BasketAliases (a custom
    // off-registry basket carries identity alone). relativeFlow is the key
    // cross-basket signal — a directional word, not an ordered scale — and the
    // dose range lets the advisor flag a dose outside the basket's rating.
    if (!in.basketBrand.isEmpty() || !in.basketModel.isEmpty()) {
        QJsonObject basket;
        basket["brand"] = in.basketBrand;
        basket["model"] = in.basketModel;
        if (const BasketAliases::BasketEntry* e =
                BasketAliases::findEntry(in.basketBrand, in.basketModel)) {
            basket["wallProfile"] = BasketAliases::wallProfileName(e->wall);
            basket["relativeFlow"] = BasketAliases::flowRateName(e->flow);
            basket["precision"] = e->precision;
            if (e->doseMaxG > 0) {
                QJsonObject dose;
                dose["min"] = e->doseMinG;
                dose["max"] = e->doseMaxG;
                basket["doseRangeG"] = dose;
            }
        }
        bean["basket"] = basket;
    }

    // Puck-prep sub-object (add-puckprep-equipment). The set flags plus the derived
    // `distribution` rollup — the signal the advisor branches its channeling
    // guidance on ("none/light → fix prep" vs "thorough → grind/dose"). Omitted
    // when the package has no puck prep.
    if (!in.puckPrep.isEmpty()) {
        QJsonObject puck;
        for (const QString& k : PuckPrep::flagKeys())
            puck[k] = PuckPrep::has(in.puckPrep, k);
        puck["distribution"] = PuckPrep::distribution(in.puckPrep);
        bean["puckPrep"] = puck;
    }

    const QJsonObject freshness = DialingHelpers::buildBeanFreshness(
        in.roastDate, in.frozenDate, in.defrostDate);
    if (!freshness.isEmpty())
        bean["beanFreshness"] = freshness;

    // Canonical Bean Base attributes (when the bean was linked): everything
    // the advisor can reason from. roasterTastingNotes is the headline — the
    // roaster's flavor EXPECTATIONS, which the AI compares against the
    // user's actual tasting feedback to pick an extraction direction.
    // Terroir/processing fields inform grind/temp priors (washed vs natural,
    // high-grown dense beans); roastedFor flags filter roasts pulled as
    // espresso. Keys follow MCP conventions (units/meaning in names).
    if (!in.beanBaseJson.isEmpty()) {
        const QJsonObject src = QJsonDocument::fromJson(in.beanBaseJson.toUtf8()).object();
        QJsonObject bb;
        auto put = [&](const char* outKey, const char* srcKey) {
            const QString v = src.value(QLatin1String(srcKey)).toString();
            if (!v.isEmpty()) bb[QLatin1String(outKey)] = v;
        };
        put("roasterTastingNotes", "tastingNotes");  // e.g. "Orange, Honeycomb, Cane Sugar"
        put("description", "description");
        put("origin", "origin");
        put("region", "region");
        put("producer", "producer");
        put("variety", "variety");
        put("process", "process");
        put("roastLevel", "degree");          // Bean Base's richer string, e.g. "Light To Medium-light"
        put("roastedFor", "beanType");        // "Espresso" | "Filter" | "Omni"
        put("harvest", "harvest");
        // Canonical-sourced blobs carry a single `elevation` display string;
        // Bean Base-sourced blobs carry numeric min/max. Pass through both.
        put("elevation", "elevation");
        const int loM = src.value(QLatin1String("minElevationM")).toInt();
        const int hiM = src.value(QLatin1String("maxElevationM")).toInt();
        if (loM > 0) bb["minElevationM"] = loM;
        if (hiM > 0) bb["maxElevationM"] = hiM;
        if (!bb.isEmpty())
            bean["beanBase"] = bb;
    }

    return bean;
}

// Per-user cross-profile grinder calibration block. Rewritten for issue
// #1223 (openspec `fix-grinder-calibration-cross-profile`). Model:
//   grind(profile, coffeeBatch) ≈ batchBaseline + UGS·conversionKey
// `conversionKey` is mined from WITHIN-ROAST-BATCH paired slopes (same
// bean batch, two profiles — cancels the dominant per-batch baseline that
// made the old pooled all-coffee slope wrong-signed), gated dimensionlessly
// (IQR ≤ ratio·|key|, grinder-portable), and is a per-grinder runtime
// value — never a shipped constant. Numbers are anchored on the most recent
// dialed-in shot of the CURRENT roast batch and never extrapolated past a
// hard UGS cap. With no usable signal the block is *directional*: each
// profile carries only finer/coarser from KB UGS ordering vs the current
// profile (anchor-free, grinder-convention-free) — never a fabricated
// number. `confidence` is `"approximate"` (numeric, gated) or
// `"directional"`. `resolvedShotId` IS used — it supplies the current
// roast batch and current-profile UGS.
//
// Returns an empty `QJsonObject` (caller suppresses the key) when:
//   - `grinderModel` is empty, OR
//   - `beverageType` is filter / pourover, OR
//   - the resolved shot is invalid, OR
//   - there are no dialed-in shots on this grinder + burrs.
// Otherwise the block is always present (directional at minimum).
//
// Background-thread / DB-owning: must be called from the same thread that
// owns `db` (same tier as `buildGrinderContextBlock`).
QJsonObject buildGrinderCalibrationBlock(QSqlDatabase& db,
                                         const QString& grinderModel,
                                         const QString& grinderBurrs,
                                         const QString& beverageType,
                                         qint64 resolvedShotId);

// SAW (Stop-at-Weight) prediction for the resolved shot. Returns an
// empty `QJsonObject` when any of the following hold:
//   - the shot is not espresso, OR
//   - the shot lacks usable flow samples in the last 2 seconds, OR
//   - either `settings` or `profileManager` is null, OR
//   - no scale is configured (`Settings::scaleType()` empty), OR
//   - no profile is configured (`ProfileManager::baseProfileName()` empty).
//
// Gates fire in that order — pure-shot gates first, pointer guards next,
// then the Settings/ProfileManager-dependent gates. That ordering is
// deliberate so coverage tests can exercise each gate in isolation.
//
// Main-thread only — touches `settings->calibration()` and
// `profileManager` which are not thread-safe.
QJsonObject buildSawPredictionBlock(Settings* settings,
                                    ProfileManager* profileManager,
                                    const ShotProjection& currentShot);

// Predicted parameter changes + expected ranges out of one `structuredNext`
// block (see ShotSummarizer's "Response Format" schema), as short
// human-readable fragments. Shared by `buildRecentAdviceBlock`'s one-line
// recommendation fallback (dialing_blocks.cpp, used when the model omitted
// `reasoning`) and the in-app advisor's `## Recent Advice Tracking`
// renderer (AIManager::emitRecentShotContext) — both describe the same
// structuredNext shape and must not drift into two independent field lists
// (the in-app `recentAdvice` wiring itself went stale exactly this way).
// Defined inline for the same cross-binary-reuse reason as the other
// helpers in this header.
struct StructuredNextSummary {
    QStringList predictedParts;  // "grinder 4.75", "dose 18.0g", "profile X"
    QStringList expectedParts;   // "32-38s", "1.0-1.5 ml/s", "6.0-9.0 bar"
};

inline StructuredNextSummary summarizeStructuredNext(const QJsonObject& sn)
{
    StructuredNextSummary out;
    if (sn.contains(QStringLiteral("grinderSetting")))
        out.predictedParts << QStringLiteral("grinder %1").arg(sn.value("grinderSetting").toString());
    if (sn.contains(QStringLiteral("doseG")))
        out.predictedParts << QStringLiteral("dose %1g").arg(sn.value("doseG").toDouble(), 0, 'f', 1);
    if (sn.contains(QStringLiteral("profileTitle")))
        out.predictedParts << QStringLiteral("profile %1").arg(sn.value("profileTitle").toString());

    const QJsonArray dur = sn.value(QStringLiteral("expectedDurationSec")).toArray();
    const QJsonArray flow = sn.value(QStringLiteral("expectedFlowMlPerSec")).toArray();
    const QJsonArray pressure = sn.value(QStringLiteral("expectedPeakPressureBar")).toArray();
    if (dur.size() == 2)
        out.expectedParts << QStringLiteral("%1-%2s")
            .arg(dur.at(0).toDouble(), 0, 'f', 0).arg(dur.at(1).toDouble(), 0, 'f', 0);
    if (flow.size() == 2)
        out.expectedParts << QStringLiteral("%1-%2 ml/s")
            .arg(flow.at(0).toDouble(), 0, 'f', 1).arg(flow.at(1).toDouble(), 0, 'f', 1);
    if (pressure.size() == 2)
        out.expectedParts << QStringLiteral("%1-%2 bar")
            .arg(pressure.at(0).toDouble(), 0, 'f', 1).arg(pressure.at(1).toDouble(), 0, 'f', 1);
    return out;
}

// Inputs for the closed-loop coaching `recentAdvice` block (issue #1053).
// The caller pulls qualifying assistant turns from the active conversation
// (`AIConversation::recentAssistantTurns(max)` for the in-app advisor, or
// `AIConversation::loadRecentAssistantTurnsForKey(...)` for the MCP path)
// and passes them in along with the resolved current shot's profile_kb_id
// and id. The builder runs SQL on the caller's thread.
struct RecentAdviceInputs {
    QList<AIConversation::HistoricalAssistantTurn> turns;  // most-recent-first; caller-capped
    QString currentProfileKbId;
    qint64 currentShotId = 0;  // excluded from follow-up lookup
};

// Build the recentAdvice array. For each input turn (in order) tries to
// pair it with the user's actual follow-up shot on the same profile and
// computes adherence + outcomeInPredictedRange + outcomeRating0to100 attribution.
// Turns that don't qualify (cross-profile, no follow-up shot yet) are
// skipped without consuming a turnsAgo slot. Returns an empty array when
// no entries qualify; caller MUST suppress the `recentAdvice` key in
// that case (no `recentAdvice: []` placeholders).
QJsonArray buildRecentAdviceBlock(QSqlDatabase& db,
                                  const RecentAdviceInputs& in);

} // namespace DialingBlocks
