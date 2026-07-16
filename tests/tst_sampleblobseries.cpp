#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

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

private:
    // convertShotRecord() early-returns an empty projection for id == 0, so a
    // record built by hand needs one or every assertion below passes vacuously.
    static void giveIdentity(ShotRecord& record) {
        record.summary.id = 1;
    }

    // Feed the model a few samples with distinct basket/mix goals.
    static void populate(ShotDataModel& model) {
        for (int i = 0; i < 5; i++) {
            double t = i * 0.2;
            model.addSample(t, 9.0, 2.0, 92.0, 90.0, 9.0, 0.0, /*tempGoal*/ 92.0,
                            /*tempMixGoal*/ 94.0);
        }
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
