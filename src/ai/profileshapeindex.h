#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// For ProfileFieldDelta, which DialInComparison carries by value. The TU-split
// note below is about the .cpp's link closure, not this header — every file that
// includes this one already has Profile in scope.
#include "profile/profile.h"

// Resolves a profile to KB entries by its SHAPE, for profiles whose TITLE
// resolves to nothing (change: resolve-profile-kb-by-shape).
//
// The problem: a user copies a documented profile, re-tunes temperature or
// yield, and renames it to something the title steps cannot reach. The KB's
// suppression flags — flow_trend_ok, channeling_expected — are claims about the
// extraction SHAPE, and remain true of that copy, but nothing delivers them.
// The shot is then eligible for findings the named original correctly
// suppresses: it gets told a by-design curve is a fault.
//
// WHY ITS OWN TU, not shotsummarizer_kb.cpp: that file exists to be a lean
// KB-data layer over `:/ai/*` + QString, so the offline consumer that needs
// it — shot_eval, whose CMake source list deliberately excludes profile*.cpp
// — can link a small closure. (Only shot_eval: every tst_* binary reaches
// decenza_testlib, which compiles the whole Profile and BLE closure anyway,
// so this split buys them nothing.) Shape resolution needs
// Profile, which reaches Qt Bluetooth through profileframe.cpp's use of
// DE1::FrameFlag. Putting the index there would drag ~4,900 lines and a
// Bluetooth dependency into a curve-eval CLI. Keeping it separate costs
// nothing: the callers that need shape resolution (shot save, shot load,
// the profile catalog scan) all already have a Profile in hand.
//
// Consequence worth knowing: shot_eval does NOT do shape resolution, so the
// regression corpus gates the title steps but not this one. Widening it would
// mean giving that tool the Profile closure — see the note above.
namespace ProfileShapeIndex {

// One bundled profile that landed in a shape bucket: where to load it from, and
// which KB entry its own title resolves to.
//
// A PAIR, not just a path, because the two consumers ask different questions of
// it. Resolution wants the ids; the dial-in difference block wants the FILE, and
// wants to filter files by id — one KB entry can be authored against several
// bundled profiles (gentle-flat-long-preinfusion-family has four), so an id does
// not name a file and a "distance to an entry" is not defined.
struct BundledMatch {
    QString resourcePath;
    QString kbId;
};

// The bundled profiles whose shape equals `p`'s, ordered by resource path so the
// answer never depends on directory enumeration order. Empty when the profile
// matches nothing, which is the ordinary outcome.
QVector<BundledMatch> bundledProfilesForShape(const Profile& p);

// KB ids whose shipped profile has the same shape as `p`. Empty when the
// profile matches nothing, which is the ordinary outcome and means "stay
// unresolved" exactly as today.
//
// Returns a SET, not a winner: several shipped profiles can share one shape
// (measured: 2 such buckets over the shipped set, pinned by
// tst_shotsummarizer::shippedShapeCollisionsAreExactlyTheKnownTwo), and
// picking one of them
// would assert an identity the shape never established. Callers apply per-fact
// transfer rules to the set — see the profile-knowledge-base capability.
//
// Order is deterministic and independent of directory enumeration order.
QStringList candidatesForShape(const Profile& p);

// Test seam: drop the cached index so a test can rebuild it. Not for
// production use — the index is immutable data derived from shipped
// resources, and nothing at runtime invalidates it.
void resetForTesting();

} // namespace ProfileShapeIndex

// The full resolution of a profile to KB entries, and HOW it got there.
//
// Origin matters to callers, not just the ids: a Title match is an identity
// the user's own naming asserts, while a Shape match is one this code
// inferred. Surfaces that name the matched profile to a user must say which
// they are looking at rather than presenting an inference as a fact.
struct KbResolution {
    enum class Origin { None, Title, Shape };

    QStringList ids;
    Origin origin = Origin::None;

    bool isEmpty() const { return ids.isEmpty(); }
    // Identity facts — the prose body, the canonical display name, roast
    // affinity — require a SINGLE member, not merely agreement. With two
    // candidates there is no one profile to name, and naming an arbitrary
    // member asserts something the resolution never established. Analysis
    // facts still transfer in that case under their own rules: a false
    // positive can be suppressed without claiming to know which profile the
    // shot's was derived from.
    bool hasIdentity() const { return ids.size() == 1; }

    // What may be written to the shot's `profile_kb_id` column — a TITLE
    // resolution only, empty for everything else.
    //
    // That column is not merely an analysis key: it is the grouping key for
    // dial-in history (`WHERE profile_kb_id = ?`). Shape equivalence
    // deliberately ignores temperature, pressure/flow setpoints, volume and
    // exit thresholds, which is exactly what makes it safe for transferring
    // suppression FACTS and exactly what makes it wrong as an identity —
    // persisting one would merge a user's 6-bar variant into the documented
    // 9-bar profile's dialInSessions and have the advisor compute grind advice
    // across two different coffees.
    //
    // Nothing is lost by withholding it: every analysis path re-resolves from
    // the shot's own profileJson via prepareAnalysisInputs, so the shape facts
    // still reach the shot. A method rather than an inline ternary at the one
    // call site, so the rule has a name and a test.
    QString persistableId() const
    {
        return origin == Origin::Title ? ids.first() : QString();
    }
};

// The bundled profile a dial-in difference block compares `p` against, and the
// differences themselves. `base` is empty exactly when no block may be shown.
//
// One computation, not two: the base is chosen by which candidate `p` differs
// from on the FEWEST dial-in fields, so the winner's difference list is the same
// list that selected it. A separate scoring pass could disagree with the list it
// was scoring; this cannot.
struct DialInComparison {
    QString baseResourcePath;   // empty when there is no base
    QString baseTitle;          // the bundled profile's own title
    QString baseKbId;
    QVector<ProfileFieldDelta> deltas;   // empty AND a base set == unchanged copy

    bool hasBase() const { return !baseResourcePath.isEmpty(); }
};

// Compare `p` against the bundled profile its knowledge was authored against.
//
// Gated on SHAPE equality however the KB id was reached, so a profile that
// merely shares a name with a documented one does not get diffed against it —
// that comparison would be unbounded and would falsely present the profile as a
// modified copy. `resolution` is `p`'s own resolution, whose ORIGIN chooses the
// candidate list:
//   - Title: only bundled profiles carrying the resolved id, because that is the
//     entry whose prose is on screen.
//   - Shape: the whole bucket, since the shape is all that was established.
// Both then converge on fewest-differences, abstaining unless one candidate is
// strictly nearest.
//
// A bundled profile compared with itself yields no base: there is nothing to
// tell the user about a profile that IS the documentation.
DialInComparison compareWithBundledBase(const Profile& p, const KbResolution& resolution);

// Resolve `p` to KB entries: the existing TITLE steps first (exact alias →
// recipe-alias longest-boundary-prefix → editor-type default), unchanged and
// always winning; the SHAPE step only when those all miss.
//
// A title-resolvable profile never touches the shape index, so it pays
// nothing for this and its resolution is byte-identical to before.
KbResolution resolveProfileKb(const Profile& p);
