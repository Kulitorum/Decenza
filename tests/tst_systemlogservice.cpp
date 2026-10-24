#include <QtTest>
#include <QQmlEngine>
#include <QRemoteObjectNode>
#include <QPointer>
#include <QSignalSpy>

#include <memory>

#include "remote/systemlogservice_qml.h"

class tst_SystemLogService : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

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
        QVERIFY(service->state());
        delete view;
        QVERIFY(!service->state());

        delete service;
        SystemLogService::setPublishedNode(nullptr);
    }

    void rebindStopsWhenStateNotificationDeletesService()
    {
        QRemoteObjectNode firstNode;
        QRemoteObjectNode replacementNode;
        SystemLogService::setPublishedNode(&firstNode);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        auto* view = new QObject;
        service->setViewActive(view, true);
        QVERIFY(service->state());

        QPointer<SystemLogService> guard(service);
        connect(service, &SystemLogService::stateChanged, service,
                [service] { delete service; }, Qt::DirectConnection);
        service->setNode(&replacementNode);
        QVERIFY(guard.isNull());
        delete view;
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
        auto* replacementReplica = service->state();
        QVERIFY(replacementReplica);
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
