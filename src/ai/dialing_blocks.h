#pragma once

#include "dialing_helpers.h"  // buildBeanFreshness — composed inside buildCurrentBeanBlock

#include <QJsonArray>
#include <QJsonObject>
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

} // namespace DialingBlocks
