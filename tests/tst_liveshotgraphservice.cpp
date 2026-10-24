#include <QtTest>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>

#include "remote/liveshotgraphservice_qml.h"
#include "remote/liveshotgraphservicesource.h"
#include "models/shotdatamodel.h"

#include <cmath>
#include <limits>

namespace {
QVariantList points(std::initializer_list<QPointF> values)
{
    QVariantList result;
    for (const QPointF& value : values) {
        if (std::isnan(value.x()) && std::isnan(value.y()))
            result.append(QVariant::fromValue(QVariantList{QVariant{}, QVariant{}}));
        else
            result.append(QVariant::fromValue(value));
    }
    return result;
}

QVariantMap makeSnapshot(const QVariantList& pressure = {})
{
    QVariantMap series;
    for (const QString& name : {
             QStringLiteral("pressure"), QStringLiteral("flow"), QStringLiteral("temperature"),
             QStringLiteral("temperatureMix"), QStringLiteral("weight"), QStringLiteral("weightFlow"),
             QStringLiteral("resistance"), QStringLiteral("conductance"), QStringLiteral("darcyResistance")})
        series.insert(name, name == QLatin1String("pressure") ? pressure : QVariantList{});
    return {
        {QStringLiteral("series"), series},
        {QStringLiteral("rawTime"), 0.0},
        {QStringLiteral("maxTime"), 5.0},
        {QStringLiteral("stopTime"), -1.0},
        {QStringLiteral("weightAtStop"), 0.0},
        {QStringLiteral("finalWeight"), 0.0},
        {QStringLiteral("phaseMarkers"), QVariantList{}},
        {QStringLiteral("pressureGoalPoints"), QVariantList{}},
        {QStringLiteral("flowGoalPoints"), QVariantList{}},
        {QStringLiteral("temperatureGoalPoints"), QVariantList{}},
        {QStringLiteral("temperatureMixGoalPoints"), QVariantList{}},
        {QStringLiteral("portalSamples"), QVariantList{}},
        {QStringLiteral("portalSampleCount"), 0},
        {QStringLiteral("portalEcMin"), 0.0},
        {QStringLiteral("portalEcMax"), 0.1},
        {QStringLiteral("profileTargetWeight"), 0.0},
        {QStringLiteral("machineTargetWeight"), 0.0}
    };
}
}

class TestLiveShotGraphSource final : public LiveShotGraphStateSimpleSource
{
    Q_OBJECT
public:
    TestLiveShotGraphSource()
    {
        setPackageId(QStringLiteral("package-test"));
        setHostId(QStringLiteral("host-test"));
        setReady(true);
        setEpoch(2);
        setSequence(4);
        m_snapshot = makeSnapshot(points({QPointF(1, 2), QPointF(2, 3)}));
    }

    void requestSnapshot(QString requestId) override
    {
        ++snapshotRequests;
        emit snapshotResult(requestId, epoch(), sequence(), m_snapshot, {});
    }

    void appendPoint(double x, double y, qint64 sequence, bool emitDelta = true)
    {
        QVariantMap series = m_snapshot.value(QStringLiteral("series")).toMap();
        QVariantList pressure = series.value(QStringLiteral("pressure")).toList();
        pressure.append(QVariant::fromValue(QPointF(x, y)));
        series.insert(QStringLiteral("pressure"), pressure);
        m_snapshot.insert(QStringLiteral("series"), series);
        m_snapshot.insert(QStringLiteral("rawTime"), x);
        setSequence(sequence);
        if (emitDelta) {
            emit graphDelta({
                {QStringLiteral("type"), QStringLiteral("append")},
                {QStringLiteral("epoch"), epoch()},
                {QStringLiteral("sequence"), sequence},
                {QStringLiteral("append"), QVariantMap{{QStringLiteral("pressure"), points({QPointF(x, y)})}}},
                {QStringLiteral("rawTime"), x}
            });
        }
    }

    void reset(const QVariantMap& snapshot)
    {
        m_snapshot = snapshot;
        setEpoch(epoch() + 1);
        setSequence(1);
        emit graphDelta({
            {QStringLiteral("type"), QStringLiteral("reset")},
            {QStringLiteral("epoch"), epoch()},
            {QStringLiteral("sequence"), sequence()},
            {QStringLiteral("snapshot"), snapshot},
            {QStringLiteral("reason"), QString()}
        });
    }

    using LiveShotGraphStateSimpleSource::setSequence;
    void setSnapshotForTest(const QVariantMap& snapshot) { m_snapshot = snapshot; }
    int snapshotRequests = 0;

private:
    QVariantMap m_snapshot;
};

class tst_LiveShotGraphService : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); LiveShotGraphService::setPublishedNode(nullptr); }
    void cleanup() { LiveShotGraphService::setPublishedNode(nullptr); }

    void midShotJoinAndAppendDelta()
    {
        TestLiveShotGraphSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->epoch(), qint64(2));
        QCOMPARE(service->sequence(), qint64(4));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).size(), qsizetype(2));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).at(1), QPointF(2, 3));

        source.appendPoint(3, 4, 5);
        QTRY_COMPARE(service->sequence(), qint64(5));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).size(), qsizetype(3));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).last(), QPointF(3, 4));
        QCOMPARE(service->rawTime(), 3.0);
        QCOMPARE(source.snapshotRequests, 1);
    }

    void sequenceGapRequestsFreshSnapshotAndResetReplacesCurves()
    {
        TestLiveShotGraphSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        // The source has advanced one point without sending its delta. A later
        // sequence gap causes an explicit snapshot request and recovers the point.
        source.appendPoint(3, 4, 5, false);
        source.appendPoint(4, 5, 6);
        QTRY_COMPARE(service->sequence(), qint64(6));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).size(), qsizetype(4));
        QCOMPARE(source.snapshotRequests, 2);

        source.reset(makeSnapshot(points({QPointF(0, 9)})));
        QTRY_COMPARE(service->epoch(), qint64(3));
        QCOMPARE(service->sequence(), qint64(1));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).size(), qsizetype(1));
        QCOMPARE(service->m_series.value(QStringLiteral("pressure")).first(), QPointF(0, 9));
    }

    void reentrantSnapshotRequestSupersedesPreNotificationAdmission()
    {
        TestLiveShotGraphSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QCOMPARE(source.snapshotRequests, 1);

        bool reentered = false;
        connect(service, &LiveShotGraphService::graphChanged, service, [&] {
            if (reentered)
                return;
            reentered = true;
            service->requestSnapshot();
        }, Qt::DirectConnection);
        service->requestSnapshot();

        QVERIFY(reentered);
        QTRY_COMPARE(source.snapshotRequests, 2);
    }

    void snapshotAndResetRejectInvalidPortalOrdering()
    {
        TestLiveShotGraphSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        QVariantMap invalid = makeSnapshot();
        invalid.insert(QStringLiteral("portalSamples"), QVariantList{
            QVariantMap{{QStringLiteral("time"), 2.0}, {QStringLiteral("ecRaw"), 10.0},
                        {QStringLiteral("temperatureC"), 90.0}, {QStringLiteral("breakBefore"), false}},
            QVariantMap{{QStringLiteral("time"), 1.0}, {QStringLiteral("ecRaw"), 11.0},
                        {QStringLiteral("temperatureC"), 91.0}, {QStringLiteral("breakBefore"), false}}
        });
        invalid.insert(QStringLiteral("portalSampleCount"), 2);
        source.reset(invalid);
        QTRY_VERIFY(!service->ready()
                    && service->errorMessage().contains(QStringLiteral("snapshot"), Qt::CaseInsensitive));
        QCOMPARE(service->portalSamples().size(), 0);
    }

    void initialSnapshotRejectsInvalidPortalOrdering()
    {
        TestLiveShotGraphSource source;
        QVariantMap invalid = makeSnapshot();
        invalid.insert(QStringLiteral("portalSamples"), QVariantList{
            QVariantMap{{QStringLiteral("time"), 3.0}, {QStringLiteral("ecRaw"), 10.0},
                        {QStringLiteral("temperatureC"), 90.0}, {QStringLiteral("breakBefore"), false}},
            QVariantMap{{QStringLiteral("time"), 2.0}, {QStringLiteral("ecRaw"), 11.0},
                        {QStringLiteral("temperatureC"), 91.0}, {QStringLiteral("breakBefore"), false}}
        });
        invalid.insert(QStringLiteral("portalSampleCount"), 2);
        source.setSnapshotForTest(invalid);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(!service->ready()
                    && service->errorMessage().contains(QStringLiteral("snapshot"), Qt::CaseInsensitive));
        QCOMPARE(service->portalSamples().size(), 0);
    }

    void goalSuffixPreservesSegmentsAndPairedNanBreaks()
    {
        TestLiveShotGraphSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LiveShotGraphStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        LiveShotGraphService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = LiveShotGraphServiceForeign::create(&engine, &engine);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        source.setSequence(5);
        emit source.graphDelta({
            {QStringLiteral("type"), QStringLiteral("append")},
            {QStringLiteral("epoch"), source.epoch()},
            {QStringLiteral("sequence"), 5},
            {QStringLiteral("goalAppend"), QVariantMap{
                {QStringLiteral("pressure"), QVariantList{QVariantMap{
                    {QStringLiteral("segment"), 0},
                    {QStringLiteral("points"), points({QPointF(1, 4)})}
                }}},
                {QStringLiteral("temperature"), points({QPointF(nan, nan)})}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(service->temperatureGoalPoints().size() == 1, 5000);
        const QVariantList pressureSegments = service->pressureGoalPoints();
        QCOMPARE(pressureSegments.size(), 1);
        QCOMPARE(pressureSegments.first().toList().first().toPointF(), QPointF(1, 4));
        const QPointF temperatureBreak = service->temperatureGoalPoints().first().toPointF();
        QVERIFY(std::isnan(temperatureBreak.x()) && std::isnan(temperatureBreak.y()));
    }

    void nativeSourcePublishesMidShotSnapshotAndCorrections()
    {
        ShotDataModel model;
        model.clear(); // Starts the same periodic flush used at native shot start.
        NativeLiveShotGraphSource source(&model, nullptr, nullptr);
        source.setIdentity(QStringLiteral("package-native"), QStringLiteral("host-native"));
        QVERIFY(source.ready());
        QSignalSpy deltas(&source, &LiveShotGraphStateSource::graphDelta);
        QSignalSpy snapshots(&source, &LiveShotGraphStateSource::snapshotResult);
        QTRY_VERIFY(deltas.count() >= 1);
        deltas.clear(); // Ignore the initial empty epoch baseline.

        model.addSample(1.0, 2.0, 1.0, 90.0, 80.0, 3.0, 2.0, 92.0, 82.0);
        QTRY_VERIFY(deltas.count() >= 1);
        const QVariantMap append = deltas.last().at(0).toMap();
        QCOMPARE(append.value(QStringLiteral("type")).toString(), QStringLiteral("append"));
        QCOMPARE(append.value(QStringLiteral("append")).toMap()
                     .value(QStringLiteral("pressure")).toList().size(), qsizetype(1));
        const QVariantMap goalAppend = append.value(QStringLiteral("goalAppend")).toMap();
        QCOMPARE(goalAppend.value(QStringLiteral("temperature")).toList().size(), qsizetype(1));
        QCOMPARE(goalAppend.value(QStringLiteral("temperature")).toList()
                     .constFirst().toPointF(), QPointF(1, 92));
        QCOMPARE(goalAppend.value(QStringLiteral("pressure")).toList().size(), qsizetype(1));

        source.requestSnapshot(QStringLiteral("f7d332db-b1e1-44e7-9e85-3ee17084575f"));
        QCOMPARE(snapshots.count(), 1);
        const QVariantMap snapshot = snapshots.constFirst().at(3).toMap();
        QCOMPARE(snapshot.value(QStringLiteral("series")).toMap()
                     .value(QStringLiteral("pressure")).toList().size(), qsizetype(1));
        const QVariantList pressureGoals = snapshot.value(QStringLiteral("pressureGoalPoints")).toList();
        QCOMPARE(pressureGoals.size(), 1);
        QCOMPARE(pressureGoals.first().metaType().id(), QMetaType::QVariantList);
        QCOMPARE(pressureGoals.first().toList().size(), 1);

        const qsizetype beforeCorrection = deltas.count();
        model.smoothWeightFlowRate();
        QTRY_VERIFY(deltas.count() > beforeCorrection);
        QCOMPARE(deltas.last().at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("reset"));

        const qsizetype beforeClear = deltas.count();
        model.clear();
        QTRY_VERIFY(deltas.count() > beforeClear);
        QCOMPARE(deltas.last().at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("reset"));
        QCOMPARE(deltas.last().at(0).toMap().value(QStringLiteral("snapshot")).toMap()
                     .value(QStringLiteral("series")).toMap()
                     .value(QStringLiteral("pressure")).toList().size(), qsizetype(0));
    }

    void unchangedFlushDoesNotEmitDuplicateDelta()
    {
        ShotDataModel model;
        NativeLiveShotGraphSource source(&model, nullptr, nullptr);
        source.setIdentity(QStringLiteral("package-native"), QStringLiteral("host-native"));
        QSignalSpy deltas(&source, &LiveShotGraphStateSource::graphDelta);
        model.addSample(1.0, 2.0, 1.0, 90.0, 80.0, 3.0, 2.0, 92.0, 82.0);
        QTRY_COMPARE(deltas.count(), 1);
        model.flushed(); // A duplicate notification with unchanged vectors is a no-op.
        QCOMPARE(deltas.count(), 1);
    }
};

QTEST_MAIN(tst_LiveShotGraphService)
#include "tst_liveshotgraphservice.moc"
