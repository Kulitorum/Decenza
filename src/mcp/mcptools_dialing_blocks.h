#pragma once

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

namespace McpDialingBlocks {

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

// SAW (Stop-at-Weight) prediction for the resolved shot. Returns an
// empty `QJsonObject` when:
//   - the shot is not espresso, OR
//   - no scale is configured (`Settings::scaleType()` empty), OR
//   - no profile is configured (`ProfileManager::baseProfileName()` empty), OR
//   - the shot lacks usable flow samples in the last 2 seconds.
//
// Main-thread only — touches `settings->calibration()` and
// `profileManager` which are not thread-safe.
QJsonObject buildSawPredictionBlock(Settings* settings,
                                    ProfileManager* profileManager,
                                    const ShotProjection& currentShot);

} // namespace McpDialingBlocks
