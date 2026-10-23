#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QtTest>

#include "remote/globalsupportservice.h"
#include "remote/globalsupportservice_qml.h"
#include "rep_globalsupportservice_replica.h"
#include "rep_globalsupportservice_source.h"

class GlobalSupportPrivateReplyFixture final : public GlobalSupportStateSimpleSource
{
    Q_OBJECT
public:
    using GlobalSupportStateSimpleSource::setReady;

    GlobalSupportPrivateReplyFixture()
    {
        setReady(true);
    }

    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap) override
    {
        pendingOperations.append({requestId, operation});
    }

    void complete(const QString& requestId, const QString& operation)
    {
        emit operationCompleted(requestId, operation, true, {}, {});
    }

    QList<QPair<QString, QString>> pendingOperations;

    QVariantMap requestCrashReport(QString requestId) override
    {
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("crashLog"), QStringLiteral("private crash fixture")},
                    {QStringLiteral("debugLogTail"), QStringLiteral("private debug fixture")}}},
                {QStringLiteral("requestId"), requestId}};
    }
};

class GlobalSupportPrivateReplyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void privateCrashReplyReachesOnlyRequester()
    {
        GlobalSupportPrivateReplyFixture source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<GlobalSupportStateSourceAPI>(&source));

        QRemoteObjectNode requesterNode;
        QRemoteObjectNode otherNode;
        QVERIFY(requesterNode.connectToNode(host.hostUrl()));
        QVERIFY(otherNode.connectToNode(host.hostUrl()));
        auto* requester = requesterNode.acquire<GlobalSupportStateReplica>();
        auto* other = otherNode.acquire<GlobalSupportStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(requester->isReplicaValid() && other->isReplicaValid(), 5000);

        QSignalSpy otherOperations(other, &GlobalSupportStateReplica::operationCompleted);
        auto* watcher = new QRemoteObjectPendingCallWatcher(
            requester->requestCrashReport(QStringLiteral("requester-id")), this);
        QSignalSpy finished(watcher, &QRemoteObjectPendingCallWatcher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(watcher->error(), QRemoteObjectPendingCall::NoError);
        const QVariantMap reply = watcher->returnValue().toMap();
        QCOMPARE(reply.value(QStringLiteral("requestId")).toString(),
                 QStringLiteral("requester-id"));
        QCOMPARE(reply.value(QStringLiteral("result")).toMap()
                     .value(QStringLiteral("crashLog")).toString(),
                 QStringLiteral("private crash fixture"));

        QCoreApplication::processEvents();
        QCOMPARE(otherOperations.count(), 0);
        watcher->deleteLater();
    }

    void facadeFailsPendingOnReplicaLossAndDoesNotReviveAfterReconnect()
    {
        GlobalSupportPrivateReplyFixture source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<GlobalSupportStateSourceAPI>(&source));

        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        GlobalSupportService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = GlobalSupportServiceForeign::create(&engine, nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);

        QSignalSpy completed(service, &GlobalSupportService::operationCompleted);
        const QString oldId = service->execute(QStringLiteral("hold"));
        QVERIFY(!oldId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(source.pendingOperations.size(), 1, 5000);

        QVERIFY(host.disableRemoting(&source));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        const QList<QVariant> disconnected = completed.takeFirst();
        QCOMPARE(disconnected.at(0).toString(), oldId);
        QCOMPARE(disconnected.at(2).toBool(), false);
        QCOMPARE(disconnected.at(4).toString(), QStringLiteral("disconnected"));
        QVERIFY(service->execute(QStringLiteral("whileDisconnected")).isEmpty());

        QVERIFY(host.enableRemoting<GlobalSupportStateSourceAPI>(&source));
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        source.complete(oldId, QStringLiteral("hold"));
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 0);

        const QString newId = service->execute(QStringLiteral("hold"));
        QVERIFY(!newId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(source.pendingOperations.size(), 2, 5000);
        source.complete(newId, QStringLiteral("hold"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        const QList<QVariant> reconnected = completed.takeFirst();
        QCOMPARE(reconnected.at(0).toString(), newId);
        QCOMPARE(reconnected.at(2).toBool(), true);

        service->setNode(nullptr);
        GlobalSupportService::setPublishedNode(nullptr);
    }
};

QTEST_GUILESS_MAIN(GlobalSupportPrivateReplyTest)
#include "tst_globalsupportprivatereply.moc"
