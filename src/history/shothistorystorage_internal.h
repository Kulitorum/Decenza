#pragma once

// Internal helpers shared between the ShotHistoryStorage translation units
// (today: shothistorystorage.cpp + shothistorystorage_serialize.cpp; the
// queries split planned in openspec/changes/split-shothistorystorage-by-concern/
// will add a third TU). NOT part of the public API — do not include from
// outside src/history/.

#include <QString>
#include <QStringList>

namespace decenza::storage::detail {

// Parsed metadata about the configured frames in a profile JSON blob.
// Used to populate `firstFrameSeconds` and `expectedFrameCount` arguments
// to ShotAnalysis::analyzeShot. Defaults (frameCount=-1, firstFrameSeconds=-1.0)
// signal "unknown" so analyzeShot's skip-first-frame detection falls back
// to its hard 2 s window.
struct ProfileFrameInfo {
    int frameCount = -1;
    double firstFrameSeconds = -1.0;
};

ProfileFrameInfo profileFrameInfoFromJson(const QString& profileJson);

// Bundle of every helper-derived input ShotAnalysis::analyzeShot needs that
// isn't already on the ShotRecord/ShotSaveData. Single source of truth so
// the three storage-layer call sites (saveShot, loadShotRecordStatic,
// convertShotRecord) prepare analyzeShot arguments identically.
//
// A future addition to analyzeShot's required helper-derived inputs (e.g.
// a new analysisFlags entry, a new firstFrameSeconds/frameCount sibling)
// is a one-place change here and a one-line update at each call site —
// instead of three inline preparation blocks that have to stay in sync
// by hand.
struct AnalysisInputs {
    QStringList analysisFlags;
    double firstFrameSeconds = -1.0;
    int frameCount = -1;
};

AnalysisInputs prepareAnalysisInputs(const QString& profileKbId,
                                     const QString& profileJson);

// True when the OS reports a 12-hour locale (e.g. "h:mm AP" rather than
// "HH:mm"). Cached after the first call so we don't re-walk QLocale on every
// row. Used by the date-formatting code that emits `dateTime` strings to
// QML across the storage TUs (currently three call sites; grep
// `use12h\\b` to find them).
bool use12h();

} // namespace decenza::storage::detail
