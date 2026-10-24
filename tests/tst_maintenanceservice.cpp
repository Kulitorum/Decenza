#include <QtTest>
#include <QCoreApplication>
#include <QPointer>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>

#include "../src/remote/calibrationservice_qml.h"
#include "../src/remote/maintenanceservice_qml.h"
#include "../src/remote/maintenanceoperationpolicy.h"
#include "rep_calibrationservice_source.h"
#include "rep_maintenanceservice_source.h"

#include <utility>

class TestMaintenanceSource final : public MaintenanceStateSimpleSource
{
    Q_OBJECT
public:
    struct Request { QString id; QString operation; QVariantMap arguments; qint64 revision; };
    QList<Request> requests;

    TestMaintenanceSource()
    {
        setReady(true);
        setRevision(4);
        setSnapshot({{QStringLiteral("connected"), true}});
    }

    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap arguments,
                 qint64 expectedRevision) override
    {
        requests.append({std::move(requestId), std::move(operation),
                         std::move(arguments), expectedRevision});
    }
};

class TestCalibrationSource final : public CalibrationStateSimpleSource
{
    Q_OBJECT
public:
    struct Request { QString id; QString operation; qint64 revision; };
    QList<Request> requests;

    TestCalibrationSource()
    {
        setReady(true);
        setRevision(4);
        setSnapshot({{QStringLiteral("connected"), true}});
        setErrorMessage({});
    }

    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap,
                 qint64 expectedRevision) override
    {
        requests.append({std::move(requestId), std::move(operation), expectedRevision});
    }
};

class MaintenanceServiceTest final : public QObject
{
    Q_OBJECT
private:
    struct Fixture {
        TestMaintenanceSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode clientNode;
        QQmlEngine engine;
        MaintenanceService* service = nullptr;

        Fixture()
        {
            if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))))
                qFatal("Could not start test QtRO host");
            if (!host.enableRemoting<MaintenanceStateSourceAPI>(&source))
                qFatal("Could not publish maintenance fixture");
            if (!clientNode.connectToNode(host.hostUrl()))
                qFatal("Could not connect maintenance test node");
            MaintenanceService::setPublishedNode(&clientNode);
            service = MaintenanceService::create(&engine);
            QTRY_VERIFY(service->ready());
        }

        ~Fixture()
        {
            MaintenanceService::setPublishedNode(nullptr);
        }
    };

    struct CalibrationFixture {
        TestCalibrationSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode clientNode;
        QQmlEngine engine;
        CalibrationService* service = nullptr;

        CalibrationFixture()
        {
            if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))))
                qFatal("Could not start calibration QtRO host");
            if (!host.enableRemoting<CalibrationStateSourceAPI>(&source))
                qFatal("Could not publish calibration fixture");
            if (!clientNode.connectToNode(host.hostUrl()))
                qFatal("Could not connect calibration test node");
            CalibrationService::setPublishedNode(&clientNode);
            service = CalibrationService::create(&engine);
            QTRY_VERIFY(service->ready());
        }

        ~CalibrationFixture()
        {
            CalibrationService::setPublishedNode(nullptr);
        }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    void operationCompletionMustMatchIdAndOperation()
    {
        Fixture fixture;
        QSignalSpy completed(fixture.service, &MaintenanceService::operationCompleted);
        QVERIFY(completed.isValid());
        const QString id = fixture.service->execute(QStringLiteral("startTransport"), {});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(fixture.source.requests.size(), 1);
        const auto request = fixture.source.requests.first();
        QCOMPARE(request.id, id);
        QCOMPARE(request.operation, QStringLiteral("startTransport"));
        QCOMPARE(request.revision, qint64(4));

        emit fixture.source.operationCompleted(QStringLiteral("other"), QStringLiteral("startTransport"),
                                               true, {}, {}, qint64(5));
        emit fixture.source.operationCompleted(id, QStringLiteral("startDescale"), true, {}, {}, qint64(5));
        QCoreApplication::processEvents();
        QCOMPARE(completed.size(), 0);

        emit fixture.source.operationCompleted(id, QStringLiteral("startTransport"), true,
                                               {{QStringLiteral("submitted"), true}}, {}, qint64(5));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(completed.first().at(2).toBool());
    }

    void connectingObserverDoesNotDispatchAnOperation()
    {
        Fixture fixture;
        QQmlEngine secondEngine;
        MaintenanceService* second = MaintenanceService::create(&secondEngine);
        QTRY_VERIFY(second->ready());
        // Connecting a second facade must not claim or release host cooling.
        QCoreApplication::processEvents();
        QCOMPARE(fixture.source.requests.size(), 0);
        QCOMPARE(second->snapshot().value(QStringLiteral("connected")).toBool(), true);
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("cancelDescale")));
    }

    void deletingBusyObserverDoesNotContinueAdmission()
    {
        Fixture fixture;
        QPointer<MaintenanceService> service(fixture.service);
        connect(fixture.service, &MaintenanceService::busyChanged, fixture.service, [&service] {
            delete service.data();
        }, Qt::DirectConnection);

        const QString id = service->execute(QStringLiteral("startTransport"), {});

        QVERIFY(!id.isEmpty());
        QVERIFY(service.isNull());
        QCOMPARE(fixture.source.requests.size(), 0);
    }

    void deletingCalibrationBusyObserverDoesNotContinueAdmission()
    {
        CalibrationFixture fixture;
        QPointer<CalibrationService> service(fixture.service);
        connect(fixture.service, &CalibrationService::busyChanged,
                fixture.service, [&service] { delete service.data(); }, Qt::DirectConnection);

        const QString id = service->execute(QStringLiteral("setFlowCalibration"), {});

        QVERIFY(!id.isEmpty());
        QVERIFY(service.isNull());
        QCOMPARE(fixture.source.requests.size(), 0);
    }

    void explicitCancelEndsSharedWorkflow()
    {
        Fixture fixture;
        QSignalSpy completed(fixture.service, &MaintenanceService::operationCompleted);
        const QString id = fixture.service->execute(QStringLiteral("cancelDescale"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(fixture.source.requests.size(), 1);
        QCOMPARE(fixture.source.requests.first().operation, QStringLiteral("cancelDescale"));
        emit fixture.source.operationCompleted(id, QStringLiteral("cancelDescale"), true,
                                               {{QStringLiteral("finished"), true}}, {}, qint64(5));
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!fixture.service->busy());
    }

    void reconnectRefreshesStateWithoutReplayingOldRequests()
    {
        Fixture fixture;
        QSignalSpy completed(fixture.service, &MaintenanceService::operationCompleted);
        const QString firstId = fixture.service->execute(QStringLiteral("startTransport"), {});
        QTRY_COMPARE(fixture.source.requests.size(), 1);

        fixture.service->setNode(nullptr);
        QTRY_VERIFY(!fixture.service->ready());
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), firstId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));

        fixture.service->setNode(&fixture.clientNode);
        QTRY_VERIFY(fixture.service->ready());
        QCOMPARE(fixture.source.requests.size(), 1); // The disconnected command was not replayed.
        fixture.service->refresh();
        const QString secondId = fixture.service->execute(QStringLiteral("startDescale"),
            {{QStringLiteral("allowHotSteam"), false}});
        QVERIFY(!secondId.isEmpty());
        QTRY_COMPARE(fixture.source.requests.size(), 2);
        QCOMPARE(fixture.source.requests.last().id, secondId);
        QCOMPARE(fixture.source.requests.last().operation, QStringLiteral("startDescale"));
        emit fixture.source.operationCompleted(secondId, QStringLiteral("startDescale"), true,
                                               {{QStringLiteral("submitted"), true}}, {}, qint64(5));
        QTRY_COMPARE(completed.size(), 2);
    }
};

QTEST_MAIN(MaintenanceServiceTest)
#include "tst_maintenanceservice.moc"
