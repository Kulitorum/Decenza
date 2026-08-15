#pragma once

// Internal helpers shared between the three ShotHistoryStorage translation
// units: shothistorystorage.cpp, shothistorystorage_serialize.cpp, and
// shothistorystorage_queries.cpp. NOT part of the public API — do not
// include from outside src/history/.

#include <QString>
#include <QStringList>

#include "../ai/shotanalysis.h"  // ShotAnalysis::ExpertBand
#include "../profile/profile.h" // ProfileFrameInfo::profile
#include "../ai/profileshapeindex.h" // KbResolution

namespace decenza::storage::detail {

// Parsed metadata about the configured frames in a profile JSON blob.
// Used to populate `firstFrameSeconds` and `expectedFrameCount` arguments
// to ShotAnalysis::analyzeShot. Defaults (frameCount=-1, firstFrameSeconds=-1.0)
// signal "unknown" so analyzeShot's skip-first-frame detection falls back
// to its hard 2 s window.
struct ProfileFrameInfo {
    int frameCount = -1;
    double firstFrameSeconds = -1.0;
    // For a *fresh* expert-band kbId re-resolution against the current KB
    // (the persisted profileKbId can be stale for shots saved before a KB
    // reorganization — #1160/#1175). Same parse as frameCount.
    QString profileTitle;
    QString editorType;
    // The parsed profile itself, so callers needing SHAPE resolution reuse
    // this parse rather than doing a second Profile::fromJson on the same
    // JSON. Default-constructed (no steps) when the JSON was absent or
    // unparseable, which every consumer already treats as "no context".
    Profile profile;
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
    std::optional<ShotAnalysis::ExpertBand> expertBand;  // cited per-profile band (D14); nullopt → no-op

    // Gates grind Arm 1 (openspec: skip-grind-arm1-when-kb-unresolved).
    //
    // Callers MUST read this rather than deriving it from the persisted
    // profileKbId, which both call sites used to do. That derivation is now
    // wrong: a profile can resolve by SHAPE with no persisted id at all
    // (change: resolve-profile-kb-by-shape), and a shape-resolved profile has
    // exactly the context Arm 1 needs — the question Arm 1 asks is whether the
    // flow goal is a real target or a safety limiter, which is a structural
    // question the shape answers directly.
    bool profileKbResolved = false;

    // The KB entry to NAME when a surface shows the user which profile's
    // knowledge was used. Empty when nothing resolved, and also empty when the
    // shape matched several entries at once — there is no single profile to
    // name then, and naming an arbitrary one asserts an identity the
    // resolution never established. Analysis facts above still transfer in
    // that case; only the claim about identity is withheld.
    QString identityKbId;

    // True when `identityKbId` came from the shape step rather than the title,
    // so a surface can present it as a derivation ("Based on X") rather than
    // as something the profile's own name asserted.
    bool identityFromShape = false;
};

AnalysisInputs prepareAnalysisInputs(const QString& profileKbId,
                                     const QString& profileJson);

// True when the OS reports a 12-hour locale (e.g. "h:mm AP" rather than
// "HH:mm"). Cached after the first call so we don't re-walk QLocale on every
// row. Used by the date-formatting code that emits `dateTime` strings to
// QML — see `convertShotRecord` in shothistorystorage_serialize.cpp and
// the filtered-list / auto-favorite paths in shothistorystorage_queries.cpp.
bool use12h();

} // namespace decenza::storage::detail
