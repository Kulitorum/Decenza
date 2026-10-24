#include <QtTest>

#include <QCoreApplication>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <utility>

#include "../src/remote/comparisonservice.h"
#include "../src/remote/comparisonservice_qml.h"
#include "../src/remote/comparisonservicesource.h"
#include "rep_comparisonservice_source.h"

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
    void answer(int index, int shotCount)
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
    void deletingSelectionObserverStopsAdmission();
    void deletingShotsChangedObserverDoesNotDispatchSnapshot();
    void facadeSelectionsAreIndependentAcrossEngines();
    void disconnectClearsDataAndReconnectRequeriesWithoutReplay();
    void sourceSnapshotPreservesOneSeriesPerDisplayedShot();
    void restoredSelectionQueriesOnlyItsFinalBaseAndWindow();
};

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

void ComparisonServiceTest::deletingShotsChangedObserverDoesNotDispatchSnapshot()
{
    Fixture f;
    QVERIFY(f.valid());
    QPointer<ComparisonService> service(f.serviceA);
    int notifications = 0;
    connect(f.serviceA, &ComparisonService::shotsChanged, f.serviceA, [&] {
        if (++notifications == 2)
            delete service.data();
    }, Qt::DirectConnection);

    f.serviceA->setShots({qint64(41), qint64(42)});

    QVERIFY(service.isNull());
    QCOMPARE(f.source.requests.size(), 0);
}

void ComparisonServiceTest::deletingSelectionObserverStopsAdmission()
{
    Fixture f;
    QVERIFY(f.valid());
    QPointer<ComparisonService> service(f.serviceA);
    connect(f.serviceA, &ComparisonService::shotsChanged, f.serviceA, [&] {
        delete service.data();
    }, Qt::DirectConnection);

    f.serviceA->setShots({qint64(51), qint64(52)});

    QVERIFY(service.isNull());
    QCOMPARE(f.source.requests.size(), 0);
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
