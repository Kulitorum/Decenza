#include <QtTest>

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>

#include "remote/shotuploadservice.h"
#include "remote/shotuploadservice_qml.h"
#include "rep_shotuploadservice_source.h"

class FakeShotUploadSource final : public ShotUploadStateSimpleSource
{
    Q_OBJECT
public:
    FakeShotUploadSource()
    {
        setSnapshot({{QStringLiteral("visualizer"), QVariantMap{{QStringLiteral("connected"), false}}},
                     {QStringLiteral("decent"), QVariantMap{{QStringLiteral("state"), QStringLiteral("notLinked")}}}});
        setReady(true);
    }
    void refresh() override {}
    void execute(QString id, QString operation, QVariantMap arguments) override
    {
        lastId = std::move(id);
        lastOperation = std::move(operation);
        if (lastOperation == QLatin1String("openDecentAccount")) {
            lastReceipt = arguments.value(QStringLiteral("receipt")).toString();
            m_privateResults.insert(lastReceipt,
                                    {{QStringLiteral("url"), QStringLiteral("https://account.example.test/private?token=secret")}});
        }
    }
    QVariantMap takePrivateResult(QString receipt) override
    {
        return m_privateResults.take(receipt);
    }
    void complete(const QString& id, const QString& operation, bool success,
                  const QString& reason = {})
    {
        emit operationCompleted(id, operation, success, {}, reason);
    }
    void requestMachineChoice(const QString& id)
    {
        emit machineChoiceNeeded(id, {QStringLiteral("123456")},
                                 {QStringLiteral("123456 · DE1")});
    }
    QString lastId;
    QString lastOperation;
    QString lastReceipt;
    QHash<QString, QVariantMap> m_privateResults;
};

class ShotUploadServiceTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void facadeCorrelatesPendingOperationAndDoesNotReplayAfterDisconnect()
    {
        FakeShotUploadSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotUploadStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        ShotUploadService::setPublishedNode(&host);
        ShotUploadService* service = ShotUploadService::create(&engine, nullptr);
        QVERIFY(service);
        service->setNode(&node); // Exercise the browser requester, not native root-modal ownership.
        QTRY_VERIFY(service->ready());
        QVERIFY(!service->snapshot().contains(QStringLiteral("password")));
        const QVariantMap visualizer = service->snapshot().value(QStringLiteral("visualizer")).toMap();
        QVERIFY(!visualizer.contains(QStringLiteral("password")));
        QVERIFY(!visualizer.contains(QStringLiteral("token")));

        QSignalSpy completed(service, &ShotUploadService::operationCompleted);
        QVERIFY(completed.isValid());
        const QString id = service->execute(QStringLiteral("linkDecent"),
                                            {{QStringLiteral("email"), QStringLiteral("a@example.test")},
                                             {QStringLiteral("password"), QStringLiteral("one-shot")}});
        QTRY_COMPARE(source.lastId, id);
        QCOMPARE(source.lastOperation, QStringLiteral("linkDecent"));
        QSignalSpy choiceNeeded(service, &ShotUploadService::machineChoiceNeeded);
        QVERIFY(choiceNeeded.isValid());
        source.requestMachineChoice(QStringLiteral("other-client-link"));
        QCoreApplication::processEvents();
        QCOMPARE(choiceNeeded.count(), 0);
        source.requestMachineChoice(id);
        QTRY_COMPARE(choiceNeeded.count(), 1);
        QCOMPARE(choiceNeeded.first().at(0).toString(), id);

        source.complete(QStringLiteral("forged"), QString(), false);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 0);
        QVERIFY(service->busy());

        source.complete(id, QStringLiteral("unlinkDecent"), true);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 0);
        QVERIFY(service->busy());

        service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        QVERIFY(!service->busy());
        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        source.complete(id, QStringLiteral("linkDecent"), true);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
    }

    void accountRedirectIsReturnedOnlyToRequestingReplica()
    {
        FakeShotUploadSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotUploadStateSourceAPI>(&source));
        QRemoteObjectNode firstNode;
        QRemoteObjectNode secondNode;
        QVERIFY(firstNode.connectToNode(host.hostUrl()));
        QVERIFY(secondNode.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine;
        QQmlEngine secondEngine;
        ShotUploadService::setPublishedNode(&firstNode);
        auto* first = ShotUploadService::create(&firstEngine, nullptr);
        ShotUploadService::setPublishedNode(&secondNode);
        auto* second = ShotUploadService::create(&secondEngine, nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);

        QSignalSpy firstCompleted(first, &ShotUploadService::operationCompleted);
        QSignalSpy secondCompleted(second, &ShotUploadService::operationCompleted);
        QSignalSpy sourceCompleted(&source, &ShotUploadStateSimpleSource::operationCompleted);
        const QString id = first->execute(QStringLiteral("openDecentAccount"));
        QTRY_COMPARE_WITH_TIMEOUT(source.lastId, id, 5000);
        QVERIFY(!source.lastReceipt.isEmpty());
        QCOMPARE(sourceCompleted.count(), 0);
        source.complete(id, QStringLiteral("openDecentAccount"), true);
        QTRY_COMPARE_WITH_TIMEOUT(firstCompleted.count(), 1, 5000);
        QCoreApplication::processEvents();
        QCOMPARE(secondCompleted.count(), 0);
        QCOMPARE(sourceCompleted.count(), 1);
        QVERIFY(sourceCompleted.first().at(3).toMap().isEmpty());
        const QVariantMap result = firstCompleted.first().at(3).toMap();
        QCOMPARE(result.value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://account.example.test/private?token=secret"));
    }
};

QTEST_MAIN(ShotUploadServiceTest)
#include "tst_shotuploadservice.moc"
