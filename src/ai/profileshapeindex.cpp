#include "profileshapeindex.h"

#include "shotsummarizer.h"
#include "profile/profile.h"

#include <QDir>
#include <QFile>
#include <QHash>
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
// Cost: one-time per process, and paid only by a profile that failed title
// resolution — a title-resolvable profile never reaches this at all.
//
// MEASURED, 2023 MacBook Pro, DEBUG build with ASan+UBSan, 100 shipped
// profiles through Profile::fromJson: 48 ms, yielding 85 shapes. Release is
// materially faster (sanitizers dominate this workload) but is not measured
// here, so treat 48 ms as the pessimistic bound rather than the typical one.
// A warm lookup afterwards is a hash probe.
//
// Where that lands, all three callers:
//   - shot SAVE (main thread, machine idle after the shot),
//   - shot LOAD (background DB thread),
//   - the profile CATALOG SCAN, ProfileManager::refreshProfiles() — main
//     thread, synchronous, and reached from profile save/delete/import,
//     Visualizer import and startup. This is the one a user can feel: a cold
//     index (48 ms worst case above) lands on top of that scan's own
//     per-profile Profile::fromJson. It is a discrete user action rather than
//     a repeating binding, and it is paid once per process, which is why it is
//     accepted rather than threaded.
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
    QString bestPath, bestTitle, bestId;
    QVector<ProfileFieldDelta> bestDeltas;
    qsizetype bestCount = -1;
    bool tied = false;

    for (const BundledMatch& m : candidates) {
        const Profile bundled = Profile::loadFromFile(m.resourcePath);
        if (!bundled.isValid()) {
            // Unreadable here means the same packaging defect loadIndexLocked()
            // already warns about per file; it cannot be a data outcome, and it
            // must not silently promote a worse candidate to winner.
            qWarning().nospace()
                << "ProfileShapeIndex: bundled base '" << m.resourcePath
                << "' did not load; it cannot be compared against";
            continue;
        }

        // A profile compared with itself is the documentation, not a copy of it.
        if (Profile::functionallyEqual(bundled, p) && bundled.title() == p.title())
            return {};

        QVector<ProfileFieldDelta> deltas = Profile::dialInDeltas(bundled, p);
        const qsizetype count = deltas.size();
        if (bestCount < 0 || count < bestCount) {
            bestCount = count;
            tied = false;
            bestPath = m.resourcePath;
            bestTitle = bundled.title();
            bestId = m.kbId;
            bestDeltas = std::move(deltas);
        } else if (count == bestCount) {
            tied = true;
        }
    }

    // No strictly-fewest means no base. Naming one would assert a relationship
    // the comparison did not establish, which is the false attribution this
    // whole gate exists to avoid.
    if (bestCount < 0 || tied) return {};

    out.baseResourcePath = bestPath;
    out.baseTitle = bestTitle;
    out.baseKbId = bestId;
    out.deltas = std::move(bestDeltas);
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
