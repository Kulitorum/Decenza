#pragma once

#include "dialing_helpers.h"  // buildBeanFreshness — composed inside buildCurrentBeanBlock
#include "aiconversation.h"   // HistoricalAssistantTurn — input to buildRecentAdviceBlock

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>

class QSqlDatabase;
class Settings;
class ProfileManager;
class ShotProjection;

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
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    double doseWeightG = 0;
};

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
    bean["doseWeightG"] = in.doseWeightG;

    const QJsonObject freshness = DialingHelpers::buildBeanFreshness(in.roastDate);
    if (!freshness.isEmpty())
        bean["beanFreshness"] = freshness;

    return bean;
}

// Per-user grinder calibration block. Derives a conversion key (settings per
// UGS unit) from the user's all-time shot history on the same grinder model +
// burrs and uses it to compute a Relative Grind Setting (RGS) for every
// profile in the knowledge base that carries a UGS value. All-time (no window)
// because the conversion key is a physical property of the grinder+burrs pair.
//
// Returns an empty `QJsonObject` (caller suppresses the key) when:
//   - `grinderModel` is empty, OR
//   - `beverageType` is filter / pourover, OR
//   - fewer than 2 qualifying profiles with a usable UGS (canonical preferred;
//     inferred used as fallback when canonical-only pair is degenerate), OR
//   - no non-degenerate anchor pair exists (setting difference < 0.5).
//
// `resolvedShotId` is accepted for API symmetry with other block builders but
// is not used — the all-time query is intentional (see proposal.md).
//
// Background-thread / DB-owning: must be called from the same thread that
// owns `db` (same tier as `buildGrinderContextBlock`).
QJsonObject buildGrinderCalibrationBlock(QSqlDatabase& db,
                                         const QString& grinderModel,
                                         const QString& grinderBurrs,
                                         const QString& beverageType,
                                         qint64 resolvedShotId  /* unused — see comment */);

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
