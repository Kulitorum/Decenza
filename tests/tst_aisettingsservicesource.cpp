#include <QtTest>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QSignalSpy>
#include <QQmlEngine>

#include "core/settings.h"
#include "core/settings_ai.h"
#include "remote/aisettingsservice.h"
#include "remote/aisettingssnapshot.h"
#include "rep_aisettingsservice_source.h"
#include "rep_aisettingsservice_replica.h"

class AISettingsFixtureSource final : public AISettingsStateSimpleSource {
public:
    AISettingsFixtureSource() { setSnapshot({}); setReady(true); }
    void refresh() override {}
    void testConnection(QString) override {}
    void refreshOllamaModels(QString) override {}
    QVariantMap requestMcpUrls() override
    {
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("connectorUrl"), QStringLiteral("https://mcp.example.test/private?token=fixture")}}}};
    }
    QVariantMap rotateMcpToken() override { return {}; }
    void forgetTailscale(QString) override {}
};

class AISettingsServiceSourceTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void snapshotNeverContainsCredentialOrTokenMaterial()
    {
        Settings settings;
        const QString oldOpenAI = settings.ai()->openaiApiKey();
        const bool oldRemoteEnabled = settings.mcp()->remoteMcpEnabled();
        settings.ai()->setOpenaiApiKey(QStringLiteral("fixture-openai-secret"));
        settings.mcp()->setRemoteMcpEnabled(true);

        const QVariantMap snapshot = AISettingsSnapshot::fromSettings(&settings);
        settings.ai()->setOpenaiApiKey(oldOpenAI);
        settings.mcp()->setRemoteMcpEnabled(oldRemoteEnabled);
        QVERIFY(snapshot.value(QStringLiteral("hasOpenaiApiKey")).toBool());
        QVERIFY(snapshot.contains(QStringLiteral("mcpEnabled")));
        QVERIFY(!snapshot.contains(QStringLiteral("openaiApiKey")));
        QVERIFY(!snapshot.contains(QStringLiteral("mcpApiKey")));
        QVERIFY(!snapshot.contains(QStringLiteral("remoteMcpToken")));
        QVERIFY(!snapshot.contains(QStringLiteral("connectorUrl")));
        QVERIFY(!snapshot.contains(QStringLiteral("loginUrl")));
        QVERIFY(!snapshot.values().contains(QStringLiteral("fixture-openai-secret")));
    }

    void mcpUrlsAreReturnedOnlyToRequestingReplica()
    {
        AISettingsFixtureSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<AISettingsStateSourceAPI>(&source));
        QRemoteObjectNode firstNode;
        QRemoteObjectNode secondNode;
        QVERIFY(firstNode.connectToNode(host.hostUrl()));
        QVERIFY(secondNode.connectToNode(host.hostUrl()));
        auto* first = firstNode.acquire<AISettingsStateReplica>();
        auto* second = secondNode.acquire<AISettingsStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(first->isReplicaValid() && second->isReplicaValid(), 5000);
        QSignalSpy secondSignal(second, &AISettingsStateReplica::operationCompleted);
        auto* watcher = new QRemoteObjectPendingCallWatcher(first->requestMcpUrls(), this);
        QSignalSpy finished(watcher, &QRemoteObjectPendingCallWatcher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(watcher->error(), QRemoteObjectPendingCall::NoError);
        const QVariantMap response = watcher->returnValue().toMap();
        QCOMPARE(response.value(QStringLiteral("success")).toBool(), true);
        QCOMPARE(response.value(QStringLiteral("result")).toMap()
                     .value(QStringLiteral("connectorUrl")).toString(),
                 QStringLiteral("https://mcp.example.test/private?token=fixture"));
        QCoreApplication::processEvents();
        QCOMPARE(secondSignal.count(), 0);
        QVERIFY(!second->snapshot().contains(QStringLiteral("connectorUrl")));
        watcher->deleteLater();
    }

    void facadeConsumesPrivateMcpReplyAndFailsPendingCallsOnDisconnect()
    {
        AISettingsFixtureSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<AISettingsStateSourceAPI>(&source));
        QRemoteObjectNode requester;
        QVERIFY(requester.connectToNode(host.hostUrl()));
        AISettingsService::setPublishedNode(&requester);
        QQmlEngine engine;
        AISettingsService* service = AISettingsService::create(&engine, nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);

        QSignalSpy completed(service, &AISettingsService::completed);
        const QString requestId = service->request(QStringLiteral("requestMcpUrls"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        const QList<QVariant> reply = completed.takeFirst();
        QCOMPARE(reply.at(0).toString(), requestId);
        QCOMPARE(reply.at(1).toString(), QStringLiteral("requestMcpUrls"));
        QCOMPARE(reply.at(2).toBool(), true);
        QCOMPARE(reply.at(3).toMap().value(QStringLiteral("connectorUrl")).toString(),
                 QStringLiteral("https://mcp.example.test/private?token=fixture"));
        QVERIFY(!service->snapshot().contains(QStringLiteral("connectorUrl")));

        service->setNode(nullptr);
        QVERIFY(!service->ready());
        const QString disconnectedRequestId = service->request(QStringLiteral("requestMcpUrls"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        const QList<QVariant> disconnectedReply = completed.takeFirst();
        QCOMPARE(disconnectedReply.at(0).toString(), disconnectedRequestId);
        QCOMPARE(disconnectedReply.at(2).toBool(), false);
        QVERIFY(disconnectedReply.at(3).toMap().isEmpty());
        QCOMPARE(disconnectedReply.at(4).toString(), QStringLiteral("unavailable"));

        AISettingsService::setPublishedNode(nullptr);
    }
};

QTEST_MAIN(AISettingsServiceSourceTest)
#include "tst_aisettingsservicesource.moc"
