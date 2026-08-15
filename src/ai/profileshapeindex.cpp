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
#include <QDebug>

namespace {

// signature -> KB ids, built once from the shipped profile set.
QHash<QString, QStringList> s_index;
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
// Where that lands: shot SAVE (after the shot, machine idle) and shot LOAD
// (background thread). Neither is a path a user waits on, and neither runs
// while the machine is pouring. It must NOT be called from a QML binding or
// anything per-sample — one call on a cold index would be a visible hitch.
void loadIndex()
{
    if (s_loaded) return;
    QMutexLocker lock(&s_mutex);
    if (s_loaded) return;   // re-check after acquiring

    QElapsedTimer timer;
    timer.start();

    QDir dir(QStringLiteral(":/profiles"));
    const QStringList files =
        dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);

    int parsed = 0, resolved = 0;
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const Profile p = Profile::fromJson(QJsonDocument::fromJson(f.readAll()));
        ++parsed;

        const QString sig = p.shapeSignature();
        if (sig.isEmpty()) continue;   // no frames: nothing to match on

        const QString kbId =
            ShotSummarizer::computeProfileKbId(p.title(), p.editorType());
        if (kbId.isEmpty()) continue;  // no facts to lend
        ++resolved;

        QStringList& ids = s_index[sig];
        if (!ids.contains(kbId))
            ids.append(kbId);
    }

    // Sorted so a bucket's contents never depend on directory enumeration
    // order — callers compare and log these sets, and an order-dependent
    // result would be an order-dependent resolution.
    for (QStringList& ids : s_index) ids.sort();

    s_loaded = true;
    // `resolved` counts PROFILES that contributed, not distinct ids — two
    // shipped profiles resolving to one entry contribute twice here and add
    // one id to one bucket, so shapes < resolved is normal and not a defect.
    qDebug().nospace()
        << "[ProfileShape] built shape index: " << s_index.size()
        << " shapes from " << resolved << " of " << parsed
        << " shipped profiles in " << timer.elapsed() << " ms";
}

} // namespace

namespace ProfileShapeIndex {

QStringList candidatesForShape(const Profile& p)
{
    const QString sig = p.shapeSignature();
    if (sig.isEmpty()) return {};
    loadIndex();
    return s_index.value(sig);
}

void resetForTesting()
{
    QMutexLocker lock(&s_mutex);
    s_index.clear();
    s_loaded = false;
}

} // namespace ProfileShapeIndex

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

    qDebug().nospace()
        << "[ProfileShape] '" << p.title() << "' resolved by shape to ["
        << byShape.join(QStringLiteral(", ")) << "]"
        << (byShape.size() > 1 ? " (ambiguous: identity withheld)" : "");
    return KbResolution{ byShape, KbResolution::Origin::Shape };
}
