#include "profileshapeindex.h"

#include "shotsummarizer.h"
#include "profile/profile.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <algorithm>
#include <QDebug>

namespace {

// signature -> what that shape resolves to, built once from the shipped profile
// set. Both members are derived from the same walk: `kbIds` is what resolution
// reads, `profiles` is what the dial-in difference block reads. Kept side by
// side rather than derived on demand so neither consumer can see a bucket the
// other's view of does not account for.
struct ShapeBucket {
    QStringList kbIds;                              // unique, sorted
    QVector<ProfileShapeIndex::BundledMatch> profiles;  // sorted by resourcePath
};

QHash<QString, ShapeBucket> s_index;
bool s_loaded = false;
QMutex s_mutex;

// Build the index from `:/profiles/*.json`, keeping only profiles whose own
// TITLE resolves to a KB entry through the existing title steps. A shipped
// profile with no KB entry has no facts to lend, so indexing it would only
// create buckets that can never transfer anything.
//
// Cost: one-time per process, paid by the first profile that reaches EITHER
// shape resolution or the dial-in difference block. It is no longer only the
// title-resolution failures: compareWithBundledBase() looks the shape up before
// it branches on origin, so a title-resolved profile builds the index too — and
// that is the population the difference block targets.
//
// MEASURED, 2023 MacBook Pro, DEBUG build with ASan+UBSan, 100 shipped
// profiles through Profile::fromJson: 48 ms, yielding 85 shapes. Release is
// materially faster (sanitizers dominate this workload) but is not measured
// here, so treat 48 ms as the pessimistic bound rather than the typical one.
// A warm lookup afterwards is a hash probe.
//
// Where that lands, all FOUR callers:
//   - shot SAVE (main thread, machine idle after the shot),
//   - shot LOAD (background DB thread),
//   - the profile CATALOG SCAN, ProfileManager::refreshProfiles() — main
//     thread, synchronous, and reached from profile save/delete/import,
//     Visualizer import and startup. This is the one a user can feel: a cold
//     index (48 ms worst case above) lands on top of that scan's own
//     per-profile Profile::fromJson. It is a discrete user action rather than
//     a repeating binding, and it is paid once per process, which is why it is
//     accepted rather than threaded.
//   - the DIAL-IN DIFFERENCE BLOCK, on knowledge-dialog open (main thread).
//     In practice the catalog scan has already paid for it by then — the dialog
//     is reached from a profile list or a shot page, both of which follow a
//     scan — but a cold index CAN land here, and this is the one place where a
//     user is waiting on a dialog rather than on a page they already asked to
//     rebuild.
// It must NOT be called from a QML binding or anything per-sample.
//
// Caller must hold s_mutex. The lock deliberately covers the READS in
// candidatesForShape and bundledProfilesForShape too: a double-checked `if (s_loaded) return;` outside the
// mutex is a data race on a plain bool AND on the QHash, and its benign
// outcome is the worst possible one here — a torn read yields an empty
// QStringList, which resolveProfileKb cannot distinguish from a legitimate
// no-match, so a shot would silently lose its suppression flags once,
// unreproducibly. An uncontended QMutex on a path that already costs a hash
// probe is not a measurable cost, and TSan is unavailable in this build so
// nothing would have caught the race.
void loadIndexLocked()
{
    if (s_loaded) return;

    QElapsedTimer timer;
    timer.start();

    QDir dir(QStringLiteral(":/profiles"));
    const QStringList files =
        dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);

    int parsed = 0, resolved = 0, unreadable = 0;
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            // A shipped profile that cannot be opened is a packaging defect,
            // not a data outcome: its shape silently stops matching for every
            // user, forever, and the only symptom is the feature quietly not
            // working. Loud, and counted, so the summary line below cannot
            // read as a healthy build.
            qWarning().nospace()
                << "ProfileShapeIndex: cannot open shipped profile '" << name
                << "' - its shape will never match; the index is incomplete";
            ++unreadable;
            continue;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning().nospace()
                << "ProfileShapeIndex: shipped profile '" << name
                << "' failed to parse: " << err.errorString()
                << " - its shape will never match";
            ++unreadable;
            continue;
        }
        const Profile p = Profile::fromJson(doc);
        ++parsed;

        // isValid() is a STRICTER bar than "parsed": it also rejects an
        // unsupported step key and a malformed value. Checked here so the index
        // never hands out a candidate the comparator would then reject — the
        // divergence was real, because a shipped profile carrying a step key the
        // parser does not know indexed cleanly with no warning anywhere, and
        // then failed at compare time where the only report of it lived.
        if (!p.isValid()) {
            qWarning().nospace()
                << "ProfileShapeIndex: shipped profile '" << name
                << "' is not valid (" << p.validationErrors().join(QStringLiteral("; "))
                << ") - excluded from the shape index";
            ++unreadable;
            continue;
        }

        const QString sig = p.shapeSignature();
        if (sig.isEmpty()) continue;   // no frames: nothing to match on

        const QString kbId =
            ShotSummarizer::computeProfileKbId(p.title(), p.editorType());
        if (kbId.isEmpty()) continue;  // no facts to lend
        ++resolved;

        ShapeBucket& bucket = s_index[sig];
        if (!bucket.kbIds.contains(kbId))
            bucket.kbIds.append(kbId);
        // Every file, even when two of them share one id: the difference block
        // compares against a profile's VALUES, and two profiles behind one entry
        // carry different ones.
        bucket.profiles.append({ dir.filePath(name), kbId });
    }

    // Sorted so a bucket's contents never depend on directory enumeration
    // order — callers compare and log these sets, and an order-dependent
    // result would be an order-dependent resolution.
    for (ShapeBucket& bucket : s_index) {
        bucket.kbIds.sort();
        std::sort(bucket.profiles.begin(), bucket.profiles.end(),
                  [](const ProfileShapeIndex::BundledMatch& l,
                     const ProfileShapeIndex::BundledMatch& r) {
                      return l.resourcePath < r.resourcePath;
                  });
    }

    s_loaded = true;

    // An empty index means shape resolution is off for this whole process, and
    // every user silently gets back the false-positive channeling and
    // flow-trend findings this feature exists to suppress. Reported, but at
    // INFO, because every cause of it is either already reported by whoever
    // owns it or is not a fault at all:
    //
    //  - `:/profiles` absent — a link-time fact. Impossible in the app
    //    (profiles.qrc is unconditional on the Decenza target) and the normal
    //    state of a test binary that links the KB but not the 494 KB set.
    //  - a file unreadable or unparseable — already warned per file above.
    //  - every file readable but none RESOLVING — that is the KB not being
    //    loaded, which loadProfileKnowledge() warns about itself. Warning here
    //    too would double-report its failure, and it is exactly the state of
    //    the four test binaries that link profiles.qrc without the KB, none of
    //    which caused a defect.
    //
    // So: state it, do not raise it. The per-file warnings above are the ones
    // that name something genuinely wrong.
    if (s_index.isEmpty()) {
        qInfo().nospace()
            << "ProfileShapeIndex: empty - " << files.size() << " file(s) in :/profiles, "
            << unreadable << " unreadable, none resolved to a KB entry. Shape "
            << "resolution is unavailable for this process; every profile reads "
            << "as unmatched.";
        return;
    }

    // `resolved` counts PROFILES that contributed, not distinct ids — two
    // shipped profiles resolving to one entry contribute twice here and add
    // one id to one bucket, so shapes < resolved is normal and not a defect.
    //
    // No bracketed marker: [ProfileShape] would be a hand-typed prefix that
    // core/logtags.h does not register, which advertises a subsystem query
    // that returns nothing (LOGGING.md, rule 5). A plain class-name prefix
    // claims nothing it cannot deliver.
    qDebug().nospace()
        << "ProfileShapeIndex: built shape index: " << s_index.size()
        << " shapes from " << resolved << " of " << parsed
        << " shipped profiles (" << unreadable << " unreadable) in "
        << timer.elapsed() << " ms";
}

} // namespace

namespace ProfileShapeIndex {

QStringList candidatesForShape(const Profile& p)
{
    const QString sig = p.shapeSignature();
    if (sig.isEmpty()) return {};
    QMutexLocker lock(&s_mutex);   // covers the build AND the read
    loadIndexLocked();
    return s_index.value(sig).kbIds;
}

QVector<BundledMatch> bundledProfilesForShape(const Profile& p)
{
    const QString sig = p.shapeSignature();
    if (sig.isEmpty()) return {};
    QMutexLocker lock(&s_mutex);   // same lock, same reason as above
    loadIndexLocked();
    return s_index.value(sig).profiles;
}

void resetForTesting()
{
    QMutexLocker lock(&s_mutex);
    s_index.clear();
    s_loaded = false;
}

} // namespace ProfileShapeIndex

namespace {

// Do two delta lists say the SAME thing about a profile? Used only to decide
// whether tied candidates may be merged into one entry-level answer: if they
// disagree about any value, there is no true "before" column to render and the
// comparison must abstain rather than pick one sibling's numbers.
bool sameDeltas(const QVector<ProfileFieldDelta>& a, const QVector<ProfileFieldDelta>& b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        const ProfileFieldDelta& x = a[i];
        const ProfileFieldDelta& y = b[i];
        if (x.kind != y.kind || x.unit != y.unit || x.frameIndex != y.frameIndex
            || x.numeric != y.numeric)
            return false;
        if (x.numeric) {
            if (qAbs(x.oldValue - y.oldValue) > x.tolerance
                || qAbs(x.newValue - y.newValue) > x.tolerance)
                return false;
        } else if (x.oldText != y.oldText || x.newText != y.newText) {
            return false;
        }
    }
    return true;
}

} // namespace

DialInComparison compareWithBundledBase(const Profile& p, const KbResolution& resolution)
{
    using ProfileShapeIndex::BundledMatch;

    DialInComparison out;
    if (resolution.isEmpty()) return out;

    // SHAPE is the gate, whichever way the id was reached. bundledProfilesForShape
    // returns nothing for a frameless profile and for one whose structure matches
    // no bundled profile, which is exactly the "no block" case — no separate
    // check needed for either.
    QVector<BundledMatch> candidates = ProfileShapeIndex::bundledProfilesForShape(p);
    if (candidates.isEmpty()) return out;

    if (resolution.origin == KbResolution::Origin::Title) {
        // The entry on screen is the one the title resolved to, so the base must
        // be a profile THAT entry was authored against. A same-shape bundled
        // profile carrying a different entry is not what the prose describes.
        const QString id = resolution.ids.first();
        QVector<BundledMatch> sameEntry;
        for (const BundledMatch& m : candidates)
            if (m.kbId == id) sameEntry.append(m);
        candidates = sameEntry;
        if (candidates.isEmpty()) return out;
    }

    // Fewest differing dial-in fields wins. The deltas are kept because the
    // winner's list IS the block — recomputing it separately would let the
    // selection and the thing selected drift apart.
    //
    // EVERY candidate level with the best count is kept, not just the first.
    // The tie is on the delta COUNT, and equal counts do not imply equal
    // content: the six bundled tea profiles all differ from an 85 degree copy
    // on the same NUMBER of fields, while each states a different "before"
    // value. (The count itself is pinned by the test, not asserted here — an
    // unverified figure in a comment is how the last three defects in this
    // file got written.) Keeping only the first one's list showed the user
    // numbers from a profile they had never brewed.
    struct Scored {
        QString path;
        QString title;
        QString kbId;
        QVector<ProfileFieldDelta> deltas;
    };
    QVector<Scored> best;
    qsizetype bestCount = -1;

    for (const BundledMatch& m : candidates) {
        const Profile bundled = Profile::loadFromFile(m.resourcePath);
        if (!bundled.isValid()) {
            // The index refuses an invalid profile at build time, so this is
            // unreachable short of the resource system changing under us. It must
            // ABSTAIN rather than skip: a candidate we cannot read makes the set
            // incomplete, and an incomplete set cannot establish "strictly
            // nearest". Skipping instead would promote a worse candidate to
            // winner and could turn a genuine tie into a confident wrong answer.
            qWarning().nospace()
                << "ProfileShapeIndex: bundled base '" << m.resourcePath
                << "' did not load; the candidate set is incomplete, so no base "
                << "can be established";
            return {};
        }

        QVector<ProfileFieldDelta> deltas = Profile::dialInDeltas(bundled, p);
        const qsizetype count = deltas.size();
        if (bestCount < 0 || count < bestCount) {
            bestCount = count;
            best.clear();
            best.append({ m.resourcePath, bundled.title(), m.kbId, std::move(deltas) });
        } else if (count == bestCount) {
            best.append({ m.resourcePath, bundled.title(), m.kbId, std::move(deltas) });
        }
    }

    if (best.isEmpty()) return {};

    // A profile compared with itself is the documentation, not a copy of it.
    //
    // Gated on the DELTAS, not on Profile::functionallyEqual(). That predicate
    // deliberately ignores profile-level limits (its own comment says so) and
    // never compares a frame's display name — which is five of the fields this
    // block exists to show. Using it meant a user who changed only the yield, a
    // pressure/flow limit, or a step name was told nothing at all, in exactly
    // the population the feature targets.
    //
    // Checked against EVERY tied candidate's title, not just the first one's.
    // chinese green and white tea are byte-identical apart from their titles, so
    // opening the dialog on the bundled white tea scores chinese green first;
    // comparing only that one's title let the self-check miss, and the block
    // announced "Unchanged copy of Tea" on the profile that IS the
    // documentation.
    if (bestCount == 0)
        for (const Scored& s : best)
            if (s.title == p.title()) return {};

    const Scored* winner = &best.first();

    if (best.size() > 1) {
        // Several candidates are equally near. Two conditions must BOTH hold for
        // the block to be shown, and the second is the one an earlier version of
        // this code missed.
        //
        // 1. They must describe the same KNOWLEDGE. Across entries, naming one
        //    asserts a relationship the comparison did not establish.
        //
        // 2. They must say the same thing about this profile. Equal counts are
        //    not equal content — an 85 degree tea copy ties all six bundled tea
        //    profiles at the same row count while each carries a different
        //    "before" value, and there is then no true column to render. Naming
        //    the entry does not rescue that: the numbers beside it would still
        //    be one arbitrary sibling's.
        //
        // Where both hold — a renamed but untouched copy of one of two identical
        // twins, say — the block is shown against the ENTRY, because every tied
        // candidate agrees on what it would say.
        QSet<QString> distinctIds;
        for (const Scored& s : best) distinctIds.insert(s.kbId);
        if (distinctIds.size() > 1) return {};

        for (const Scored& s : best)
            if (!sameDeltas(s.deltas, winner->deltas)) return {};

        // An entry-level answer needs an entry NAME. Falling through without
        // one would drop to the single-winner path below and label the block
        // with winner->title — the first tied candidate's own bundled title,
        // chosen by iteration order, which is exactly the arbitrary pick the
        // two conditions above exist to prevent. Abstain instead.
        const QString canonical = ShotSummarizer::canonicalNameForKbId(winner->kbId);
        if (canonical.isEmpty()) return {};

        out.baseResourcePath = winner->path;
        out.baseTitle = canonical;
        out.baseKbId = winner->kbId;
        out.deltas = winner->deltas;
        return out;
    }

    out.baseResourcePath = winner->path;
    out.baseTitle = winner->title;
    out.baseKbId = winner->kbId;
    out.deltas = winner->deltas;
    return out;
}

KbResolution resolveProfileKb(const Profile& p)
{
    // Title steps first and unchanged. Every shipped/starter profile and both
    // editor-canonical outputs resolve here, so the shape step cannot alter a
    // built-in's resolution — it is only ever reached after a total miss.
    const QString titleId =
        ShotSummarizer::computeProfileKbId(p.title(), p.editorType());
    if (!titleId.isEmpty())
        return KbResolution{ { titleId }, KbResolution::Origin::Title };

    const QStringList byShape = ProfileShapeIndex::candidatesForShape(p);
    if (byShape.isEmpty())
        return {};   // explicitly unresolved, exactly as before this change

    // qInfo, not qDebug: this records an inference the app made on the user's
    // behalf that changes what their shot is told about. The connections views
    // default to minLevel INFO (LOGGING.md), so a DEBUG line here would be
    // absent from the surface a user or their assistant actually reads. It is
    // not chatty — it fires only for a profile whose title resolved to nothing
    // and whose shape then matched, which no shipped profile ever does.
    qInfo().nospace()
        << "ProfileShapeIndex: '" << p.title() << "' resolved by shape to ["
        << byShape.join(QStringLiteral(", ")) << "]"
        << (byShape.size() > 1 ? " (ambiguous: identity withheld)" : "");
    return KbResolution{ byShape, KbResolution::Origin::Shape };
}
