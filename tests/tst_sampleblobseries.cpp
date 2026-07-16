#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "models/shotdatamodel.h"

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
};

QTEST_MAIN(tst_SampleBlobSeries)
#include "tst_sampleblobseries.moc"
