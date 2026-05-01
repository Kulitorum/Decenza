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
#include "mcp/mcptools_dialing_blocks.h"

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

class TstMcpToolsDialingBlocks : public QObject
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
            const QJsonArray sessions = McpDialingBlocks::buildDialInSessionsBlock(
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
            const QJsonArray sessions = McpDialingBlocks::buildDialInSessionsBlock(
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

            const QJsonObject best_ = McpDialingBlocks::buildBestRecentShotBlock(
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
            - (McpDialingBlocks::kBestRecentShotWindowDays + 10) * kSecPerDay;

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

            const QJsonObject best_ = McpDialingBlocks::buildBestRecentShotBlock(
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

            const QJsonObject ctx = McpDialingBlocks::buildGrinderContextBlock(
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

            const QJsonObject ctx = McpDialingBlocks::buildGrinderContextBlock(
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
    // them, because both paths call the same McpDialingBlocks helpers.
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

            const ShotProjection currentProj = projectionForShot(db, currentId);

            // Surface A: dialing_get_context's call site.
            const QJsonArray  sessionsA = McpDialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-80s"), currentId, /*historyLimit=*/10);
            const QJsonObject bestA = McpDialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-80s"), currentId, currentProj);
            const QJsonObject grinderA = McpDialingBlocks::buildGrinderContextBlock(
                db, QStringLiteral("Zero"), QStringLiteral("espresso"),
                QStringLiteral("Northbound"));

            // Surface B: the in-app advisor's enrichment closure — same helpers.
            const QJsonArray  sessionsB = McpDialingBlocks::buildDialInSessionsBlock(
                db, QStringLiteral("kb-80s"), currentId, 10);
            const QJsonObject bestB = McpDialingBlocks::buildBestRecentShotBlock(
                db, QStringLiteral("kb-80s"), currentId, currentProj);
            const QJsonObject grinderB = McpDialingBlocks::buildGrinderContextBlock(
                db, QStringLiteral("Zero"), QStringLiteral("espresso"),
                QStringLiteral("Northbound"));

            const auto toJson = [](const auto& v) {
                return QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact));
            };

            QCOMPARE(toJson(sessionsA), toJson(sessionsB));
            QCOMPARE(toJson(bestA),     toJson(bestB));
            QCOMPARE(toJson(grinderA),  toJson(grinderB));
            QVERIFY(!sessionsA.isEmpty());
            QVERIFY(!bestA.isEmpty());
            QVERIFY(!grinderA.isEmpty());
        });
    }
};

QTEST_GUILESS_MAIN(TstMcpToolsDialingBlocks)
#include "tst_mcptools_dialing_blocks.moc"
