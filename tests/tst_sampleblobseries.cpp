#include <QtTest>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>

#include "core/dbutils.h"
#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "history/shotprojection.h"
#include "models/shotdatamodel.h"
#include "network/visualizeruploader.h"

// Guards the mix temperature goal's trip through the sample blob.
//
// Shot series are not table columns — they live in a qCompress'd JSON object in
// shot_samples.data_blob, whose series keys are read back optionally. That is
// what makes adding a series migration-free, and what makes the "absent key"
// case real: every shot recorded before this series existed has a blob without
// it, and must load with an EMPTY vector rather than a defaulted or zeroed one.
class tst_SampleBlobSeries : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

private:
    // convertShotRecord() early-returns an empty projection for id == 0, so a
    // record built by hand needs one or every assertion below passes vacuously.
    static void giveIdentity(ShotRecord& record) {
        record.summary.id = 1;
    }

    // initialize() spawns a distinct-cache thread; close() + drain before the
    // storage destructs or it can crash (see tst_coffeebags::initAndClose,
    // tst_shotimportdedupe::drain).
    static void drain() {
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

    // Feed the model a few samples with distinct basket/mix goals. Mirrors
    // production's saveShot(), which always runs computeConductanceDerivative()
    // before compressSampleData() — skipping it here would leave the blob's
    // conductanceDerivative empty, which decompressSampleData's unconditional
    // recompute would then (correctly) flag as a correction on every test that
    // reuses this fixture.
    static void populate(ShotDataModel& model) {
        for (int i = 0; i < 5; i++) {
            double t = i * 0.2;
            model.addSample(t, 9.0, 2.0, 92.0, 90.0, 9.0, 0.0, /*tempGoal*/ 92.0,
                            /*tempMixGoal*/ 94.0);
        }
        model.computeConductanceDerivative();
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void mixGoalRoundTripsThroughBlob() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);

        QByteArray blob = storage.compressSampleData(&model);
        ShotRecord record;
        ShotHistoryStorage::decompressSampleData(blob, &record);

        QCOMPARE(record.temperatureMixGoal.size(), model.temperatureMixGoalData().size());
        QVERIFY(!record.temperatureMixGoal.isEmpty());
        QVERIFY(qAbs(record.temperatureMixGoal.first().y() - 94.0) < 0.01);
        // The basket goal must survive alongside it, not be displaced by it.
        QVERIFY(qAbs(record.temperatureGoal.first().y() - 92.0) < 0.01);
    }

    // A shot saved before this series existed: same blob shape, minus the key.
    void blobWithoutMixGoalKeyLoadsEmpty() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);

        QByteArray blob = storage.compressSampleData(&model);
        QJsonObject root = QJsonDocument::fromJson(qUncompress(blob)).object();
        root.remove("temperatureMixGoal");
        QByteArray legacyBlob = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9);

        ShotRecord record;
        ShotHistoryStorage::decompressSampleData(legacyBlob, &record);

        // Empty means "not recorded" — never a zero-filled series, which would
        // upload a 0 °C goal line and draw one on the graph.
        QVERIFY(record.temperatureMixGoal.isEmpty());
        // Every other series must still load.
        QVERIFY(!record.temperatureGoal.isEmpty());
        QVERIFY(!record.pressure.isEmpty());
    }

    // recompute-shot-curves-on-load (#1822): resistance was briefly computed as
    // P/F² instead of P/F (44bd47b6..a6a58c21), so shots recorded in that window
    // have a stored resistance curve identical to darcyResistance forever. Pin
    // that decompressSampleData recomputes from pressure/flow instead of
    // trusting the stored formula, and reports a corrected blob when it differs.
    void staleResistanceFormulaGetsCorrectedOnLoad() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);  // constant pressure 9.0, flow 2.0 -> correct P/F resistance = 4.5

        QByteArray blob = storage.compressSampleData(&model);
        QJsonObject root = QJsonDocument::fromJson(qUncompress(blob)).object();

        // Overwrite the stored resistance with what the P/F² bug would have
        // produced (9.0 / 2.0² = 2.25), same shape ({t: [...], v: [...]}) as
        // every other series in the blob.
        QJsonObject staleResistance;
        staleResistance["t"] = root["pressure"].toObject()["t"];
        QJsonArray staleValues;
        const qsizetype n = root["pressure"].toObject()["t"].toArray().size();
        for (qsizetype i = 0; i < n; i++) staleValues.append(9.0 / (2.0 * 2.0));
        staleResistance["v"] = staleValues;
        root["resistance"] = staleResistance;
        QByteArray staleBlob = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9);

        ShotRecord record;
        QByteArray corrected;
        ShotHistoryStorage::decompressSampleData(staleBlob, &record, &corrected);

        QVERIFY(!record.resistance.isEmpty());
        QVERIFY2(qAbs(record.resistance.first().y() - 4.5) < 0.01,
                 "resistance must be recomputed as P/F (4.5), not trusted as the stale P/F² value (2.25)");
        QVERIFY2(!corrected.isEmpty(), "a stale stored curve must produce a corrected blob to persist");
    }

    // The counterpart to the above: a shot whose stored curves already match
    // the current formula must NOT report a correction on every load — that
    // would rewrite shot_samples.data_blob on every single view.
    void alreadyCorrectBlobProducesNoCorrection() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);

        QByteArray blob = storage.compressSampleData(&model);
        ShotRecord record;
        QByteArray corrected;
        ShotHistoryStorage::decompressSampleData(blob, &record, &corrected);

        QVERIFY2(corrected.isEmpty(), "an already-correct blob must not be flagged for rewrite");
    }

    // The DB-backed counterpart: loadShotRecordStatic must persist the
    // corrected blob back to shot_samples on the same connection (mirroring
    // the existing badge-persist behavior), exactly once — loading the same
    // shot again afterward must not keep rewriting an already-corrected row.
    void staleResistancePersistsCorrectedBlobOnceViaLoadShotRecordStatic() {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("resistance_selfheal.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        ShotRecord record;
        record.summary.uuid = QStringLiteral("resistance-selfheal-uuid");
        record.summary.timestamp = 1755000000;
        record.summary.profileName = QStringLiteral("Profile R");
        record.summary.beverageType = QStringLiteral("espresso");
        // Correct P/F resistance for constant pressure=9.0, flow=2.0 is 4.5;
        // store the P/F² value (2.25) instead, simulating the formula bug
        // that shipped between 44bd47b6 and a6a58c21.
        for (int i = 0; i < 5; i++) {
            double t = i * 0.2;
            record.pressure.append(QPointF(t, 9.0));
            record.flow.append(QPointF(t, 2.0));
            record.resistance.append(QPointF(t, 9.0 / (2.0 * 2.0)));
        }

        const qint64 shotId = storage.importShotRecord(record, false);
        QVERIFY2(shotId > 0, "import should insert");

        auto readBlob = [&]() {
            QByteArray blob;
            withTempDb(path, "shs_test_resistance_read", [&](QSqlDatabase& db) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT data_blob FROM shot_samples WHERE shot_id = ?"));
                q.bindValue(0, shotId);
                if (q.exec() && q.next()) blob = q.value(0).toByteArray();
            });
            return blob;
        };

        const QByteArray blobBefore = readBlob();
        QVERIFY(!blobBefore.isEmpty());

        withTempDb(path, "shs_test_resistance_load", [&](QSqlDatabase& db) {
            ShotRecord loaded = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            QVERIFY(!loaded.resistance.isEmpty());
            QVERIFY2(qAbs(loaded.resistance.first().y() - 4.5) < 0.01,
                     "loaded resistance must be the corrected P/F value, not the stale stored P/F^2");
        });

        const QByteArray blobAfter = readBlob();
        QVERIFY2(blobBefore != blobAfter, "a stale stored curve must trigger a persisted correction");

        // Loading the now-corrected shot again must not keep rewriting it.
        withTempDb(path, "shs_test_resistance_reload", [&](QSqlDatabase& db) {
            ShotHistoryStorage::loadShotRecordStatic(db, shotId);
        });
        QCOMPARE(readBlob(), blobAfter);

        storage.close();
        drain();
    }

    // The seam between the blob and the upload/chart surfaces. Without this,
    // dropping the one convertShotRecord() line leaves every other test green
    // while no stored shot ever uploads mix_goal and no detail page plots it —
    // and re-upload from history is the path that runs forever, unlike the
    // single live upload.
    void mixGoalSurvivesRecordToProjectionToJson() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);

        ShotRecord record;
        giveIdentity(record);
        ShotHistoryStorage::decompressSampleData(storage.compressSampleData(&model), &record);
        ShotProjection p = ShotHistoryStorage::convertShotRecord(record);

        QVERIFY(!p.temperatureMixGoal.isEmpty());

        QJsonObject temp = QJsonDocument::fromJson(VisualizerUploader::buildHistoryShotJson(p))
                               .object()["temperature"].toObject();
        QVERIFY(temp.contains("mix_goal"));
    }

    // Same seam for a shot recorded before the series existed: absence has to
    // survive the whole chain too, or old shots upload a 0 °C goal line.
    void legacyShotStaysAbsentThroughProjectionToJson() {
        ShotHistoryStorage storage;
        ShotDataModel model;
        populate(model);

        QJsonObject root = QJsonDocument::fromJson(qUncompress(storage.compressSampleData(&model))).object();
        root.remove("temperatureMixGoal");

        ShotRecord record;
        giveIdentity(record);
        ShotHistoryStorage::decompressSampleData(
            qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9), &record);
        ShotProjection p = ShotHistoryStorage::convertShotRecord(record);

        QVERIFY(p.temperatureMixGoal.isEmpty());
        // The conversion ran for real — otherwise the assertion above would
        // hold for an empty projection and prove nothing.
        QVERIFY(!p.temperatureGoal.isEmpty());

        QJsonObject temp = QJsonDocument::fromJson(VisualizerUploader::buildHistoryShotJson(p))
                               .object()["temperature"].toObject();
        QVERIFY(!temp.contains("mix_goal"));
    }
};

QTEST_MAIN(tst_SampleBlobSeries)
#include "tst_sampleblobseries.moc"
