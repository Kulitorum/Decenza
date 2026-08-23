// tst_mcptools_dialing_blocks — DB-backed coverage for the four shared
// block builders that drive both `dialing_get_context` (MCP) and the
// in-app advisor's user-prompt enrichment (issue #1044).
//
// Pre-existing tst_aimanager.cpp coverage exercises the *gating* paths
// (empty kbId, empty grinderModel, no-flow shot). This file covers the
// *populated* paths: stand up a real SQLite schema, insert curated
// shots, call each builder, and pin the produced JSON.
//
// Determinism: the helpers under test read `QDateTime::currentSecsSinceEpoch()`
// directly (windowFloor for bestRecentShot, daysSinceShot computation).
// Tests construct fixtures with timestamps relative to "now" so the
// computed offsets are stable, and assert the time-derived fields
// against the same offsets used to build the fixtures. Static-shape
// fields (id, grinder/bean strings, ratios, change diffs) are compared
// directly to expected JSON literals.

#include <QtTest>
#include <QSet>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>

#include "history/shothistorystorage.h"
#include "history/shotprojection.h"
#include "history/equipmentstorage.h"
#include "ai/dialing_blocks.h"
#include "ai/shotsummarizer.h"  // initTestCase pins the missing-resource qWarning
#include "shotcurvefixtures.h"
#include "shotrowfixtures.h"

namespace {

using ShotRowFixtures::ShotRow;
using ShotRowFixtures::withRawDb;
using ShotRowFixtures::insertShot;
using ShotRowFixtures::projectionForShot;
using ShotRowFixtures::packageForShot;
using ShotRowFixtures::onlyEquipmentPackage;
using ShotRowFixtures::shotWith;
using ShotRowFixtures::shotInPackage;


constexpr qint64 kSecPerDay = 24 * 3600;

// editorType derived EXACTLY as Profile::editorType() does: strip a single
// leading '*' (unsaved-modified marker) BEFORE the prefix-test. Omitting the
// strip would false-green a "*D-Flow / x" title (resolves via editor-default
// in the app but not here). Shared by kbCoverage_everyBuiltInProfileResolves
// and kbCoverage_shotCorpusProfileTitlesResolve so a future editor-type rule
// change can't be fixed in one KB-coverage gate and silently missed in the
// other.
QString editorTypeForTitle(const QString& title)
{
    QString t = title;
    if (t.startsWith(QLatin1Char('*'))) t = t.mid(1);
    if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("dflow");
    if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
        return QStringLiteral("aflow");
    return QString();
}

} // namespace

class TstDialingBlocks : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    QString freshDbPath()
    {
        static int counter = 0;
        return m_tempDir.path() + QStringLiteral("/dialing_%1.db").arg(++counter);
    }

    QString m_templateDbPath;

    // Stand up a fresh DB at `path` with the full ShotHistoryStorage schema.
    // The schema template is built exactly once in initTestCase(); each test's
    // DB is a file copy of it (a few ms) rather than a fresh
    // createTables()+migration-chain run (~300ms each) — 37 call sites × the
    // migration chain was the bulk of this binary's runtime.
    void initAndClose(const QString& path)
    {
        QVERIFY(!m_templateDbPath.isEmpty());
        QVERIFY(QFile::copy(m_templateDbPath, path));
        // Copy any WAL/SHM sidecars so the schema copy is complete even if the
        // template wasn't fully checkpointed on close.
        for (const QString& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            if (QFile::exists(m_templateDbPath + suffix))
                QFile::copy(m_templateDbPath + suffix, path + suffix);
        }
        // Self-check the copy actually carried the schema. Today the schema is
        // durably checkpointed into the main .db before the copy (see
        // initTestCase), so this always holds — but if that ever changes (schema
        // migrates into an uncopied/torn WAL) an empty-schema DB would let the
        // read-only "empty DB" tests pass silently. Fail loudly instead.
        bool schemaOk = false;
        withRawDb(path, QStringLiteral("tmpl_verify"), [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            schemaOk = q.exec(QStringLiteral(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name='shots'"))
                && q.next();
        });
        QVERIFY2(schemaOk, "copied template DB is missing the 'shots' schema");
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        // Pre-warm the profile-knowledge singleton so the one-time DB load
        // happens here (in initTestCase) rather than inside individual tests.
        // ai.qrc is linked by this binary, so the load succeeds silently.
        ShotSummarizer::computeProfileKbId(QStringLiteral("dummy"), QStringLiteral("advanced"));

        // Build the schema template ONCE. initAndClose() copies this file per
        // test instead of re-running the migration chain each time. initialize()
        // creates the tables/runs migrations and then durably checkpoints them
        // into the main .db (PRAGMA wal_checkpoint(TRUNCATE)) before spawning its
        // read-only distinct-cache prewarm — so the copied .db carries the full
        // schema regardless of that detached prewarm thread (which the destructor
        // does NOT join; it's read-only and m_destroyed-guarded, so harmless).
        m_templateDbPath = m_tempDir.path() + QStringLiteral("/dialing_template.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(m_templateDbPath));
        storage.close();
    }

    // -------------------------------------------------------------------
    // beanInputsFromProjection — the single shared ShotProjection→currentBean
    // mapper. Regression guard for the advisor/dialing drift where puck-prep
    // and freeze/thaw data were dropped: a shot carrying them must surface a
    // puckPrep sub-object and a known beanFreshness block through the mapper.
    // Pure (no DB), so it runs anywhere.
    // -------------------------------------------------------------------
    void beanInputsFromProjection_carriesPuckPrepAndFreeze()
    {
        ShotProjection sd;
        sd.beanBrand = QStringLiteral("Prodigal");
        sd.beanType = QStringLiteral("Milk Blend");
        sd.roastDate = QStringLiteral("2026-04-15");
        sd.frozenDate = QStringLiteral("2026-04-16");
        sd.defrostDate = QStringLiteral("2026-06-20");
        sd.puckPrep = QStringLiteral("rdt,shaker"); // canonical sorted set
        sd.basketBrand = QStringLiteral("Decent");
        sd.basketModel = QStringLiteral("18g Ridged");
        sd.doseWeightG = 18.0;

        const QJsonObject bean = DialingBlocks::buildCurrentBeanBlock(
            DialingBlocks::beanInputsFromProjection(sd));

        // Puck-prep flowed through as a sub-object with the set flags true.
        QVERIFY2(bean.contains(QStringLiteral("puckPrep")),
                 "currentBean must carry the puckPrep sub-object");
        const QJsonObject puck = bean[QStringLiteral("puckPrep")].toObject();
        QCOMPARE(puck[QStringLiteral("rdt")].toBool(), true);
        QCOMPARE(puck[QStringLiteral("shaker")].toBool(), true);
        QCOMPARE(puck[QStringLiteral("wdt")].toBool(), false);

        // Basket flowed through too — the advisor's old hand-roll dropped it
        // entirely, so pin its identity here as part of the same regression.
        QVERIFY2(bean.contains(QStringLiteral("basket")),
                 "currentBean must carry the basket sub-object");
        const QJsonObject basket = bean[QStringLiteral("basket")].toObject();
        QCOMPARE(basket[QStringLiteral("brand")].toString(), QStringLiteral("Decent"));
        QCOMPARE(basket[QStringLiteral("model")].toString(), QStringLiteral("18g Ridged"));

        // Freeze/thaw flowed through and marked storage known.
        const QJsonObject fresh = bean[QStringLiteral("beanFreshness")].toObject();
        QCOMPARE(fresh[QStringLiteral("freshnessKnown")].toBool(), true);
        QCOMPARE(fresh[QStringLiteral("frozenDate")].toString(), QStringLiteral("2026-04-16"));
        QCOMPARE(fresh[QStringLiteral("defrostDate")].toString(), QStringLiteral("2026-06-20"));
    }

    // bean-freshness-followup: the non-frozen storage lifecycle (storageHint +
    // openedDate) flows through the same mapper for a never-frozen bag.
    void beanInputsFromProjection_carriesNonFrozenStorage()
    {
        ShotProjection sd;
        sd.beanBrand = QStringLiteral("Sey");
        sd.roastDate = QStringLiteral("2026-06-01");
        sd.storageHint = QStringLiteral("airtight");
        sd.openedDate = QStringLiteral("2026-06-25");

        const QJsonObject bean = DialingBlocks::buildCurrentBeanBlock(
            DialingBlocks::beanInputsFromProjection(sd));
        const QJsonObject fresh = bean[QStringLiteral("beanFreshness")].toObject();
        QCOMPARE(fresh[QStringLiteral("freshnessKnown")].toBool(), true);
        QCOMPARE(fresh[QStringLiteral("storageHint")].toString(), QStringLiteral("airtight"));
        QCOMPARE(fresh[QStringLiteral("openedDate")].toString(), QStringLiteral("2026-06-25"));
        QVERIFY2(!fresh.contains(QStringLiteral("frozenDate")),
                 "never-frozen bag must omit frozenDate");
    }

    // -------------------------------------------------------------------
    // dialInSessionsBlock — 4 shots on profile A across 2 sessions.
    // The first three shots cluster within the 60-min session window;
    // the fourth is 24h later, so it lands in its own session.
    // -------------------------------------------------------------------
    void dialInSessionsBlock_groupsAndHoistsAcrossSessions()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        // Anchor "fixture now" two days ago so daysSinceShot stays
        // strictly positive even on day-boundary races.
        const qint64 fixtureBase = now - 2 * kSecPerDay;

        withRawDb(path, QStringLiteral("dial_sessions"), [&](QSqlDatabase& db) {
            // Session A: three shots within ~30 min, varying grinder setting.
            ShotRow base;
            base.profileName = QStringLiteral("80's Espresso");
            base.profileKbId = QStringLiteral("kb-80s");
            base.beanBrand = QStringLiteral("Northbound");
            base.beanType = QStringLiteral("Spring Tour");
            base.grinderBrand = QStringLiteral("Niche");
            base.grinderModel = QStringLiteral("Zero");
            base.grinderBurrs = QStringLiteral("63mm conical");

            ShotRow s1 = base;
            s1.uuid = QStringLiteral("uuid-s1");
            s1.timestamp = fixtureBase - 24 * 3600 - 30 * 60; // session A, oldest
            s1.grinderSetting = QStringLiteral("4.0");
            s1.doseWeight = 18.0; s1.finalWeight = 38.0; s1.duration = 30.0;
            QVERIFY(insertShot(db, s1) > 0);

            ShotRow s2 = base;
            s2.uuid = QStringLiteral("uuid-s2");
            s2.timestamp = fixtureBase - 24 * 3600 - 15 * 60;
            s2.grinderSetting = QStringLiteral("4.2");
            s2.doseWeight = 18.0; s2.finalWeight = 36.0; s2.duration = 28.0;
            QVERIFY(insertShot(db, s2) > 0);

            ShotRow s3 = base;
            s3.uuid = QStringLiteral("uuid-s3");
            s3.timestamp = fixtureBase - 24 * 3600;             // session A, newest
            s3.grinderSetting = QStringLiteral("4.4");
            s3.doseWeight = 18.0; s3.finalWeight = 35.0; s3.duration = 27.0;
            QVERIFY(insertShot(db, s3) > 0);

            // Session B: one shot ~24h later than session A.
            ShotRow s4 = base;
            s4.uuid = QStringLiteral("uuid-s4");
            s4.timestamp = fixtureBase;
            s4.grinderSetting = QStringLiteral("4.4");
            s4.doseWeight = 18.0; s4.finalWeight = 36.0; s4.duration = 30.0;
            QVERIFY(insertShot(db, s4) > 0);

            // Resolved shot: the most recent (session B). historyLimit big
            // enough to pull all four older shots.
            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-80s"), /*resolvedShotId=*/-1, /*historyLimit=*/10,
                shotInPackage(onlyEquipmentPackage(db)));

            // Two sessions, newest first (session B with 1 shot, session A
            // with 3 shots).
            QCOMPARE(sessions.size(), 2);

            const QJsonObject sessionB = sessions[0].toObject();
            QCOMPARE(sessionB.value(QStringLiteral("shotCount")).toInt(), 1);
            QCOMPARE(sessionB.value(QStringLiteral("context")).toObject()
                         .value(QStringLiteral("grinderModel")).toString(),
                     QStringLiteral("Zero"));

            const QJsonObject sessionA = sessions[1].toObject();
            QCOMPARE(sessionA.value(QStringLiteral("shotCount")).toInt(), 3);

            // Within session A, shots are ordered ASC (older->newer) so
            // changeFromPrev reads "older then newer". The first shot in
            // each session has changeFromPrev=null.
            const QJsonArray sessionAShots = sessionA.value(QStringLiteral("shots")).toArray();
            QCOMPARE(sessionAShots.size(), 3);
            QVERIFY(sessionAShots[0].toObject().value(QStringLiteral("changeFromPrev")).isNull());
            // The second shot's diff should mention the grinder change 4.0->4.2.
            const QJsonObject diff1 = sessionAShots[1].toObject()
                .value(QStringLiteral("changeFromPrev")).toObject();
            QCOMPARE(diff1.value(QStringLiteral("grinderSetting")).toString(),
                     QStringLiteral("4.0 -> 4.2"));
            // Third shot: 4.2->4.4.
            const QJsonObject diff2 = sessionAShots[2].toObject()
                .value(QStringLiteral("changeFromPrev")).toObject();
            QCOMPARE(diff2.value(QStringLiteral("grinderSetting")).toString(),
                     QStringLiteral("4.2 -> 4.4"));

            // Identity hoisting: the per-shot block should NOT carry
            // grinderModel etc. (they live on the session context).
            for (const QJsonValue& v : sessionAShots) {
                const QJsonObject sh = v.toObject();
                QVERIFY2(!sh.contains(QStringLiteral("grinderModel")),
                         "session-invariant grinderModel must hoist to context");
                QVERIFY2(!sh.contains(QStringLiteral("beanBrand")),
                         "session-invariant beanBrand must hoist to context");
            }
        });
    }

    // -------------------------------------------------------------------
    // dialInSessionsBlock — bean-freshness-followup: the storage-lifecycle
    // fields hoist/override through the REAL block builder, not just the pure
    // hoistSessionContext helper. Exercises the projection->ShotIdentity copy
    // (dialing_blocks.cpp), the context emission, and the per-shot override in
    // shotToJson — the three segments the pure-function test can't reach.
    // -------------------------------------------------------------------
    void dialInSessionsBlock_hoistsLifecycleAndOverridesOnThaw()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 base = now - 2 * kSecPerDay;

        withRawDb(path, QStringLiteral("dial_lifecycle"), [&](QSqlDatabase& db) {
            // One session, three shots within ~30 min. All share a storageHint
            // (should hoist to context); shots 1-2 share a defrostDate and shot
            // 3 was pulled after a new thaw (should override per-shot).
            ShotRow b;
            b.profileName = QStringLiteral("80's Espresso");
            b.profileKbId = QStringLiteral("kb-lc2");
            b.beanBrand = QStringLiteral("Northbound");
            b.grinderModel = QStringLiteral("Zero");
            b.storageHint = QStringLiteral("airtight");

            ShotRow s1 = b; s1.uuid = QStringLiteral("lc-s1");
            s1.timestamp = base - 30 * 60; s1.defrostDate = QStringLiteral("2026-05-01");
            QVERIFY(insertShot(db, s1) > 0);
            ShotRow s2 = b; s2.uuid = QStringLiteral("lc-s2");
            s2.timestamp = base - 15 * 60; s2.defrostDate = QStringLiteral("2026-05-01");
            QVERIFY(insertShot(db, s2) > 0);
            ShotRow s3 = b; s3.uuid = QStringLiteral("lc-s3");
            s3.timestamp = base; s3.defrostDate = QStringLiteral("2026-05-13");
            QVERIFY(insertShot(db, s3) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-lc2"), -1, 10, shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonObject session = sessions[0].toObject();
            const QJsonObject context = session.value(QStringLiteral("context")).toObject();
            const QJsonArray shots = session.value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 3);

            // storageHint is uniform -> hoisted to context, absent per-shot.
            QCOMPARE(context.value(QStringLiteral("storageHint")).toString(),
                     QStringLiteral("airtight"));
            for (const QJsonValue& v : shots)
                QVERIFY2(!v.toObject().contains(QStringLiteral("storageHint")),
                         "uniform storageHint must hoist to context");

            // defrostDate: shared value hoists; the differing (newest) shot
            // overrides. Shots are ASC (oldest first): [0]=s1,[1]=s2,[2]=s3.
            QCOMPARE(context.value(QStringLiteral("defrostDate")).toString(),
                     QStringLiteral("2026-05-01"));
            QVERIFY2(!shots[0].toObject().contains(QStringLiteral("defrostDate")),
                     "shot matching context must not carry an override");
            QVERIFY2(!shots[1].toObject().contains(QStringLiteral("defrostDate")),
                     "shot matching context must not carry an override");
            QCOMPARE(shots[2].toObject().value(QStringLiteral("defrostDate")).toString(),
                     QStringLiteral("2026-05-13"));
        });
    }

    // -------------------------------------------------------------------
    // dialInSessionsBlock — empty when no rows.
    // -------------------------------------------------------------------
    void dialInSessionsBlock_emptyWhenNoRows()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("dial_empty"), [&](QSqlDatabase& db) {
            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-no-rows"), -1, 10, shotInPackage(onlyEquipmentPackage(db)));
            QVERIFY(sessions.isEmpty());
        });
    }

    // -------------------------------------------------------------------
    // bestRecentShotBlock — rated shot inside the 90-day window emits
    // the full block, with daysSinceShot reflecting fixture-relative age
    // and a non-empty changeFromBest diff.
    // -------------------------------------------------------------------
    void bestRecentShotBlock_emitsFullBlock_whenRatedShotInWindow()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        constexpr qint64 kBestAgeDays = 14;
        const qint64 bestTimestamp = now - kBestAgeDays * kSecPerDay;

        withRawDb(path, QStringLiteral("best_in"), [&](QSqlDatabase& db) {
            ShotRow best;
            best.uuid = QStringLiteral("uuid-best");
            best.profileName = QStringLiteral("80's Espresso");
            best.profileKbId = QStringLiteral("kb-80s");
            best.beanBrand = QStringLiteral("Northbound");
            best.beanType = QStringLiteral("Spring Tour");
            best.grinderModel = QStringLiteral("Zero");
            best.grinderSetting = QStringLiteral("4.0");
            best.timestamp = bestTimestamp;
            best.doseWeight = 18.0;
            best.finalWeight = 38.0;
            best.duration = 30.0;
            best.enjoyment = 92;
            best.espressoNotes = QStringLiteral("balanced and sweet");
            const qint64 bestId = insertShot(db, best);
            QVERIFY(bestId > 0);

            // Current shot (separate row) — the diff should be against this.
            ShotRow current = best;
            current.uuid = QStringLiteral("uuid-current");
            current.timestamp = now - kSecPerDay; // 1 day ago
            current.grinderSetting = QStringLiteral("4.4");
            current.doseWeight = 18.0;
            current.finalWeight = 35.0;
            current.duration = 27.0;
            current.enjoyment = 70;
            current.espressoNotes = QString();
            const qint64 currentId = insertShot(db, current);
            QVERIFY(currentId > 0);

            const ShotProjection currentProj = projectionForShot(db, currentId);
            QVERIFY(currentProj.isValid());

            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-80s"), currentId, currentProj);

            QVERIFY(!best_.isEmpty());
            QCOMPARE(best_.value(QStringLiteral("id")).toVariant().toLongLong(), bestId);
            QCOMPARE(best_.value(QStringLiteral("enjoyment0to100")).toInt(), 92);
            QCOMPARE(best_.value(QStringLiteral("doseG")).toDouble(), 18.0);
            QCOMPARE(best_.value(QStringLiteral("yieldG")).toDouble(), 38.0);
            QCOMPARE(best_.value(QStringLiteral("ratio")).toString(), QStringLiteral("1:2.11"));
            QCOMPARE(best_.value(QStringLiteral("daysSinceShot")).toInt(), int(kBestAgeDays));

            const QJsonObject diff = best_.value(QStringLiteral("changeFromBest")).toObject();
            QVERIFY2(!diff.isEmpty(), "changeFromBest must capture grind/yield/duration shifts");
            QCOMPARE(diff.value(QStringLiteral("grinderSetting")).toString(),
                     QStringLiteral("4.0 -> 4.4"));
        });
    }

    // -------------------------------------------------------------------
    // bean-freshness-followup: the best-recent-shot anchor carries its OWN
    // snapshotted lifecycle dates directly, distinct from the resolved shot —
    // the raw data the AI needs to notice the anchor came from a different,
    // longer-rested portion of the same bag. No hoisting, no derived flag.
    // -------------------------------------------------------------------
    void bestRecentShotBlock_carriesOwnLifecycleDates()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        const qint64 now = QDateTime::currentSecsSinceEpoch();

        withRawDb(path, QStringLiteral("best_lifecycle"), [&](QSqlDatabase& db) {
            // Best anchor: an older, longer-rested thaw.
            ShotRow best;
            best.uuid = QStringLiteral("uuid-best-lc");
            best.profileName = QStringLiteral("80's Espresso");
            best.profileKbId = QStringLiteral("kb-lc");
            best.beanBrand = QStringLiteral("Northbound");
            best.timestamp = now - 14 * kSecPerDay;
            best.doseWeight = 18.0;
            best.finalWeight = 38.0;
            best.duration = 30.0;
            best.enjoyment = 92;
            best.defrostDate = QStringLiteral("2026-05-01");
            // Also set the non-frozen lifecycle fields so the appended
            // positional read (cols 50/51 in loadShotRecordStatic) and the
            // block-emission branch are both exercised across a real DB read,
            // not just defrostDate.
            best.storageHint = QStringLiteral("airtight");
            best.openedDate = QStringLiteral("2026-05-02");
            const qint64 bestId = insertShot(db, best);
            QVERIFY(bestId > 0);

            // Current shot: a newer thaw (different portion).
            ShotRow current = best;
            current.uuid = QStringLiteral("uuid-current-lc");
            current.timestamp = now - kSecPerDay;
            current.enjoyment = 70;
            current.defrostDate = QStringLiteral("2026-05-13");
            const qint64 currentId = insertShot(db, current);
            QVERIFY(currentId > 0);

            const ShotProjection currentProj = projectionForShot(db, currentId);
            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-lc"), currentId, currentProj);

            QVERIFY(!best_.isEmpty());
            // The anchor carries its own defrostDate, distinct from the current
            // shot's 2026-05-13 — the mismatch signal is now visible.
            QCOMPARE(best_.value(QStringLiteral("defrostDate")).toString(),
                     QStringLiteral("2026-05-01"));
            QCOMPARE(currentProj.defrostDate, QStringLiteral("2026-05-13"));
            // storageHint/openedDate survive the DB read (cols 50/51) and reach
            // both the projection and the emitted block.
            QCOMPARE(best_.value(QStringLiteral("storageHint")).toString(),
                     QStringLiteral("airtight"));
            QCOMPARE(best_.value(QStringLiteral("openedDate")).toString(),
                     QStringLiteral("2026-05-02"));
            QCOMPARE(currentProj.storageHint, QStringLiteral("airtight"));
            QCOMPARE(currentProj.openedDate, QStringLiteral("2026-05-02"));
        });
    }

    // -------------------------------------------------------------------
    // bestRecentShotBlock — only-stale rated shots produce empty (>90d).
    // -------------------------------------------------------------------
    void bestRecentShotBlock_emptyWhenAllRatedShotsAreStale()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        // Window is 90 days; place the rated shot at 100 days old.
        const qint64 staleTimestamp = now
            - (DialingBlocks::kBestRecentShotWindowDays + 10) * kSecPerDay;

        withRawDb(path, QStringLiteral("best_stale"), [&](QSqlDatabase& db) {
            ShotRow stale;
            stale.uuid = QStringLiteral("uuid-stale");
            stale.profileName = QStringLiteral("80's Espresso");
            stale.profileKbId = QStringLiteral("kb-80s");
            stale.timestamp = staleTimestamp;
            stale.enjoyment = 95;
            QVERIFY(insertShot(db, stale) > 0);

            // Current shot — also rated, but it's the resolved shot so it
            // is excluded by the query's `id != ?` clause.
            ShotRow current;
            current.uuid = QStringLiteral("uuid-current");
            current.profileName = QStringLiteral("80's Espresso");
            current.profileKbId = QStringLiteral("kb-80s");
            current.timestamp = now - kSecPerDay;
            current.enjoyment = 70;
            const qint64 currentId = insertShot(db, current);
            QVERIFY(currentId > 0);

            const ShotProjection currentProj = projectionForShot(db, currentId);

            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-80s"), currentId, currentProj);
            QVERIFY2(best_.isEmpty(),
                     "no rated shot in the 90-day window must produce an empty block");
        });
    }

    // -------------------------------------------------------------------
    // grinderContextBlock — when bean-scoped query is sparse (<2 rows),
    // the cross-bean fallback fires and `allBeansSettings` carries the
    // wider observed set.
    // -------------------------------------------------------------------
    void grinderContextBlock_emitsAllBeansSettings_whenBeanScopedSparse()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        withRawDb(path, QStringLiteral("grinder_sparse"), [&](QSqlDatabase& db) {
            // One Northbound shot at setting 4.0 (sparse for that bean) —
            // forces the cross-bean fallback.
            ShotRow nb;
            nb.uuid = QStringLiteral("uuid-nb");
            nb.profileName = QStringLiteral("p");
            nb.beanBrand = QStringLiteral("Northbound");
            nb.grinderModel = QStringLiteral("Zero");
            nb.grinderSetting = QStringLiteral("4.0");
            QVERIFY(insertShot(db, nb) > 0);

            // Three Onyx shots at varying settings — cross-bean rich.
            for (const auto& setting : {QStringLiteral("3.5"),
                                         QStringLiteral("3.8"),
                                         QStringLiteral("4.2")}) {
                ShotRow o;
                o.uuid = QStringLiteral("uuid-onyx-") + setting;
                o.profileName = QStringLiteral("p");
                o.beanBrand = QStringLiteral("Onyx");
                o.grinderModel = QStringLiteral("Zero");
                o.grinderSetting = setting;
                QVERIFY(insertShot(db, o) > 0);
            }

            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Northbound"), onlyEquipmentPackage(db)));

            QCOMPARE(ctx.value(QStringLiteral("model")).toString(), QStringLiteral("Zero"));
            QCOMPARE(ctx.value(QStringLiteral("beverageType")).toString(),
                     QStringLiteral("espresso"));
            QVERIFY(ctx.value(QStringLiteral("isNumeric")).toBool());

            // Bean-scoped settings: only Northbound's single setting.
            const QJsonArray observed = ctx.value(QStringLiteral("settingsObserved")).toArray();
            QCOMPARE(observed.size(), 1);
            QCOMPARE(observed[0].toString(), QStringLiteral("4.0"));

            // Cross-bean fallback present — sees all four settings.
            const QJsonArray allBeans = ctx.value(QStringLiteral("allBeansSettings")).toArray();
            QCOMPARE(allBeans.size(), 4);
        });
    }

    // -------------------------------------------------------------------
    // grinderContextBlock — bean-scoped is rich, no allBeansSettings key.
    // -------------------------------------------------------------------
    void grinderContextBlock_omitsAllBeansSettings_whenBeanScopedRich()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        withRawDb(path, QStringLiteral("grinder_rich"), [&](QSqlDatabase& db) {
            // Three Northbound shots — bean-scoped is already rich
            // (settingsObserved.size() >= 2) so the fallback should not fire.
            for (const auto& setting : {QStringLiteral("4.0"),
                                         QStringLiteral("4.2"),
                                         QStringLiteral("4.4")}) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-nb-") + setting;
                r.profileName = QStringLiteral("p");
                r.beanBrand = QStringLiteral("Northbound");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = setting;
                QVERIFY(insertShot(db, r) > 0);
            }

            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Northbound"), onlyEquipmentPackage(db)));
            QCOMPARE(ctx.value(QStringLiteral("settingsObserved")).toArray().size(), 3);
            QVERIFY2(!ctx.contains(QStringLiteral("allBeansSettings")),
                     "rich bean-scoped result must not trigger cross-bean fallback");
            QCOMPARE(ctx.value(QStringLiteral("observedMinSetting")).toDouble(), 4.0);
            QCOMPARE(ctx.value(QStringLiteral("observedMaxSetting")).toDouble(), 4.4);
            // Typical (modal) step is 0.2 (gaps 0.2, 0.2). Allow tiny FP slack.
            const double step = ctx.value(QStringLiteral("stepSize")).toDouble();
            QVERIFY2(qAbs(step - 0.2) < 0.0001,
                     qPrintable(QString("expected stepSize ~0.2, got %1").arg(step)));
        });
    }

    // -------------------------------------------------------------------
    // stepSize — smallest commonly-repeated gap (deriveGrindStep). A lone
    // mistyped setting must not collapse the step the way a raw minimum-gap
    // would (the typo's gap occurs once and is skipped), while the finest step
    // the user makes repeatedly (0.25 here) wins over coarser ones.
    // -------------------------------------------------------------------
    void grinderContextBlock_stepSizeIsNoiseFiltered()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        withRawDb(path, QStringLiteral("grinder_step"), [&](QSqlDatabase& db) {
            auto add = [&](const QString& setting) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-step-") + setting;
                r.profileName = QStringLiteral("p");
                r.beanBrand = QStringLiteral("Cimarron");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = setting;
                QVERIFY(insertShot(db, r) > 0);
            };

            auto stepFor = [&]() {
                const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                    db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Cimarron"), onlyEquipmentPackage(db)));
                return ctx.value(QStringLiteral("stepSize")).toDouble();
            };

            // Clean history: gaps 0.5, 0.5, 0.25, 0.25 → modal tie resolved toward
            // the smaller step → 0.25.
            for (const auto& s : {QStringLiteral("7.5"), QStringLiteral("8"),
                                   QStringLiteral("8.5"), QStringLiteral("8.75"),
                                   QStringLiteral("9")})
                add(s);
            QVERIFY2(qAbs(stepFor() - 0.25) < 0.0001,
                     qPrintable(QString("clean history expected 0.25, got %1").arg(stepFor())));

            // A single mistyped 8.1: a raw minimum-gap would now report 0.1; the
            // modal estimator keeps 0.25 (the outlier's odd gaps each occur once).
            add(QStringLiteral("8.1"));
            QVERIFY2(qAbs(stepFor() - 0.25) < 0.0001,
                     qPrintable(QString("outlier must not collapse step; expected 0.25, got %1")
                                .arg(stepFor())));
        });
    }

    // -------------------------------------------------------------------
    // stepSize — omitted when fewer than 2 distinct numeric settings exist
    // (deriveGrindStep returns 0 → the block does not emit the field).
    // -------------------------------------------------------------------
    void grinderContextBlock_stepSizeOmittedWhenTooFewSettings()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        withRawDb(path, QStringLiteral("grinder_one"), [&](QSqlDatabase& db) {
            ShotRow r;
            r.uuid = QStringLiteral("uuid-one");
            r.profileName = QStringLiteral("p");
            r.grinderModel = QStringLiteral("Zero");
            r.grinderSetting = QStringLiteral("8");
            QVERIFY(insertShot(db, r) > 0);

            // Unscoped (empty beanBrand) so the single value isn't widened by the
            // cross-bean fallback — one distinct setting, no derivable step.
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QString(), onlyEquipmentPackage(db)));
            QVERIFY2(!ctx.contains(QStringLiteral("stepSize")),
                     "a single distinct setting must not yield a stepSize");
        });
    }

    // -------------------------------------------------------------------
    // stepSize — the finest REPEATED step wins even when coarser moves are
    // more common. Real-history regression: a user who dials mostly in 0.5
    // steps across beans but fine-tunes in 0.25 on the working bean should get
    // 0.25 (the grinder's resolution), not 0.5 (the most-common move). The step
    // is grinder-model-wide (all beans), independent of the bean argument.
    // -------------------------------------------------------------------
    void grinderContextBlock_stepSizeIsFinestRepeatedNotModal()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        withRawDb(path, QStringLiteral("grinder_finest"), [&](QSqlDatabase& db) {
            // Distinct settings for one grinder: five 0.5 gaps, two 0.25 gaps.
            // Modal gap would be 0.5; the finest repeated gap is 0.25.
            int i = 0;
            for (const auto& s : {QStringLiteral("5"), QStringLiteral("5.5"),
                                   QStringLiteral("6"), QStringLiteral("7"),
                                   QStringLiteral("7.5"), QStringLiteral("8"),
                                   QStringLiteral("8.5"), QStringLiteral("8.75"),
                                   QStringLiteral("9"), QStringLiteral("10"),
                                   QStringLiteral("12")}) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-finest-%1").arg(i++);
                r.profileName = QStringLiteral("p");
                r.beanBrand = QStringLiteral("Mixed");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = s;
                QVERIFY(insertShot(db, r) > 0);
            }

            // Bean argument present, but the step must be grinder-wide → 0.25.
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Mixed"), onlyEquipmentPackage(db)));
            const double step = ctx.value(QStringLiteral("stepSize")).toDouble();
            QVERIFY2(qAbs(step - 0.25) < 0.0001,
                     qPrintable(QString("finest repeated step expected 0.25, got %1").arg(step)));
        });
    }

    // stepSize — the step is grinder-model-wide, NOT scoped to the bean
    // argument. Split a grinder's settings across two beans: bean A alone is
    // coarse (would give 1.0), bean B contributes the fine 0.25 gaps. Passing
    // bean A must still yield 0.25, proving the step pools all beans.
    void grinderContextBlock_stepSizeIsGrinderWideAcrossBeans()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("grinder_wide"), [&](QSqlDatabase& db) {
            auto add = [&](const QString& bean, const QString& setting) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-gw-") + bean + setting;
                r.profileName = QStringLiteral("p");
                r.beanBrand = bean;
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = setting;
                QVERIFY(insertShot(db, r) > 0);
            };
            // Bean A: coarse only (8, 9 → a single 1.0 gap → no repeat).
            add(QStringLiteral("BeanA"), QStringLiteral("8"));
            add(QStringLiteral("BeanA"), QStringLiteral("9"));
            // Bean B: the fine steps (8.25, 8.5 → with 8 give two 0.25 gaps).
            add(QStringLiteral("BeanB"), QStringLiteral("8.25"));
            add(QStringLiteral("BeanB"), QStringLiteral("8.5"));

            // Scope the query to Bean A; the grinder-wide step still sees Bean B.
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("BeanA"), onlyEquipmentPackage(db)));
            const double step = ctx.value(QStringLiteral("stepSize")).toDouble();
            QVERIFY2(qAbs(step - 0.25) < 0.0001,
                     qPrintable(QString("grinder-wide step expected 0.25, got %1").arg(step)));
        });
    }

    // stepSize — a repeated gap below the 0.05 floor clamps up to 0.05.
    void grinderContextBlock_stepSizeFloorClamps()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("grinder_floor"), [&](QSqlDatabase& db) {
            int i = 0;
            for (const auto& s : {QStringLiteral("8"), QStringLiteral("8.02"),
                                   QStringLiteral("8.04")}) {  // two 0.02 gaps
                ShotRow r;
                r.uuid = QStringLiteral("uuid-floor-%1").arg(i++);
                r.profileName = QStringLiteral("p");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = s;
                QVERIFY(insertShot(db, r) > 0);
            }
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QString(), onlyEquipmentPackage(db)));
            const double step = ctx.value(QStringLiteral("stepSize")).toDouble();
            QVERIFY2(qAbs(step - 0.05) < 0.0001,
                     qPrintable(QString("sub-floor gap should clamp to 0.05, got %1").arg(step)));
        });
    }

    // stepSize — when NO gap repeats (scattered history), fall back to the
    // smallest gap rather than omitting or picking a coarse one.
    void grinderContextBlock_stepSizeFallsBackToSmallestWhenNoRepeat()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("grinder_norepeat"), [&](QSqlDatabase& db) {
            int i = 0;
            // Gaps 1, 2, 3 — all distinct, none repeats → smallest = 1.0.
            for (const auto& s : {QStringLiteral("5"), QStringLiteral("6"),
                                   QStringLiteral("8"), QStringLiteral("11")}) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-nr-%1").arg(i++);
                r.profileName = QStringLiteral("p");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = s;
                QVERIFY(insertShot(db, r) > 0);
            }
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QString(), onlyEquipmentPackage(db)));
            const double step = ctx.value(QStringLiteral("stepSize")).toDouble();
            QVERIFY2(qAbs(step - 1.0) < 0.0001,
                     qPrintable(QString("no-repeat fallback expected 1.0, got %1").arg(step)));
        });
    }

    // stepSize is decoupled from allNumeric: a grinder with a mix of numeric and
    // non-numeric settings still gets a numeric step, but min/max (which need an
    // all-numeric range) are omitted.
    void grinderContextBlock_stepSizeDecoupledFromAllNumeric()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("grinder_mixed"), [&](QSqlDatabase& db) {
            int i = 0;
            for (const auto& s : {QStringLiteral("8"), QStringLiteral("8.5"),
                                   QStringLiteral("9"), QStringLiteral("medium")}) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-mixed-%1").arg(i++);
                r.profileName = QStringLiteral("p");
                r.beanBrand = QStringLiteral("Mix");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = s;
                QVERIFY(insertShot(db, r) > 0);
            }
            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Mix"), onlyEquipmentPackage(db)));
            // Numeric step present (0.5 from 8/8.5/9)...
            QVERIFY2(qAbs(ctx.value(QStringLiteral("stepSize")).toDouble() - 0.5) < 0.0001,
                     "mixed history should still yield a numeric stepSize");
            // ...but the min/max range is omitted (history is not all-numeric).
            QVERIFY2(!ctx.contains(QStringLiteral("observedMinSetting")),
                     "min/max must be absent for a non-all-numeric history");
            QVERIFY(!ctx.contains(QStringLiteral("observedMaxSetting")));
            QVERIFY(!ctx.value(QStringLiteral("isNumeric")).toBool());
        });
    }

    // summarizeStructuredNext renders a recommended rpm as a whole-number
    // predicted part, alongside the grind (the advisor's RPM coaching output).
    void summarizeStructuredNext_rendersRpm()
    {
        QJsonObject sn;
        sn["grinderSetting"] = QStringLiteral("8.5");
        sn["rpm"] = 1350;
        const DialingBlocks::StructuredNextSummary s =
            DialingBlocks::summarizeStructuredNext(sn);
        QVERIFY(s.predictedParts.contains(QStringLiteral("grinder 8.5")));
        QVERIFY2(s.predictedParts.contains(QStringLiteral("1350 RPM")),
                 "recommended rpm should appear as a whole-number predicted part");
    }

    // -------------------------------------------------------------------
    // End-to-end parity (issue #1044's headline test) — the four blocks
    // should be byte-equivalent regardless of which "surface" assembled
    // them, because both paths call the same DialingBlocks helpers.
    // The check guards against future drift if either path adds a
    // post-processing step.
    // -------------------------------------------------------------------
    void endToEndParity_inAppEnrichmentMatchesDialingGetContext()
    {
        const QString path = freshDbPath();
        initAndClose(path);

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        constexpr qint64 kBestAgeDays = 7;
        const qint64 bestTs = now - kBestAgeDays * kSecPerDay;

        withRawDb(path, QStringLiteral("parity"), [&](QSqlDatabase& db) {
            ShotRow base;
            base.profileName = QStringLiteral("80's Espresso");
            base.profileKbId = QStringLiteral("kb-80s");
            base.beanBrand = QStringLiteral("Northbound");
            base.beanType = QStringLiteral("Spring Tour");
            base.grinderBrand = QStringLiteral("Niche");
            base.grinderModel = QStringLiteral("Zero");
            base.grinderBurrs = QStringLiteral("63mm");

            ShotRow s1 = base; s1.uuid = QStringLiteral("u1");
            s1.timestamp = now - 3 * kSecPerDay - 30 * 60;
            s1.grinderSetting = QStringLiteral("4.0");
            s1.doseWeight = 18.0; s1.finalWeight = 36.0; s1.duration = 30.0;
            QVERIFY(insertShot(db, s1) > 0);

            ShotRow s2 = base; s2.uuid = QStringLiteral("u2");
            s2.timestamp = now - 3 * kSecPerDay;
            s2.grinderSetting = QStringLiteral("4.2");
            s2.doseWeight = 18.0; s2.finalWeight = 35.0; s2.duration = 28.0;
            QVERIFY(insertShot(db, s2) > 0);

            ShotRow rated = base; rated.uuid = QStringLiteral("u-best");
            rated.timestamp = bestTs;
            rated.grinderSetting = QStringLiteral("4.0");
            rated.enjoyment = 90;
            rated.doseWeight = 18.0; rated.finalWeight = 38.0; rated.duration = 30.0;
            const qint64 bestId = insertShot(db, rated);
            QVERIFY(bestId > 0);

            ShotRow current = base; current.uuid = QStringLiteral("u-current");
            current.timestamp = now - kSecPerDay / 2;
            current.grinderSetting = QStringLiteral("4.4");
            current.doseWeight = 18.0; current.finalWeight = 36.0; current.duration = 27.0;
            const qint64 currentId = insertShot(db, current);
            QVERIFY(currentId > 0);

            // Surface emulators that mirror each production call site's
            // distinct argument-derivation logic. If either surface drifts
            // (e.g., starts passing a different kbId, a different
            // historyLimit, or builds the grinder block from a different
            // shot's metadata) the assertions below catch it.
            //
            // MCP path (`mcptools_dialing.cpp`): loads the record by id,
            // pulls profileKbId from the record, derives grinder/bean from
            // the converted projection, passes the caller-supplied
            // historyLimit. Mirrored here.
            constexpr int kHistoryLimit = 10;
            auto runMcpSurface = [&](qint64 shotId) {
                ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                ShotProjection sp = ShotHistoryStorage::convertShotRecord(rec);
                const QString kbId = rec.profileKbId;
                QJsonArray  sessions = DialingBlocks::buildDialInSessionsBlock(
                    db, kbId, shotId, kHistoryLimit, sp);
                QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                    db, kbId, shotId, sp);
                QJsonObject grinder = DialingBlocks::buildGrinderContextBlock(
                    db, sp);
                return std::make_tuple(sessions, best, grinder);
            };

            // In-app advisor path (`aimanager.cpp` analyzeShotWithMetadata
            // bg-thread closure): caller passes kbId + excludeId, the
            // closure loads the resolved shot inside `withTempDb`, and
            // derives the grinder block's args from the projection. The
            // historyLimit is hard-coded to 5 in production, but parity
            // is about *consistent argument derivation given the same
            // historyLimit*, not about the limits being equal. We pass
            // the same `kHistoryLimit` here so any drift in how the args
            // are derived shows up as a JSON diff.
            auto runInAppSurface = [&](const QString& kbId, qint64 excludeId) {
                ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, excludeId);
                ShotProjection sp = ShotHistoryStorage::convertShotRecord(rec);
                QJsonArray  sessions = DialingBlocks::buildDialInSessionsBlock(
                    db, kbId, excludeId, kHistoryLimit, sp);
                QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                    db, kbId, excludeId, sp);
                QJsonObject grinder = DialingBlocks::buildGrinderContextBlock(
                    db, sp);
                return std::make_tuple(sessions, best, grinder);
            };

            const auto [sessionsA, bestA, grinderA] = runMcpSurface(currentId);
            // The in-app surface gets `kbId` from a different upstream path
            // (the caller's metadata), but for this DB the value should
            // resolve to `record.profileKbId`. If a future caller drifts
            // (e.g., starts passing the profileName instead), the dialIn
            // and bestRecent blocks empty out and this test fails.
            const auto [sessionsB, bestB, grinderB] = runInAppSurface(
                QStringLiteral("kb-80s"), currentId);

            const auto toJson = [](const auto& v) {
                return QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact));
            };

            QCOMPARE(toJson(sessionsA), toJson(sessionsB));
            QCOMPARE(toJson(bestA),     toJson(bestB));
            QCOMPARE(toJson(grinderA),  toJson(grinderB));
            QVERIFY(!sessionsA.isEmpty());
            QVERIFY(!bestA.isEmpty());
            QVERIFY(!grinderA.isEmpty());

            // Negative control: prove the assertions are sensitive to
            // argument drift. Re-run the in-app surface with a wrong
            // kbId — the dialIn and bestRecent blocks must empty out so
            // the JSON diverges, demonstrating the test isn't vacuous.
            const auto [wrongSessions, wrongBest, wrongGrinder] = runInAppSurface(
                QStringLiteral("kb-WRONG"), currentId);
            QVERIFY2(toJson(sessionsA) != toJson(wrongSessions),
                     "parity test must fail when the in-app surface drifts on kbId");
            QVERIFY2(toJson(bestA) != toJson(wrongBest),
                     "parity test must fail when the in-app surface drifts on kbId");
            // grinderContext does not depend on kbId, so it stays equal —
            // that's the correct invariant, not a test bug.
            QCOMPARE(toJson(grinderA), toJson(wrongGrinder));
        });
    }

    // ---------------------------------------------------------------
    // recentAdvice block (issue #1053). Builds attribution between a
    // prior advisor turn (with structuredNext) and the user's actual
    // follow-up shot.
    // ---------------------------------------------------------------

    static QJsonObject sampleStructuredNext()
    {
        return QJsonObject{
            {"grinderSetting", "4.75"},
            {"expectedDurationSec", QJsonArray{32, 38}},
            {"expectedFlowMlPerSec", QJsonArray{1.0, 1.5}},
            {"successCondition", "OK"},
            {"reasoning", "Slow flow toward profile target"}
        };
    }

    void recentAdvice_qualifyingTurnRendersWithAdherenceFollowed()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);

        // Prior shot (the one the advisor was asked about) at T-2h with
        // grinder 5.0; follow-up shot at T-1h on the same profile with
        // grinder 4.75 (matching the recommendation), within the
        // expectedDurationSec / expectedFlowMlPerSec ranges.
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const qint64 priorTs = nowSec - 2 * 3600;
        const qint64 nextTs = nowSec - 1 * 3600;

        qint64 priorId = -1, nextId = -1;
        withRawDb(dbPath, "rec_advice_followed", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "uuid-prior", .timestamp = priorTs,
                .profileName = "80's Espresso", .profileKbId = "kb-80s",
                .duration = 28.0, .finalWeight = 36.0, .doseWeight = 18.0,
                .grinderSetting = "5.0",
                .enjoyment = 0
            });
            QVERIFY(priorId > 0);
            nextId = insertShot(db, ShotRow{
                .uuid = "uuid-next", .timestamp = nextTs,
                .profileName = "80's Espresso", .profileKbId = "kb-80s",
                .duration = 35.0, .finalWeight = 42.0, .doseWeight = 18.0,
                .grinderSetting = "4.75",
                .enjoyment = 75, .espressoNotes = "balanced and sweet"
            });
            QVERIFY(nextId > 0);

            DialingBlocks::RecentAdviceInputs in;
            in.turns = QList<AIConversation::HistoricalAssistantTurn>{
                AIConversation::HistoricalAssistantTurn{
                    priorId, "Try grinder 4.75.", sampleStructuredNext()
                }
            };
            in.currentProfileKbId = "kb-80s";
            // currentShotId points at a hypothetical "now-being-analyzed"
            // shot (later than nextId). Set higher than the follow-up so
            // the follow-up qualifies.
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            const QJsonObject entry = out.first().toObject();
            QCOMPARE(entry.value("turnsAgo").toInt(), 1);
            const QJsonObject ur = entry.value("userResponse").toObject();
            QCOMPARE(ur.value("adherence").toString(), QStringLiteral("followed"));
            QCOMPARE(ur.value("outcomeRating0to100").toInt(), 75);
            QCOMPARE(ur.value("outcomeNotes").toString(), QStringLiteral("balanced and sweet"));
            const QJsonObject rng = ur.value("outcomeInPredictedRange").toObject();
            QVERIFY(rng.value("duration").toBool());  // 35s in [32,38]
            // avg flow = 42/35 = 1.2 ml/s, in [1.0, 1.5]
            QVERIFY(rng.value("flow").toBool());
        });
    }

    // synthesizeRecommendationSummary's fallback (used when the prior turn's
    // structuredNext omits `reasoning`) was previously untested — the shared
    // DialingBlocks::summarizeStructuredNext extraction risked silently
    // widening its both-duration-and-flow-required gate for the "expect"
    // clause into a per-field-independent one. Pins the original gate: a
    // single range alone must NOT produce a partial "expect" clause.
    void recentAdvice_synthesizedRecommendation_requiresBothDurationAndFlowForExpectClause()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);

        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const qint64 priorTs = nowSec - 2 * 3600;
        const qint64 nextTs = nowSec - 1 * 3600;

        withRawDb(dbPath, "rec_advice_synth_summary", [&](QSqlDatabase& db) {
            const qint64 priorId = insertShot(db, ShotRow{
                .uuid = "uuid-prior-synth", .timestamp = priorTs,
                .profileName = "80's Espresso", .profileKbId = "kb-80s-synth",
                .duration = 28.0, .finalWeight = 36.0, .doseWeight = 18.0,
                .grinderSetting = "5.0", .enjoyment = 0
            });
            QVERIFY(priorId > 0);
            const qint64 nextId = insertShot(db, ShotRow{
                .uuid = "uuid-next-synth", .timestamp = nextTs,
                .profileName = "80's Espresso", .profileKbId = "kb-80s-synth",
                .duration = 35.0, .finalWeight = 42.0, .doseWeight = 18.0,
                .grinderSetting = "4.75", .enjoyment = 0
            });
            QVERIFY(nextId > 0);

            // No `reasoning` — forces the synthesized-summary fallback.
            // Only expectedDurationSec present, no expectedFlowMlPerSec.
            const QJsonObject durationOnly{
                {"grinderSetting", "4.75"},
                {"expectedDurationSec", QJsonArray{32, 38}},
                {"successCondition", "OK"}
            };
            DialingBlocks::RecentAdviceInputs in;
            in.turns = QList<AIConversation::HistoricalAssistantTurn>{
                AIConversation::HistoricalAssistantTurn{priorId, "unused prose", durationOnly}
            };
            in.currentProfileKbId = "kb-80s-synth";
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            const QString recommendation = out.first().toObject().value("recommendation").toString();
            QVERIFY2(recommendation.contains(QStringLiteral("grinder 4.75")),
                     "predicted field must still be summarized");
            QVERIFY2(!recommendation.contains(QStringLiteral("expect")),
                     "a single range (duration without flow) must not produce a partial expect clause");
        });
    }

    void recentAdvice_omitsRatingWhenUnrated()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

        qint64 priorId = -1;
        withRawDb(dbPath, "rec_advice_unrated", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 7200,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });
            insertShot(db, ShotRow{
                .uuid = "u-next", .timestamp = nowSec - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 35, .finalWeight = 42, .doseWeight = 18,
                .grinderSetting = "4.75",
                .enjoyment = 0  // unrated
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{
                priorId, "advice", sampleStructuredNext()}};
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            const QJsonObject ur = out.first().toObject().value("userResponse").toObject();
            QVERIFY2(!ur.contains("outcomeRating0to100"),
                     "outcomeRating0to100 must be omitted when the actual shot is unrated");
            // outcomeInPredictedRange survives — curve-based attribution
            // doesn't require a taste signal.
            QVERIFY(ur.value("outcomeInPredictedRange").toObject().contains("duration"));
        });
    }

    void recentAdvice_crossProfileFiltersOut()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

        qint64 priorId = -1;
        withRawDb(dbPath, "rec_advice_xprof", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "u-A", .timestamp = nowSec - 7200,
                .profileName = "Profile A", .profileKbId = "kb-A",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{
                priorId, "advice", sampleStructuredNext()}};
            in.currentProfileKbId = "kb-B";  // different profile
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QVERIFY2(out.isEmpty(),
                     "cross-profile prior turn must be filtered out, leaving recentAdvice empty");
        });
    }

    void recentAdvice_ignoredWhenUserDidNotFollow()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

        qint64 priorId = -1;
        withRawDb(dbPath, "rec_advice_ignored", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 7200,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });
            // User kept grinder at 5.0 — ignored the 4.75 recommendation.
            insertShot(db, ShotRow{
                .uuid = "u-next", .timestamp = nowSec - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{
                priorId, "advice", sampleStructuredNext()}};
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            const QJsonObject ur = out.first().toObject().value("userResponse").toObject();
            QCOMPARE(ur.value("adherence").toString(), QStringLiteral("ignored"));
        });
    }

    // Shared driver for the grinderSetting adherence cases. Seeds a prior shot
    // and a follow-up, runs one recommendation through buildRecentAdviceBlock,
    // and returns the adherence verdict.
    // priorDose/nextDose default to the same value so every existing caller
    // varies only the grind, as before. The ranges-only cases need a second
    // axis: with nothing recommended, the dose is one of the things that
    // decides whether the predicted repeat actually happened.
    QString adherenceForStructured(const QString& tag, const QJsonObject& sn,
                                   const QString& priorGrind, const QString& nextGrind,
                                   double priorDose = 18, double nextDose = 18,
                                   qint64 priorRpm = 0, qint64 nextRpm = 0)
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        QString verdict;

        withRawDb(dbPath, tag, [&](QSqlDatabase& db) {
            const qint64 priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 7200,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = priorDose,
                .grinderSetting = priorGrind, .rpm = priorRpm
            });
            insertShot(db, ShotRow{
                .uuid = "u-next", .timestamp = nowSec - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = nextDose,
                .grinderSetting = nextGrind, .rpm = nextRpm
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{priorId, "advice", sn}};
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            verdict = out.first().toObject().value("userResponse").toObject()
                          .value("adherence").toString();
        });
        return verdict;
    }

    // Convenience for the common case: vary only the recommended grind.
    QString adherenceFor(const QString& tag, const QJsonValue& recommendedGrind,
                         const QString& priorGrind, const QString& nextGrind)
    {
        QJsonObject sn = sampleStructuredNext();
        sn["grinderSetting"] = recommendedGrind;
        return adherenceForStructured(tag, sn, priorGrind, nextGrind);
    }

    // A ranges-only turn recommends no parameter change, so the implicit
    // instruction is "run this again, here is what I expect". That is an
    // experiment, and the verdict has to say whether it ran.
    //
    // Repeat on the same setup: it ran. This is the case that must keep
    // working — most ranges-only turns are ordinary "try that again" advice.
    void recentAdvice_rangesOnlyRepeatOnSameSetupIsFollowed()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        QCOMPARE(adherenceForStructured("adh_ranges_same", sn, "9.0", "9.0"),
                 QStringLiteral("followed"));
    }

    // Changed the grind nobody asked them to change: the predicted repeat did
    // not happen, so the prediction was never tested. Asserting BOTH verdicts
    // because "followed" is the specific wrong answer this replaced — it told
    // the model "the experiment ran and failed", so a bad outcome made it
    // revise a direction its prediction never covered.
    void recentAdvice_rangesOnlyWithChangedGrindIsIgnored()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        const QString verdict =
            adherenceForStructured("adh_ranges_grind", sn, "9.0", "7.5");
        QCOMPARE(verdict, QStringLiteral("ignored"));
        QVERIFY2(verdict != QStringLiteral("followed"),
                 "a regrind is not the controlled repeat that was predicted");
    }

    // Same for a dose change, and the tolerance has to hold: 18.0 -> 19.5 is a
    // decision, 18.0 -> 18.2 is scale noise and must stay "followed".
    void recentAdvice_rangesOnlyDoseChangeRespectsTolerance()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        QCOMPARE(adherenceForStructured("adh_ranges_dose_big", sn, "9.0", "9.0", 18.0, 19.5),
                 QStringLiteral("ignored"));
        QCOMPARE(adherenceForStructured("adh_ranges_dose_noise", sn, "9.0", "9.0", 18.0, 18.2),
                 QStringLiteral("followed"));
    }

    // Notation is not a setup change. "1 + 4" and "1+4" are the same dial
    // position, and a shot recorded with the RPM annotation is the same
    // setting as one recorded without it — neither may read as a regrind.
    void recentAdvice_rangesOnlyNotationIsNotAChange()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        QCOMPARE(adherenceForStructured("adh_ranges_compound", sn, "1 + 4", "1+4"),
                 QStringLiteral("followed"));
        QCOMPARE(adherenceForStructured("adh_ranges_annot", sn, "23.5", "23.5 1400rpm"),
                 QStringLiteral("followed"));
    }

    // A blank grinder setting is missing data, not proof of a regrind. Older
    // shots have no recorded setting, and downgrading those to "ignored" would
    // rewrite long-settled history on no evidence at all.
    // Lettered dials ("3F" -> "3C") are an unmistakable regrind, but they
    // parse as NO number — leadingDialNumber() only knows the numeric and
    // compound shapes. The first version of sameGrinderSetting() answered
    // "same" for anything it could not compare numerically, which swallowed
    // this case whole and scored it "followed": the precise defect this
    // function exists to catch, surviving inside the fix for it.
    void recentAdvice_rangesOnlyLetteredRegrindIsIgnored()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        const QString verdict =
            adherenceForStructured("adh_ranges_lettered", sn, "3F", "3C");
        QCOMPARE(verdict, QStringLiteral("ignored"));
        QVERIFY2(verdict != QStringLiteral("followed"),
                 "a lettered-dial regrind is still a regrind");
    }

    // The RPM axis, which nothing reached before: ShotRow had no rpm field, so
    // every fixture wrote NULL and the comparison skipped. 1400 -> 1200 is a
    // deliberate move; 1400 -> 1410 is inside the +/-25 band and is not.
    void recentAdvice_rangesOnlyRpmChangeRespectsTolerance()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        QCOMPARE(adherenceForStructured("adh_ranges_rpm_big", sn, "9.0", "9.0",
                                        18, 18, 1400, 1200),
                 QStringLiteral("ignored"));
        QCOMPARE(adherenceForStructured("adh_ranges_rpm_noise", sn, "9.0", "9.0",
                                        18, 18, 1400, 1410),
                 QStringLiteral("followed"));
        // Unrecorded on one side is missing data, not a change.
        QCOMPARE(adherenceForStructured("adh_ranges_rpm_unset", sn, "9.0", "9.0",
                                        18, 18, 1400, 0),
                 QStringLiteral("followed"));
    }

    // A profile RENAME must not read as a setup change. buildRecentAdviceBlock
    // only ever pairs shots sharing profile_kb_id, so differing stored titles
    // mean the user renamed the profile between them — nothing about the
    // coffee moved. The profile comparison was removed for this reason; this
    // pins that it stays removed.
    void recentAdvice_rangesOnlyProfileRenameIsNotAChange()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");

        withRawDb(dbPath, "adh_ranges_rename", [&](QSqlDatabase& db) {
            const qint64 priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 7200,
                .profileName = "Old Title", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "9.0"
            });
            insertShot(db, ShotRow{
                .uuid = "u-next", .timestamp = nowSec - 3600,
                .profileName = "New Title", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "9.0"
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{priorId, "advice", sn}};
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            QCOMPARE(out.first().toObject().value("userResponse").toObject()
                         .value("adherence").toString(),
                     QStringLiteral("followed"));
        });
    }

    void recentAdvice_rangesOnlyUnknownSettingStaysFollowed()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove("grinderSetting");
        QCOMPARE(adherenceForStructured("adh_ranges_blank_next", sn, "9.0", ""),
                 QStringLiteral("followed"));
        QCOMPARE(adherenceForStructured("adh_ranges_blank_prior", sn, "", "9.0"),
                 QStringLiteral("followed"));
    }

    // Prose in `grinderSetting` — "a touch coarser than 9" (GPT-5.6 Terra,
    // observed live 2026-07-30). It matches no setting and parses as no number,
    // so whether the user followed it is UNKNOWABLE.
    //
    // It must not report "ignored" (the user may have complied) and must not
    // report "followed" (they may have changed nothing) — "followed" is the
    // worse of the two, because the system prompt reads it as "the experiment
    // ran" and tells the model to revise direction or commit harder on that
    // basis. Both wrong answers are asserted against here, because the first
    // version of this fix produced the second one.
    void recentAdvice_proseGrinderSettingIsUnclear()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("grinderSetting is prose"));
        // User DID move — still unknowable, because the advice named no value.
        QCOMPARE(adherenceFor("adh_prose_moved", "a touch coarser than 9", "9.0", "8.75"),
                 QStringLiteral("unclear"));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("grinderSetting is prose"));
        // User did NOT move. Same verdict — the point is that the recommendation
        // is unscoreable, so the follow-up shot cannot change the answer. If this
        // case ever diverges from the one above, scoring is reading the seeded
        // data it must not be able to reach.
        QCOMPARE(adherenceFor("adh_prose_still", "slightly coarser than 9", "9.0", "9.0"),
                 QStringLiteral("unclear"));
    }

    // Compound notation ("1 + 4") is a REAL setting for every Eureka Mignon
    // the Eureka Mignon and 1Zpresso families — not prose, despite the spaces.
    // An earlier version of the guard rejected on any whitespace and silently
    // stopped scoring every one of them.
    void recentAdvice_compoundNotationScoresNormally()
    {
        QCOMPARE(adherenceFor("adh_compound_followed", "1 + 4", "1 + 2", "1 + 4"),
                 QStringLiteral("followed"));
        QCOMPARE(adherenceFor("adh_compound_ignored", "1 + 4", "1 + 2", "1 + 2"),
                 QStringLiteral("ignored"));
    }

    // A non-numeric single-token setting ("3F") scores via exact string
    // equality. Both verdicts are exercised so the test cannot pass by simply
    // never scoring.
    void recentAdvice_nonNumericSettingStillScores()
    {
        QCOMPARE(adherenceFor("adh_3f_followed", "3F", "3C", "3F"),
                 QStringLiteral("followed"));
        QCOMPARE(adherenceFor("adh_3f_ignored", "3F", "3C", "3C"),
                 QStringLiteral("ignored"));
    }

    // Incidental padding is not prose. " 8.75 " is the same dial position as
    // "8.75" and must score, not fall into the unscoreable path.
    void recentAdvice_paddedSettingIsTrimmedNotRejected()
    {
        QCOMPARE(adherenceFor("adh_padded", " 8.75 ", "9.0", "8.75"),
                 QStringLiteral("followed"));
    }

    // The schema says grinderSetting is a string, but a model may emit it
    // unquoted. QJsonValue::toString() returns an EMPTY QString for a non-string
    // type, which previously read as "no grind change" and scored the turn as
    // fully followed — a recommendation nobody could check, reported as complied
    // with. It must be unscoreable instead.
    void recentAdvice_numericJsonGrinderSettingIsUnclear()
    {
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("grinderSetting is not a JSON string"));
        QCOMPARE(adherenceFor("adh_numeric_json", QJsonValue(8.75), "9.0", "9.0"),
                 QStringLiteral("unclear"));
    }

    // Single-word prose ("coarser") has no whitespace. An earlier version of
    // looksLikeSetting() returned true for ANY whitespace-free string, so this
    // was classified scoreable, failed the numeric compare, and reported
    // "ignored" — the false-non-adherence bug the guard exists to prevent,
    // surviving in the one shape the prose tests didn't cover.
    void recentAdvice_singleWordProseIsUnclear()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("grinderSetting is prose"));
        QCOMPARE(adherenceFor("adh_word_prose", "coarser", "9.0", "8.75"),
                 QStringLiteral("unclear"));
    }

    // The comparator must accept every form looksLikeSetting() admits.
    // Compound spacing is not meaningful: recommending "1 + 4" against a
    // recorded "1+4" is adherence. Before grinderMatches() went through
    // GrinderAliases, only byte-identical strings could score.
    void recentAdvice_compoundSpacingDoesNotDecideAdherence()
    {
        QCOMPARE(adherenceFor("adh_compound_spacing", "1 + 4", "1 + 2", "1+4"),
                 QStringLiteral("followed"));
    }

    // Variable-RPM grinders commonly annotate the recorded setting with the
    // RPM. A recommended "23.5" against a recorded "23.5 1400rpm" is the user
    // doing exactly what was asked; a bare QString::toDouble() rejected the
    // trailing text and scored it "ignored".
    void recentAdvice_annotatedSettingStillScores()
    {
        QCOMPARE(adherenceFor("adh_annotated", "23.5", "24 1400rpm", "23.5 1400rpm"),
                 QStringLiteral("followed"));
    }

    // rpm had the same fail-open hazard grinderSetting was fixed for:
    // QJsonValue::toInt() yields 0 for a JSON string, and the matcher treated
    // <= 0 as a free match, so a malformed rpm scored "followed" — "the
    // experiment ran". It must be unscoreable.
    void recentAdvice_malformedRpmIsUnclearNotFollowed()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove(QStringLiteral("grinderSetting"));   // rpm is the only axis
        sn["rpm"] = QStringLiteral("1400");            // string, not number

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rpm is not a JSON number"));
        QCOMPARE(adherenceForStructured("adh_rpm_string", sn, "9.0", "9.0"),
                 QStringLiteral("unclear"));
    }

    // A model writing rpm: 0 to mean "unchanged" violates the schema (which says
    // omit), and previously bought a free match. Also unscoreable.
    void recentAdvice_zeroRpmIsUnclearNotFollowed()
    {
        QJsonObject sn = sampleStructuredNext();
        sn.remove(QStringLiteral("grinderSetting"));
        sn["rpm"] = 0;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rpm is 0"));
        QCOMPARE(adherenceForStructured("adh_rpm_zero", sn, "9.0", "9.0"),
                 QStringLiteral("unclear"));
    }

    void recentAdvice_emptyTurnsOmitsBlock()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        withRawDb(dbPath, "rec_advice_empty", [&](QSqlDatabase& db) {
            DialingBlocks::RecentAdviceInputs in;
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;
            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QVERIFY(out.isEmpty());
        });
    }

    void recentAdvice_skipsTurnsWithoutFollowUpShot()
    {
        // A prior turn with a shotId on the right profile but no later
        // shot recorded → entry is skipped (user hasn't pulled a
        // follow-up yet, attribution is impossible).
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

        qint64 priorId = -1;
        withRawDb(dbPath, "rec_advice_no_followup", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{
                priorId, "advice", sampleStructuredNext()}};
            in.currentProfileKbId = "kb";
            in.currentShotId = priorId;  // analyzing this same shot, no later one

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QVERIFY2(out.isEmpty(),
                     "prior turn without a follow-up shot must be skipped");
        });
    }

    // Parity test (in-app surface vs MCP surface) lives in tst_aimanager.cpp
    // where AIManager + AIConversation are linked. This file's test binary
    // intentionally avoids the AI module to stay focused on the SQL block
    // builders.
    // -----------------------------------------------------------------
    // bestRecentShot — user-rated only (Layer 3 inferred fallback removed)
    // -----------------------------------------------------------------

    void bestRecentShot_highestUserRatedWins()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        withRawDb(dbPath, "best_user_only", [&](QSqlDatabase& db) {
            insertShot(db, ShotRow{
                .uuid = "user70", .timestamp = now - 24*3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "4.0", .enjoyment = 70
            });
            insertShot(db, ShotRow{
                .uuid = "user85", .timestamp = now - 12*3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "4.0", .enjoyment = 85
            });
            const qint64 currentId = insertShot(db, ShotRow{
                .uuid = "current", .timestamp = now - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "4.0", .enjoyment = 0
            });

            ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, currentId);
            const ShotProjection cur = ShotHistoryStorage::convertShotRecord(rec);

            const QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                db, "kb", currentId, cur);
            QVERIFY(!best.isEmpty());
            QCOMPARE(best.value("enjoyment0to100").toInt(), 85);
            // Layer 3 removed: bestRecentShot no longer carries `confidence`.
            QVERIFY2(!best.contains("confidence"),
                     "bestRecentShot must not carry the removed `confidence` field");
        });
    }

    void bestRecentShot_emptyWhenNoCandidates()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        withRawDb(dbPath, "best_empty", [&](QSqlDatabase& db) {
            // Only unrated rows in the window — block must be omitted.
            insertShot(db, ShotRow{
                .uuid = "u-cur", .timestamp = now - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "4.0", .enjoyment = 0
            });
            const qint64 currentId = insertShot(db, ShotRow{
                .uuid = "current", .timestamp = now - 60,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "4.0", .enjoyment = 0
            });
            ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, currentId);
            const ShotProjection cur = ShotHistoryStorage::convertShotRecord(rec);

            const QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                db, "kb", currentId, cur);
            QVERIFY2(best.isEmpty(), "no rated rows → block omitted");
        });
    }

    // ------------------------------------------------------------------
    // buildGrinderCalibrationBlock — rewritten for issue #1223
    // (openspec fix-grinder-calibration-cross-profile). Within-batch
    // paired slope, dimensionless gate, per-batch anchor, hard cap,
    // directional fallback. KB UGS: d-flow 0.5, d-flow-q-variant 1.0,
    // adaptive-v2 1.25, allonge 8.0, londinium 0.0, turboturbo 6.0,
    // blooming-espresso -0.5, gentle-and-sweet 2.0.
    //
    // Fixtures default beanBrand/beanType empty and use close
    // timestamps, so every dialed-in shot is one undated roast batch.
    // A shot qualifies as dialed-in via enjoyment >= 50 (insertShot
    // writes no roast_date / drink_tds; targetWeight → yield_override,
    // not profile_json, so the on-target path is not exercised here).
    // ------------------------------------------------------------------

    // Helper: seed a UGS-placed dialed-in shot on the shared batch.
    qint64 calSeed(QSqlDatabase& db, const QString& uuid, qint64 ts,
                   const QString& name, const QString& kbId,
                   const QString& setting, const QString& model = QStringLiteral("Niche Zero"),
                   const QString& burrs = QStringLiteral("63mm conical"))
    {
        // Non-empty bean so the shot is batch-knowable (#1236 empty-bean
        // guard); shared across calSeed calls so they form one roast batch.
        return insertShot(db, ShotRow{
            .uuid = uuid, .timestamp = ts,
            .profileName = name, .profileKbId = kbId,
            .finalWeight = 36.0,
            .beanBrand = QStringLiteral("TestRoaster"),
            .beanType = QStringLiteral("TestBean"),
            .grinderModel = model, .grinderBurrs = burrs,
            .grinderSetting = setting, .enjoyment = 80 });
    }

    // calSeed with an explicit basket, so two shots can share a grinder and a
    // coffee while living in different equipment packages.
    qint64 calSeedBasket(QSqlDatabase& db, const QString& uuid, qint64 ts,
                         const QString& name, const QString& kbId,
                         const QString& setting,
                         const QString& basketBrand, const QString& basketModel)
    {
        return insertShot(db, ShotRow{
            .uuid = uuid, .timestamp = ts,
            .profileName = name, .profileKbId = kbId,
            .finalWeight = 36.0,
            .beanBrand = QStringLiteral("TestRoaster"),
            .beanType = QStringLiteral("TestBean"),
            .grinderModel = QStringLiteral("Niche Zero"),
            .grinderBurrs = QStringLiteral("63mm conical"),
            .basketBrand = basketBrand, .basketModel = basketModel,
            .grinderSetting = setting, .enjoyment = 80 });
    }

    static bool calAnyHasRgs(const QJsonObject& block)
    {
        const QJsonArray ps = block.value(QStringLiteral("profiles")).toArray();
        for (const QJsonValue& v : ps)
            if (v.toObject().contains(QStringLiteral("rgs"))) return true;
        return false;
    }
    static QJsonObject calProfile(const QJsonObject& block, const QString& name)
    {
        const QJsonArray ps = block.value(QStringLiteral("profiles")).toArray();
        for (const QJsonValue& v : ps)
            if (v.toObject().value(QStringLiteral("profileName")).toString() == name)
                return v.toObject();
        return {};
    }

    void calibrationBlock_emptyWhenGrinderModelEmpty()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_empty_model"), [&](QSqlDatabase& db) {
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral(""), QStringLiteral(""), QStringLiteral("espresso"), 0);
            QVERIFY2(r.isEmpty(), "empty grinderModel → empty block");
        });
    }

    void calibrationBlock_emptyWhenFilterBeverage()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_filter_bev"), [&](QSqlDatabase& db) {
            QVERIFY(DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral(""),
                QStringLiteral("filter"), 0).isEmpty());
            QVERIFY(DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral(""),
                QStringLiteral("pourover"), 0).isEmpty());
        });
    }

    void calibrationBlock_emptyWhenResolvedShotInvalid()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_bad_shot"), [&](QSqlDatabase& db) {
            // No such shot id → resolved shot invalid → empty block.
            QTest::ignoreMessage(QtWarningMsg,
                "ShotHistoryStorage::loadShotRecordStatic: Shot not found: 999999");
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), 999999);
            QVERIFY2(r.isEmpty(), "invalid resolved shot → empty block");
        });
    }

    void calibrationBlock_emptyWhenNoDialedInShots()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_no_dialed"), [&](QSqlDatabase& db) {
            // Current shot exists (resolved OK) but is unrated, no TDS,
            // no target → not dialed-in → no rows → empty block.
            const qint64 cur = insertShot(db, ShotRow{
                .uuid = QStringLiteral("u-cur"), .timestamp = 1000,
                .profileName = QStringLiteral("D-Flow / Q"),
                .profileKbId = QStringLiteral("d-flow-q-variant"),
                .grinderModel = QStringLiteral("Niche Zero"),
                .grinderBurrs = QStringLiteral("63mm conical"),
                .grinderSetting = QStringLiteral("6"), .enjoyment = 0 });
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY2(r.isEmpty(), "no dialed-in shots → empty block");
        });
    }

    // grinderContext reports only the resolved package's settings and range.
    // Two things fail here if the scoping is wrong, and the second is not
    // obvious: the settings query and the RPM query BOTH already use named
    // binds (":model"/":bev"), so the equipment predicate has to be named too.
    // A positional "?" there does not quietly filter nothing — Qt's SQLite
    // driver reports "Parameter count mismatch" and exec() returns false, which
    // reaches the user as a grinder-context block that silently went missing.
    // Asserting on the RPM range as well as the settings covers both queries.
    void grinderContext_reportsOnlyThisPackagesSettingsAndRange()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("gctx_pkg"), [&](QSqlDatabase& db) {
            auto seed = [&](const QString& uuid, const QString& setting, qint64 rpm,
                            const QString& basketModel) {
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = 1000,
                    .profileName = QStringLiteral("p"),
                    .beanBrand = QStringLiteral("Northbound"),
                    .grinderModel = QStringLiteral("Zero"),
                    .basketBrand = QStringLiteral("Decent"),
                    .basketModel = basketModel,
                    .grinderSetting = setting, .rpm = rpm });
            };

            // Straight-wall package: a tight dial around 4, low RPM.
            QVERIFY(seed(QStringLiteral("sw-1"), QStringLiteral("4.0"), 800,
                         QStringLiteral("18g Ridged")) > 0);
            QVERIFY(seed(QStringLiteral("sw-2"), QStringLiteral("4.2"), 900,
                         QStringLiteral("18g Ridged")) > 0);
            // Stepped package on the SAME grinder: a dial an order of magnitude
            // away, and a much higher RPM. Pooled, these would widen the reported
            // range to 4.0-17 and 800-1600 — a range in which almost any proposed
            // setting looks plausible, which is what the block exists to judge.
            QVERIFY(seed(QStringLiteral("st-1"), QStringLiteral("17"), 1600,
                         QStringLiteral("Stepped 58-46mm")) > 0);
            // A 0.1 gap that exists ONLY on the stepped package. stepSize is
            // derived grinder-wide, so it must see this; every setting on the
            // straight-wall package is 0.2 apart. Without a gap unique to the
            // other package the two derivations coincide and a stepSize
            // assertion cannot tell them apart — which is how the first version
            // of this test failed to mean anything.
            QVERIFY(seed(QStringLiteral("st-2"), QStringLiteral("17.1"), 1600,
                         QStringLiteral("Stepped 58-46mm")) > 0);

            const qint64 cur = seed(QStringLiteral("sw-cur"), QStringLiteral("4.0"), 800,
                                    QStringLiteral("18g Ridged"));
            QVERIFY(cur > 0);
            const qint64 bucket = packageForShot(db, cur);
            QVERIFY(bucket > 0);

            const QJsonObject ctx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Northbound"), bucket));
            QVERIFY2(!ctx.isEmpty(), "scoped grinder context must still be produced");

            const QJsonArray observed = ctx.value(QStringLiteral("settingsObserved")).toArray();
            QStringList seen;
            for (const QJsonValue& v : observed) seen << v.toString();
            QVERIFY2(seen.contains(QStringLiteral("4.0")),
                     "this package's own settings must be reported");
            QVERIFY2(!seen.contains(QStringLiteral("17")),
                     "another package's setting must not appear in settingsObserved");
            QCOMPARE(ctx.value(QStringLiteral("observedMaxSetting")).toDouble(), 4.2);

            // RPM query — the second named-bind site.
            QCOMPARE(ctx.value(QStringLiteral("observedMaxRpm")).toInt(), 900);

            // stepSize is deliberately NOT package-scoped: it is the grinder's
            // mechanical resolution, unchanged by a basket swap, and must keep
            // matching the grind widget. Pinned here because it is the exact
            // shape a later reader "fixes" for consistency. The stepped
            // package's 17 is in the sample, so a package-scoped step would be
            // 0.2 (from 4.0/4.2 alone) rather than the grinder-wide value.
            QVERIFY2(ctx.contains(QStringLiteral("stepSize")),
                     "the grinder-wide step must still be reported");
            QVERIFY2(qFuzzyCompare(ctx.value(QStringLiteral("stepSize")).toDouble() + 1.0, 0.1 + 1.0),
                     qPrintable(QStringLiteral("stepSize must be derived grinder-wide (0.1, the gap "
                                               "on the OTHER package), not narrowed to this one "
                                               "(0.2). Got: ")
                                + QString::number(ctx.value(QStringLiteral("stepSize")).toDouble())));

            // Cross-bean fallback widens the BEAN, never the equipment. Ask for a
            // bean with a single setting so the fallback fires, and check the
            // other package's dial still does not turn up in it.
            QVERIFY(insertShot(db, ShotRow{
                .uuid = QStringLiteral("sw-onyx"), .timestamp = 1000,
                .profileName = QStringLiteral("p"),
                .beanBrand = QStringLiteral("Onyx"),
                .grinderModel = QStringLiteral("Zero"),
                .basketBrand = QStringLiteral("Decent"),
                .basketModel = QStringLiteral("18g Ridged"),
                .grinderSetting = QStringLiteral("4.4") }) > 0);
            const QJsonObject onyx = DialingBlocks::buildGrinderContextBlock(
                db, shotWith(QStringLiteral("Zero"), QStringLiteral("espresso"), QStringLiteral("Onyx"), bucket));
            const QJsonArray allBeans = onyx.value(QStringLiteral("allBeansSettings")).toArray();
            QVERIFY2(!allBeans.isEmpty(), "sparse bean-scoped result must trigger the fallback");
            QStringList fallbackSeen;
            for (const QJsonValue& v : allBeans) fallbackSeen << v.toString();
            QVERIFY2(fallbackSeen.contains(QStringLiteral("4.0")),
                     "the cross-bean fallback must widen the bean");
            QVERIFY2(!fallbackSeen.contains(QStringLiteral("17")),
                     "the cross-bean fallback must NOT widen the equipment package");
        });
    }

    // recentAdvice pairs a recommendation with the user's follow-up shot, and
    // that follow-up must be on the same equipment package as the shot the
    // advice was given for. Without the match, a user who took the advice on one
    // basket and then pulled on the other has the OTHER basket's shot scored as
    // their response — the model is told its advice was ignored (or wildly
    // overshot) on the strength of a grind move the user never made.
    void recentAdvice_followUpMustBeOnTheSamePackage()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("rec_advice_pkg"), [&](QSqlDatabase& db) {
            auto seed = [&](const QString& uuid, qint64 ts, const QString& setting,
                            const QString& basketModel) {
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = ts,
                    .profileName = QStringLiteral("80's Espresso"),
                    .profileKbId = QStringLiteral("kb-80s"),
                    .duration = 28.0, .finalWeight = 36.0, .doseWeight = 18.0,
                    .grinderModel = QStringLiteral("Niche Zero"),
                    .basketBrand = QStringLiteral("Decent"),
                    .basketModel = basketModel,
                    .grinderSetting = setting });
            };

            // Advice was given on this shot, on the straight-wall basket.
            const qint64 priorId = seed(QStringLiteral("uuid-prior"), 1000,
                                        QStringLiteral("5.0"), QStringLiteral("18g Ridged"));
            QVERIFY(priorId > 0);
            // The very next shot in time is on the OTHER basket. Unscoped, this
            // wins the "first shot after" race and gets scored as the response.
            QVERIFY(seed(QStringLiteral("uuid-other"), 1100, QStringLiteral("17"),
                         QStringLiteral("Stepped 58-46mm")) > 0);
            // The real response, on the same basket, comes later.
            const qint64 realNextId = seed(QStringLiteral("uuid-next"), 1200,
                                           QStringLiteral("4.75"), QStringLiteral("18g Ridged"));
            QVERIFY(realNextId > 0);

            DialingBlocks::RecentAdviceInputs in;
            in.turns = QList<AIConversation::HistoricalAssistantTurn>{
                AIConversation::HistoricalAssistantTurn{
                    priorId, QStringLiteral("Try grinder 4.75."), sampleStructuredNext()
                }
            };
            in.currentProfileKbId = QStringLiteral("kb-80s");
            in.currentShotId = 99999;

            const QJsonArray out = DialingBlocks::buildRecentAdviceBlock(db, in);
            QCOMPARE(out.size(), 1);
            const QJsonObject ur = out.first().toObject().value(QStringLiteral("userResponse")).toObject();
            QCOMPARE(ur.value(QStringLiteral("actualNextShotId")).toDouble(),
                     static_cast<double>(realNextId));
            QCOMPARE(ur.value(QStringLiteral("grinderSetting")).toString(),
                     QStringLiteral("4.75"));
        });
    }

    // `ShotProjection::equipmentId` is now the SOLE source of the equipment
    // bucket: the conversation key's write side (mcptools_ai.cpp, main thread,
    // no DB connection) reads it, and so does every scoped query, off the record
    // it already loaded. There is no second path left to cross-check it against,
    // which makes this the pin that matters — the projection must equal what the
    // column actually holds, including the unpackaged case where it is NULL and
    // the projection must read 0. If they diverge, a turn is filed under a key
    // nothing later reads: no error, no warning, just a thread that never
    // resumes. Nothing compiled by a test target exercises mcptools_ai.cpp, so
    // this is the only place that property is asserted.
    void equipmentId_projectionAndBucketAgreeForTheSameShot()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("equip_agree"), [&](QSqlDatabase& db) {
            const qint64 packaged = insertShot(db, ShotRow{
                .uuid = QStringLiteral("packaged"), .timestamp = 1000,
                .profileName = QStringLiteral("D-Flow / Q"),
                .grinderModel = QStringLiteral("Niche Zero"),
                .basketBrand = QStringLiteral("Decent"),
                .basketModel = QStringLiteral("18g Ridged"),
                .grinderSetting = QStringLiteral("9") });
            QVERIFY(packaged > 0);

            // No gear at all -> equipment_id stays NULL.
            const qint64 unpackaged = insertShot(db, ShotRow{
                .uuid = QStringLiteral("unpackaged"), .timestamp = 1100,
                .profileName = QStringLiteral("D-Flow / Q"),
                .grinderSetting = QStringLiteral("9") });
            QVERIFY(unpackaged > 0);

            for (qint64 id : {packaged, unpackaged}) {
                const ShotProjection p = ShotHistoryStorage::convertShotRecord(
                    ShotHistoryStorage::loadShotRecordStatic(db, id));
                QVERIFY(p.isValid());
                // Read straight off the column, not through another production
                // helper — the point is to catch the projection drifting from
                // what the database holds.
                QCOMPARE(p.equipmentId, packageForShot(db, id));
            }

            // ...and the two shots really are in different buckets, or the
            // comparison above would hold for an uninteresting reason.
            QVERIFY(packageForShot(db, packaged) != packageForShot(db, unpackaged));
            QCOMPARE(packageForShot(db, unpackaged), qint64(0));
        });
    }

    // dialInSessions and bestRecentShot both exclude shots from another
    // equipment package. Same grinder, same coffee, same profile, same window —
    // only the basket differs, which is precisely the case that made the advisor
    // compare a stepped-basket dial of 17 against a straight-wall dial of 9.75
    // and conclude the burrs had shifted.
    void equipmentScoping_excludesOtherPackagesFromHistoryAndAnchor()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("equip_scope"), [&](QSqlDatabase& db) {
            const qint64 nowTs = QDateTime::currentSecsSinceEpoch();

            auto seed = [&](const QString& uuid, qint64 ts, const QString& setting,
                            int enjoyment, const QString& bBrand, const QString& bModel) {
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = ts,
                    .profileName = QStringLiteral("80's Espresso"),
                    .profileKbId = QStringLiteral("kb-80s"),
                    .finalWeight = 36.0,
                    .beanBrand = QStringLiteral("TestRoaster"),
                    .beanType = QStringLiteral("TestBean"),
                    .grinderModel = QStringLiteral("Niche Zero"),
                    .grinderBurrs = QStringLiteral("63mm conical"),
                    .basketBrand = bBrand, .basketModel = bModel,
                    .grinderSetting = setting, .enjoyment = enjoyment });
            };

            // Straight-wall basket: one prior shot, modestly rated.
            const qint64 ownPrior = seed(QStringLiteral("own-prior"), nowTs - 7200,
                                         QStringLiteral("9.75"), 60,
                                         QStringLiteral("Decent"), QStringLiteral("18g Ridged"));
            QVERIFY(ownPrior > 0);
            // Stepped basket: a shot in the same window, rated HIGHER — so if the
            // anchor were unscoped it would win on rating, which is exactly the
            // failure being guarded (a target the user cannot hit on this basket).
            QVERIFY(seed(QStringLiteral("other-basket"), nowTs - 3600,
                         QStringLiteral("17"), 95,
                         QStringLiteral("Graph Coffee"), QStringLiteral("Stepped 58-46mm")) > 0);

            const qint64 currentId = seed(QStringLiteral("current"), nowTs,
                                          QStringLiteral("9.75"), 0,
                                          QStringLiteral("Decent"), QStringLiteral("18g Ridged"));
            QVERIFY(currentId > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-80s"), currentId, /*historyLimit=*/10,
                shotInPackage(packageForShot(db, currentId)));
            QStringList settingsSeen;
            for (const QJsonValue& sv : sessions)
                for (const QJsonValue& shv : sv.toObject().value(QStringLiteral("shots")).toArray())
                    settingsSeen << shv.toObject().value(QStringLiteral("grinderSetting")).toString();
            QVERIFY2(settingsSeen.contains(QStringLiteral("9.75")),
                     "the same package's prior shot must remain in the history");
            QVERIFY2(!settingsSeen.contains(QStringLiteral("17")),
                     "a shot on another basket must not appear in the history");

            // The session context names the basket, so the model can attribute
            // the history to the gear it came from rather than to a bare dial.
            QVERIFY(!sessions.isEmpty());
            bool namedBasket = false;
            for (const QJsonValue& sv : sessions) {
                const QJsonObject ctx = sv.toObject().value(QStringLiteral("context")).toObject();
                // Brand AND model: the hoist emits them independently, so
                // checking only the model would leave a dropped brand invisible.
                if (ctx.value(QStringLiteral("basketBrand")).toString() == QStringLiteral("Decent")
                    && ctx.value(QStringLiteral("basketModel")).toString() == QStringLiteral("18g Ridged"))
                    namedBasket = true;
            }
            QVERIFY2(namedBasket, "session context must name the basket the shots were pulled on");

            const ShotProjection currentProj = projectionForShot(db, currentId);
            QVERIFY(currentProj.isValid());
            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-80s"), currentId, currentProj);
            QCOMPARE(best_.value(QStringLiteral("id")).toVariant().toLongLong(), ownPrior);
            QVERIFY2(best_.value(QStringLiteral("enjoyment0to100")).toInt() == 60,
                     "the higher-rated shot on another basket must not become the anchor");
        });
    }

    // A user with no equipment packages at all keeps their whole history: every
    // shot sits in the same "unpackaged" bucket, so the scoping is a no-op for
    // them. Worth its own case because the natural implementation — comparing
    // equipment_id with `=` — silently drops every one of these rows, SQL NULL
    // never being equal to NULL.
    void equipmentScoping_isNoOpForUserWithNoPackages()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("equip_nopkg"), [&](QSqlDatabase& db) {
            const qint64 nowTs = QDateTime::currentSecsSinceEpoch();
            auto seed = [&](const QString& uuid, qint64 ts, const QString& setting) {
                // No grinder and no basket → insertShot leaves equipment_id NULL.
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = ts,
                    .profileName = QStringLiteral("80's Espresso"),
                    .profileKbId = QStringLiteral("kb-80s"),
                    .finalWeight = 36.0,
                    .beanBrand = QStringLiteral("TestRoaster"),
                    .beanType = QStringLiteral("TestBean"),
                    .grinderSetting = setting, .enjoyment = 70 });
            };
            QVERIFY(seed(QStringLiteral("np1"), nowTs - 7200, QStringLiteral("4.0")) > 0);
            const qint64 currentId = seed(QStringLiteral("np-cur"), nowTs, QStringLiteral("4.2"));
            QVERIFY(currentId > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-80s"), currentId, /*historyLimit=*/10,
                shotInPackage(packageForShot(db, currentId)));
            int shotCount = 0;
            for (const QJsonValue& sv : sessions)
                shotCount += sv.toObject().value(QStringLiteral("shots")).toArray().size();
            QCOMPARE(shotCount, 1);
        });
    }

    // The calibration pool is scoped to the RESOLVED SHOT'S equipment package,
    // not to every package that happens to share a grinder model and burrs.
    //
    // This replaces an older test that asserted the grinder-identity match was
    // case- and whitespace-insensitive. That matcher is gone: the clause now
    // compares package ids, and an integer key cannot disagree with
    // EquipmentStorage about padding or capitalisation, so the property it
    // guarded is structural rather than something a test can break. What DOES
    // need guarding is the new scoping — and note what it protects, which is
    // subtler than "a pair might straddle two baskets": the endpoint medians are
    // pooled by (batch, kbId) BEFORE any pair is formed, so an unscoped pool
    // corrupts the ENDPOINT. That is why a same-grinder/different-basket shot
    // must not contribute even when it is the same coffee and the same profile.
    void calibrationBlock_poolIsScopedToResolvedShotsPackage()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_pkg"), [&](QSqlDatabase& db) {
            // A well-populated STEPPED-basket history: three dialed-in shots
            // across two profiles, which unscoped is more than enough to build a
            // pool from.
            calSeedBasket(db, QStringLiteral("g1"), 1000,
                          QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                          QStringLiteral("17"), QStringLiteral("Graph Coffee"),
                          QStringLiteral("Stepped 58-46mm"));
            calSeedBasket(db, QStringLiteral("g2"), 1100,
                          QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                          QStringLiteral("17"), QStringLiteral("Graph Coffee"),
                          QStringLiteral("Stepped 58-46mm"));
            calSeedBasket(db, QStringLiteral("g3"), 1200,
                          QStringLiteral("Londinium"), QStringLiteral("londinium"),
                          QStringLiteral("16"), QStringLiteral("Graph Coffee"),
                          QStringLiteral("Stepped 58-46mm"));

            // The resolved shot is on the STRAIGHT-WALL basket and is the only
            // shot on that package — and it does not itself qualify as dialed-in
            // (no rating, no refractometer, no stop-at-weight target), so its
            // package contributes no rows at all.
            const qint64 cur = insertShot(db, ShotRow{
                .uuid = QStringLiteral("d1"), .timestamp = 1300,
                .profileName = QStringLiteral("D-Flow / Q"),
                .profileKbId = QStringLiteral("d-flow-q-variant"),
                .finalWeight = 36.0,
                .beanBrand = QStringLiteral("TestRoaster"),
                .beanType = QStringLiteral("TestBean"),
                .grinderModel = QStringLiteral("Niche Zero"),
                .grinderBurrs = QStringLiteral("63mm conical"),
                .basketBrand = QStringLiteral("Decent"),
                .basketModel = QStringLiteral("18g Ridged"),
                .grinderSetting = QStringLiteral("9.75"), .enjoyment = 0 });
            QVERIFY(cur > 0);

            // Scoped: nothing to calibrate from, so no block. Unscoped, the three
            // stepped-basket shots would have supplied the pool — which is the
            // whole failure being guarded, since the pool feeds the endpoint
            // medians and the anchor.
            const QJsonObject scoped = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY2(scoped.isEmpty(),
                     "a package with no dialed-in shots must not borrow another basket's history");

            // Control: the same call resolved against a shot on the STEPPED
            // package does produce a block. Without this the assertion above
            // would pass for any reason at all — a typo in the bean name, a
            // broken fixture — rather than because of the scoping.
            const qint64 curStepped = calSeedBasket(db, QStringLiteral("g4"), 1400,
                          QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                          QStringLiteral("17"), QStringLiteral("Graph Coffee"),
                          QStringLiteral("Stepped 58-46mm"));
            QVERIFY(curStepped > 0);
            const QJsonObject onOwnPackage = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), curStepped);
            QVERIFY2(!onOwnPackage.isEmpty(),
                     "a package WITH dialed-in shots must still calibrate normally");
        });
    }

    // #1223 core: dialed-in on D-Flow / Q, asking about far profiles →
    // directional only. No conversionKey, no rgs anywhere, no negative
    // numbers; TurboTurbo (UGS 6, far above current UGS 1.0) is coarser.
    void calibrationBlock_directionalSparse_1223()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_1223"), [&](QSqlDatabase& db) {
            calSeed(db, QStringLiteral("u1"), 1000,
                    QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                    QStringLiteral("6"));
            const qint64 cur = calSeed(db, QStringLiteral("u2"), 1100,
                    QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                    QStringLiteral("6"));
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY(!r.isEmpty());
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("directional"));
            QVERIFY2(!r.contains(QStringLiteral("conversionKey")),
                     "directional must carry no conversionKey");
            QVERIFY2(!calAnyHasRgs(r), "directional must emit no rgs anywhere");
            const QJsonObject tt = calProfile(r, QStringLiteral("TurboTurbo"));
            QVERIFY(!tt.isEmpty());
            QCOMPARE(tt.value(QStringLiteral("source")).toString(),
                     QStringLiteral("directional"));
            QCOMPARE(tt.value(QStringLiteral("direction")).toString(),
                     QStringLiteral("coarser"));
            QVERIFY2(!tt.contains(QStringLiteral("rgs")),
                     "TurboTurbo must not carry a number (#1223)");
        });
    }

    // The legacy pooled cross-coffee path produced conversionKey = -2.4
    // here. Different beans → different batches → no within-batch pair →
    // directional; a wrong-signed key can no longer be emitted.
    void calibrationBlock_wrongSignImpossible_1223()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_wrongsign"), [&](QSqlDatabase& db) {
            // Londinium (UGS 0) high settings on bean A; Adaptive v2
            // (UGS 1.25) low settings on bean B — the real-data shape
            // that pooled to a negative slope.
            for (int i = 0; i < 3; ++i) {
                insertShot(db, ShotRow{
                    .uuid = QStringLiteral("u-lon-%1").arg(i), .timestamp = 1000 + i,
                    .profileName = QStringLiteral("Londinium"),
                    .profileKbId = QStringLiteral("londinium"),
                    .beanBrand = QStringLiteral("RoasterA"), .beanType = QStringLiteral("BeanA"),
                    .grinderModel = QStringLiteral("Niche Zero"),
                    .grinderBurrs = QStringLiteral("63mm conical"),
                    .grinderSetting = QStringLiteral("12"), .enjoyment = 80 });
            }
            qint64 cur = 0;
            for (int i = 0; i < 3; ++i) {
                cur = insertShot(db, ShotRow{
                    .uuid = QStringLiteral("u-av-%1").arg(i), .timestamp = 5000 + i,
                    .profileName = QStringLiteral("Adaptive v2"),
                    .profileKbId = QStringLiteral("adaptive-v2"),
                    .beanBrand = QStringLiteral("RoasterB"), .beanType = QStringLiteral("BeanB"),
                    .grinderModel = QStringLiteral("Niche Zero"),
                    .grinderBurrs = QStringLiteral("63mm conical"),
                    .grinderSetting = QStringLiteral("9"), .enjoyment = 80 });
            }
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY(!r.isEmpty());
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("directional"));
            QVERIFY2(!r.contains(QStringLiteral("conversionKey")),
                     "no conversionKey from cross-batch data (no wrong sign)");
            QVERIFY2(!calAnyHasRgs(r), "no numbers from cross-batch data");
        });
    }

    // Same batch, three profiles on an exact line → gate passes →
    // approximate. Within-cap profile derived; far profile (TurboTurbo)
    // capped to directional with no number.
    void calibrationBlock_approximatePublishesAndCaps()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_approx"), [&](QSqlDatabase& db) {
            // Three profiles, every pairwise UGS gap ≥ 0.75 so all 3
            // pairs survive the span filter, settings on an exact line
            // (setting = 2 + 2·UGS) → every pairwise slope = 2 → IQR 0 →
            // gate passes. londinium 0.0→2, d-flow-q-variant 1.0→4,
            // gentle-and-sweet 2.0→6.
            for (int i = 0; i < 2; ++i) {
                calSeed(db, QStringLiteral("u-lo-%1").arg(i), 1000 + i,
                        QStringLiteral("Londinium"), QStringLiteral("londinium"),
                        QStringLiteral("2"));
                calSeed(db, QStringLiteral("u-dq-%1").arg(i), 1100 + i,
                        QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                        QStringLiteral("4"));
            }
            qint64 cur = 0;
            for (int i = 0; i < 2; ++i)
                cur = calSeed(db, QStringLiteral("u-gs-%1").arg(i), 1200 + i,
                        QStringLiteral("Gentle & Sweet"), QStringLiteral("gentle-and-sweet"),
                        QStringLiteral("6"));
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY(!r.isEmpty());
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("approximate"));
            QCOMPARE(r.value(QStringLiteral("conversionKey")).toDouble(), 2.0);
            QVERIFY(r.contains(QStringLiteral("coffeeAnchor")));
            // Seeded profiles are in the current batch → history median.
            const QJsonObject lo = calProfile(r, QStringLiteral("Londinium"));
            QCOMPARE(lo.value(QStringLiteral("source")).toString(),
                     QStringLiteral("history"));
            QCOMPARE(lo.value(QStringLiteral("rgs")).toString(), QStringLiteral("2"));
            // Validated span 0.0..2.0; cap 1.5 → numeric in [-1.5, 3.5].
            // Adaptive v2 (UGS 1.25) is in range but unseeded → derived;
            // anchor = recent Gentle&Sweet (UGS 2.0, setting 6) →
            // 6 + (1.25 − 2.0)·2 = 4.5.
            const QJsonObject av = calProfile(r, QStringLiteral("Adaptive v2"));
            QCOMPARE(av.value(QStringLiteral("source")).toString(),
                     QStringLiteral("derived"));
            QCOMPARE(av.value(QStringLiteral("rgs")).toString(),
                     QStringLiteral("4.5"));
            // TurboTurbo UGS 6 far outside → directional, no number.
            const QJsonObject tt = calProfile(r, QStringLiteral("TurboTurbo"));
            QCOMPARE(tt.value(QStringLiteral("source")).toString(),
                     QStringLiteral("directional"));
            QVERIFY2(!tt.contains(QStringLiteral("rgs")),
                     "out-of-cap profile must have no rgs");
            QCOMPARE(tt.value(QStringLiteral("direction")).toString(),
                     QStringLiteral("coarser"));
        });
    }

    // Zero history beyond one current shot: direction still correct from
    // KB ordering alone (anchor-free, grinder-convention-free, D5a).
    void calibrationBlock_directionAnchorFreeZeroData()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_zero"), [&](QSqlDatabase& db) {
            const qint64 cur = calSeed(db, QStringLiteral("only"), 1000,
                    QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                    QStringLiteral("6"));
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("directional"));
            QVERIFY(r.value(QStringLiteral("currentProfileUgsPlaced")).toBool());
            QCOMPARE(calProfile(r, QStringLiteral("TurboTurbo"))
                         .value(QStringLiteral("direction")).toString(),
                     QStringLiteral("coarser"));
            QCOMPARE(calProfile(r, QStringLiteral("Blooming Espresso"))
                         .value(QStringLiteral("direction")).toString(),
                     QStringLiteral("finer"));
            QVERIFY2(!calAnyHasRgs(r), "zero-data must yield no numbers");
        });
    }

    // Current profile not on the UGS chart → ordering withheld, flagged.
    void calibrationBlock_currentProfileNoUgsWithholdsDirection()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_nougs"), [&](QSqlDatabase& db) {
            const qint64 cur = insertShot(db, ShotRow{
                .uuid = QStringLiteral("u-custom"), .timestamp = 1000,
                .profileName = QStringLiteral("My Secret Pull"),
                .grinderModel = QStringLiteral("Niche Zero"),
                .grinderBurrs = QStringLiteral("63mm conical"),
                .grinderSetting = QStringLiteral("6"), .enjoyment = 80 });
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QVERIFY(!r.isEmpty());
            QVERIFY2(!r.value(QStringLiteral("currentProfileUgsPlaced")).toBool(),
                     "custom profile is not UGS-placed");
            const QJsonObject tt = calProfile(r, QStringLiteral("TurboTurbo"));
            QCOMPARE(tt.value(QStringLiteral("source")).toString(),
                     QStringLiteral("directional"));
            QVERIFY2(!tt.contains(QStringLiteral("direction")),
                     "no direction when current profile has no UGS");
        });
    }

    // Both consumer surfaces call this one builder; identical input →
    // byte-identical JSON (cross-surface drift guard, review S4).
    void calibrationBlock_byteStableForIdenticalInput()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_bytestable"), [&](QSqlDatabase& db) {
            const qint64 cur = calSeed(db, QStringLiteral("b1"), 1000,
                    QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                    QStringLiteral("6"));
            const QJsonObject a = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            const QJsonObject b = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            QCOMPARE(QJsonDocument(a).toJson(QJsonDocument::Compact),
                     QJsonDocument(b).toJson(QJsonDocument::Compact));
        });
    }

    // Variable-RPM grinder (DF83V): users annotate dial with RPM
    // ("24 1400rpm"). The parser accepts the leading dial and the rest
    // is ignorable annotation; without this fix 93% of such users' data
    // was silently discarded (review on PR #1236 / #1223 reporter DB).
    void calibrationBlock_acceptsNumericWithSuffix_DF83V()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_suffix"), [&](QSqlDatabase& db) {
            // Three profiles on an exact line through (setting = 2 + 2·UGS).
            // Settings written WITH the variable-RPM suffix.
            auto seed = [&](const QString& uuid, qint64 ts,
                            const QString& name, const QString& kbId,
                            const QString& setting) {
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = ts,
                    .profileName = name, .profileKbId = kbId,
                    .finalWeight = 36.0,
                    .beanBrand = QStringLiteral("TestRoaster"),
                    .beanType = QStringLiteral("TestBean"),
                    .grinderModel = QStringLiteral("DF83V"),
                    .grinderBurrs = QStringLiteral("83mm flat steel"),
                    .grinderSetting = setting, .enjoyment = 80 });
            };
            for (int i = 0; i < 2; ++i) {
                seed(QStringLiteral("u-lo-%1").arg(i), 1000 + i,
                     QStringLiteral("Londinium"), QStringLiteral("londinium"),
                     QStringLiteral("2 1400rpm"));
                seed(QStringLiteral("u-dq-%1").arg(i), 1100 + i,
                     QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                     QStringLiteral("4 1400rpm"));
            }
            qint64 cur = 0;
            for (int i = 0; i < 2; ++i)
                cur = seed(QStringLiteral("u-gs-%1").arg(i), 1200 + i,
                           QStringLiteral("Gentle & Sweet"),
                           QStringLiteral("gentle-and-sweet"),
                           QStringLiteral("6 1400rpm"));
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("DF83V"), QStringLiteral("83mm flat steel"),
                QStringLiteral("espresso"), cur);
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("approximate"));
            QCOMPARE(r.value(QStringLiteral("conversionKey")).toDouble(), 2.0);
            // History rgs emits dial-only (no rpm round-trip).
            const QJsonObject lo = calProfile(r, QStringLiteral("Londinium"));
            QCOMPARE(lo.value(QStringLiteral("rgs")).toString(), QStringLiteral("2"));
        });
    }

    // Compound notation (Eureka Mignon Specialita): "1+4" → linear
    // 1·100 + 4 = 104; recommended-grind round-trips back as "a+b".
    void calibrationBlock_compoundNotation_MignonSpecialita()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_compound"), [&](QSqlDatabase& db) {
            // setting = 100 + 4·linearUGS, linear = a·100 + b.
            // londinium(0)→1+4 (=104); d-flow-q(1)→2+8 (=208); g&s(2)→3+12 (=312)
            // pairwise slopes consistent (104→208→312 over ugs 0/1/2):
            // slope per UGS = 104; all pairs equal → gate passes.
            auto seed = [&](const QString& uuid, qint64 ts,
                            const QString& name, const QString& kbId,
                            const QString& setting) {
                return insertShot(db, ShotRow{
                    .uuid = uuid, .timestamp = ts,
                    .profileName = name, .profileKbId = kbId,
                    .finalWeight = 36.0,
                    .beanBrand = QStringLiteral("TestRoaster"),
                    .beanType = QStringLiteral("TestBean"),
                    .grinderModel = QStringLiteral("Mignon Specialita"),
                    .grinderBurrs = QStringLiteral("55mm flat steel"),
                    .grinderSetting = setting, .enjoyment = 80 });
            };
            for (int i = 0; i < 2; ++i) {
                seed(QStringLiteral("u-lo-%1").arg(i), 1000 + i,
                     QStringLiteral("Londinium"), QStringLiteral("londinium"),
                     QStringLiteral("1+4"));
                seed(QStringLiteral("u-dq-%1").arg(i), 1100 + i,
                     QStringLiteral("D-Flow / Q"), QStringLiteral("d-flow-q-variant"),
                     QStringLiteral("2+8"));
            }
            qint64 cur = 0;
            for (int i = 0; i < 2; ++i)
                cur = seed(QStringLiteral("u-gs-%1").arg(i), 1200 + i,
                           QStringLiteral("Gentle & Sweet"),
                           QStringLiteral("gentle-and-sweet"),
                           QStringLiteral("3+12"));
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Mignon Specialita"),
                QStringLiteral("55mm flat steel"),
                QStringLiteral("espresso"), cur);
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("approximate"));
            QCOMPARE(r.value(QStringLiteral("conversionKey")).toDouble(), 104.0);
            // History rgs round-trips in "a+b" notation.
            const QJsonObject lo = calProfile(r, QStringLiteral("Londinium"));
            QCOMPARE(lo.value(QStringLiteral("rgs")).toString(), QStringLiteral("1+4"));
            const QJsonObject gs = calProfile(r, QStringLiteral("Gentle & Sweet"));
            QCOMPARE(gs.value(QStringLiteral("rgs")).toString(), QStringLiteral("3+12"));
        });
    }

    // Eureka multi-turn "1+4" must NOT be silently mis-parsed as "1" on
    // a non-compound (plain numeric) grinder — those rows are excluded
    // so a future grinder added without a Compound entry can't fabricate
    // numbers from misread multi-turn notation. Same guard catches
    // "1 + 4" (spaced multi-turn) on every grinder.
    void calibrationBlock_compoundSyntax_rejectedOnNonCompoundGrinder()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("calib_compound_rej"), [&](QSqlDatabase& db) {
            // All shots on a plain-numeric grinder ("Niche Zero") with
            // compound notation in the setting field → all rejected at
            // parse → no dialed-in rows → block empty.
            for (int i = 0; i < 3; ++i) {
                insertShot(db, ShotRow{
                    .uuid = QStringLiteral("u-%1").arg(i),
                    .timestamp = 1000 + i,
                    .profileName = QStringLiteral("D-Flow / Q"),
                    .profileKbId = QStringLiteral("d-flow-q-variant"),
                    .finalWeight = 36.0,
                    .beanBrand = QStringLiteral("TestRoaster"),
                    .beanType = QStringLiteral("TestBean"),
                    .grinderModel = QStringLiteral("Niche Zero"),
                    .grinderBurrs = QStringLiteral("63mm conical"),
                    .grinderSetting = i == 0 ? QStringLiteral("1+4")
                                   : i == 1 ? QStringLiteral("1 + 4")
                                            : QStringLiteral("medium"),
                    .enjoyment = 80 });
            }
            // Need a valid resolved shot — add one current shot with a
            // proper numeric setting (it just provides the bean/profile).
            const qint64 cur = insertShot(db, ShotRow{
                .uuid = QStringLiteral("u-cur"), .timestamp = 1100,
                .profileName = QStringLiteral("D-Flow / Q"),
                .profileKbId = QStringLiteral("d-flow-q-variant"),
                .finalWeight = 36.0,
                .beanBrand = QStringLiteral("TestRoaster"),
                .beanType = QStringLiteral("TestBean"),
                .grinderModel = QStringLiteral("Niche Zero"),
                .grinderBurrs = QStringLiteral("63mm conical"),
                .grinderSetting = QStringLiteral("6"), .enjoyment = 80 });
            const QJsonObject r = DialingBlocks::buildGrinderCalibrationBlock(
                db, QStringLiteral("Niche Zero"), QStringLiteral("63mm conical"),
                QStringLiteral("espresso"), cur);
            // 3 compound-syntax shots rejected; the one cur shot is the
            // only kept row → only one profile, no pairs → directional.
            // Crucially: no numbers anywhere, no `1` rgs leaking from
            // a mis-parsed `1+4`.
            QCOMPARE(r.value(QStringLiteral("confidence")).toString(),
                     QStringLiteral("directional"));
            QVERIFY(!calAnyHasRgs(r));
        });
    }

    void recentAdvice_byteStabilityAcrossCalls()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

        qint64 priorId = -1;
        withRawDb(dbPath, "rec_advice_byte_stable", [&](QSqlDatabase& db) {
            priorId = insertShot(db, ShotRow{
                .uuid = "u-prior", .timestamp = nowSec - 7200,
                .profileName = "P", .profileKbId = "kb",
                .duration = 30, .finalWeight = 36, .doseWeight = 18,
                .grinderSetting = "5.0"
            });
            insertShot(db, ShotRow{
                .uuid = "u-next", .timestamp = nowSec - 3600,
                .profileName = "P", .profileKbId = "kb",
                .duration = 35, .finalWeight = 42, .doseWeight = 18,
                .grinderSetting = "4.75",
                .enjoyment = 75
            });

            DialingBlocks::RecentAdviceInputs in;
            in.turns = {AIConversation::HistoricalAssistantTurn{
                priorId, "advice", sampleStructuredNext()}};
            in.currentProfileKbId = "kb";
            in.currentShotId = 99999;

            const QByteArray a = QJsonDocument(DialingBlocks::buildRecentAdviceBlock(db, in))
                .toJson(QJsonDocument::Compact);
            const QByteArray b = QJsonDocument(DialingBlocks::buildRecentAdviceBlock(db, in))
                .toJson(QJsonDocument::Compact);
            QCOMPARE(a, b);
        });
    }

    // -------------------------------------------------------------------
    // pourControlFromProfileJson (issue #1158) — pure derivation, no DB.
    // Reads the profile recipe (`steps`), picks the longest frame (the
    // pour), and reports its `pump`. Empty / malformed / step-less JSON
    // → "" so the field stays sparse (a confidently-wrong value is
    // worse than an absent one — that was the v1 phase-marker bug). A
    // short trailing decline frame must not flip the classification.
    // -------------------------------------------------------------------
    void pourControlFromProfileJson_derivesFromLongestFrame()
    {
        // Empty / malformed / no steps → omitted.
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(QString()), QString());
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(QStringLiteral("{not json")), QString());
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(QStringLiteral("{\"steps\":[]}")), QString());

        // D-Flow / Q shape: Filling(25s,P) Infusing(1s,P) Pouring(127s,FLOW)
        // — the long pour is flow-controlled. This is the exact #1147
        // shot whose v1 phase-marker derivation wrongly said "pressure".
        const QString dflowQ = QStringLiteral(
            "{\"steps\":["
            "{\"name\":\"Filling\",\"pump\":\"pressure\",\"seconds\":25},"
            "{\"name\":\"Infusing\",\"pump\":\"pressure\",\"seconds\":1},"
            "{\"name\":\"Pouring\",\"pump\":\"flow\",\"seconds\":127}]}");
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(dflowQ), QStringLiteral("flow"));

        // Classic pressure pour: preinfusion(flow,short) then long
        // pressure pour.
        const QString ninebar = QStringLiteral(
            "{\"steps\":["
            "{\"name\":\"Preinfusion\",\"pump\":\"flow\",\"seconds\":10},"
            "{\"name\":\"Pour\",\"pump\":\"pressure\",\"seconds\":40}]}");
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(ninebar), QStringLiteral("pressure"));

        // A short trailing pressure "decline" frame must NOT flip a
        // long flow pour to "pressure".
        const QString flowWithTail = QStringLiteral(
            "{\"steps\":["
            "{\"name\":\"Fill\",\"pump\":\"pressure\",\"seconds\":20},"
            "{\"name\":\"Pour\",\"pump\":\"flow\",\"seconds\":110},"
            "{\"name\":\"Decline\",\"pump\":\"pressure\",\"seconds\":3}]}");
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(flowWithTail),
                 QStringLiteral("flow"));

        // `seconds` as a string (de1app Tcl-origin JSON) still parses.
        const QString stringSeconds = QStringLiteral(
            "{\"steps\":["
            "{\"name\":\"Fill\",\"pump\":\"pressure\",\"seconds\":\"25\"},"
            "{\"name\":\"Pour\",\"pump\":\"flow\",\"seconds\":\"127\"}]}");
        QCOMPARE(DialingBlocks::pourControlFromProfileJson(stringSeconds),
                 QStringLiteral("flow"));
    }

    // -------------------------------------------------------------------
    // withStopAtWeightNote (issue #1158) — pure, no DB. No-op on empty
    // recipe or non-positive target; appends exactly one note otherwise,
    // preserving the original recipe prefix.
    // -------------------------------------------------------------------
    void withStopAtWeightNote_appendsNoteOnlyWhenWeightAndRecipePresent()
    {
        // Empty recipe → unchanged regardless of weight.
        QCOMPARE(DialingBlocks::withStopAtWeightNote(QString(), 36.0), QString());

        const QString recipe =
            QStringLiteral("## Profile Recipe (1 frames)\n1. Pouring FLOW\n");
        // Non-positive target → recipe untouched (no note).
        QCOMPARE(DialingBlocks::withStopAtWeightNote(recipe, 0.0), recipe);
        QCOMPARE(DialingBlocks::withStopAtWeightNote(recipe, -1.0), recipe);

        // Recipe + positive target → exactly one note appended after the
        // original recipe text.
        const QString out = DialingBlocks::withStopAtWeightNote(recipe, 36.0);
        QVERIFY(out.startsWith(recipe));
        QVERIFY(out.contains(QStringLiteral("Stop-at-weight:")));
        QVERIFY(out.contains(QStringLiteral("weight cutoff")));
        QCOMPARE(out.count(QStringLiteral("Stop-at-weight:")), 1);
    }

    // -------------------------------------------------------------------
    // dialInSessions pourControl (issue #1158) — DB-level. A uniform
    // session hoists pourControl to context with no per-shot field; a
    // session that mixes flow/pressure variants emits it per-shot and
    // omits the context value. (Previously only the pure derivation was
    // covered; the hoist/per-shot wiring was dark.)
    // -------------------------------------------------------------------
    void dialInSessions_hoistsPourControlWhenUniform_elsePerShot()
    {
        const QString kFlow = QStringLiteral(
            "{\"steps\":[{\"name\":\"Filling\",\"pump\":\"pressure\",\"seconds\":25},"
            "{\"name\":\"Pouring\",\"pump\":\"flow\",\"seconds\":127}]}");
        const QString kPressure = QStringLiteral(
            "{\"steps\":[{\"name\":\"Preinfusion\",\"pump\":\"flow\",\"seconds\":10},"
            "{\"name\":\"Pour\",\"pump\":\"pressure\",\"seconds\":40}]}");
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        // Uniform-flow session → hoisted.
        const QString pathU = freshDbPath();
        initAndClose(pathU);
        withRawDb(pathU, QStringLiteral("pc_uniform"), [&](QSqlDatabase& db) {
            ShotRow a1;
            a1.uuid = QStringLiteral("pc-a1");
            a1.profileName = QStringLiteral("D-Flow / Q");
            a1.profileKbId = QStringLiteral("kb-dfq");
            a1.timestamp = now - 4000;
            a1.grinderModel = QStringLiteral("Zero");
            a1.grinderSetting = QStringLiteral("5");
            a1.profileJson = kFlow;
            QVERIFY(insertShot(db, a1) > 0);
            ShotRow a2 = a1;
            a2.uuid = QStringLiteral("pc-a2");
            a2.timestamp = now - 3700;
            QVERIFY(insertShot(db, a2) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10,
                shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonObject session = sessions.at(0).toObject();
            QCOMPARE(session.value(QStringLiteral("context")).toObject()
                         .value(QStringLiteral("pourControl")).toString(),
                     QStringLiteral("flow"));
            const QJsonArray shots = session.value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 2);
            for (const auto& s : shots)
                QVERIFY(!s.toObject().contains(QStringLiteral("pourControl")));
        });

        // Mixed flow/pressure session → per-shot, not hoisted.
        const QString pathM = freshDbPath();
        initAndClose(pathM);
        withRawDb(pathM, QStringLiteral("pc_mixed"), [&](QSqlDatabase& db) {
            ShotRow m1;
            m1.uuid = QStringLiteral("pc-m1");
            m1.profileName = QStringLiteral("D-Flow / Q");
            m1.profileKbId = QStringLiteral("kb-dfq");
            m1.timestamp = now - 4000;
            m1.grinderModel = QStringLiteral("Zero");
            m1.grinderSetting = QStringLiteral("5");
            m1.profileJson = kFlow;
            QVERIFY(insertShot(db, m1) > 0);
            ShotRow m2 = m1;
            m2.uuid = QStringLiteral("pc-m2");
            m2.timestamp = now - 3700;
            m2.profileJson = kPressure;
            QVERIFY(insertShot(db, m2) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-dfq"), -1, 10, shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonObject session = sessions.at(0).toObject();
            QVERIFY(!session.value(QStringLiteral("context")).toObject()
                         .contains(QStringLiteral("pourControl")));
            const QJsonArray shots = session.value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 2);
            bool sawFlow = false, sawPressure = false;
            for (const auto& s : shots) {
                const QString pc = s.toObject()
                                       .value(QStringLiteral("pourControl")).toString();
                if (pc == QStringLiteral("flow")) sawFlow = true;
                if (pc == QStringLiteral("pressure")) sawPressure = true;
            }
            QVERIFY(sawFlow);
            QVERIFY(sawPressure);
        });
    }

    // -------------------------------------------------------------------
    // dialInSessions profileName / targetWeightG / temperatureOverrideC
    // hoisting (issue #1164 finding #3) — DB-level. Same dedup discipline
    // as pourControl: hoist to session context when uniform across the
    // session, emit per-shot only when the session mixes the value.
    // -------------------------------------------------------------------
    void dialInSessions_hoistsProfileTargetTempWhenUniform_elsePerShot()
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        // Uniform session: same profile, target weight, and temp override
        // on both shots → all three hoist to context, absent per-shot.
        const QString pathU = freshDbPath();
        initAndClose(pathU);
        withRawDb(pathU, QStringLiteral("ptt_uniform"), [&](QSqlDatabase& db) {
            ShotRow a1;
            a1.uuid = QStringLiteral("ptt-a1");
            a1.profileName = QStringLiteral("D-Flow / Q");
            a1.profileKbId = QStringLiteral("kb-dfq");
            a1.timestamp = now - 4000;
            a1.grinderModel = QStringLiteral("Zero");
            a1.grinderSetting = QStringLiteral("5");
            a1.targetWeight = 36.0;
            a1.temperatureOverride = 84.0;
            QVERIFY(insertShot(db, a1) > 0);
            ShotRow a2 = a1;
            a2.uuid = QStringLiteral("ptt-a2");
            a2.timestamp = now - 3700;
            a2.grinderSetting = QStringLiteral("5.5");
            QVERIFY(insertShot(db, a2) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10,
                shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonObject ctx = sessions.at(0).toObject()
                .value(QStringLiteral("context")).toObject();
            QCOMPARE(ctx.value(QStringLiteral("profileName")).toString(),
                     QStringLiteral("D-Flow / Q"));
            QCOMPARE(ctx.value(QStringLiteral("targetWeightG")).toDouble(), 36.0);
            QCOMPARE(ctx.value(QStringLiteral("temperatureOverrideC")).toDouble(), 84.0);
            const QJsonArray shots = sessions.at(0).toObject()
                .value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 2);
            for (const auto& s : shots) {
                const QJsonObject sh = s.toObject();
                QVERIFY2(!sh.contains(QStringLiteral("profileName")),
                         "uniform profileName must hoist to context");
                QVERIFY2(!sh.contains(QStringLiteral("targetWeightG")),
                         "uniform targetWeightG must hoist to context");
                QVERIFY2(!sh.contains(QStringLiteral("temperatureOverrideC")),
                         "uniform temperatureOverrideC must hoist to context");
            }
        });

        // Mixed session: the two shots differ in all three fields → none
        // hoisted, each emitted per-shot. They share kbId + timestamps so
        // they still group into one session.
        const QString pathM = freshDbPath();
        initAndClose(pathM);
        withRawDb(pathM, QStringLiteral("ptt_mixed"), [&](QSqlDatabase& db) {
            ShotRow m1;
            m1.uuid = QStringLiteral("ptt-m1");
            m1.profileName = QStringLiteral("D-Flow / Q");
            m1.profileKbId = QStringLiteral("kb-dfq");
            m1.timestamp = now - 4000;
            m1.grinderModel = QStringLiteral("Zero");
            m1.grinderSetting = QStringLiteral("5");
            m1.targetWeight = 36.0;
            m1.temperatureOverride = 84.0;
            QVERIFY(insertShot(db, m1) > 0);
            ShotRow m2 = m1;
            m2.uuid = QStringLiteral("ptt-m2");
            m2.timestamp = now - 3700;
            m2.profileName = QStringLiteral("Damian's LM Leva");
            m2.targetWeight = 42.0;
            m2.temperatureOverride = 89.0;
            QVERIFY(insertShot(db, m2) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-dfq"), -1, 10, shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonObject ctx = sessions.at(0).toObject()
                .value(QStringLiteral("context")).toObject();
            QVERIFY(!ctx.contains(QStringLiteral("profileName")));
            QVERIFY(!ctx.contains(QStringLiteral("targetWeightG")));
            QVERIFY(!ctx.contains(QStringLiteral("temperatureOverrideC")));
            const QJsonArray shots = sessions.at(0).toObject()
                .value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 2);
            QSet<QString> profiles;
            QList<double> weights;
            QList<double> temps;
            for (const auto& s : shots) {
                const QJsonObject sh = s.toObject();
                profiles.insert(sh.value(QStringLiteral("profileName")).toString());
                weights.append(sh.value(QStringLiteral("targetWeightG")).toDouble());
                temps.append(sh.value(QStringLiteral("temperatureOverrideC")).toDouble());
            }
            QVERIFY(profiles.contains(QStringLiteral("D-Flow / Q")));
            QVERIFY(profiles.contains(QStringLiteral("Damian's LM Leva")));
            QVERIFY(weights.contains(36.0));
            QVERIFY(weights.contains(42.0));
            QVERIFY(temps.contains(84.0));
            QVERIFY(temps.contains(89.0));
        });
    }

    // -------------------------------------------------------------------
    // bestRecentShot pourControl + targetWeightG (issue #1158) — DB-level.
    // -------------------------------------------------------------------
    void bestRecentShot_emitsPourControlAndTargetWeightFromRecipe()
    {
        const QString kFlow = QStringLiteral(
            "{\"steps\":[{\"name\":\"Filling\",\"pump\":\"pressure\",\"seconds\":25},"
            "{\"name\":\"Pouring\",\"pump\":\"flow\",\"seconds\":127}]}");
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("pc_best"), [&](QSqlDatabase& db) {
            ShotRow best;
            best.uuid = QStringLiteral("pc-best");
            best.profileName = QStringLiteral("D-Flow / Q");
            best.profileKbId = QStringLiteral("kb-dfq");
            best.timestamp = now - 7 * kSecPerDay;
            best.grinderModel = QStringLiteral("Zero");
            best.grinderSetting = QStringLiteral("5");
            best.enjoyment = 85;
            best.profileJson = kFlow;
            best.targetWeight = 36.0;
            const qint64 bestId = insertShot(db, best);
            QVERIFY(bestId > 0);

            ShotRow current = best;
            current.uuid = QStringLiteral("pc-cur");
            current.timestamp = now - kSecPerDay;
            current.enjoyment = 0;
            const qint64 currentId = insertShot(db, current);
            QVERIFY(currentId > 0);
            const ShotProjection currentProj = projectionForShot(db, currentId);
            QVERIFY(currentProj.isValid());

            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-dfq"), currentId, currentProj);
            QVERIFY(!best_.isEmpty());
            QCOMPARE(best_.value(QStringLiteral("id")).toVariant().toLongLong(), bestId);
            QCOMPARE(best_.value(QStringLiteral("pourControl")).toString(),
                     QStringLiteral("flow"));
            QCOMPARE(best_.value(QStringLiteral("targetWeightG")).toDouble(), 36.0);
        });
    }

    // -------------------------------------------------------------------
    // #1161: stoppedBy is surfaced sparsely. "manual"/"weight"/"volume"
    // are emitted per-shot (and on bestRecentShot); "profileEnd" and ""
    // are omitted so the AI falls back to yield-vs-target there.
    // -------------------------------------------------------------------
    void dialInSessions_and_bestRecentShot_surfaceStoppedBySparsely()
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("sb"), [&](QSqlDatabase& db) {
            // One session, three shots with different stop reasons.
            ShotRow base;
            base.profileName = QStringLiteral("D-Flow / Q");
            base.profileKbId = QStringLiteral("kb-dfq");
            base.grinderModel = QStringLiteral("Zero");
            base.grinderSetting = QStringLiteral("5");

            ShotRow manual = base;
            manual.uuid = QStringLiteral("sb-manual");
            manual.timestamp = now - 3000;
            manual.stoppedBy = QStringLiteral("manual");
            QVERIFY(insertShot(db, manual) > 0);

            ShotRow profileEnd = base;
            profileEnd.uuid = QStringLiteral("sb-pe");
            profileEnd.timestamp = now - 2700;
            profileEnd.stoppedBy = QStringLiteral("profileEnd");
            QVERIFY(insertShot(db, profileEnd) > 0);

            ShotRow weight = base;
            weight.uuid = QStringLiteral("sb-weight");
            weight.timestamp = now - 2400;
            weight.stoppedBy = QStringLiteral("weight");
            QVERIFY(insertShot(db, weight) > 0);

            ShotRow volume = base;
            volume.uuid = QStringLiteral("sb-volume");
            volume.timestamp = now - 2100;
            volume.stoppedBy = QStringLiteral("volume");
            QVERIFY(insertShot(db, volume) > 0);

            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10,
                shotInPackage(onlyEquipmentPackage(db)));
            QCOMPARE(sessions.size(), 1);
            const QJsonArray shots = sessions.at(0).toObject()
                .value(QStringLiteral("shots")).toArray();
            QCOMPARE(shots.size(), 4);

            int sawManual = 0, sawWeight = 0, sawVolume = 0, sawProfileEndKey = 0;
            for (const auto& v : shots) {
                const QJsonObject s = v.toObject();
                if (!s.contains(QStringLiteral("stoppedBy"))) { ++sawProfileEndKey; continue; }
                const QString sb = s.value(QStringLiteral("stoppedBy")).toString();
                if (sb == QStringLiteral("manual")) ++sawManual;
                if (sb == QStringLiteral("weight")) ++sawWeight;
                if (sb == QStringLiteral("volume")) ++sawVolume;
            }
            QCOMPARE(sawManual, 1);
            QCOMPARE(sawWeight, 1);
            QCOMPARE(sawVolume, 1);
            QVERIFY2(sawProfileEndKey == 1,
                     "profileEnd must be omitted (no stoppedBy key)");

            // bestRecentShot: a manually-stopped rated anchor surfaces it.
            ShotRow bestManual = base;
            bestManual.uuid = QStringLiteral("sb-best");
            bestManual.timestamp = now - 5 * kSecPerDay;
            bestManual.enjoyment = 90;
            bestManual.stoppedBy = QStringLiteral("manual");
            const qint64 bestId = insertShot(db, bestManual);
            QVERIFY(bestId > 0);

            ShotRow cur = base;
            cur.uuid = QStringLiteral("sb-cur");
            cur.timestamp = now - kSecPerDay;
            const qint64 curId = insertShot(db, cur);
            QVERIFY(curId > 0);
            const ShotProjection curProj = projectionForShot(db, curId);
            QVERIFY(curProj.isValid());

            const QJsonObject best_ = DialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-dfq"), curId, curProj);
            QVERIFY(!best_.isEmpty());
            QCOMPARE(best_.value(QStringLiteral("id")).toVariant().toLongLong(), bestId);
            QCOMPARE(best_.value(QStringLiteral("stoppedBy")).toString(),
                     QStringLiteral("manual"));
        });
    }

    // -------------------------------------------------------------------
    // #1160: the D-Flow umbrella section is split so pressure-distinct
    // variants resolve to distinct UGS positions and canonical names,
    // instead of all collapsing to one UGS 0.5 / "D-Flow". Asserts the
    // content contract from the correct-dflow-variant-ugs spec.
    // -------------------------------------------------------------------
    void dflowVariantUgs_distinctPositions_1160()
    {
        const QString kbBase =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / default"),
                                               QStringLiteral("dflow"));
        const QString kbQ =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / Q"),
                                               QStringLiteral("dflow"));
        const QString kbDamianQ =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's Q"),
                                               QStringLiteral("dflow"));
        const QString kbLrv3 =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's LRv3"),
                                               QStringLiteral("dflow"));
        QVERIFY(!kbBase.isEmpty());
        QVERIFY(!kbQ.isEmpty());
        QVERIFY(!kbDamianQ.isEmpty());
        QVERIFY(!kbLrv3.isEmpty());

        const double ugsBase = ShotSummarizer::ugsForKbId(kbBase);
        const double ugsQ    = ShotSummarizer::ugsForKbId(kbQ);
        const double ugsLrv3 = ShotSummarizer::ugsForKbId(kbLrv3);

        // Base D-Flow keeps the chart-authoritative canonical 0.5.
        QVERIFY(qFuzzyCompare(ugsBase, 0.5));
        QVERIFY(!ShotSummarizer::ugsInferredForKbId(kbBase));

        // D-Flow/Q resolves strictly coarser than base, and is inferred,
        // with a canonical name distinct from base D-Flow.
        QVERIFY(ugsQ > ugsBase);
        QVERIFY(ShotSummarizer::ugsInferredForKbId(kbQ));
        QVERIFY(ShotSummarizer::canonicalNameForKbId(kbQ)
                != ShotSummarizer::canonicalNameForKbId(kbBase));

        // "Damian's Q" resolves to the same position as D-Flow/Q.
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbDamianQ),
                 ShotSummarizer::canonicalNameForKbId(kbQ));
        QVERIFY(qFuzzyCompare(ShotSummarizer::ugsForKbId(kbDamianQ), ugsQ));

        // Damian's LRv3 == canonical Londinium/LRv3 position (0),
        // strictly finer than base D-Flow.
        QVERIFY(qFuzzyIsNull(ugsLrv3));
        QVERIFY(ugsLrv3 < ugsBase);

        // Shared behavioral suppression preserved on every variant.
        const QString kbLrv2 =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's LRv2"),
                                               QStringLiteral("dflow"));
        QVERIFY(!kbLrv2.isEmpty());
        // LRv2 and LRv3 land on DIFFERENT sections, and both by real
        // Also-matches keys rather than a fuzzy substring fallback on the bare
        // "lrv3" title token — which is what #1160 was guarding against, and
        // is still what this asserts.
        //
        // They used to share a section. They are not the same profile: LRv2 is
        // byte-identical in extraction to the shipped Londonium profile (its
        // notes say so) and belongs to the `londinium` entry; LRv3 has eight
        // frames to their seven, 90C, and a 9-bar hold. Same UGS, different
        // sections — which is exactly why this pins the NAMES and the UGS
        // separately rather than treating one as evidence of the other.
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbLrv2),
                 QStringLiteral("Londinium"));
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbLrv3),
                 QStringLiteral("Damian's LRv3"));
        QVERIFY(qFuzzyCompare(ShotSummarizer::ugsForKbId(kbLrv2) + 1.0,
                              ugsLrv3 + 1.0));
        QVERIFY(ShotSummarizer::getAnalysisFlags(kbBase)
                .contains(QStringLiteral("flow_trend_ok")));
        QVERIFY(ShotSummarizer::getAnalysisFlags(kbQ)
                .contains(QStringLiteral("flow_trend_ok")));
        QVERIFY(ShotSummarizer::getAnalysisFlags(kbLrv2)
                .contains(QStringLiteral("flow_trend_ok")));
    }

    // split-dflow-la-pavoni-kb-section: completes the #1160 per-profile
    // split for the profile #1160 deferred. D-Flow / La Pavoni must
    // resolve to its own section (84°C / 6–9 bar dial-in), NOT inherit
    // the base ## D-Flow (default, 0.5, 88°C) section.
    void dflowLaPavoniVariant_distinctPosition()
    {
        const QString kbBase =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / default"),
                                               QStringLiteral("dflow"));
        const QString kbLP =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / La Pavoni"),
                                               QStringLiteral("dflow"));
        const QString kbQ =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / Q"),
                                               QStringLiteral("dflow"));
        QVERIFY(!kbBase.isEmpty());
        QVERIFY(!kbLP.isEmpty());
        QVERIFY(!kbQ.isEmpty());

        // La Pavoni resolves to its own canonical name, distinct from base
        // D-Flow / default (it no longer aliases the base section) and from
        // the Q variant (it is its own section).
        QVERIFY(ShotSummarizer::canonicalNameForKbId(kbLP)
                != ShotSummarizer::canonicalNameForKbId(kbBase));
        QVERIFY(ShotSummarizer::canonicalNameForKbId(kbLP)
                != ShotSummarizer::canonicalNameForKbId(kbQ));
        // Pin the positive resolution target absolutely: a future KB edit
        // that renamed/merged the section but kept it distinct from base
        // and Q would leave the relative checks above green while still
        // violating the "resolves to its own section" spec invariant.
        // Also defeats the bare-"d-flow" fuzzy-fallback confound (the base
        // section's title-split emits a bare "d-flow" key that would
        // prefix-match "d-flow / la pavoni" if the direct alias regressed).
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbLP),
                 QStringLiteral("D-Flow La Pavoni variant"));

        // Strictly coarser than base, and inferred (same lower-pressure +
        // 84°C-fill mechanism the Q variant documents).
        QVERIFY(ShotSummarizer::ugsForKbId(kbLP)
                > ShotSummarizer::ugsForKbId(kbBase));
        QVERIFY(ShotSummarizer::ugsInferredForKbId(kbLP));

        // Shared lever-decline behavioral suppression preserved.
        QVERIFY(ShotSummarizer::getAnalysisFlags(kbLP)
                .contains(QStringLiteral("flow_trend_ok")));
    }

    // #1198: deterministic recipe-alias longest-boundary-prefix resolution.
    // A user-renamed/numbered variant of a documented recipe inherits that
    // recipe's KB entry; built-ins still resolve by exact match; the editor
    // namespace is never a prefix anchor; matching is profile-general.
    void recipeVariantPrefixResolution_1198()
    {
        const QString kbBase =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / default"),
                                               QStringLiteral("dflow"));
        const QString kbQ =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / Q"),
                                               QStringLiteral("dflow"));
        const QString kbLP =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / La Pavoni"),
                                               QStringLiteral("dflow"));
        QVERIFY(!kbBase.isEmpty());
        QVERIFY(!kbQ.isEmpty());
        QVERIFY(!kbLP.isEmpty());
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbQ),
                 QStringLiteral("D-Flow Q variant"));

        // (3.1a) D-Flow/Q cluster: suffixed, bean-suffixed, numbered, and
        // hyphen-joined renames all resolve to the Q variant via the
        // recipe-prefix step (separator ∈ { / - space digit }, D1/D3).
        for (const QString& t : {
                 QStringLiteral("D-Flow / Q - Jeff"),
                 QStringLiteral("D-Flow / Q - Ethiopia Natural"),
                 QStringLiteral("D-Flow / Q2"),
                 QStringLiteral("D-Flow / Q3"),
                 QStringLiteral("D-Flow / Q-Jeff"),
                 QStringLiteral("Damian's Q - decaf") }) {
            QCOMPARE(ShotSummarizer::computeProfileKbId(t, QStringLiteral("dflow")), kbQ);
        }
        // La Pavoni suffixed → its own variant (longest-prefix wins over the
        // shorter, excluded "D-Flow" editor anchor), strictly coarser than base.
        const QString kbLP80 =
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / La Pavoni 80s"),
                                               QStringLiteral("dflow"));
        QCOMPARE(kbLP80, kbLP);
        QVERIFY(ShotSummarizer::ugsForKbId(kbLP) > ShotSummarizer::ugsForKbId(kbBase));

        // (3.1b) Generality (D9) — NOT D-Flow-specific. No editor hint, so
        // resolution is the recipe-prefix step itself, not editor-default.
        // (normalizeProfileKey folds é→e, so the ASCII form suffices.)
        const QString kbAdaptive =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Adaptive v2"), QString());
        const QString kbLond =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Londinium"), QString());
        const QString kbAllonge =
            ShotSummarizer::computeProfileKbId(QStringLiteral("Allonge"), QString());
        QVERIFY(!kbAdaptive.isEmpty());
        QVERIFY(!kbLond.isEmpty());
        QVERIFY(!kbAllonge.isEmpty());
        QCOMPARE(ShotSummarizer::computeProfileKbId(
                     QStringLiteral("Adaptive v2 - Jeff"), QString()), kbAdaptive);
        QCOMPARE(ShotSummarizer::computeProfileKbId(
                     QStringLiteral("Londinium - Jeff"), QString()), kbLond);
        QCOMPARE(ShotSummarizer::computeProfileKbId(
                     QStringLiteral("Allonge - decaf"), QString()), kbAllonge);

        // (3.2) Negatives: a following LETTER is not a boundary, so
        // "D-Flow / Quark" must NOT absorb the "D-Flow / Q" alias, and
        // "D-FlowX" must NOT absorb "D-Flow". The editor name is not an
        // anchor (D2): a fully-custom title falls to the editor default
        // with a hint, and is unresolved without one.
        QVERIFY(ShotSummarizer::computeProfileKbId(
                    QStringLiteral("D-Flow / Quark"), QString()).isEmpty());
        QVERIFY(ShotSummarizer::computeProfileKbId(
                    QStringLiteral("D-Flow / Quark"), QString()) != kbQ);
        QCOMPARE(ShotSummarizer::computeProfileKbId(
                     QStringLiteral("D-FlowX"), QStringLiteral("dflow")), kbBase);
        QCOMPARE(ShotSummarizer::computeProfileKbId(
                     QStringLiteral("My Morning Pull"), QStringLiteral("dflow")), kbBase);
        QVERIFY(ShotSummarizer::computeProfileKbId(
                    QStringLiteral("My Morning Pull"), QString()).isEmpty());

        // (3.2a / D8) Built-ins resolve by EXACT match and are never
        // collapsed by the prefix step: the three canonical D-Flow built-ins
        // stay three distinct entries, exact precedence intact.
        QVERIFY(kbBase != kbQ);
        QVERIFY(kbBase != kbLP);
        QVERIFY(kbQ != kbLP);
        QCOMPARE(ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / Q"),
                                                    QStringLiteral("dflow")), kbQ);
        QCOMPARE(ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's Q"),
                                                    QStringLiteral("dflow")), kbQ);

        // (3.3 / D6) A legacy persisted normalized-title kbId heals to the
        // parent recipe id via the SAME shared step (resolveKbId →
        // resolveKbInput), under the recompute-on-load contract.
        QCOMPARE(ShotSummarizer::resolveKbId(QStringLiteral("d-flow / q - jeff")), kbQ);
    }

    // flag-off-expert-band-in-shot-summary: a shot saved before the
    // #1160/#1175 KB splits can carry a STALE persisted profileKbId
    // (real case: shot 819 — profileName "D-Flow / Q" but stored
    // "d-flow / default"). prepareAnalysisInputs must NOT key the band
    // off that stale value; it re-resolves canonical identity from the
    // title via computeProfileKbId against the CURRENT KB (D7/D14).
    // This pins the mechanism the fix depends on: keying off the stale
    // stored kbId loses the band; the fresh title resolution recovers it.
    void expertBand_staleKbId_freshTitleResolutionRecoversBand()
    {

        // The stale stored value the buggy path used → no band (this is
        // exactly why shot 819 wrongly read "Clean shot").
        const auto stale =
            ShotSummarizer::expertBandForKbId(QStringLiteral("d-flow / default"));
        QVERIFY2(!stale.has_value(),
                 "stale 'd-flow / default' kbId resolves to the band-less "
                 "base section — keying off it loses the band (the bug)");

        // The fix: fresh re-resolution from the real title against the
        // current KB → the gold band.
        const QString freshKb = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow / Q"), QStringLiteral("dflow"));
        const auto fresh = ShotSummarizer::expertBandForKbId(freshKb);
        QVERIFY2(fresh.has_value(),
                 "fresh title resolution must recover the D-Flow/Q band");
        QCOMPARE(fresh->axis, ExpertBand::Axis::PressurePeak);
        QCOMPARE(*fresh->lo, 6.0);
        QCOMPARE(*fresh->hi, 9.0);

        // Twin collapses to the same canonical entry.
        const auto twin = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's Q"),
                                               QStringLiteral("dflow")));
        QVERIFY(twin.has_value());
        QCOMPARE(*twin->lo, *fresh->lo);
        QCOMPARE(*twin->hi, *fresh->hi);

        // Inverse: a genuinely band-less profile stays absent — the fresh
        // resolution governs in both directions, never fabricates a band.
        const auto none = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("D-Flow / default"),
                                               QStringLiteral("dflow")));
        QVERIFY2(!none.has_value(),
                 "D-Flow / default has no cited band — absent by fresh "
                 "resolution too");
    }

    // Phase B end-to-end: the kBands["A-Flow"] row must be reachable via
    // the real KB-resolution path (computeProfileKbId → expertBandForKbId),
    // not just constructed inline in a slice test. This is the guard that
    // catches a key typo / `## A-Flow` heading rename — either would make
    // every A-Flow shot silently lose its band in production.
    void expertBand_aflow_resolvesFromTitle()
    {

        const QString kb = ShotSummarizer::computeProfileKbId(
            QStringLiteral("A-Flow / default-medium"), QStringLiteral("aflow"));
        const auto band = ShotSummarizer::expertBandForKbId(kb);
        QVERIFY2(band.has_value(),
                 "A-Flow / default-medium must resolve to the cited band "
                 "via the production KB path");
        QCOMPARE(band->axis, ExpertBand::Axis::PressurePeak);
        QCOMPARE(*band->lo, 6.0);
        QCOMPARE(*band->hi, 9.0);
        // Reviewed-correction (restructure-kb-as-validated-json): src is now
        // a followable real-doc URL, not the old "[SRC:token]" form.
        QCOMPARE(band->src, QStringLiteral("https://github.com/Jan3kJ/A_Flow"));
        QCOMPARE(band->confidence, QStringLiteral("medium"));

        // All shipped A-Flow variants canonical-key to the one `## A-Flow`
        // section → identical band (structural dedup, like the gold pair).
        for (const QString& title : { QStringLiteral("A-Flow / default-light"),
                                      QStringLiteral("A-Flow / default-dark"),
                                      QStringLiteral("A-Flow / default-very-dark"),
                                      QStringLiteral("A-Flow / default-like-dflow") }) {
            const auto v = ShotSummarizer::expertBandForKbId(
                ShotSummarizer::computeProfileKbId(title, QStringLiteral("aflow")));
            QVERIFY2(v.has_value(), qPrintable(title + " must resolve to the A-Flow band"));
            QCOMPARE(*v->lo, *band->lo);
            QCOMPARE(*v->hi, *band->hi);
            QCOMPARE(v->src, band->src);
        }
    }

    // Phase C end-to-end: the kBands["Londinium"] row resolves via the
    // production KB path, and — critically — the standalone Londinium key
    // does NOT collide with Damian's LRv2/LRv3. They stay separate because
    // those titles resolve to their own canonical section
    // (`Damian's LRv2 / LRv3`) absent from kBands — NOT because of the
    // `## Londinium` Note (inert LLM prose). Guards a key typo /
    // `## Londinium` rename and the Damian-LR separation in one shot.
    void expertBand_londinium_resolvesAndDoesNotCatchDamianLRv3()
    {
        // Londinium, Londonium and Damian's LRv2 are ONE profile under three
        // names — londonium.json and damian_s_lrv2.json are byte-identical
        // across all seven frames, and Londonium's own notes say so. All three
        // therefore carry the cited decent-guide band.
        //
        // This test previously asserted the opposite for LRv2, on the strength
        // of a KB claim ("different fill/infuse behavior and higher frame
        // temperatures") that the shipped files disprove: both run
        // 89/89/88.5/88.5/88/88/88. The entries were merged; this is the
        // corrected pin.
        for (const QString& title : { QStringLiteral("Londinium"),
                                      QStringLiteral("Londonium"),
                                      QStringLiteral("Damian's LRv2") }) {
            const auto band = ShotSummarizer::expertBandForKbId(
                ShotSummarizer::computeProfileKbId(title, QStringLiteral("advanced")));
            QVERIFY2(band.has_value(),
                     qPrintable(title + " must resolve to the decent-guide band"));
            QCOMPARE(band->axis, ExpertBand::Axis::PressurePeak);
            QCOMPARE(*band->lo, 8.0);
            QCOMPARE(*band->hi, 9.0);
            QCOMPARE(band->src,
                     QStringLiteral("https://decentespresso.com/first_decent_espresso"));
            QCOMPARE(band->confidence, QStringLiteral("medium"));
        }

        // Damian's LRv3 IS a different profile — eight frames against seven,
        // 90C, a 9-bar hold before declining to 5.5 bar, and no flow-control
        // safety step. Its pressure story is not Londinium's 9->8 taper, so it
        // must not pick up this band.
        const auto v3 = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("Damian's LRv3"),
                                               QStringLiteral("advanced")));
        QVERIFY2(!v3.has_value() ||
                 v3->src != QStringLiteral("https://decentespresso.com/first_decent_espresso"),
                 "Damian's LRv3 must NOT resolve to the Londinium band");
    }

    // Phase C — Adaptive v2: Decent authored the profile, so decent-guide
    // is the author authority. Band is 6–9 (contains the by-design
    // adaptive envelope; observational "could be better" outside it).
    // `## Gagné Adaptive` is a separate canonical section and must not
    // pick up this band.
    void expertBand_adaptiveV2_resolvesAndDoesNotCatchGagne()
    {
        const auto band = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("Adaptive v2"),
                                               QStringLiteral("advanced")));
        QVERIFY2(band.has_value(), "Adaptive v2 must resolve to the band");
        QCOMPARE(band->axis, ExpertBand::Axis::PressurePeak);
        QCOMPARE(*band->lo, 6.0);
        QCOMPARE(*band->hi, 9.0);
        QCOMPARE(band->src,
                 QStringLiteral("https://decentespresso.com/first_decent_espresso"));

        const auto g = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("Gagné Adaptive"),
                                               QStringLiteral("advanced")));
        QVERIFY2(!g.has_value()
                     || g->src != QStringLiteral("https://decentespresso.com/first_decent_espresso")
                     || *g->lo != 6.0,
                 "Gagné Adaptive must NOT pick up the Adaptive v2 band");
    }

    // Phase C — Allongé: a ONE-SIDED flow floor (lo set, hi unset). All
    // its aliases resolve to the single `## Allonge` section.
    void expertBand_allonge_resolvesAsOneSidedFlowFloor()
    {
        for (const QString& title : { QStringLiteral("Rao Allongé"),
                                      QStringLiteral("Allongé"),
                                      QStringLiteral("Allonge") }) {
            const auto band = ShotSummarizer::expertBandForKbId(
                ShotSummarizer::computeProfileKbId(title, QStringLiteral("advanced")));
            QVERIFY2(band.has_value(),
                     qPrintable(title + " must resolve to the Allongé flow floor"));
            QCOMPARE(band->axis, ExpertBand::Axis::ExtractionFlow);
            QVERIFY2(band->lo.has_value(), "floor must set lo");
            QCOMPARE(*band->lo, 4.5);
            QVERIFY2(!band->hi.has_value(),
                     "one-sided floor must leave hi unset (no fabricated ceiling)");
            QCOMPARE(band->src,
                     QStringLiteral("https://decentespresso.com/blog/5_espresso_profiles_for_light_roasted_coffee_beans"));
        }
    }

    // Phase D (D1): complete seed-coverage — every shipped kBands row
    // resolves to its EXACT cited band via the production KB path. One
    // table locks the whole expert-band surface: a dropped/mis-keyed row,
    // a changed bound, a wrong [SRC] tag, or a confidence drift all fail
    // here. (Per-row tests above also assert non-collision; this is the
    // single completeness pin across all five canonical entries.)
    void expertBand_allShippedRows_seedCoverage()
    {
        using Axis = ExpertBand::Axis;
        struct Row {
            QString title; QString editor; Axis axis;
            double lo; bool hasHi; double hi; QString src; QString conf;
        };
        const QVector<Row> rows = {
            // src is now a real-doc URL / intrinsic token (reviewed-correction,
            // restructure-kb-as-validated-json — was the old "[SRC:token]").
            { "D-Flow / Q",          "dflow",    Axis::PressurePeak,  6.0, true, 9.0, "profile-notes", "high"   },
            { "D-Flow / La Pavoni",  "dflow",    Axis::PressurePeak,  6.0, true, 9.0, "profile-notes", "high"   },
            { "A-Flow / default-medium", "aflow", Axis::PressurePeak, 6.0, true, 9.0, "https://github.com/Jan3kJ/A_Flow", "medium" },
            { "Londinium",           "advanced", Axis::PressurePeak,  8.0, true, 9.0, "https://decentespresso.com/first_decent_espresso", "medium" },
            { "Adaptive v2",         "advanced", Axis::PressurePeak,  6.0, true, 9.0, "https://decentespresso.com/first_decent_espresso", "medium" },
            { "Rao Allongé",         "advanced", Axis::ExtractionFlow,4.5, false,0.0, "https://decentespresso.com/blog/5_espresso_profiles_for_light_roasted_coffee_beans", "medium" },
        };
        for (const Row& r : rows) {
            const auto b = ShotSummarizer::expertBandForKbId(
                ShotSummarizer::computeProfileKbId(r.title, r.editor));
            QVERIFY2(b.has_value(), qPrintable(r.title + " must resolve to a band"));
            QCOMPARE(b->axis, r.axis);
            QVERIFY2(b->lo.has_value(), qPrintable(r.title + " lo must be set"));
            QCOMPARE(*b->lo, r.lo);
            QCOMPARE(b->hi.has_value(), r.hasHi);
            if (r.hasHi) QCOMPARE(*b->hi, r.hi);
            QCOMPARE(b->src, r.src);
            QCOMPARE(b->confidence, r.conf);
        }
    }

    // -------------------------------------------------------------------
    // correct-dflow-aflow-editor-profile-docs: regression guard for the
    // shipped KB the LLM ingests. It is a *known-bad blocklist + known-good
    // allow-list*, NOT a full referential-integrity check: (a) none of the
    // four KNOWN stale fictitious A-Flow names, and every real shipped
    // built-in title present (the asserted title set is the authoritative
    // list from resources/profiles/*.json — a brand-new fictitious name
    // would still pass; that gap is accepted for a doc guard) — now checked
    // by RESOLUTION (each real title → a non-empty canonical id) rather than
    // md-text presence; (b) no reintroduction of the exact profile-implying
    // "base D-Flow / variant / family" framings this change removed
    // (paraphrases are not caught — the guard prevents *this* drift back, it
    // does not prove the prose teaches the right model); (c) the #1160/#1175
    // split mechanics as RESOLUTION invariants (D-Flow/Q ≡ Damian's Q same
    // id; D-Flow/La Pavoni ≠ default; Damian LRv2 ≠ standalone Londinium) +
    // a "id": entry-count pin — replacing the obsolete md heading/alias-line
    // byte-count drift-check (the structured format + build-time validator
    // now enforce alias→exactly-one-id integrity).
    // -------------------------------------------------------------------
    void shippedKb_editorModelAndRealProfileNames_guard()
    {
        QFile f(QStringLiteral(":/ai/profile_knowledge.json"));
        QVERIFY2(f.open(QIODevice::ReadOnly),
                 "shipped KB resource :/ai/profile_knowledge.json not found "
                 "(ai.qrc must be linked)");
        const QString kb = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY(!kb.isEmpty());
        // Structural-stability pin (restructure-kb-as-validated-json): exactly
        // 48 entries. Parsed (not a raw "id": substring count, which a future
        // prose/rationale string containing `"id":` would falsely break). If a
        // KB entry is intentionally added/removed, update this count AND
        // re-verify #1160 resolution.
        //
        // 43 -> 47 (sync-builtin-profiles): the seven profiles Decaid shipped and
        // Decenza did not are now built-ins, and every built-in needs a KB entry —
        // baseline-contact-series covers all four Baseline levels through
        // alsoMatches, plus icbinf, psph and soup-58.
        //
        // 47 -> 48: de1app added a new built-in, Adaptive v3 (best_practice.tcl),
        // synced in with KB entry `adaptive-v3`.
        const QJsonArray kbProfiles =
            QJsonDocument::fromJson(kb.toUtf8()).object()
                .value(QStringLiteral("profiles")).toArray();
        QCOMPARE(kbProfiles.size(), 48);

        // LLM-facing scope: the (a)/(c) framing checks target what the model
        // ingests — `prose` + identity. `rationale` (and `src`) are
        // human/validator audit metadata: never read by the loader, never
        // assembled into the prompt. The verbatim kBands audit text legally
        // contains loose phrasing ("other shipped A-Flow variants' limiters
        // …"); excluding `"rationale":` lines scopes the guard to its true
        // intent without weakening it (prose/displayName/alsoMatches stay).
        QStringList llmLines;
        for (const QString& ln : kb.split(QLatin1Char('\n')))
            if (!ln.contains(QStringLiteral("\"rationale\":")))
                llmLines << ln;
        const QString llmText = llmLines.join(QLatin1Char('\n'));

        // (a) Stale, fictitious A-Flow names must be gone. None of these is
        // a substring of a real built-in title (e.g. "A-Flow / dark" is NOT
        // a substring of "A-Flow / default-dark"), so plain contains() is
        // a safe exact check.
        for (const QString& stale : {
                 QStringLiteral("A-Flow / medium"),
                 QStringLiteral("A-Flow / dark"),
                 QStringLiteral("A-Flow / very dark"),
                 QStringLiteral("A-Flow / like D-Flow") }) {
            QVERIFY2(!llmText.contains(stale),
                     qPrintable(QStringLiteral("shipped KB still references "
                         "stale non-existent A-Flow profile name: ") + stale));
        }

        // (b) Every real shipped built-in D-Flow/A-Flow title must RESOLVE
        // (stronger than the old md-text presence check): each maps to a
        // non-empty canonical id via the production resolver — D-Flow/* by
        // explicit alias, A-Flow/* via the editor-type default fallback.
        const QVector<QPair<QString, QString>> reals = {
            { QStringLiteral("D-Flow / default"),          QStringLiteral("dflow") },
            { QStringLiteral("D-Flow / La Pavoni"),         QStringLiteral("dflow") },
            { QStringLiteral("D-Flow / Q"),                 QStringLiteral("dflow") },
            { QStringLiteral("A-Flow / default-light"),     QStringLiteral("aflow") },
            { QStringLiteral("A-Flow / default-medium"),    QStringLiteral("aflow") },
            { QStringLiteral("A-Flow / default-dark"),      QStringLiteral("aflow") },
            { QStringLiteral("A-Flow / default-very-dark"), QStringLiteral("aflow") },
            { QStringLiteral("A-Flow / default-like-dflow"),QStringLiteral("aflow") },
        };
        for (const auto& rp : reals) {
            QVERIFY2(!ShotSummarizer::computeProfileKbId(rp.first, rp.second).isEmpty(),
                     qPrintable(QStringLiteral("real built-in profile no longer "
                         "resolves to a KB id: ") + rp.first));
        }

        // (c) Profile-implying wrong-model phrasing must not return. These
        // are the exact framings this change removed; "D-Flow"/"A-Flow"
        // unqualified mean the editor type, never a profile.
        for (const QString& bad : {
                 QStringLiteral("base D-Flow"),
                 QStringLiteral("D-Flow (base)"),
                 QStringLiteral("D-Flow / Damian family"),
                 QStringLiteral("D-Flow variant"),
                 QStringLiteral("A-Flow variant"),
                 QStringLiteral("standard D-Flow variant") }) {
            QVERIFY2(!llmText.contains(bad, Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("shipped KB reintroduced "
                         "profile-implying D-Flow/A-Flow framing: ") + bad));
        }

        // (d) #1160 / #1175 split mechanics — now expressed as RESOLUTION
        // invariants on the structured KB (stronger and more direct than
        // the old md heading/alias-line byte-count drift-check, which the
        // JSON format obsoletes; the validator additionally enforces unique
        // id + alias→exactly-one-id integrity at build time).
        const QString idQ        = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow / Q"), QStringLiteral("dflow"));
        const QString idDamianQ  = ShotSummarizer::computeProfileKbId(
            QStringLiteral("Damian's Q"), QStringLiteral("advanced"));
        const QString idDefault  = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow / default"), QStringLiteral("dflow"));
        const QString idLaPavoni = ShotSummarizer::computeProfileKbId(
            QStringLiteral("D-Flow / La Pavoni"), QStringLiteral("dflow"));
        const QString idLondinium = ShotSummarizer::computeProfileKbId(
            QStringLiteral("Londinium"), QStringLiteral("advanced"));
        const QString idLRv2 = ShotSummarizer::computeProfileKbId(
            QStringLiteral("Damian's LRv2"), QStringLiteral("dflow"));

        QVERIFY2(!idQ.isEmpty() && idQ == idDamianQ,
                 "#1160 twin: D-Flow / Q and Damian's Q must collapse to the "
                 "same canonical id");
        QVERIFY2(!idLaPavoni.isEmpty() && idLaPavoni != idDefault,
                 "#1175 split: D-Flow / La Pavoni must resolve to its OWN id, "
                 "distinct from D-Flow / default");
        // Reversed deliberately. This once asserted the opposite, on the
        // strength of a KB claim the shipped files disprove: londonium.json
        // and damian_s_lrv2.json are byte-identical across all seven frames,
        // and Londonium's notes read "This is identical to the LRv2 profile,
        // but renamed to be easier to understand." One profile, one entry.
        QVERIFY2(!idLondinium.isEmpty() && idLRv2 == idLondinium,
                 "Damian's LRv2 IS the Londonium profile and must resolve to "
                 "the same entry");
        // LRv3 is the one that genuinely differs — eight frames, 90C, a 9-bar
        // hold — and keeps its own id.
        const QString idLRv3 = ShotSummarizer::computeProfileKbId(
            QStringLiteral("Damian's LRv3"), QStringLiteral("dflow"));
        QVERIFY2(!idLRv3.isEmpty() && idLRv3 != idLondinium,
                 "Damian's LRv3 must keep its OWN id, distinct from Londinium");
    }

    // -------------------------------------------------------------------
    // KB roastAffinity (add-recipe-wizard-tea): the wizard's "suits your
    // roast" tier resolves through the same title/alias matching as every
    // other KB lookup. Pins a dark-affinity entry (Londinium), a
    // light-affinity entry resolved via alias (Blooming Espresso), a
    // no-claim entry staying empty, and the unresolved-title fallback.
    // -------------------------------------------------------------------
    void shippedKb_roastAffinityResolution()
    {
        const QStringList londinium =
            ShotSummarizer::roastAffinityForTitle(QStringLiteral("Londinium"));
        QVERIFY(londinium.contains(QStringLiteral("dark")));
        QVERIFY(londinium.contains(QStringLiteral("medium-dark")));
        QVERIFY(!londinium.contains(QStringLiteral("light")));

        const QStringList blooming =
            ShotSummarizer::roastAffinityForTitle(QStringLiteral("Blooming Espresso"));
        QVERIFY(blooming.contains(QStringLiteral("light")));
        QVERIFY(!blooming.contains(QStringLiteral("dark")));

        // Entries with no stated affinity make NO claim (never guessed).
        QVERIFY(ShotSummarizer::roastAffinityForTitle(
                    QStringLiteral("Filter 2.0")).isEmpty());
        // Unresolved titles fall to empty, not a fabricated affinity.
        QVERIFY(ShotSummarizer::roastAffinityForTitle(
                    QStringLiteral("No Such Profile XYZ")).isEmpty());
    }

    // -------------------------------------------------------------------
    // KB UGS grind direction (add-recipe-wizard-tea): the wizard's grind
    // hint gives DIRECTION ONLY between two profiles' UGS positions — per
    // the KB's own cross-profile rule, never a click count. Cremina anchors
    // UGS 0 (finest); Rao Allongé anchors UGS 8 (coarsest).
    // -------------------------------------------------------------------
    void shippedKb_grindDirectionBetween()
    {
        QCOMPARE(ShotSummarizer::grindDirectionBetween(
                     QStringLiteral("Cremina lever machine"), QStringLiteral("Rao Allongé")),
                 QStringLiteral("coarser"));
        QCOMPARE(ShotSummarizer::grindDirectionBetween(
                     QStringLiteral("Rao Allongé"), QStringLiteral("Cremina lever machine")),
                 QStringLiteral("finer"));
        // Same profile (via alias resolution) → "same".
        QCOMPARE(ShotSummarizer::grindDirectionBetween(
                     QStringLiteral("D-Flow / default"), QStringLiteral("D-Flow / default")),
                 QStringLiteral("same"));
        // Either side unresolved or UGS-less → empty (no fabricated direction).
        QVERIFY(ShotSummarizer::grindDirectionBetween(
                    QStringLiteral("No Such Profile XYZ"), QStringLiteral("Rao Allongé")).isEmpty());
        QVERIFY(ShotSummarizer::grindDirectionBetween(
                    QString(), QStringLiteral("Rao Allongé")).isEmpty());
    }

    // -------------------------------------------------------------------
    // restructure-kb-as-validated-json (task 5.2 / 6.6): KB-COVERAGE GATE.
    // Every shipped built-in profile (resources/profiles/*.json) MUST
    // resolve to a KB entry via the production resolver. A NEW built-in
    // added without a matching KB entry FAILS here — forcing the KB to be
    // extended (an explicit alias or entry). This is the permanent guard
    // for "when a new built-in profile is added, a matching KB is added
    // too". Tea converges to the `tea` entry (whose prose tells the AI
    // "this is tea, do not run espresso analysis") so the advisor never
    // mis-handles a tea shot. No allowlist: post alias-fixes every one of
    // the ~94 built-ins resolves; if a genuinely uncoverable profile is
    // ever shipped, add an explicit commented exception WITH rationale —
    // never a silent gap.
    // -------------------------------------------------------------------
    void kbCoverage_everyBuiltInProfileResolves()
    {
        QDir dir(QStringLiteral(PROFILES_DIR));
        QVERIFY2(dir.exists(),
                 qPrintable(QStringLiteral("profiles dir not found: ") + dir.path()));
        const QStringList files = dir.entryList({ QStringLiteral("*.json") },
                                                QDir::Files);
        QVERIFY2(files.size() >= 90,
                 qPrintable(QStringLiteral("expected the full built-in corpus, "
                     "found %1").arg(files.size())));

        QStringList unresolved;
        int resolved = 0;
        for (const QString& fn : files) {
            QFile pf(dir.filePath(fn));
            QVERIFY2(pf.open(QIODevice::ReadOnly),
                     qPrintable(QStringLiteral("cannot open ") + fn));
            const QJsonObject po =
                QJsonDocument::fromJson(pf.readAll()).object();
            pf.close();
            const QString title = po.value(QStringLiteral("title")).toString();
            if (title.isEmpty()) continue;  // not a titled brew profile

            if (ShotSummarizer::computeProfileKbId(title, editorTypeForTitle(title)).isEmpty())
                unresolved << title;
            else
                ++resolved;
        }
        QVERIFY2(unresolved.isEmpty(),
                 qPrintable(QStringLiteral("built-in profile(s) with NO matching "
                     "KB entry — add a KB entry or an alsoMatches alias "
                     "(restructure-kb-as-validated-json):\n  ")
                     + unresolved.join(QStringLiteral("\n  "))));
        // Floor: a regression that blanked many titles would shrink the
        // checked set via the isEmpty()-continue above without tripping
        // `unresolved`. Assert a healthy resolved count too.
        QVERIFY2(resolved >= 85,
                 qPrintable(QStringLiteral("only %1 built-ins resolved — "
                     "expected >= 85 (have titles been blanked?)")
                     .arg(resolved)));
    }

    // -------------------------------------------------------------------
    // restructure-kb-as-validated-json task 5.2 supplement: the spec's
    // "corpus resolution test" (`## A corpus resolution test SHALL be a
    // hard gate`) names "every profile title appearing in tests/data/
    // shots/" as required coverage, distinct from kbCoverage_
    // everyBuiltInProfileResolves above (which only walks resources/
    // profiles/*.json shipped built-ins). Enumerate every shot fixture's
    // `profile_title` and assert it resolves — EXCEPT fixtures manifest.json
    // marks `"kbResolvable": false` (deliberately-unresolvable
    // regression-guard fixtures, not a coverage gap — see each entry's own
    // `description` for why). Data-driven so a new deliberately-unresolvable
    // fixture only needs a manifest.json edit, not a code change here. Any
    // OTHER unresolved title is a real gap: add a KB entry/alias, per the
    // "no allowlist" policy kbCoverage_everyBuiltInProfileResolves documents
    // above.
    // -------------------------------------------------------------------
    void kbCoverage_shotCorpusProfileTitlesResolve()
    {
        QDir dir(QStringLiteral(SHOTS_CORPUS_DIR));
        QVERIFY2(dir.exists(),
                 qPrintable(QStringLiteral("shots corpus dir not found: ") + dir.path()));
        const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files);
        QVERIFY2(files.size() >= 15,
                 qPrintable(QStringLiteral("expected the full shot corpus, found %1")
                     .arg(files.size())));

        QFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
        QVERIFY2(manifestFile.open(QIODevice::ReadOnly), "cannot open manifest.json");
        const QJsonArray manifestShots =
            QJsonDocument::fromJson(manifestFile.readAll())
                .object().value(QStringLiteral("shots")).toArray();
        manifestFile.close();
        QSet<QString> kbUnresolvableFiles;
        for (const QJsonValue& v : manifestShots) {
            const QJsonObject entry = v.toObject();
            if (!entry.value(QStringLiteral("kbResolvable")).toBool(true))
                kbUnresolvableFiles.insert(entry.value(QStringLiteral("file")).toString());
        }
        QVERIFY2(!kbUnresolvableFiles.isEmpty(),
                 "expected at least the two known kbResolvable:false manifest entries");

        QStringList unresolved;
        int resolved = 0, checked = 0;
        for (const QString& fn : files) {
            if (fn == QStringLiteral("manifest.json")) continue;  // not a shot fixture
            if (kbUnresolvableFiles.contains(fn)) continue;  // manifest-marked exception
            QFile pf(dir.filePath(fn));
            QVERIFY2(pf.open(QIODevice::ReadOnly),
                     qPrintable(QStringLiteral("cannot open ") + fn));
            const QJsonObject po = QJsonDocument::fromJson(pf.readAll()).object();
            pf.close();
            const QString title = po.value(QStringLiteral("profile_title")).toString();
            if (title.isEmpty()) continue;  // synthetic curve-only fixture, no profile identity
            ++checked;

            if (ShotSummarizer::computeProfileKbId(title, editorTypeForTitle(title)).isEmpty())
                unresolved << (title + QStringLiteral(" (") + fn + QLatin1Char(')'));
            else
                ++resolved;
        }
        QVERIFY2(unresolved.isEmpty(),
                 qPrintable(QStringLiteral("shot corpus profile title(s) with NO matching "
                     "KB entry — add a KB entry/alias, or if genuinely intentional mark the "
                     "fixture \"kbResolvable\": false in manifest.json:\n  ")
                     + unresolved.join(QStringLiteral("\n  "))));
        // Sanity floor: confirm the exclusion filters above did not eat the
        // whole corpus silently (e.g. a profile_title field rename).
        QVERIFY2(checked >= 10 && resolved == checked,
                 qPrintable(QStringLiteral("only checked %1 / resolved %2 shot-corpus "
                     "titles — expected >= 10 checked, all resolved")
                     .arg(checked).arg(resolved)));
    }

    // -------------------------------------------------------------------
    // restructure-kb-as-validated-json task 5.3: fact-value parity as a
    // frozen-baseline regression guard. The literal pre-migration markdown
    // source (resources/ai/profile_knowledge.md) no longer exists in the
    // repo (task 6.1e deleted it), so a live pre/post diff is impossible
    // now — this instead PINS the currently-resolved UGS/inferred/
    // analysisFlags facts for a representative profile set as one
    // consolidated table, so a future KB edit that silently drifts one of
    // them fails loudly here. Deliberately does NOT re-pin exact band
    // axis/lo/hi values — those are already the exclusive, precisely-pinned
    // concern of expertBand_allShippedRows_seedCoverage (bands present) and
    // expertBand_staleKbId_freshTitleResolutionRecoversBand (D-Flow /
    // default's no-band case); duplicating those numbers here would create
    // a second, driftable "known-good" set for the same facts. This table
    // only asserts band *presence* (has/hasn't a cited band), which those
    // band-value tests don't independently guarantee stays in sync with
    // UGS/flags drift.
    // -------------------------------------------------------------------
    void factValueParity_frozenBaselineForRepresentativeProfiles()
    {
        struct Row {
            QString title, editor;
            double ugs; bool ugsInferred;
            QStringList analysisFlags;
            bool hasBand;
        };
        const QVector<Row> rows = {
            // D-Flow / default: base section, no band (matches
            // expertBand_staleKbId_freshTitleResolutionRecoversBand's
            // "D-Flow / default has no cited band" assertion).
            { "D-Flow / default", "dflow", 0.5, false, { "flow_trend_ok" }, false },
            // D-Flow / Q ≡ Damian's Q (structural zero-dup).
            { "D-Flow / Q", "dflow", 1.0, true, { "flow_trend_ok" }, true },
            // D-Flow / La Pavoni: strictly coarser than base (1.0 > 0.5).
            { "D-Flow / La Pavoni", "dflow", 1.0, true, { "flow_trend_ok" }, true },
            // Damian's LRv2 IS the Londonium profile (byte-identical frames)
            // and resolves to `londinium`, band and all. LRv3 is genuinely
            // different (eight frames, 90C, 9-bar hold) and keeps its own
            // band-free section. Same UGS, opposite band answers — which is
            // the point of listing both rows.
            { "Damian's LRv2", "dflow", 0.0, false, { "flow_trend_ok" }, true },
            { "Damian's LRv3", "dflow", 0.0, false, { "flow_trend_ok" }, false },
            // A-Flow / default-medium: cited band, matches
            // expertBand_aflow_resolvesFromTitle.
            { "A-Flow / default-medium", "aflow", 1.5, false, { "flow_trend_ok" }, true },
            // Londinium: matches expertBand_londinium_resolvesAndDoesNotCatchDamianLRv3.
            { "Londinium", "advanced", 0.0, false, { "flow_trend_ok" }, true },
            // Adaptive v2: matches expertBand_adaptiveV2_resolvesAndDoesNotCatchGagne.
            { "Adaptive v2", "advanced", 1.25, false, {}, true },
            // Rao Allongé: one-sided flow floor, matches
            // expertBand_allonge_resolvesAsOneSidedFlowFloor.
            { "Rao Allongé", "advanced", 8.0, false,
              { "channeling_expected", "grind_check_skip" }, true },
        };

        for (const Row& row : rows) {
            const QString kbId = ShotSummarizer::computeProfileKbId(row.title, row.editor);
            QVERIFY2(!kbId.isEmpty(), qPrintable(row.title + QStringLiteral(" must resolve")));

            QVERIFY2(qFuzzyIsNull(ShotSummarizer::ugsForKbId(kbId) - row.ugs),
                     qPrintable(QStringLiteral("%1: ugs drifted, expected %2 got %3")
                         .arg(row.title).arg(row.ugs).arg(ShotSummarizer::ugsForKbId(kbId))));
            QCOMPARE(ShotSummarizer::ugsInferredForKbId(kbId), row.ugsInferred);

            const QStringList flags = ShotSummarizer::getAnalysisFlags(kbId);
            QCOMPARE(flags.size(), row.analysisFlags.size());
            for (const QString& f : row.analysisFlags)
                QVERIFY2(flags.contains(f),
                         qPrintable(QStringLiteral("%1: analysisFlags missing %2")
                             .arg(row.title, f)));

            QCOMPARE(ShotSummarizer::expertBandForKbId(kbId).has_value(), row.hasBand);
        }
    }

    // -------------------------------------------------------------------
    // restructure-kb-as-validated-json task 5.4: the assembled LLM-facing
    // blob for a profile with a cited expertBand carries the band claim
    // rendered from the struct EXACTLY once. Mirrors the real assembly
    // split (shotAnalysisSystemPrompt's "## Current Profile Knowledge"
    // section emits the raw KB `prose`; buildUserPrompt/
    // renderShotAnalysisProse's "## Detector Observations" section emits
    // ShotAnalysis::analyzeShot's struct-rendered band-deviation sentence,
    // which only fires when the shot's actual peak/flow falls outside the
    // band): concatenating both halves — the way the real advisor turn
    // does — must not contain two copies of the same band claim (D9: prose
    // numbers are commentary, the struct is the sole source of the cited
    // band). Uses A-Flow (cited provenance, no `proseRestatesBand`
    // acknowledgement needed — its prose narrates "9–10 bar" as the
    // profile's own ramp-target commentary, distinct from the struct's
    // "6.0–9.0 band" formatted clause) rather than the D-Flow/Q example
    // named in the task description, whose prose is a REVIEWED, ACKED
    // near-verbatim restatement (see its expertBand.proseRestatesBand) —
    // deliberately picking the profile whose prose does NOT already
    // license restating the bound keeps this test unambiguous about what
    // "duplicate" means.
    // -------------------------------------------------------------------
    void assembledBlob_bandRenderedOnceFromStruct_notDuplicatedInProse()
    {

        const QString kbId = ShotSummarizer::computeProfileKbId(
            QStringLiteral("A-Flow / default-medium"), QStringLiteral("aflow"));
        QVERIFY(!kbId.isEmpty());

        const auto band = ShotSummarizer::expertBandForKbId(kbId);
        QVERIFY2(band.has_value(), "A-Flow must carry a cited expertBand for this test");
        QCOMPARE(band->axis, ExpertBand::Axis::PressurePeak);
        QVERIFY(band->lo.has_value() && band->hi.has_value());
        QCOMPARE(*band->lo, 6.0);
        QCOMPARE(*band->hi, 9.0);

        const QString prose = ShotSummarizer::profileKnowledgeForKbId(kbId);
        QVERIFY2(!prose.isEmpty(), "A-Flow KB prose must be present");

        // ShotCurveFixtures::bandFixture (shared with tst_shotanalysis.cpp):
        // a clean shot (no channeling, no truncation, flow tracking goal)
        // whose pressure peak (10 bar) sits outside the 6.0-9.0 band by more
        // than EXPERT_BAND_PRESSURE_MARGIN_BAR (0.3), isolating the
        // band-deviation line with no other detector confounding it.
        QList<HistoryPhaseMarker> phases;
        QVector<QPointF> pressure, flow, weight, dCdt, pressureGoal, flowGoal;
        ShotCurveFixtures::bandFixture(/*peakBar=*/10.0, phases, pressure, flow, weight,
                                        dCdt, pressureGoal, flowGoal);

        const auto result = ShotAnalysis::analyzeShot(
            pressure, flow, weight, dCdt, phases,
            QStringLiteral("espresso"), 30.0, pressureGoal, flowGoal,
            ShotSummarizer::getAnalysisFlags(kbId),
            -1.0, 36.0, 36.0, -1, band, /*profileKbResolved=*/true);

        QString deviationLine;
        for (const QVariant& v : result.lines) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("kind")).toString() == QStringLiteral("expert_band_deviation")) {
                deviationLine = m.value(QStringLiteral("text")).toString();
                break;
            }
        }
        QVERIFY2(!deviationLine.isEmpty(),
                 "expected the struct-rendered expert_band_deviation line to fire "
                 "for a 10 bar peak vs the 6.0-9.0 A-Flow band");
        QVERIFY2(deviationLine.contains(QStringLiteral("outside"))
                     && deviationLine.contains(QStringLiteral("judge by taste")),
                 qPrintable(deviationLine));

        // Assemble the blob the way the real advisor turn does: the KB
        // prose (system-prompt "## Current Profile Knowledge" section)
        // followed by the detector observation (user-prompt "## Detector
        // Observations" section).
        const QString blob = prose + QStringLiteral("\n\n") + deviationLine;

        QVERIFY2(!blob.isEmpty(), "assembled blob must be non-empty");
        QVERIFY2(blob.contains(deviationLine),
                 "assembled blob must carry the struct-rendered band sentence");

        // The exact struct-formatted clause (matches the %1–%2 formatting
        // in ShotAnalysis::analyzeShot's expert-band block: fixed one
        // decimal, en-dash, the word "band") — this precise substring is
        // what a duplicate-copy regression (D9: a `prose` line hand-
        // authored to restate the band) would double up. It must appear
        // in the blob exactly once, contributed solely by the struct-
        // rendered line, never a second time from raw prose text.
        const QString bandClause = QStringLiteral("%1–%2 band")
            .arg(*band->lo, 0, 'f', 1).arg(*band->hi, 0, 'f', 1);
        QCOMPARE(blob.count(bandClause), 1);
        // And confirm prose alone (without the struct line) does NOT
        // already contain it — otherwise the count-of-1 above would be
        // vacuously true for the wrong reason (both halves contributing
        // zero, or prose alone already carrying it once and the struct
        // line's copy pushing it to 2, which the QCOMPARE above would
        // have already caught, but this isolates *which* half the one
        // occurrence came from).
        QCOMPARE(prose.count(bandClause), 0);
    }

    // restructure-kb-as-validated-json: the resolver's CENTRAL safety
    // property — an UNKNOWN input is a strict no-op across every consumer
    // (the order-dependent greedy startsWith/contains fallback is deleted
    // and must stay dead). Success paths are covered above; this is the
    // only test pinning the negative branch, so a future re-introduced
    // fuzzy fallback or default-entry leak fails loudly here.
    void resolver_unknownInput_isStrictNoOpAcrossConsumers()
    {
        const QString junk = QStringLiteral("no-such-profile-xyz-9000");
        const QString editor = QStringLiteral("advanced");  // not dflow/aflow
        QVERIFY(ShotSummarizer::computeProfileKbId(junk, editor).isEmpty());
        QVERIFY(ShotSummarizer::getAnalysisFlags(junk).isEmpty());
        QVERIFY(qIsNaN(ShotSummarizer::ugsForKbId(junk)));
        QVERIFY(!ShotSummarizer::ugsInferredForKbId(junk));
        QVERIFY(ShotSummarizer::canonicalNameForKbId(junk).isEmpty());
        QVERIFY(!ShotSummarizer::expertBandForKbId(junk).has_value());
        QVERIFY(ShotSummarizer::profileKnowledgeForKbId(junk).isEmpty());
    }
    // =====================================================================
    // Bean Base snapshot: advisor remap + DB round-trip (review follow-up)
    // =====================================================================

    // The blob-key -> advisor-key remap IS the advisor contract: a silent
    // rename upstream would empty currentBean.beanBase with no failure
    // anywhere (the builder omits empty values by design). Pin it.
    void beanBaseBlockRemapsBlobKeys()
    {
        DialingBlocks::CurrentBeanBlockInputs in;
        in.beanBrand = "Prodigal Coffee";
        in.beanType = "Milk Blend";
        in.beanBaseJson = QStringLiteral(
            "{\"id\":\"abc-123\",\"tastingNotes\":\"Orange, Honeycomb\","
            "\"degree\":\"Light To Medium-light\",\"beanType\":\"Espresso\","
            "\"origin\":\"Brazil, Colombia\",\"process\":\"Natural\","
            "\"elevation\":\"1100-1200 m\","
            "\"minElevationM\":1100,\"maxElevationM\":1200}");

        const QJsonObject bean = DialingBlocks::buildCurrentBeanBlock(in);
        QVERIFY(bean.contains("beanBase"));
        const QJsonObject bb = bean["beanBase"].toObject();
        QCOMPARE(bb["roasterTastingNotes"].toString(), QString("Orange, Honeycomb"));
        QCOMPARE(bb["roastLevel"].toString(), QString("Light To Medium-light"));
        QCOMPARE(bb["roastedFor"].toString(), QString("Espresso"));
        QCOMPARE(bb["origin"].toString(), QString("Brazil, Colombia"));
        QCOMPARE(bb["process"].toString(), QString("Natural"));
        QCOMPARE(bb["elevation"].toString(), QString("1100-1200 m"));
        QCOMPARE(bb["minElevationM"].toInt(), 1100);
        QCOMPARE(bb["maxElevationM"].toInt(), 1200);
    }

    void beanBaseBlockOmittedWhenEmptyOrGarbage()
    {
        DialingBlocks::CurrentBeanBlockInputs in;
        in.beanBrand = "X";
        QVERIFY(!DialingBlocks::buildCurrentBeanBlock(in).contains("beanBase"));
        in.beanBaseJson = "not json";
        QVERIFY(!DialingBlocks::buildCurrentBeanBlock(in).contains("beanBase"));
        in.beanBaseJson = "{\"tastingNotes\":\"\",\"minElevationM\":0}";
        QVERIFY(!DialingBlocks::buildCurrentBeanBlock(in).contains("beanBase"));
    }

    // add-basket-equipment: this helper produces the currentBean.basket payload
    // for BOTH dialing_get_context (MCP) and the in-app advisor, so it is the
    // single point that proves basket info reaches the AI. Identity + registry-
    // derived specs when present; omitted when absent; identity-only for a custom
    // (off-registry) basket — never fabricated specs.
    void basketBlockEmitsIdentityAndDerivedSpecs()
    {
        DialingBlocks::CurrentBeanBlockInputs in;
        in.beanBrand = "Saka";
        // No basket -> no sub-object.
        QVERIFY(!DialingBlocks::buildCurrentBeanBlock(in).contains("basket"));

        // Registry basket -> identity + derived specs (human-readable strings).
        in.basketBrand = "Weber Workshops";
        in.basketModel = "20g Unibasket";
        const QJsonObject bean = DialingBlocks::buildCurrentBeanBlock(in);
        QVERIFY(bean.contains("basket"));
        const QJsonObject b = bean["basket"].toObject();
        QCOMPARE(b["brand"].toString(), QString("Weber Workshops"));
        QCOMPARE(b["model"].toString(), QString("20g Unibasket"));
        QCOMPARE(b["wallProfile"].toString(), QString("straight"));
        QCOMPARE(b["relativeFlow"].toString(), QString("open"));  // the key cross-basket signal
        QCOMPARE(b["precision"].toBool(), true);
        QVERIFY(b.contains("doseRangeG"));
        QCOMPARE(b["doseRangeG"].toObject()["max"].toDouble(), 21.0);

        // Custom (off-registry) basket -> identity only, derived specs omitted.
        in.basketBrand = "Acme";
        in.basketModel = "Mystery Basket";
        const QJsonObject custom = DialingBlocks::buildCurrentBeanBlock(in)["basket"].toObject();
        QCOMPARE(custom["brand"].toString(), QString("Acme"));
        QVERIFY(!custom.contains("wallProfile"));
        QVERIFY(!custom.contains("relativeFlow"));
        QVERIFY(!custom.contains("doseRangeG"));
    }

    // add-puckprep-equipment: the puckPrep sub-object carries the set flags + the
    // derived distribution rollup the advisor branches channeling guidance on;
    // omitted when the package has no puck prep.
    void puckPrepBlockEmitsFlagsAndDistribution()
    {
        DialingBlocks::CurrentBeanBlockInputs in;
        in.beanBrand = "Saka";
        // No puck prep -> no sub-object.
        QVERIFY(!DialingBlocks::buildCurrentBeanBlock(in).contains("puckPrep"));

        // Canonical flag string -> flags + derived distribution.
        in.puckPrep = "shaker,wdt";
        const QJsonObject bean = DialingBlocks::buildCurrentBeanBlock(in);
        QVERIFY(bean.contains("puckPrep"));
        const QJsonObject p = bean["puckPrep"].toObject();
        QCOMPARE(p["wdt"].toBool(), true);
        QCOMPARE(p["shaker"].toBool(), true);
        QCOMPARE(p["puckScreen"].toBool(), false);
        QCOMPARE(p["paperFilter"].toBool(), false);
        QCOMPARE(p["rdt"].toBool(), false);
        QCOMPARE(p["distribution"].toString(), QString("thorough"));

        // Shaker alone is ALSO thorough — equal weight with WDT, not ranked below it.
        in.puckPrep = "shaker";
        QCOMPARE(DialingBlocks::buildCurrentBeanBlock(in)["puckPrep"].toObject()["distribution"].toString(),
                 QString("thorough"));
        // A non-distribution flag alone → none; RDT alone (anti-static) → light.
        in.puckPrep = "puckScreen";
        QCOMPARE(DialingBlocks::buildCurrentBeanBlock(in)["puckPrep"].toObject()["distribution"].toString(),
                 QString("none"));
        in.puckPrep = "rdt";
        QCOMPARE(DialingBlocks::buildCurrentBeanBlock(in)["puckPrep"].toObject()["distribution"].toString(),
                 QString("light"));
    }

    // beanbase_json is read by POSITIONAL index in loadShotRecordStatic — a
    // future column inserted mid-SELECT would silently shift the read and
    // every consumer would treat the garbage as "unlinked". Round-trip via
    // the production write path (updateShotMetadataStatic) pins the index,
    // the sparse-emit contract, the ""-clears contract, and partial-update
    // preservation in one go.
    void beanBaseJsonDbRoundTripAndClear()
    {
        const QString dbPath = freshDbPath();
        initAndClose(dbPath);

        const QString blob = QStringLiteral(
            "{\"id\":\"abc-123\",\"visualizerCanonicalId\":\"abc-123\","
            "\"roasterName\":\"Prodigal Coffee\",\"origin\":\"Colombia\"}");

        withRawDb(dbPath, "beanbase_roundtrip", [&](QSqlDatabase& db) {
            const qint64 id = insertShot(db, ShotRow{
                .uuid = "uuid-bb", .timestamp = QDateTime::currentSecsSinceEpoch(),
                .profileName = "D-Flow", .profileKbId = "kb-dflow",
                .duration = 28.0, .finalWeight = 36.0, .doseWeight = 18.0,
                .grinderSetting = "5.0", .enjoyment = 0
            });
            QVERIFY(id > 0);

            // Unlinked: empty field, sparse-emit omits the key.
            ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(rec.beanBaseJson, QString());
            QVERIFY(!projectionForShot(db, id).toVariantMap().contains("beanBaseJson"));

            // Link via the production edit path; byte-identical round-trip.
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(
                db, id, {{"beanBaseJson", blob}}));
            rec = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(rec.beanBaseJson, blob);
            QCOMPARE(projectionForShot(db, id).toVariantMap()
                         .value("beanBaseJson").toString(), blob);

            // Partial update of an unrelated field preserves the snapshot.
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(
                db, id, {{"enjoyment", 80}}));
            rec = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(rec.beanBaseJson, blob);

            // "" clears (the unlink-in-edit-mode / MCP {} contract).
            QVERIFY(ShotHistoryStorage::updateShotMetadataStatic(
                db, id, {{"beanBaseJson", QString()}}));
            rec = ShotHistoryStorage::loadShotRecordStatic(db, id);
            QCOMPARE(rec.beanBaseJson, QString());
            QVERIFY(!projectionForShot(db, id).toVariantMap().contains("beanBaseJson"));
        });
    }
    // ==========================================
    // Grind step derivation and the distinct-value getters
    // ==========================================
    //
    // Not migration tests — nothing here touches the schema. They live in this
    // binary because it already owns a DB fixture that COPIES a prebuilt schema
    // template per test instead of re-running the migration chain, and because
    // queryGrinderContext's stepSize coverage is already here. Adding a target
    // for them would have cost a whole compile+link on every build to buy
    // nothing but a filename.

    // Seed `count` settings stepping by 0.25 on one grinder, so deriveGrindStep's
    // smallest-repeated-gap is unambiguously 0.25.
    void seedQuarterStepHistory(const QString& path, const QString& model, int count) {
        withRawDb(path, "seed_" + model + QString::number(count), [&](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (int i = 0; i < count; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-%1-%2").arg(model).arg(i);
                r.timestamp = now - i * 3600;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Niche");
                r.grinderModel = model;
                r.grinderSetting = QString::number(7.5 + 0.25 * i);
                QVERIFY(insertShot(db, r) > 0);
            }
        });
    }

    // ==========================================
    // Grind step derivation and the distinct-value getters.
    //
    // NOT migration tests — nothing below touches the schema. They live here
    // because this file already owns the DB fixtures and links decenza_shotlib,
    // and a separate target for nine tests would cost a build target to buy
    // nothing but a better filename.
    // ==========================================

    // THE regression test for this change. The step derived correctly as 0.25 and
    // was then lost: it lived behind a distinct-cache key, an invalidation cleared
    // that key, and the re-fetch racing the next refresh was discarded in silence.
    // The widget sat on its 1.0 fallback for the rest of the session while the AI
    // payload still reported 0.25 for the same grinder.
    //
    // The trigger was a data change — every shot save wiped the cache. So that is
    // what this drives: derive, write a shot that MOVES the answer, derive again.
    //
    // The write must change the derived step, not just add a row. An earlier
    // version of this test inserted a setting already on the seeded lattice, so
    // SELECT DISTINCT returned byte-identical rows and the second assertion could
    // not fail unless the first did — it was a copy of its own first half. Here
    // the history starts on a 0.5 lattice and the new shot introduces the first
    // repeated 0.25 gap, so a reader that did not see the write answers 0.5.
    void grindStepSurvivesDataChange() {
        QString path = freshDbPath();
        initAndClose(path);

        // 6.0, 6.5, 7.0, 7.5 — smallest repeated gap is 0.5.
        withRawDb(path, "step_seed_half", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (int i = 0; i < 4; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-half-%1").arg(i);
                r.timestamp = now - i * 60;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Niche");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = QString::number(6.0 + 0.5 * i);
                QVERIFY(insertShot(db, r) > 0);
            }
        });

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        QCOMPARE(s.grindStepForGrinder("Zero"), 0.5);

        // 6.25 and 6.75 introduce two 0.25 gaps — the smallest repeated gap moves.
        withRawDb(path, "step_after_write", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const char* settings[] = { "6.25", "6.75" };
            for (int i = 0; i < 2; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-after-write-%1").arg(i);
                r.timestamp = now + 1 + i;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Niche");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = QString::fromLatin1(settings[i]);
                QVERIFY(insertShot(db, r) > 0);
            }
        });
        QCOMPARE(s.grindStepForGrinder("Zero"), 0.25);
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // The getters read the database, so a write is visible to the very next call
    // with no signal in between. Before this change they returned a cached list
    // and the new value appeared only once an async refresh had landed — and for
    // composite keys like this one, often never.
    void distinctSettingsSeeWritesImmediately() {
        QString path = freshDbPath();
        initAndClose(path);
        seedQuarterStepHistory(path, QStringLiteral("Zero"), 6);

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        const qsizetype before = s.getDistinctGrinderSettingsForGrinder("Zero").size();
        QVERIFY(before > 0);

        withRawDb(path, "distinct_after_write", [](QSqlDatabase& db) {
            ShotRow r;
            r.uuid = QStringLiteral("uuid-distinct-new");
            r.timestamp = QDateTime::currentSecsSinceEpoch() + 1;
            r.profileName = QStringLiteral("p");
            r.grinderBrand = QStringLiteral("Niche");
            r.grinderModel = QStringLiteral("Zero");
            r.grinderSetting = QStringLiteral("42.5");   // not on the seeded lattice
            QVERIFY(insertShot(db, r) > 0);
        });

        const QStringList after = s.getDistinctGrinderSettingsForGrinder("Zero");
        QCOMPARE(after.size(), before + 1);
        QVERIFY2(after.contains("42.5"), "a live getter must see a write immediately");
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // One distinct setting defines no step. 0 means "cannot derive" and the caller
    // substitutes its own fallback — it is NOT a step of zero.
    // Seeds a SECOND grinder that does derive, deliberately. 0.0 is this
    // function's universal failure value — not-ready, query-failed and thin
    // history all return it — so asserting only `Solo == 0` would pass just as
    // well against a completely broken query path. Asserting Zero == 0.25 in the
    // same database proves the query works, which is what makes Solo's 0 mean
    // "one sample defines no step".
    void grindStepThinHistoryReturnsZero() {
        QString path = freshDbPath();
        initAndClose(path);
        seedQuarterStepHistory(path, QStringLiteral("Solo"), 1);
        seedQuarterStepHistory(path, QStringLiteral("Zero"), 6);

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        QCOMPARE(s.grindStepForGrinder("Zero"), 0.25);   // the query works
        QCOMPARE(s.grindStepForGrinder("Solo"), 0.0);    // ...and one sample still yields 0
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // Same grinder, differently typed. NOT a regression test — main folded here
    // too (both its cached getter and its derivation used LOWER(TRIM())). This is
    // a forward guard on the now-shared grinderModelMatchSql(): an exact compare
    // would read a differently-cased model back as a grinder with no history,
    // which is invisible because empty is indistinguishable from new.
    void grindStepFoldsModelCaseAndWhitespace() {
        QString path = freshDbPath();
        initAndClose(path);
        seedQuarterStepHistory(path, QStringLiteral("Zero"), 28);

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        QCOMPARE(s.grindStepForGrinder("  zero  "), 0.25);
        QCOMPARE(s.grindStepForGrinder("ZERO"), 0.25);
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // An empty model means "no grinder selected" and must derive from the full
    // cross-grinder history — the ShotServer /beans form depends on it, since a new
    // bag has no equipment chosen yet.
    void grindStepEmptyModelUsesFullHistory() {
        QString path = freshDbPath();
        initAndClose(path);
        seedQuarterStepHistory(path, QStringLiteral("Zero"), 28);

        // Shots with NO equipment row at all, stepping by 0.1. Only a query that
        // drops the equipment join can see these, so without them the test cannot
        // tell an all-grinders pass from a per-grinder one.
        withRawDb(path, "no_equipment", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (int i = 0; i < 6; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-bare-%1").arg(i);
                r.timestamp = now - i * 60;
                r.profileName = QStringLiteral("p");
                // No grinder brand/model/burrs => insertShot leaves equipment_id NULL.
                r.grinderSetting = QString::number(2.0 + 0.1 * i);
                QVERIFY(insertShot(db, r) > 0);
            }
        });

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        // 0.1 is the smallest repeated gap across the full history, reachable only
        // if equipment-less shots are included.
        QCOMPARE(s.grindStepForGrinder(""), 0.1);
        // The per-grinder answer is unchanged.
        QCOMPARE(s.grindStepForGrinder("Zero"), 0.25);
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // grindRpmStepForGrinder had NO coverage at all while this change rewrote both
    // it and its helper. It returns the same 0.0 sentinel for "no grinder", "not
    // ready" and "query failed", and the caller silently substitutes a 50 RPM
    // default — the same shape as the bug this PR fixes, on the RPM wheel.
    void grindRpmStepDerivesFromHistory() {
        QString path = freshDbPath();
        initAndClose(path);

        // 800, 900, 1000, 1100 — smallest repeated gap is 100.
        withRawDb(path, "rpm_seed", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (int i = 0; i < 4; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-rpm-%1").arg(i);
                r.timestamp = now - i * 60;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Mahlkonig");
                r.grinderModel = QStringLiteral("E80");
                r.grinderSetting = QString::number(3.0 + i);
                r.rpm = 800 + 100 * i;
                QVERIFY(insertShot(db, r) > 0);
            }
        });

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        QCOMPARE(s.grindRpmStepForGrinder("E80"), 100.0);
        // Folded like its settings twin — both go through grinderModelMatchSql().
        QCOMPARE(s.grindRpmStepForGrinder("  e80  "), 100.0);
        // An empty model has no meaningful RPM history to pool: the documented
        // precondition of grinderWideRpmStep, guarded at the call site.
        QCOMPARE(s.grindRpmStepForGrinder(""), 0.0);
        // A grinder with no recorded RPMs at all.
        QCOMPARE(s.grindRpmStepForGrinder("Zero"), 0.0);
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // The SQL orders lexicographically, so "10" sorts before "9". sortGrinderSettings()
    // re-sorts numerically, and GrindRowSource pushes this list onto the wheel IN
    // LIST ORDER — so the ordering is directly user-visible. Nothing asserted it:
    // deleting the sortGrinderSettings() call left the whole suite green.
    void distinctSettingsAreSortedNumerically() {
        QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, "sort_seed", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const char* settings[] = { "9", "10", "11", "2" };
            for (int i = 0; i < 4; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-sort-%1").arg(i);
                r.timestamp = now - i * 60;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Niche");
                r.grinderModel = QStringLiteral("Zero");
                r.grinderSetting = QString::fromLatin1(settings[i]);
                QVERIFY(insertShot(db, r) > 0);
            }
        });

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        const QStringList got = s.getDistinctGrinderSettingsForGrinder("Zero");
        QCOMPARE(got, QStringList({ "2", "9", "10", "11" }));
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

    // getDistinctGrinderBurrsForModel binds brand and model POSITIONALLY. A
    // transposition returns an empty list, which is indistinguishable from "no
    // burrs recorded" — silent, and the only getter with two positional binds.
    void distinctBurrsBindBrandAndModelInOrder() {
        QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, "burrs_seed", [](QSqlDatabase& db) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            // Two models under ONE brand, so a swapped bind cannot accidentally match.
            const char* models[] = { "Zero", "Duo" };
            const char* burrs[]  = { "Steel", "Ceramic" };
            for (int i = 0; i < 2; ++i) {
                ShotRow r;
                r.uuid = QStringLiteral("uuid-burrs-%1").arg(i);
                r.timestamp = now - i * 60;
                r.profileName = QStringLiteral("p");
                r.grinderBrand = QStringLiteral("Niche");
                r.grinderModel = QString::fromLatin1(models[i]);
                r.grinderBurrs = QString::fromLatin1(burrs[i]);
                r.grinderSetting = QStringLiteral("5");
                QVERIFY(insertShot(db, r) > 0);
            }
        });

        ShotHistoryStorage s;
        QVERIFY(s.initialize(path));
        QCOMPARE(s.getDistinctGrinderBurrsForModel("Niche", "Zero"), QStringList({ "Steel" }));
        QCOMPARE(s.getDistinctGrinderBurrsForModel("Niche", "Duo"),  QStringList({ "Ceramic" }));
        s.close();
        QTRY_VERIFY(s.isDbWorkIdle());
    }

};

QTEST_GUILESS_MAIN(TstDialingBlocks)
#include "tst_dialing_blocks.moc"
