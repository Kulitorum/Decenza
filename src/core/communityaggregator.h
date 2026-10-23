#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Bean Search — Phase 2. Pure, networkless summariser over community shots for
// one bean. The Phase-4 network client (CommunitySettingsClient) hands this the
// rows it parsed from GET /api/shots?community=1&full=1 and renders the returned summary;
// keeping all ranking/statistics here (Qt-core only, no Q_OBJECT, no QNetwork)
// is what makes the algorithm unit-testable in isolation. Temperature is NOT a
// shot column — Phase 4 reads it from the top shot's profile, never here.
namespace CommunityAggregator {

struct Params {
    QString myGrinder;         // user's grinder_model; empty or unmatched -> grind hidden
    qint64  nowEpochSec = 0;   // injected clock (pure core takes no QDate::currentDate())
    int     minEnjoyment = 0;  // ranking floor, 0..100; filters RANKING input, NOT the fallback shot list
};

// shots: QVariantList of QVariantMap rows keyed by the Phase-3 endpoint attributes
//   (bean_weight, drink_weight, duration, espresso_enjoyment, grinder_model,
//    grinder_setting, profile_title, user_id, start_time[epoch sec][, id]).
// Each numeric field may arrive as a JSON number (QVariant double) OR as free
// text ("18.5g", "18,5") — the cleaner handles both.
//
// Returns a summary QVariantMap. EVERY key below is always present, even on empty
// input, so QML bindings never hit an undefined key:
//   shotCount, userCount, ratedShotCount (int)   -- over the kept window
//   hasSuggestion (bool)                          -- true iff >=3 distinct users and >=1 ranked profile
//   isAllTime (bool)                              -- true when the 12-month window was empty and we fell back
//   profiles (QVariantList of QVariantMap)        -- ranked best-first; see communityaggregator.cpp for row keys
//   shots (QVariantList of QVariantMap)           -- normalised kept rows (parsed doubles + family key), capped
QVariantMap summarize(const QVariantList& shots, const Params& params);

} // namespace CommunityAggregator
