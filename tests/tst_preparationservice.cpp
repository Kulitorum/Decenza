#include <QtTest>
#include <QEvent>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QUuid>

#include "remote/preparationservice_qml.h"
#include "remote/preparationservicesource.h"

class PreparationControllerStub final : public PreparationState
{
public:
    PreparationControllerStub() { publish({}); }
    void publish(const PreparationSnapshot& value, bool available = true)
    { applySnapshot(value, available, false); }
    bool selectMode(const QString& mode) override { return command(mode); }
    bool startMilkAttempt(const QString& owner) override
    { owners.append(owner); return command(QStringLiteral("milk-start")); }
    bool cancelMilkAttempt(const QString& owner) override
    { owners.append(owner); return command(QStringLiteral("milk-cancel")); }
    bool setMilkTimeLocked(bool locked) override
    { return command(locked ? QStringLiteral("milk-lock") : QStringLiteral("milk-unlock")); }
    QStringList calls;
    QStringList owners;
    bool accept = true;
private:
    bool command(const QString& action) { calls.append(action); return accept; }
};

class PendingPreparationSource final : public PreparationServiceStateSimpleSource
{
public:
    PendingPreparationSource()
    {
        setSourceId(QUuid::createUuid().toString(QUuid::WithoutBraces));
        setSnapshot(PreparationSnapshot{}.toJson().toVariantMap());
        setReady(true);
    }
    void execute(QString id, QString action, QString owner) override
    { ids.append(id); actions.append(action); owners.append(owner); }
    void finish(const QString& id, const QString& action, bool success)
    { emit operationCompleted(id, action, success, success ? QString() : QStringLiteral("refused")); }
    QStringList ids;
    QStringList actions;
    QStringList owners;
};

class tst_PreparationService final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning();
        PreparationService::setPublishedNode(nullptr);
    }
    void cleanup() { PreparationService::setPublishedNode(nullptr); }

    void hostUsesControllerAllowlistAndQueuesRefusals()
    {
        PreparationControllerStub controller;
        NativePreparationServiceSource source(&controller);
        QSignalSpy completed(&source, &PreparationServiceStateSimpleSource::operationCompleted);
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        controller.accept = false;
        source.execute(id, QStringLiteral("milk-start"), QStringLiteral("client/review"));
        source.execute(id, QStringLiteral("milk-start"), QStringLiteral("client/review"));
        QCOMPARE(controller.calls, QStringList{QStringLiteral("milk-start")});
        QCOMPARE(completed.size(), 0);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(!completed.first().at(3).toString().isEmpty());

        source.execute(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("tare"), {});
        source.execute(QStringLiteral("not-a-uuid"), QStringLiteral("steam"), {});
        // An ownerless offer could never be released when its client closes.
        source.execute(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("milk-start"), {});
        QTRY_COMPARE(completed.size(), 4);
        QCOMPARE(controller.calls.size(), 1);
        QVERIFY(!completed.at(1).at(2).toBool());
        QVERIFY(!completed.at(2).at(2).toBool());
        QVERIFY(!completed.at(3).at(2).toBool());

        controller.accept = true;
        for (const QString& action : {QStringLiteral("espresso"), QStringLiteral("steam"),
                QStringLiteral("hotwater"), QStringLiteral("flush"), QStringLiteral("milk-start"),
                QStringLiteral("milk-cancel"), QStringLiteral("milk-lock"), QStringLiteral("milk-unlock")}) {
            source.execute(QUuid::createUuid().toString(QUuid::WithoutBraces), action,
                           action.startsWith(QLatin1String("milk-")) ? QStringLiteral("client/review") : QString());
            QCOMPARE(controller.calls.last(), action);
        }
        QCOMPARE(controller.owners.last(), QStringLiteral("client/review"));
        QTRY_COMPARE(completed.size(), 12);
        for (qsizetype index = 4; index < completed.size(); ++index)
            QVERIFY(completed.at(index).at(2).toBool());

        controller.publish({}, false);
        source.execute(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("milk-start"),
                       QStringLiteral("client/review"));
        QTRY_COMPARE(completed.size(), 13);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(controller.calls.size(), 9);
    }

    void completionMatchesUuidAndActionAndDisconnectNeverReplays()
    {
        PendingPreparationSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("preparation-test:///commands")),
                                QRemoteObjectHost::AllowExternalRegistration));
        const QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        host.setProperty("decenzaClientId", clientId);
        QVERIFY(host.enableRemoting<PreparationServiceStateSourceAPI>(&source));
        PreparationService::setPublishedNode(&host);
        QQmlEngine engine;
        auto* service = PreparationServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &PreparationService::completed);
        QSignalSpy failed(service->state(), &PreparationState::actionFailed);

        QVERIFY(service->state()->selectMode(QStringLiteral("steam")));
        QCOMPARE(service->state()->mode(), QStringLiteral("espresso"));
        QTRY_COMPARE(source.ids.size(), 1);
        const QString id = source.ids.first();
        QVERIFY(!QUuid::fromString(id).isNull());
        source.finish(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("steam"), true);
        source.finish(id, QStringLiteral("milk-start"), true);
        source.finish(id, QStringLiteral("steam"), false);
        QCOMPARE(completed.size(), 0);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(failed.size(), 1);

        QVERIFY(service->state()->startMilkAttempt(QStringLiteral("review")));
        QTRY_COMPARE(source.ids.size(), 2);
        // The host releases this client's offers by the id it sees here.
        QCOMPARE(source.owners.last(), clientId + QStringLiteral("/review"));
        const QString disconnectedId = source.ids.last();
        service->setNode(nullptr);
        QVERIFY(!service->ready());
        QCOMPARE(completed.size(), 1);
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), disconnectedId);
        QVERIFY(!completed.last().at(2).toBool());
        service->setNode(&host);
        QTRY_VERIFY(service->ready());
        QCOMPARE(source.ids.size(), 2);
        source.finish(disconnectedId, QStringLiteral("milk-start"), true);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(completed.size(), 2);
    }

    void captureEventsRebaselineOnReconnectAndSourceReplacement()
    {
        PreparationControllerStub controller;
        PreparationSnapshot snapshot;
        snapshot.beanCaptureSequence = 4;
        snapshot.milkCaptureSequence = 7;
        snapshot.lastBeanGrams = 18.0;
        snapshot.lastMilkGrams = 35.0;
        snapshot.lastMilkSeconds = 5;
        snapshot.steamTimeoutScaled = true;
        controller.publish(snapshot);
        NativePreparationServiceSource source(&controller);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<PreparationServiceStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        PreparationService::setPublishedNode(&client);
        QQmlEngine engine;
        auto* service = PreparationServiceForeign::create(&engine, &engine);
        QSignalSpy beans(service->state(), &PreparationState::beanCaptured);
        QSignalSpy milk(service->state(), &PreparationState::milkCaptured);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->state()->snapshot().beanCaptureSequence, qint64(4));
        QVERIFY(service->state()->steamTimeoutScaled());
        QCOMPARE(beans.size(), 0);
        QCOMPARE(milk.size(), 0);

        ++snapshot.milkCaptureSequence;
        snapshot.lastMilkGrams = 37.0;
        snapshot.lastMilkSeconds = 6;
        controller.publish(snapshot);
        QTRY_COMPARE(milk.size(), 1);
        QCOMPARE(milk.first().at(0).toDouble(), 37.0);
        QCOMPARE(milk.first().at(1).toInt(), 6);
        service->setNode(nullptr);
        ++snapshot.milkCaptureSequence;
        controller.publish(snapshot);
        // Browser reconnect replaces the node; reusing an in-process replica
        // before its deferred deletion skips source signal wiring in Qt 6.12
        // (qremoteobjectnode.cpp:631-636,1532-1535).
        QRemoteObjectNode reconnectedClient;
        QVERIFY(reconnectedClient.connectToNode(host.hostUrl()));
        PreparationService::setPublishedNode(&reconnectedClient);
        service->setNode(&reconnectedClient);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->state()->snapshot().beanCaptureSequence, snapshot.beanCaptureSequence);
        QCOMPARE(service->state()->snapshot().milkCaptureSequence, snapshot.milkCaptureSequence);
        QCOMPARE(milk.size(), 1);
        ++snapshot.beanCaptureSequence;
        controller.publish(snapshot);
        QTRY_COMPARE(beans.size(), 1);

        QRemoteObjectHost replacementHost;
        QVERIFY(replacementHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        NativePreparationServiceSource replacement(&controller);
        QVERIFY(replacement.sourceId() != source.sourceId());
        QVERIFY(replacementHost.enableRemoting<PreparationServiceStateSourceAPI>(&replacement));
        QRemoteObjectNode replacementClient;
        QVERIFY(replacementClient.connectToNode(replacementHost.hostUrl()));
        PreparationService::setPublishedNode(&replacementClient);
        service->setNode(&replacementClient);
        QTRY_VERIFY(service->ready());
        QCOMPARE(beans.size(), 1);
        QCOMPARE(milk.size(), 1);
    }
};

QTEST_GUILESS_MAIN(tst_PreparationService)
#include "tst_preparationservice.moc"
