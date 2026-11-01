#include <QtTest>

#include <QCoreApplication>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <utility>

#include "../src/remote/comparisonservice.h"
#include "../src/remote/comparisonservice_qml.h"
#include "../src/remote/comparisonservicesource.h"
#include "rep_comparisonservice_source.h"
#include "../src/models/shotcomparisonmodel.h"
#include "../src/history/shothistorystorage.h"

class TestComparisonSource final : public ComparisonStateSimpleSource
{
    Q_OBJECT
public:
    struct Request {
        QString id;
        QVariantList shotIds;
        qint64 baseShotId = 0;
        int windowStart = 0;
    };
    QList<Request> requests;

    void query(QString requestId, QVariantList shotIds, qint64 baseShotId, int windowStart) override
    {
        requests.append({std::move(requestId), std::move(shotIds), baseShotId, windowStart});
    }
    void answer(int index, int shotCount, const QVariantMap& values = {})
    {
        const Request request = requests.at(index);
        QVariantMap snapshot{
            {QStringLiteral("selectedShotIds"), request.shotIds},
            {QStringLiteral("shotCount"), shotCount},
            {QStringLiteral("maxTime"), 12.0},
            {QStringLiteral("baseShotId"), request.baseShotId},
            {QStringLiteral("comparison"), QVariantMap{{QStringLiteral("same"), true}}},
            {QStringLiteral("windowStart"), request.windowStart},
            {QStringLiteral("totalShots"), request.shotIds.size()},
            {QStringLiteral("otherWindowSize"), 2},
            {QStringLiteral("canShiftLeft"), request.windowStart > 0},
            {QStringLiteral("canShiftRight"), false},
            {QStringLiteral("shotInfo"), QVariantList{}},
            {QStringLiteral("phaseMarkers"), QVariantList{}}
        };
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            snapshot.insert(it.key(), it.value());
        emit snapshotReady(request.id, snapshot, {});
    }
};

class ComparisonServiceTest final : public QObject
{
    Q_OBJECT
    struct Fixture {
        TestComparisonSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engineA;
        QQmlEngine engineB;
        ComparisonService* serviceA = nullptr;
        ComparisonService* serviceB = nullptr;

        Fixture()
        {
            if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0")))) return;
            if (!host.enableRemoting<ComparisonStateSourceAPI>(&source)) return;
            if (!client.connectToNode(host.hostUrl())) return;
            ComparisonService::setPublishedNode(&client);
            serviceA = ComparisonServiceForeign::create(&engineA, &engineA);
            serviceB = ComparisonServiceForeign::create(&engineB, &engineB);
        }
        ~Fixture() { ComparisonService::setPublishedNode(nullptr); }
        bool valid() const { return serviceA && serviceB && !host.hostUrl().isEmpty(); }
    };

private slots:
    void init() { QTest::failOnWarning(); }
    void requestCorrelationDiscardsSupersededSnapshots();
    void facadeSelectionsAreIndependentAcrossEngines();
    void disconnectClearsDataAndReconnectRequeriesWithoutReplay();
    void sourceSnapshotPreservesOneSeriesPerDisplayedShot();
    void inspectorReadoutPreservesChannelListsAcrossTheRemoteSnapshot();
    void restoredSelectionQueriesOnlyItsFinalBaseAndWindow();
    void shotInfoKeepsRecordedValuesAcrossPagingAndRebase();
};

void ComparisonServiceTest::shotInfoKeepsRecordedValuesAcrossPagingAndRebase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("shots.db"));
    ShotHistoryStorage storage;
    QVERIFY(storage.initialize(path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("comparison-info-seed"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO shots (id, uuid, timestamp, profile_name, duration_seconds, dose_weight, grinder_setting, temperature_override) VALUES "
            "(1, 'a', 1000, 'Test', 30, 18.0, '10', 92), "
            "(2, 'b', 2000, 'Test', 30, 18.2, '9', 0), "
            "(3, 'c', 3000, 'Test', 30, 0, '', 93), "
            "(4, 'd', 4000, 'Test', 30, 19, '8', 94)")));
        QVERIFY(ShotHistoryStorage::updateShotWaterResultStatic(db, 1, "a",
            {{"status", "dispensed"}, {"amountSource", "scale"}, {"weightG", 100.0}, {"temperatureC", 85.0}}));
        QVERIFY(ShotHistoryStorage::updateShotWaterResultStatic(db, 2, "b",
            {{"status", "dispensed"}, {"amountSource", "scale"}, {"weightG", 120.0}, {"temperatureC", 90.0}}));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("comparison-info-seed"));

    ShotComparisonModel model;
    model.setStorage(&storage);
    model.setSelection({qint64(1), qint64(2), qint64(3), qint64(4)});
    QTRY_VERIFY(!model.loading());
    QCOMPARE(model.getShotInfo(0).value(QStringLiteral("doseG")).toDouble(), 18.0);
    QCOMPARE(model.getShotInfo(1).value(QStringLiteral("doseG")).toDouble(), 18.2);
    QVERIFY(model.getShotInfo(1).value(QStringLiteral("temperatureOverrideC")).isNull());
    QVERIFY(model.getShotInfo(2).value(QStringLiteral("doseG")).isNull());
    QVERIFY(model.getShotInfo(2).value(QStringLiteral("grinderSetting")).toString().isEmpty());
    model.shiftWindowRight();
    QTRY_VERIFY(!model.loading());
    QCOMPARE(model.getShotInfo(2).value(QStringLiteral("grinderSetting")).toString(), QStringLiteral("8"));
    model.setBaseShot(2);
    QTRY_VERIFY(!model.loading());
    QCOMPARE(model.getShotInfo(0).value(QStringLiteral("doseG")).toDouble(), 18.2);
    const auto compared = model.comparison().value("shots").toList();
    QCOMPARE(compared.first().toMap().value("hotWaterResult").toMap().value("weightG").toDouble(), 120.0);
    QVERIFY(model.getShotInfo(0).value(QStringLiteral("isBase")).toBool());
}

void ComparisonServiceTest::requestCorrelationDiscardsSupersededSnapshots()
{
    Fixture f;
    QVERIFY(f.valid());
    QSignalSpy changed(f.serviceA, &ComparisonService::shotsChanged);
    f.serviceA->setShots({qint64(11), qint64(12)});
    QTRY_COMPARE(f.source.requests.size(), 1);
    f.serviceA->setShots({qint64(21), qint64(22), qint64(23)});
    QTRY_COMPARE(f.source.requests.size(), 2);
    const qsizetype beforeResponses = changed.count();

    f.source.answer(0, 2);
    QCoreApplication::processEvents();
    QCOMPARE(f.serviceA->shotCount(), 0);
    QCOMPARE(changed.count(), beforeResponses);
    f.source.answer(1, 3);
    QTRY_COMPARE(f.serviceA->shotCount(), 3);
    QCOMPARE(f.serviceA->totalShots(), 3);
    QTRY_VERIFY(changed.count() > beforeResponses);
}

void ComparisonServiceTest::facadeSelectionsAreIndependentAcrossEngines()
{
    Fixture f;
    QVERIFY(f.valid());
    f.serviceA->setShots({qint64(1), qint64(2)});
    QTRY_COMPARE(f.source.requests.size(), 1);
    f.serviceB->setShots({qint64(7), qint64(8), qint64(9)});
    QTRY_COMPARE(f.source.requests.size(), 2);

    QCOMPARE(f.serviceA->totalShots(), 2);
    QCOMPARE(f.serviceB->totalShots(), 3);
    f.source.answer(0, 2);
    f.source.answer(1, 3);
    QTRY_COMPARE(f.serviceA->shotCount(), 2);
    QTRY_COMPARE(f.serviceB->shotCount(), 3);
    QCOMPARE(f.serviceA->totalShots(), 2);
    QCOMPARE(f.serviceB->totalShots(), 3);
}

void ComparisonServiceTest::disconnectClearsDataAndReconnectRequeriesWithoutReplay()
{
    Fixture f;
    QVERIFY(f.valid());
    f.serviceA->setShots({qint64(31), qint64(32)});
    QTRY_COMPARE(f.source.requests.size(), 1);
    f.source.answer(0, 2);
    QTRY_COMPARE(f.serviceA->shotCount(), 2);

    f.serviceA->setBaseShot(32);
    QTRY_COMPARE(f.source.requests.size(), 2);
    const QString oldRequest = f.source.requests.at(1).id;
    f.serviceA->setNode(nullptr);
    QCOMPARE(f.serviceA->shotCount(), 0);
    QCOMPARE(f.serviceA->loading(), false);
    QCOMPARE(f.serviceA->totalShots(), 2); // selection survives; no action is replayed
    f.source.answer(1, 1);
    QCoreApplication::processEvents();
    QCOMPARE(f.serviceA->shotCount(), 0); // late host result is ignored
    QVERIFY(!oldRequest.isEmpty());

    f.serviceA->setNode(&f.client);
    QTRY_COMPARE(f.source.requests.size(), 3);
    QCOMPARE(f.source.requests.at(2).shotIds, QVariantList({qint64(31), qint64(32)}));
    QCOMPARE(f.source.requests.at(2).baseShotId, qint64(32));
    f.source.answer(2, 2);
    QTRY_COMPARE(f.serviceA->shotCount(), 2);
}

void ComparisonServiceTest::restoredSelectionQueriesOnlyItsFinalBaseAndWindow()
{
    Fixture f;
    QVERIFY(f.valid());
    const QVariantList ids{qint64(11), qint64(12), qint64(13), qint64(14), qint64(15)};
    f.serviceA->restoreSelection(ids, 13, 2);
    QTRY_COMPARE(f.source.requests.size(), 1);
    QCOMPARE(f.source.requests.first().shotIds, ids);
    QCOMPARE(f.source.requests.first().baseShotId, qint64(13));
    QCOMPARE(f.source.requests.first().windowStart, 2);
    QCOMPARE(f.serviceA->selectedShotIds(), ids);
    QCOMPARE(f.serviceB->selectedShotIds().size(), 0);
    f.source.answer(0, 3);
    QTRY_COMPARE(f.serviceA->shotCount(), 3);

    f.serviceA->restoreSelection(ids, 999, 99);
    QTRY_COMPARE(f.source.requests.size(), 2);
    QCOMPARE(f.source.requests.last().baseShotId, qint64(0));
    QCOMPARE(f.source.requests.last().windowStart, 2);
    f.serviceA->setShots({qint64(21), qint64(22)});
    QTRY_COMPARE(f.source.requests.size(), 3);
    QCOMPARE(f.source.requests.last().baseShotId, qint64(0));
    QCOMPARE(f.source.requests.last().windowStart, 0);
}

void ComparisonServiceTest::inspectorReadoutPreservesChannelListsAcrossTheRemoteSnapshot()
{
    Fixture f;
    QVERIFY(f.valid());
    f.serviceA->setShots({qint64(41), qint64(42)});
    QTRY_COMPARE(f.source.requests.size(), 1);
    const QStringList wireNames{"pressure", "flow", "temperature", "weight", "weightFlow",
        "resistance", "conductance", "conductanceDerivative", "darcyResistance",
        "temperatureMix", "temperatureMixGoal"};
    const QStringList valueNames{"pressure", "flow", "temperature", "weight", "weightFlow",
        "resistance", "conductance", "conductanceDerivative", "darcyResistance",
        "temperatureMix", "temperatureMixGoal"};
    const QStringList flags{"hasPressure", "hasFlow", "hasTemperature", "hasWeight", "hasWeightFlow",
        "hasResistance", "hasConductance", "hasConductanceDerivative", "hasDarcyResistance",
        "hasTemperatureMix", "hasTemperatureMixGoal"};
    QVariantMap snapshot{{QStringLiteral("maxTime"), 20.0}};
    for (int channel = 0; channel < wireNames.size(); ++channel) {
        const double first = channel == 7 ? -0.6 : 10.0 + channel;
        const double second = channel == 7 ? -0.25 : 110.0 + channel;
        const QVariantList shotA{QPointF(15.0, first), QPointF(15.5, first + 1.0)};
        // Missing middle/last channels must retain their positions, rather than
        // shifting the remaining readings into another channel.
        const QVariantList shotB = channel == 1 || channel == 10 ? QVariantList{}
            : QVariantList{QPointF(15.0, second), QPointF(15.5, second + 1.0)};
        snapshot.insert(wireNames[channel], NativeComparisonServiceSource::nestedSamples({shotA, shotB}));
    }
    f.source.answer(0, 2, snapshot);
    QTRY_COMPARE(f.serviceA->shotCount(), 2);
    QCOMPARE(f.serviceA->getPressureData(0).size(), 2);
    QCOMPARE(f.serviceA->getPressureData(0).first().toPointF(), QPointF(15.0, 10.0));
    QVERIFY(f.serviceA->getFlowData(1).isEmpty());

    const QVariantMap first = f.serviceA->getValuesAtTime(0, 15.2);
    const QVariantMap second = f.serviceA->getValuesAtTime(1, 15.2);
    for (int channel = 0; channel < valueNames.size(); ++channel) {
        QVERIFY2(first.value(flags[channel]).toBool(), qPrintable(flags[channel]));
        QCOMPARE(first.value(valueNames[channel]).toDouble(), channel == 7 ? -0.6 : 10.0 + channel);
        const bool present = channel != 1 && channel != 10;
        QCOMPARE(second.value(flags[channel]).toBool(), present);
        QCOMPARE(second.value(valueNames[channel]).toDouble(),
                 !present ? -1.0 : channel == 7 ? -0.25 : 110.0 + channel);
    }
    QCOMPARE(f.serviceA->getValuesAtTime(0, 15.45).value(QStringLiteral("pressure")).toDouble(), 11.0);
    for (const QVariantMap& unavailable : {f.serviceA->getValuesAtTime(-1, 15.2),
            f.serviceA->getValuesAtTime(2, 15.2), f.serviceA->getValuesAtTime(0, 18.0)}) {
        for (const QString& flag : flags)
            QVERIFY2(!unavailable.value(flag).toBool(), qPrintable(flag));
    }
    QCOMPARE(f.serviceB->shotCount(), 0);
    QVERIFY(!f.serviceB->getValuesAtTime(0, 15.2).value(QStringLiteral("hasPressure")).toBool());
}

void ComparisonServiceTest::sourceSnapshotPreservesOneSeriesPerDisplayedShot()
{
    const QVariantList nested = NativeComparisonServiceSource::nestedSamples({
        QVariantList{QPointF(0.0, 1.0), QPointF(1.0, 2.0)},
        QVariantList{QPointF(3.0, 4.0)}
    });
    QCOMPARE(nested.size(), 2);
    QCOMPARE(nested.at(0).toList().size(), 2);
    QCOMPARE(nested.at(1).toList().size(), 1);
    QCOMPARE(nested.at(1).toList().first().toPointF(), QPointF(3.0, 4.0));
}

QTEST_GUILESS_MAIN(ComparisonServiceTest)
#include "tst_comparisonservice.moc"
