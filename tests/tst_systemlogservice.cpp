#include <QtTest>
#include <QQmlEngine>
#include <QRemoteObjectNode>
#include <QRemoteObjectHost>
#include "rep_systemlogservice_source.h"
#include <QPointer>
#include <QSignalSpy>

#include <memory>

#include "remote/systemlogservice_qml.h"

class PendingLogSource final : public SystemLogStateSimpleSource
{
public:
    struct Request { QString id; QString operation; QVariantMap arguments; };
    QList<Request> requests;
    PendingLogSource() { setReady(true); }
    QStringList snapshotRequests;
    void requestSnapshot(QString id, QStringList, QString, int) override
    {
        snapshotRequests.append(id);
        emit snapshotCompleted(id, true, {QStringLiteral("[0.100] INFO [DE1] host snapshot")}, 10, {});
    }
    void requestBoundary(QString) override {}
    void execute(QString id, QString operation, QVariantMap arguments) override
    { requests.append({id, operation, arguments}); }
    void reply(const Request& request)
    { emit operationCompleted(request.id, request.operation, true, {}, {}); }
};

class tst_SystemLogService : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void inProcessViewStartupAndRapidReactivationKeepSnapshotAndStreamSignals()
    {
        PendingLogSource source;
        QRemoteObjectHost host(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        QVERIFY(host.enableRemoting(&source));
        SystemLogService::setPublishedNode(&host);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        QObject firstView;
        QObject secondView;
        QSignalSpy snapshots(service, &SystemLogService::snapshotCompleted);
        QSignalSpy lines(service, &SystemLogService::lineAppended);
        // Construction-time visibility changes can release the first view and
        // activate another before DeferredDelete gets a turn.
        service->setViewActive(&firstView, true);
        QVERIFY(service->ready());
        const QString first = service->requestSnapshot({QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 1);
        QCOMPARE(snapshots.last().at(0).toString(), first);
        QCOMPARE(snapshots.last().at(2).toStringList().size(), 1);
        service->setViewActive(&firstView, false);
        service->setViewActive(&secondView, true);
        QTRY_VERIFY(service->ready());
        const QString second = service->requestSnapshot({QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 2);
        QCOMPARE(snapshots.last().at(0).toString(), second);
        emit source.lineAppended(QStringLiteral("[0.200] INFO [DE1] live"), 11);
        QTRY_COMPARE(lines.count(), 1);
        QCOMPARE(lines.first().at(1).toLongLong(), qint64(11));
        service->setViewActive(&firstView, true);
        service->setViewActive(&secondView, false);
        QVERIFY(service->ready());
        service->requestSnapshot({QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 3);
        delete service;
        SystemLogService::setPublishedNode(nullptr);
    }

    void pendingReplicaRetirementBindsOnlyTheNewestSurvivingNode()
    {
        PendingLogSource firstSource;
        PendingLogSource replacementSource;
        QRemoteObjectHost firstHost(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        QRemoteObjectHost replacementHost(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        QVERIFY(firstHost.enableRemoting(&firstSource));
        QVERIFY(replacementHost.enableRemoting(&replacementSource));
        SystemLogService::setPublishedNode(&firstHost);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        QObject view;
        service->setViewActive(&view, true);
        QVERIFY(service->ready());
        service->setViewActive(&view, false);
        service->setNode(&replacementHost);
        SystemLogService::setPublishedNode(&replacementHost);
        service->setViewActive(&view, true);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->state()->node(), &replacementHost);
        QSignalSpy snapshots(service, &SystemLogService::snapshotCompleted);
        service->requestSnapshot({QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 1);
        QCOMPARE(firstSource.snapshotRequests.size(), 0);
        QCOMPARE(replacementSource.snapshotRequests.size(), 1);
        // Destroying an abandoned host while retirement is pending must not
        // resurrect its replica or leave the current binding unusable.
        service->setViewActive(&view, false);
        auto abandonedHost = std::make_unique<QRemoteObjectHost>(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        PendingLogSource abandonedSource;
        QVERIFY(abandonedHost->enableRemoting(&abandonedSource));
        service->setNode(abandonedHost.get());
        service->setViewActive(&view, true);
        abandonedHost.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!service->state());
        service->setNode(&replacementHost);
        QTRY_VERIFY(service->ready());
        service->requestSnapshot({QStringLiteral("[DE1]")}, QStringLiteral("INFO"), 400);
        QTRY_COMPARE(snapshots.count(), 2);
        delete service;
        SystemLogService::setPublishedNode(nullptr);
    }

    void fileRequestsUseTransportClientAndAreNeverReplayedAfterDisconnect()
    {
        PendingLogSource source;
        QRemoteObjectHost host(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        QVERIFY(host.enableRemoting(&source));
        QRemoteObjectNode node;
        const QString clientId = QStringLiteral("a1234567-b123-c123-d123-e123456789ab");
        node.setProperty("decenzaClientId", clientId);
        QVERIFY(node.connectToNode(host.hostUrl()));
        SystemLogService::setPublishedNode(nullptr);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        service->setNode(&node);
        QObject view;
        service->setViewActive(&view, true);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &SystemLogService::operationCompleted);
        const QString id = service->execute(QStringLiteral("prepareLogDownload"),
            {{QStringLiteral("format"), QStringLiteral("zip")},
             {QStringLiteral("clientId"), QStringLiteral("forged-client")}});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(source.requests.size(), 1);
        QCOMPARE(source.requests.first().arguments.value(QStringLiteral("clientId")).toString(), clientId);
        QCOMPARE(source.requests.first().id, id);
        service->setNode(nullptr);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        source.reply(source.requests.first());
        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(source.requests.size(), 1);
        const QString next = service->execute(QStringLiteral("prepareLogDownload"),
            {{QStringLiteral("format"), QStringLiteral("log")}});
        QTRY_COMPARE(source.requests.size(), 2);
        source.reply(source.requests.last());
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), next);
        QVERIFY(completed.last().at(2).toBool());
        delete service;
    }

    void leavingLogViewKeepsOutstandingFileRequestUntilItsTerminalReply()
    {
        PendingLogSource source;
        QRemoteObjectHost host(QUrl(QStringLiteral("tcp://127.0.0.1:0")));
        QVERIFY(host.enableRemoting(&source));
        QRemoteObjectNode node;
        node.setProperty("decenzaClientId", QStringLiteral("a1234567-b123-c123-d123-e123456789ab"));
        QVERIFY(node.connectToNode(host.hostUrl()));
        SystemLogService::setPublishedNode(nullptr);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        service->setNode(&node);
        QObject view;
        service->setViewActive(&view, true);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &SystemLogService::operationCompleted);
        service->execute(QStringLiteral("prepareLogDownload"), {{QStringLiteral("format"), QStringLiteral("zip")}});
        QTRY_COMPARE(source.requests.size(), 1);
        service->setViewActive(&view, false);
        QVERIFY(service->state());
        source.reply(source.requests.first());
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        QVERIFY(!service->state());
        delete service;
    }

    void replicaExistsOnlyWhileAViewIsActive()
    {
        QRemoteObjectNode node;
        SystemLogService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        QVERIFY(service);
        QVERIFY(!service->state());

        auto* view = new QObject;
        service->setViewActive(view, true);
        QVERIFY(service->state());
        service->setViewActive(view, false);
        QVERIFY(!service->state());

        service->setViewActive(view, true);
        QTRY_VERIFY(service->state());
        delete view;
        QVERIFY(!service->state());

        delete service;
        SystemLogService::setPublishedNode(nullptr);
    }

    void nestedRebindDuringReplicaReleaseKeepsNewestNode()
    {
        QRemoteObjectNode firstNode;
        auto requestedNode = std::make_unique<QRemoteObjectNode>();
        QRemoteObjectNode replacementNode;
        SystemLogService::setPublishedNode(&firstNode);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        auto* view = new QObject;
        service->setViewActive(view, true);
        QVERIFY(service->state());

        bool rebound = false;
        connect(service, &SystemLogService::stateChanged, service,
                [service, &replacementNode, &rebound] {
            if (rebound)
                return;
            rebound = true;
            service->setNode(&replacementNode);
        }, Qt::DirectConnection);
        service->setNode(requestedNode.get());
        QVERIFY(rebound);
        QTRY_VERIFY(service->state());
        auto* replacementReplica = service->state();
        QCOMPARE(replacementReplica->node(), &replacementNode);

        // If the outer setNode overwrote the reentrant binding, destroying
        // its requested node leaves the facade believing it must bind again.
        requestedNode.reset();
        QSignalSpy stateChanges(service, &SystemLogService::stateChanged);
        service->setNode(&replacementNode);
        QCOMPARE(stateChanges.size(), 0);
        QCOMPARE(service->state(), replacementReplica);

        delete service;
        delete view;
        SystemLogService::setPublishedNode(nullptr);
    }

    void deletedRequestedNodeIsNotBoundAfterReleaseNotification()
    {
        QRemoteObjectNode firstNode;
        auto requestedNode = std::make_unique<QRemoteObjectNode>();
        SystemLogService::setPublishedNode(&firstNode);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        auto* view = new QObject;
        service->setViewActive(view, true);
        QVERIFY(service->state());

        bool deleted = false;
        connect(service, &SystemLogService::stateChanged, service,
                [&requestedNode, &deleted] {
            if (deleted)
                return;
            deleted = true;
            requestedNode.reset();
        }, Qt::DirectConnection);
        service->setNode(requestedNode.get());
        QVERIFY(deleted);
        QVERIFY(!requestedNode);
        QVERIFY(!service->state());

        delete service;
        delete view;
        SystemLogService::setPublishedNode(nullptr);
    }
};

QTEST_MAIN(tst_SystemLogService)
#include "tst_systemlogservice.moc"
