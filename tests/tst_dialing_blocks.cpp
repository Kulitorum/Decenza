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
#include <QDir>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>

#include "history/shothistorystorage.h"
#include "history/shotprojection.h"
#include "history/equipmentstorage.h"
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
    // Issue #1158: profile recipe snapshot + SAW target. Empty/0 by
    // default so existing fixtures are unaffected (pourControl /
    // targetWeightG simply stay absent, exactly as before this PR).
    QString profileJson;
    double targetWeight = 0.0;  // → shots.yield_override
    // #1164 finding #3: per-shot temperature override → shots
    // .temperature_override. 0 by default so existing fixtures are
    // unaffected (the field stays absent / hoist-neutral, as before).
    double temperatureOverride = 0.0;
    // #1161: why the shot ended → shots.stopped_by. "" by default so
    // existing fixtures are unaffected (sparse-omitted from the blocks).
    QString stoppedBy;
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
    // Grinder identity is no longer a per-shot column (migration 23) — it
    // resolves through equipment_id to a package's grinder item. Mirror the
    // production save path: find-or-create a package for this row's grinder
    // identity and link the shot to it. The per-shot grind setting stays on the
    // row. An empty identity leaves equipment_id NULL.
    qint64 equipmentId = 0;
    if (!(r.grinderBrand.isEmpty() && r.grinderModel.isEmpty() && r.grinderBurrs.isEmpty())) {
        equipmentId = EquipmentStorage::findPackageByGrinderIdentityStatic(
            db, r.grinderBrand, r.grinderModel, r.grinderBurrs);
        if (equipmentId <= 0) {
            EquipmentPackage pkg;
            equipmentId = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, r.grinderBrand, r.grinderModel, r.grinderBurrs);
        }
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_level,
            grinder_setting, equipment_id,
            enjoyment, espresso_notes, profile_kb_id,
            profile_json, yield_override, temperature_override, stopped_by
        ) VALUES (
            :uuid, :timestamp, :profile_name, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_level,
            :grinder_setting, :equipment_id,
            :enjoyment, :espresso_notes, :profile_kb_id,
            :profile_json, :yield_override, :temperature_override, :stopped_by
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
    q.bindValue(":grinder_setting", r.grinderSetting);
    q.bindValue(":equipment_id", equipmentId > 0 ? QVariant(equipmentId) : QVariant());
    q.bindValue(":enjoyment", r.enjoyment);
    q.bindValue(":espresso_notes", r.espressoNotes);
    q.bindValue(":profile_kb_id", r.profileKbId.isEmpty() ? QVariant() : r.profileKbId);
    q.bindValue(":profile_json", r.profileJson);
    q.bindValue(":yield_override", r.targetWeight);
    q.bindValue(":temperature_override", r.temperatureOverride);
    q.bindValue(":stopped_by", r.stoppedBy);
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
        // Pre-warm the profile-knowledge singleton so the one-time DB load
        // happens here (in initTestCase) rather than inside individual tests.
        // ai.qrc is linked by this binary, so the load succeeds silently.
        ShotSummarizer::computeProfileKbId(QStringLiteral("dummy"), QStringLiteral("advanced"));
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
            QCOMPARE(ctx.value(QStringLiteral("observedMinSetting")).toDouble(), 4.0);
            QCOMPARE(ctx.value(QStringLiteral("observedMaxSetting")).toDouble(), 4.4);
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
            QCOMPARE(ur.value("outcomeRating0to100").toInt(), 75);
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
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10);
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
                db, QStringLiteral("kb-dfq"), -1, 10);
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
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10);
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
                db, QStringLiteral("kb-dfq"), -1, 10);
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
                db, QStringLiteral("kb-dfq"), /*resolvedShotId=*/-1, /*historyLimit=*/10);
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
        // #1160 review: LRv2 and LRv3 must resolve to the SAME section via
        // real Also-matches keys, not a fragile length>=4 fuzzy-substring
        // fallback on the bare "lrv3" title-split token.
        QCOMPARE(ShotSummarizer::canonicalNameForKbId(kbLrv3),
                 ShotSummarizer::canonicalNameForKbId(kbLrv2));
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
        using ExpertBand = ShotAnalysis::ExpertBand;

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
        using ExpertBand = ShotAnalysis::ExpertBand;

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
    void expertBand_londinium_resolvesAndDoesNotCatchDamianLR()
    {
        using ExpertBand = ShotAnalysis::ExpertBand;

        const auto band = ShotSummarizer::expertBandForKbId(
            ShotSummarizer::computeProfileKbId(QStringLiteral("Londinium"),
                                               QStringLiteral("advanced")));
        QVERIFY2(band.has_value(),
                 "Londinium must resolve to the decent-guide band");
        QCOMPARE(band->axis, ExpertBand::Axis::PressurePeak);
        QCOMPARE(*band->lo, 8.0);
        QCOMPARE(*band->hi, 9.0);
        QCOMPARE(band->src,
                 QStringLiteral("https://decentespresso.com/first_decent_espresso"));
        QCOMPARE(band->confidence, QStringLiteral("medium"));

        // Damian's LRv2/LRv3 are D-Flow-family variants, NOT the standalone
        // Londinium — they must not pick up this band.
        for (const QString& lr : { QStringLiteral("Damian's LRv2"),
                                    QStringLiteral("Damian's LRv3") }) {
            const auto v = ShotSummarizer::expertBandForKbId(
                ShotSummarizer::computeProfileKbId(lr, QStringLiteral("dflow")));
            QVERIFY2(!v.has_value() ||
                     v->src != QStringLiteral("https://decentespresso.com/first_decent_espresso"),
                     qPrintable(lr + " must NOT resolve to the standalone "
                                     "Londinium band"));
        }
    }

    // Phase C — Adaptive v2: Decent authored the profile, so decent-guide
    // is the author authority. Band is 6–9 (contains the by-design
    // adaptive envelope; observational "could be better" outside it).
    // `## Gagné Adaptive` is a separate canonical section and must not
    // pick up this band.
    void expertBand_adaptiveV2_resolvesAndDoesNotCatchGagne()
    {
        using ExpertBand = ShotAnalysis::ExpertBand;
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
        using ExpertBand = ShotAnalysis::ExpertBand;
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
        using ExpertBand = ShotAnalysis::ExpertBand;
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
        // 43 entries. Parsed (not a raw "id": substring count, which a future
        // prose/rationale string containing `"id":` would falsely break). If a
        // KB entry is intentionally added/removed, update this count AND
        // re-verify #1160 resolution.
        const QJsonArray kbProfiles =
            QJsonDocument::fromJson(kb.toUtf8()).object()
                .value(QStringLiteral("profiles")).toArray();
        QCOMPARE(kbProfiles.size(), 43);

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
        QVERIFY2(!idLondinium.isEmpty() && !idLRv2.isEmpty()
                     && idLRv2 != idLondinium,
                 "Damian's LRv2 must NOT collapse into the standalone "
                 "Londinium entry");
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

            // editorType is derived EXACTLY as Profile::editorType() does:
            // strip a single leading '*' (unsaved-modified marker) BEFORE the
            // prefix-test. Omitting the strip would false-green a
            // "*D-Flow / x" title (resolves via editor-default in the app but
            // not here). This mirrors the production call site.
            QString t = title;
            if (t.startsWith(QLatin1Char('*'))) t = t.mid(1);
            QString editor;
            if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
                editor = QStringLiteral("dflow");
            else if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
                editor = QStringLiteral("aflow");

            if (ShotSummarizer::computeProfileKbId(title, editor).isEmpty())
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
};

QTEST_GUILESS_MAIN(TstDialingBlocks)
#include "tst_dialing_blocks.moc"
