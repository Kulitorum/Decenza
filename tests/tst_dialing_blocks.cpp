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
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>

#include "history/shothistorystorage.h"
#include "history/shotprojection.h"
#include "ai/dialing_blocks.h"
#include "ai/shotsummarizer.h"  // initTestCase pins the missing-resource qWarning

namespace {

// One shot's input fields. Keep this near-identical to ShotSaveData so
// the parameter list is grep-able from the production save path.
struct ShotRow {
    QString uuid;
    qint64 timestamp = 0;
    QString profileName;
    QString profileKbId;
    QString beverageType = QStringLiteral("espresso");
    double duration = 30.0;
    double finalWeight = 36.0;
    double doseWeight = 18.0;
    QString beanBrand;
    QString beanType;
    QString roastLevel;
    QString grinderBrand;
    QString grinderModel;
    QString grinderBurrs;
    QString grinderSetting;
    int enjoyment = 0;
    QString espressoNotes;
};

// Run work with a scoped raw SQLite connection on `path`. Same pattern
// tst_dbmigration uses; the connection is removed deterministically when
// `work` returns so Qt does not warn about open connections.
template<typename Work>
void withRawDb(const QString& path, const QString& connName, Work&& work)
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(path);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery (db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        work(db);
    }
    QSqlDatabase::removeDatabase(connName);
}

qint64 insertShot(QSqlDatabase& db, const ShotRow& r)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_level,
            grinder_brand, grinder_model, grinder_burrs, grinder_setting,
            enjoyment, espresso_notes, profile_kb_id
        ) VALUES (
            :uuid, :timestamp, :profile_name, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_level,
            :grinder_brand, :grinder_model, :grinder_burrs, :grinder_setting,
            :enjoyment, :espresso_notes, :profile_kb_id
        )
    )"));
    q.bindValue(":uuid", r.uuid);
    q.bindValue(":timestamp", r.timestamp);
    q.bindValue(":profile_name", r.profileName);
    q.bindValue(":beverage_type", r.beverageType);
    q.bindValue(":duration", r.duration);
    q.bindValue(":final_weight", r.finalWeight);
    q.bindValue(":dose_weight", r.doseWeight);
    q.bindValue(":bean_brand", r.beanBrand);
    q.bindValue(":bean_type", r.beanType);
    q.bindValue(":roast_level", r.roastLevel);
    q.bindValue(":grinder_brand", r.grinderBrand);
    q.bindValue(":grinder_model", r.grinderModel);
    q.bindValue(":grinder_burrs", r.grinderBurrs);
    q.bindValue(":grinder_setting", r.grinderSetting);
    q.bindValue(":enjoyment", r.enjoyment);
    q.bindValue(":espresso_notes", r.espressoNotes);
    q.bindValue(":profile_kb_id", r.profileKbId.isEmpty() ? QVariant() : r.profileKbId);
    if (!q.exec ()) {
        qWarning() << "insertShot failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

ShotProjection projectionForShot(QSqlDatabase& db, qint64 shotId)
{
    return ShotHistoryStorage::convertShotRecord(
        ShotHistoryStorage::loadShotRecordStatic(db, shotId));
}

constexpr qint64 kSecPerDay = 24 * 3600;

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

    // Stand up a fresh DB at `path` with the full ShotHistoryStorage
    // schema, then close so callers can attach a raw connection.
    // initialize() launches a bg-thread distinct-cache prewarm; we drain
    // it the same way tst_dbmigration does so the connection cleanup
    // does not race the worker thread.
    void initAndClose(const QString& path)
    {
        ShotHistoryStorage storage;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("connection.*still in use")));
        QVERIFY(storage.initialize(path));
        storage.close();
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());

        // The test binary doesn't link the :/ai/profile_knowledge.md
        // resource, so the first call to ShotSummarizer's profile-knowledge
        // loader emits a single qWarning. After commit fixing the
        // s_knowledgeLoaded latch, the warning fires once per process. Pin
        // it here so individual tests don't have to ignoreMessage it
        // themselves — this is fixture noise, not a behavior to test.
        QTest::ignoreMessage(QtWarningMsg,
            "ShotSummarizer: Failed to load profile knowledge resource");
        ShotSummarizer::computeProfileKbId(QStringLiteral("dummy"), QStringLiteral("advanced"));
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
                db, QStringLiteral("kb-80s"), /*resolvedShotId=*/-1, /*historyLimit=*/10);

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
    // dialInSessionsBlock — empty when no rows.
    // -------------------------------------------------------------------
    void dialInSessionsBlock_emptyWhenNoRows()
    {
        const QString path = freshDbPath();
        initAndClose(path);
        withRawDb(path, QStringLiteral("dial_empty"), [&](QSqlDatabase& db) {
            const QJsonArray sessions = DialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-no-rows"), -1, 10);
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
                db, QStringLiteral("Zero"), QStringLiteral("espresso"),
                QStringLiteral("Northbound"));

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
                db, QStringLiteral("Zero"), QStringLiteral("espresso"),
                QStringLiteral("Northbound"));
            QCOMPARE(ctx.value(QStringLiteral("settingsObserved")).toArray().size(), 3);
            QVERIFY2(!ctx.contains(QStringLiteral("allBeansSettings")),
                     "rich bean-scoped result must not trigger cross-bean fallback");
            QCOMPARE(ctx.value(QStringLiteral("minSetting")).toDouble(), 4.0);
            QCOMPARE(ctx.value(QStringLiteral("maxSetting")).toDouble(), 4.4);
            // Smallest step is 0.2 (2.0e-1). Allow tiny FP slack.
            const double step = ctx.value(QStringLiteral("smallestStep")).toDouble();
            QVERIFY2(qAbs(step - 0.2) < 0.0001,
                     qPrintable(QString("expected smallestStep ~0.2, got %1").arg(step)));
        });
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
                    db, kbId, shotId, kHistoryLimit);
                QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                    db, kbId, shotId, sp);
                QJsonObject grinder = DialingBlocks::buildGrinderContextBlock(
                    db, sp.grinderModel, sp.beverageType, sp.beanBrand);
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
                    db, kbId, excludeId, kHistoryLimit);
                QJsonObject best = DialingBlocks::buildBestRecentShotBlock(
                    db, kbId, excludeId, sp);
                QJsonObject grinder = DialingBlocks::buildGrinderContextBlock(
                    db, sp.grinderModel, sp.beverageType, sp.beanBrand);
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
            QCOMPARE(ur.value("outcomeRating").toInt(), 75);
            QCOMPARE(ur.value("outcomeNotes").toString(), QStringLiteral("balanced and sweet"));
            const QJsonObject rng = ur.value("outcomeInPredictedRange").toObject();
            QVERIFY(rng.value("duration").toBool());  // 35s in [32,38]
            // avg flow = 42/35 = 1.2 ml/s, in [1.0, 1.5]
            QVERIFY(rng.value("flow").toBool());
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
            QVERIFY2(!ur.contains("outcomeRating"),
                     "outcomeRating must be omitted when the actual shot is unrated");
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
};

QTEST_GUILESS_MAIN(TstDialingBlocks)
#include "tst_dialing_blocks.moc"
