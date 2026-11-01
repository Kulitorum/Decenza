#include <QtTest>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>

#include <utility>

#include "remote/settingsconnectionsservice.h"
#include "remote/settingsconnectionsservice_qml.h"
#include "remote/manualwifirequestowner.h"
#include "rep_settingsconnectionsservice_source.h"

class PendingConnectionsSource final : public ConnectionsStateSimpleSource
{
    Q_OBJECT
public:
    using ConnectionsStateSimpleSource::setReady;
    using ConnectionsStateSimpleSource::setSnapshot;

    struct Request { QString id; QString operation; QVariantMap arguments; };
    QList<Request> requests;

    PendingConnectionsSource()
    {
        setReady(true);
        setSnapshot({{QStringLiteral("fixture"), true}});
    }

    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap arguments) override
    {
        requests.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }
};

class tst_SettingsConnectionsService final : public QObject
{
    Q_OBJECT
private:
    struct Fixture {
        PendingConnectionsSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode clientNode;
        QQmlEngine engine;
        SettingsConnectionsService* service = nullptr;

        Fixture()
        {
            if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))))
                qFatal("Could not start test QtRO host");
            if (!host.enableRemoting<ConnectionsStateSourceAPI>(&source))
                qFatal("Could not publish connections fixture");
            if (!clientNode.connectToNode(host.hostUrl()))
                qFatal("Could not connect test QtRO client");
            SettingsConnectionsService::setPublishedNode(&clientNode);
            service = SettingsConnectionsServiceForeign::create(&engine, &engine);
            QTRY_VERIFY(service->ready());
        }

        ~Fixture()
        {
            SettingsConnectionsService::setNativeLogShareHandler({});
            SettingsConnectionsService::setPublishedNode(nullptr);
        }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    void requestCompletionMustMatchIdAndOperation()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsConnectionsService::operationCompleted);
        QVERIFY(completed.isValid());
        const QString id = f.service->execute(QStringLiteral("connectScale"),
                                               {{QStringLiteral("address"), QStringLiteral("AA:BB")}});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(f.source.requests.size(), 1);
        QCOMPARE(f.source.requests.first().id, id);

        emit f.source.operationCompleted(QStringLiteral("other-id"), QStringLiteral("connectScale"),
                                         true, {}, {});
        emit f.source.operationCompleted(QString(), QString(), true, {}, {});
        emit f.source.operationCompleted(id, QStringLiteral("differentOperation"), true, {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(completed.size(), 0);

        const QVariantMap result{{QStringLiteral("dispatched"), true}};
        emit f.source.operationCompleted(id, QStringLiteral("connectScale"), true, result, {});
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("connectScale"));
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toMap(), result);
        QVERIFY(!f.service->busy());
    }

    void disconnectFailsPendingAndNeverReplays()
    {
        Fixture f;
        const QVariantMap calibration{{QStringLiteral("proportionalK"), 1.25},
                                      {QStringLiteral("shotCount"), 4}};
        f.source.setPortalCalibration(calibration);
        QTRY_COMPARE(f.service->portalCalibration(), calibration);
        QSignalSpy completed(f.service, &SettingsConnectionsService::operationCompleted);
        const QString id = f.service->execute(QStringLiteral("connectDe1"),
                                               {{QStringLiteral("address"), QStringLiteral("AA:BB")}});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(f.source.requests.size(), 1);
        QVERIFY(f.service->busy());

        QVERIFY(f.host.disableRemoting(&f.source));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        QTRY_VERIFY(!f.service->ready());
        QVERIFY(f.service->portalCalibration().isEmpty());
        QCOMPARE(f.source.requests.size(), 1);
        QVERIFY(!f.service->busy());
    }

    void pendingRequestsAreBounded()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsConnectionsService::operationCompleted);
        for (int i = 0; i < 65; ++i)
            QVERIFY(!f.service->execute(QStringLiteral("connectScale"),
                                        {{QStringLiteral("address"), QString::number(i)}}).isEmpty());

        QTRY_COMPARE(f.source.requests.size(), 64);
        QTRY_VERIFY(completed.size() >= 1);
        bool sawCapRefusal = false;
        for (const auto& event : completed) {
            if (event.at(4).toString() == QLatin1String("tooManyRequests"))
                sawCapRefusal = true;
        }
        QVERIFY(sawCapRefusal);
        for (const auto& request : std::as_const(f.source.requests))
            emit f.source.operationCompleted(request.id, request.operation, true, {}, {});
        QTRY_VERIFY(!f.service->busy());
    }

    void downloadRouteIsSameOriginAndRejectsArbitraryPaths()
    {
        Fixture f;
        f.service->setResourceOrigin(QUrl(QStringLiteral("https://device.example:8123")));
        QCOMPARE(f.service->resolveLogDownloadUrl(QStringLiteral("/api/debug/file/zip")),
                 QStringLiteral("https://device.example:8123/api/debug/file/zip"));
        QVERIFY(f.service->resolveLogDownloadUrl(QStringLiteral("https://evil.example/file")).isEmpty());
        QVERIFY(f.service->resolveLogDownloadUrl(QStringLiteral("/api/debug/file/other")).isEmpty());
    }

    void nativeLogShareUsesPlatformCallback()
    {
        Fixture f;
        int shareCalls = 0;
        SettingsConnectionsService::setNativeLogShareHandler([&shareCalls] { ++shareCalls; });
        QSignalSpy completed(f.service, &SettingsConnectionsService::operationCompleted);

        const QString id = f.service->execute(QStringLiteral("requestSystemLogDownload"), {});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(shareCalls, 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toMap().value(QStringLiteral("nativeShare")).toBool(), true);
        QCOMPARE(f.source.requests.size(), 0); // Native does not require the HTTP server route.
    }

    void manualWifiProbeOwnershipTransfersOnlyForTheOwningClient()
    {
        ManualWifiRequestOwner probeOwner;
        ManualWifiRequestOwner connectOwner;
        QVERIFY(probeOwner.acquire(QStringLiteral("browser-a-probe")));
        QVERIFY(!ManualWifiRequestOwner::transfer(probeOwner, QStringLiteral("browser-b-probe"),
                                                   connectOwner, QStringLiteral("browser-a-connect")));
        QVERIFY(ManualWifiRequestOwner::transfer(probeOwner, QStringLiteral("browser-a-probe"),
                                                  connectOwner, QStringLiteral("browser-a-connect")));
        QVERIFY(probeOwner.requestId().isEmpty());
        QVERIFY(connectOwner.owns(QStringLiteral("browser-a-connect")));
        QVERIFY(!connectOwner.owns(QStringLiteral("browser-b-connect")));
    }

    void manualWifiCancellationKeepsItsRequestIdentity()
    {
        Fixture f;
        QSignalSpy cancelled(f.service, &SettingsConnectionsService::manualWifiValidationCancelled);
        QVERIFY(cancelled.isValid());
        emit f.source.manualWifiValidationCancelled(QStringLiteral("browser-a-connect"));
        QTRY_COMPARE(cancelled.size(), 1);
        QCOMPARE(cancelled.first().first().toString(), QStringLiteral("browser-a-connect"));
    }
};

QTEST_GUILESS_MAIN(tst_SettingsConnectionsService)
#include "tst_settingsconnectionsservice.moc"
