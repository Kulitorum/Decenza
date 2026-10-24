#include "remote/ghcsimulatorservice.h"
#include "rep_ghcsimulatorservice_source.h"
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopedPointer>
#include <QSignalSpy>

#include "../src/remote/ghcsimulatoroperationpolicy.h"

#include <QtTest>

class PendingGHCSimulatorServiceSource final : public GHCSimulatorStateSimpleSource
{
public:
    QStringList requests;
    PendingGHCSimulatorServiceSource()
    {
        setReady(true);
        setSnapshot({{QStringLiteral("available"), true}});
    }
    void refresh() override {}
    void execute(QString id, QString operation) override
    {
        requests.append(id);
        emit operationCompleted(id, operation, true, {});
    }
};

class GHCSimulatorOperationPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void requestAdmissionSurvivesSynchronousObservers_data()
    {
        QTest::addColumn<QString>("transition");
        for (const char* transition : {"detach", "rebind", "same-node"})
            QTest::newRow(transition) << QString::fromLatin1(transition);
    }

    void requestAdmissionSurvivesSynchronousObservers()
    {
        QFETCH(QString, transition);
        PendingGHCSimulatorServiceSource firstSource, secondSource;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<GHCSimulatorStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<GHCSimulatorStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        // Prepare a ready replacement before request admission.
        QScopedPointer<GHCSimulatorStateReplica> warmed(secondClient.acquire<GHCSimulatorStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        QPointer<GHCSimulatorService> service = GHCSimulatorService::create(&engine, nullptr);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &GHCSimulatorService::operationCompleted);
        bool transitioned = false;
        connect(service, &GHCSimulatorService::busyChanged, this, [&] {
            if (!service) return;
            if (transitioned || !service->busy()) return;
            transitioned = true;
            if (transition == QLatin1String("same-node")) {
                service->setNode(&firstClient);
            } else {
                service->setNode(transition == QLatin1String("rebind") ? &secondClient : nullptr);
            }
        });
        const QString retiredId = service->execute(QStringLiteral("pressSteam"));
        QVERIFY(transitioned);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), retiredId);
        if (transition == QLatin1String("same-node")) {
            QVERIFY(completed.first().at(2).toBool());
            QCOMPARE(firstSource.requests, QStringList{retiredId});
            QVERIFY(secondSource.requests.isEmpty());
            return;
        }
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toString(), QStringLiteral("disconnected"));
        service->setNode(&secondClient);
        QTRY_VERIFY(service->ready());
        const QString freshId = service->execute(QStringLiteral("pressSteam"));
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), freshId);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(firstSource.requests.isEmpty());
        QCOMPARE(secondSource.requests, QStringList{freshId});
        QVERIFY(!service->busy());
    }

    void init() { QTest::failOnWarning(); }

    void controlsRequireAHostSimulatorAndSimulationMode()
    {
        QVERIFY(GHCSimulatorOperationPolicy::simulatorAvailable(true, true));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(false, true));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(true, false));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(false, false));

        // A real-device connection is insufficient: the remote service must
        // remain unavailable unless this host owns a DE1Simulator instance.
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(
            QStringLiteral("pressEspresso"), false, false));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(
            QStringLiteral("pressEspresso"), true, false));
    }

    void onlyGroupHeadButtonActionsAreExposed()
    {
        for (const QString& operation : {
                 QStringLiteral("pressEspresso"), QStringLiteral("pressSteam"),
                 QStringLiteral("pressHotWater"), QStringLiteral("pressFlush"),
                 QStringLiteral("pressStop")}) {
            QVERIFY(GHCSimulatorOperationPolicy::supported(operation));
            QVERIFY(GHCSimulatorOperationPolicy::canExecute(operation, true, true));
        }
        QVERIFY(!GHCSimulatorOperationPolicy::supported(QStringLiteral("setState")));
        QVERIFY(!GHCSimulatorOperationPolicy::supported(QStringLiteral("releaseStop")));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(QStringLiteral("setState"), true, true));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(QStringLiteral("pressStop"), false, true));
    }

    void snapshotSeparatesAvailabilityFromControllerState()
    {
        const QVariantList leds{QStringLiteral("off"), QStringLiteral("lit")};
        const QVariantMap active = GHCSimulatorOperationPolicy::snapshot(true, true, leds, true, 2);
        QVERIFY(active.value(QStringLiteral("available")).toBool());
        QCOMPARE(active.value(QStringLiteral("ledColors")).toList(), leds);
        QVERIFY(active.value(QStringLiteral("stopPressed")).toBool());
        QCOMPARE(active.value(QStringLiteral("activeFunction")).toInt(), 2);

        const QVariantMap physical = GHCSimulatorOperationPolicy::snapshot(false, false, leds, false, 0);
        QVERIFY(!physical.value(QStringLiteral("available")).toBool());
        QCOMPARE(physical.value(QStringLiteral("ledColors")).toList(), leds);
    }
};

QTEST_GUILESS_MAIN(GHCSimulatorOperationPolicyTest)
#include "tst_ghcsimulatoroperationpolicy.moc"
