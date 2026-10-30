#include <QtTest>

#include <QCoreApplication>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QVariantMap>
#include <limits>
#include <memory>

#include <utility>

#include "core/machinecontrolstartpolicy.h"
#include "remote/machinecontrolespressopolicy.h"
#include "remote/machinecontrolservice_qml.h"
#include "rep_machinecontrolservice_source.h"

class PendingMachineControlSource final : public MachineControlStateSimpleSource
{
    Q_OBJECT
public:
    using MachineControlStateSimpleSource::setSelectedRecipeId;
    using MachineControlStateSimpleSource::setEventSourceId;
    using MachineControlStateSimpleSource::setEventSequence;
    using MachineControlStateSimpleSource::setPhase;
    using MachineControlStateSimpleSource::setCurrentFrameIndex;
    using MachineControlStateSimpleSource::setCurrentFrameName;
    using MachineControlStateSimpleSource::setPendingDrinkStep;

    int calls = 0;
    QStringList requestIds;
    QString requestId;
    QString operation;
    QVariantMap fields;
    QString expectedContextId;

    void publishEvent(const QString& event, const QVariantMap& payload = {})
    {
        const qint64 next = eventSequence() + 1;
        setEventSequence(next);
        emit presentationEvent(eventSourceId(), next, event, payload);
    }

    void execute(QString id, QString op, QVariantMap requestFields,
                 QString contextId) override
    {
        ++calls;
        requestIds.append(id);
        requestId = std::move(id);
        operation = std::move(op);
        fields = std::move(requestFields);
        expectedContextId = std::move(contextId);
    }

    void complete(const QString& id, const QString& op, bool success,
                  const QString& message = {})
    {
        emit operationCompleted(id, op, success, message,
                                success ? QVariantMap{{QStringLiteral("dispatched"), true}}
                                        : QVariantMap{});
    }
};

class tst_MachineControlService : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning();
        MachineControlService::setPublishedNode(nullptr);
    }
    void cleanup() { MachineControlService::setPublishedNode(nullptr); }

    void notReadyFailureIsQueued();
    void completionIsCorrelatedAndQueued();
    void disconnectFailsPendingWithoutReplay();
    void hostErrorIsVisibleAndClearedOnNextRequest();
    void layoutAndPreparationOperationsAreTypedAndCorrelated();
    void startPolicyRejectsStaleDeferredContexts();
    void temperatureOverrideDispatchCarriesExpectedProfileAndOverride();
    void brewSettingsRequestsCarrySnapshotCasAndContext();
    void espressoActionsCarryContextAndExactPayloads();
    void presentationEventsBaselineReconnectAndReportGaps();
    void espressoActionPolicyRejectsStaleContextAndUnsafePhases();
};

void tst_MachineControlService::notReadyFailureIsQueued()
{
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QVERIFY(!service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QVariantMap expectedPreset{{QStringLiteral("name"), QStringLiteral("Pitcher")}};
    const QString requestId = service->startSteam(QStringLiteral("context"), 1, expectedPreset);
    QVERIFY(!requestId.isEmpty());
    QVERIFY(!service->busy());
    QCOMPARE(completed.size(), 0); // The caller can store its request ID first.
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toString(), requestId);
    QCOMPARE(completed.first().at(1).toString(), QStringLiteral("start-steam"));
    QVERIFY(!completed.first().at(2).toBool());
    QVERIFY(completed.first().at(3).toString().contains(QStringLiteral("not ready"), Qt::CaseInsensitive));
}

void tst_MachineControlService::completionIsCorrelatedAndQueued()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("machine-control-test:///native")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    MachineControlService::setPublishedNode(&host);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    source.setSelectedRecipeId(44);
    QTRY_COMPARE(service->selectedRecipeId(), qint64(44));
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString requestId = service->startRecipe(QStringLiteral("ctx-1"), 44);
    QVERIFY(!requestId.isEmpty());
    QCOMPARE(completed.size(), 0);
    QTRY_COMPARE(source.calls, 1);
    QVERIFY(service->busy());
    QCOMPARE(source.requestId, requestId);
    QCOMPARE(source.operation, QStringLiteral("start-recipe"));
    QCOMPARE(source.fields.value(QStringLiteral("recipeId")).toLongLong(), qint64(44));
    QCOMPARE(source.expectedContextId, QStringLiteral("ctx-1"));

    source.complete(QStringLiteral("not-the-request"), source.operation, true);
    source.complete(requestId, QStringLiteral("start-espresso"), true);
    QCoreApplication::processEvents();
    QCOMPARE(completed.size(), 0); // Unknown IDs and operation mismatches are ignored.
    QVERIFY(service->busy());

    source.complete(requestId, source.operation, true);
    QCOMPARE(completed.size(), 0); // Facade completion is queued after source notification.
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toString(), requestId);
    QVERIFY(completed.first().at(2).toBool());
    QVERIFY(completed.first().at(4).toMap().value(QStringLiteral("dispatched")).toBool());
    QVERIFY(!service->busy());
}

void tst_MachineControlService::disconnectFailsPendingWithoutReplay()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    QRemoteObjectNode clientNode;
    QVERIFY(clientNode.connectToNode(host.hostUrl()));
    MachineControlService::setPublishedNode(&clientNode);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString requestId = service->stopEspresso(QStringLiteral("ctx-2"));
    QTRY_COMPARE(source.calls, 1);
    QVERIFY(service->busy());
    QCOMPARE(source.operation, QStringLiteral("stop-espresso"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("ctx-2"));
    QVERIFY(host.disableRemoting(&source));
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toString(), requestId);
    QVERIFY(!completed.first().at(2).toBool());
    QVERIFY(completed.first().at(3).toString().contains(QStringLiteral("not retried")));
    QVERIFY(!service->busy());
    QTRY_VERIFY(!service->ready());

    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    QTRY_VERIFY(service->ready());
    const QString nextId = service->skipFrame(QStringLiteral("ctx-3"));
    QTRY_COMPARE(source.calls, 2);
    QCOMPARE(source.requestIds, (QStringList{requestId, nextId}));
    QCOMPARE(source.operation, QStringLiteral("skip-frame"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("ctx-3"));
    source.complete(nextId, QStringLiteral("skip-frame"), true);
    QTRY_COMPARE(completed.size(), 2);

    const QString skipStepId = service->skipDrinkStep(QStringLiteral("ctx-4"));
    QTRY_COMPARE(source.calls, 3);
    QCOMPARE(source.operation, QStringLiteral("skip-drink-step"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("ctx-4"));
    source.complete(skipStepId, QStringLiteral("skip-drink-step"), true);
    QTRY_COMPARE(completed.size(), 3);

    const QString purgeId = service->startSteamPurge(QStringLiteral("ctx-5"));
    QTRY_COMPARE(source.calls, 4);
    QCOMPARE(source.operation, QStringLiteral("start-steam-purge"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("ctx-5"));
    source.complete(purgeId, QStringLiteral("start-steam-purge"), true);
    QTRY_COMPARE(completed.size(), 4);
}

void tst_MachineControlService::hostErrorIsVisibleAndClearedOnNextRequest()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    QRemoteObjectNode clientNode;
    QVERIFY(clientNode.connectToNode(host.hostUrl()));
    MachineControlService::setPublishedNode(&clientNode);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString failedId = service->startEspresso(QStringLiteral("ctx"), QStringLiteral("default.tcl"));
    QTRY_COMPARE(source.calls, 1);
    source.complete(failedId, QStringLiteral("start-espresso"), false, QStringLiteral("GHC is active"));
    QTRY_COMPARE(completed.size(), 1);
    QVERIFY(!completed.first().at(2).toBool());
    QCOMPARE(service->errorMessage(), QStringLiteral("GHC is active"));

    const QVariantMap expectedPreset{{QStringLiteral("name"), QStringLiteral("Flush")}};
    const QString nextId = service->selectFlushPreset(0, expectedPreset);
    QVERIFY(!nextId.isEmpty());
    QTRY_COMPARE(source.calls, 2);
    QVERIFY(service->errorMessage().isEmpty());
    source.complete(nextId, QStringLiteral("select-flush-preset"), true);
    QTRY_COMPARE(completed.size(), 2);
    QVERIFY(completed.last().at(2).toBool());
}

void tst_MachineControlService::layoutAndPreparationOperationsAreTypedAndCorrelated()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("machine-control-test:///layout")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    MachineControlService::setPublishedNode(&host);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString sleepId = service->sleepMachine(QStringLiteral("sleep-context"));
    QTRY_COMPARE(source.calls, 1);
    QCOMPARE(source.operation, QStringLiteral("sleep-machine"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("sleep-context"));
    source.complete(sleepId, source.operation, true);
    QTRY_COMPARE(completed.size(), 1);

    const QString flowId = service->setHotWaterFlow(QStringLiteral("water-context"), 45);
    QTRY_COMPARE(source.calls, 2);
    QCOMPARE(source.operation, QStringLiteral("set-hot-water-flow"));
    QCOMPARE(source.fields, (QVariantMap{{QStringLiteral("flow"), 45}}));
    QCOMPARE(source.expectedContextId, QStringLiteral("water-context"));
    source.complete(flowId, source.operation, true);
    QTRY_COMPARE(completed.size(), 2);

    const QString stopId = service->stopFlush(QStringLiteral("flush-context"));
    QTRY_COMPARE(source.calls, 3);
    QCOMPARE(source.operation, QStringLiteral("stop-flush"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("flush-context"));
    source.complete(stopId, source.operation, true);
    QTRY_COMPARE(completed.size(), 3);

    const QString scanId = service->scanForDevices();
    QTRY_COMPARE(source.calls, 4);
    QCOMPARE(source.operation, QStringLiteral("scan-for-devices"));
    QVERIFY(source.fields.isEmpty());
    QVERIFY(source.expectedContextId.isEmpty());
    source.complete(scanId, source.operation, false, QStringLiteral("scan unavailable"));
    QTRY_COMPARE(completed.size(), 4);
    QCOMPARE(completed.last().at(0).toString(), scanId);
    QVERIFY(!completed.last().at(2).toBool());
    QCOMPARE(completed.last().at(3).toString(), QStringLiteral("scan unavailable"));

    const QString wakeId = service->wakeMachine(QStringLiteral("wake-context"));
    QTRY_COMPARE(source.calls, 5);
    QCOMPARE(source.operation, QStringLiteral("wake-machine"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, QStringLiteral("wake-context"));
    source.complete(wakeId, source.operation, true);
    QTRY_COMPARE(completed.size(), 5);
    QCOMPARE(completed.last().at(0).toString(), wakeId);
    QVERIFY(completed.last().at(2).toBool());

    const auto verifyEmptyContextOperation = [&](const QString& id, int callCount,
                                                  const QString& expectedOperation,
                                                  const QString& expectedContext) {
        QTRY_COMPARE(source.calls, callCount);
        QCOMPARE(source.operation, expectedOperation);
        QVERIFY(source.fields.isEmpty());
        QCOMPARE(source.expectedContextId, expectedContext);
        source.complete(id, expectedOperation, true);
        QTRY_COMPARE(completed.size(), callCount);
        QCOMPARE(completed.last().at(0).toString(), id);
    };

    const QString idleId = service->requestIdle(QStringLiteral("idle-context"));
    verifyEmptyContextOperation(idleId, 6, QStringLiteral("request-idle"), QStringLiteral("idle-context"));
    const QString disconnectId = service->disconnectMachine(QStringLiteral("disconnect-context"));
    verifyEmptyContextOperation(disconnectId, 7, QStringLiteral("disconnect-machine"), QStringLiteral("disconnect-context"));
    const QString heaterId = service->toggleSteamHeater(QStringLiteral("heater-context"));
    verifyEmptyContextOperation(heaterId, 8, QStringLiteral("toggle-steam-heater"), QStringLiteral("heater-context"));
    const QString chargingId = service->toggleCharging(QStringLiteral("charging-context"));
    verifyEmptyContextOperation(chargingId, 9, QStringLiteral("toggle-charging"), QStringLiteral("charging-context"));
    const QString previousId = service->loadPreviousProfile(QStringLiteral("profile-context"));
    verifyEmptyContextOperation(previousId, 10, QStringLiteral("load-previous-profile"), QStringLiteral("profile-context"));
    const QString applyWaterId = service->applyHotWaterSettings(QStringLiteral("apply-water-context"));
    verifyEmptyContextOperation(applyWaterId, 11, QStringLiteral("apply-hot-water-settings"), QStringLiteral("apply-water-context"));
    const QString applyFlushId = service->applyFlushSettings(QStringLiteral("apply-flush-context"));
    verifyEmptyContextOperation(applyFlushId, 12, QStringLiteral("apply-flush-settings"), QStringLiteral("apply-flush-context"));
    const QString stopWaterId = service->stopHotWater(QStringLiteral("stop-water-context"));
    verifyEmptyContextOperation(stopWaterId, 13, QStringLiteral("stop-hot-water"), QStringLiteral("stop-water-context"));
    const QString fakeShotId = service->generateFakeShot(QStringLiteral("simulator-context"));
    verifyEmptyContextOperation(fakeShotId, 14, QStringLiteral("generate-fake-shot"), QStringLiteral("simulator-context"));
}

void tst_MachineControlService::startPolicyRejectsStaleDeferredContexts()
{
    QVERIFY(MachineControlStartPolicy::contextMatches(QStringLiteral("a"), QStringLiteral("a")));
    QVERIFY(!MachineControlStartPolicy::contextMatches(QString(), QStringLiteral("a")));
    QVERIFY(!MachineControlStartPolicy::contextMatches(QStringLiteral("a"), QStringLiteral("b")));

    const auto machineAllowed = [](const QString& expected, const QString& current,
                                   bool ready, bool headless, bool simulation) {
        return MachineControlStartPolicy::machineCanStart(expected, current,
                                                           ready, headless, simulation);
    };
    QVERIFY(machineAllowed(QStringLiteral("a"), QStringLiteral("a"), true, false, true));
    QVERIFY(machineAllowed(QStringLiteral("a"), QStringLiteral("a"), true, true, false));
    QVERIFY(!machineAllowed(QStringLiteral("a"), QStringLiteral("b"), true, true, true));
    QVERIFY(!machineAllowed({}, QStringLiteral("a"), true, true, true));
    QVERIFY(!machineAllowed(QStringLiteral("a"), QStringLiteral("a"), false, true, true));
    QVERIFY(!machineAllowed(QStringLiteral("a"), QStringLiteral("a"), true, false, false));
    // A purge is the one start a GHC machine accepts from the app.
    QVERIFY(MachineControlStartPolicy::purgeCanStart(QStringLiteral("a"), QStringLiteral("a"), true));
    QVERIFY(!MachineControlStartPolicy::purgeCanStart(QStringLiteral("a"), QStringLiteral("a"), false));
    QVERIFY(!MachineControlStartPolicy::purgeCanStart(QStringLiteral("a"), QStringLiteral("b"), true));

    QVERIFY(MachineControlStartPolicy::simulatedShotCanRun(true, false, false, false,
                                                            true, true, true, true));
    QVERIFY(!MachineControlStartPolicy::simulatedShotCanRun(false, false, false, false,
                                                             true, true, true, true));
    QVERIFY(!MachineControlStartPolicy::simulatedShotCanRun(true, true, false, false,
                                                             true, true, true, true));
    QVERIFY(!MachineControlStartPolicy::simulatedShotCanRun(true, false, true, false,
                                                             true, true, true, true));
    QVERIFY(!MachineControlStartPolicy::simulatedShotCanRun(true, false, false, true,
                                                             true, true, true, true));
    QVERIFY(!MachineControlStartPolicy::simulatedShotCanRun(true, false, false, false,
                                                             false, true, true, true));

    const auto recipeAllowed = [](const QString& expectedContext, const QString& currentContext,
                                  qint64 expected, qint64 selected, qint64 active,
                                  bool ready, bool headless, bool simulation) {
        return MachineControlStartPolicy::recipeCanStart(
            expectedContext, currentContext, expected, selected, active,
            ready, headless, simulation);
    };
    QVERIFY(recipeAllowed(QStringLiteral("a"), QStringLiteral("a"), 8, 8, 8,
                          true, false, true));
    QVERIFY(!recipeAllowed(QStringLiteral("a"), QStringLiteral("b"), 8, 8, 8,
                           true, false, true)); // Machine changed while activation was pending.
    QVERIFY(!recipeAllowed(QStringLiteral("a"), QStringLiteral("a"), 8, 9, 8,
                           true, false, true)); // User changed recipe before deferred dispatch.
    QVERIFY(!recipeAllowed(QStringLiteral("a"), QStringLiteral("a"), 8, 8, 7,
                           true, false, true)); // The requested recipe has not become active yet.
    QVERIFY(!recipeAllowed(QStringLiteral("a"), QStringLiteral("a"), 8, 8, 8,
                           false, true, true)); // Ready state changed while activation was pending.
}

void tst_MachineControlService::temperatureOverrideDispatchCarriesExpectedProfileAndOverride()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("machine-control-test:///temperature")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    MachineControlService::setPublishedNode(&host);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QVariantMap expectedOverride{
        {QStringLiteral("hasOverride"), true},
        {QStringLiteral("valueC"), 92.5}
    };
    const QVariantMap expectedProfile{
        {QStringLiteral("filename"), QStringLiteral("default.tcl")},
        {QStringLiteral("title"), QStringLiteral("Default")}
    };
    const QString requestId = service->applyTemperatureOverride(
        QStringLiteral("context-7"), expectedProfile, expectedOverride, 94.0);
    QVERIFY(!requestId.isEmpty());
    QTRY_COMPARE(source.calls, 1);
    QCOMPARE(source.operation, QStringLiteral("apply-temperature-override"));
    QCOMPARE(source.expectedContextId, QStringLiteral("context-7"));
    QCOMPARE(source.fields.value(QStringLiteral("expectedProfile")).toMap(), expectedProfile);
    QCOMPARE(source.fields.value(QStringLiteral("expectedOverride")).toMap(), expectedOverride);
    QCOMPARE(source.fields.value(QStringLiteral("valueC")).toDouble(), 94.0);

    source.complete(requestId, source.operation, true);
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toString(), requestId);
    QCOMPARE(completed.first().at(1).toString(), QStringLiteral("apply-temperature-override"));
    QVERIFY(completed.first().at(2).toBool());
}

void tst_MachineControlService::brewSettingsRequestsCarrySnapshotCasAndContext()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("machine-control-test:///brew-settings")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    MachineControlService::setPublishedNode(&host);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString snapshotId = service->requestBrewSettings();
    QTRY_COMPARE(source.calls, 1);
    QCOMPARE(source.operation, QStringLiteral("brew-settings-snapshot"));
    QVERIFY(source.fields.isEmpty());
    QVERIFY(source.expectedContextId.isEmpty());
    source.complete(snapshotId, source.operation, true);
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.last().at(0).toString(), snapshotId);
    QCOMPARE(completed.last().at(1).toString(), QStringLiteral("brew-settings-snapshot"));

    const QVariantMap values{
        {QStringLiteral("doseG"), 18.1},
        {QStringLiteral("grindSetting"), QStringLiteral("4.2")},
        {QStringLiteral("grindRpm"), 1200},
        {QStringLiteral("yieldMode"), QStringLiteral("ratio")},
        {QStringLiteral("yieldValue"), 2.0},
        {QStringLiteral("temperatureC"), 93.5}
    };
    const QString revision(64, QLatin1Char('a'));
    const QString applyId = service->applyBrewSettings(revision, values,
                                                        QStringLiteral("brew-context"));
    QTRY_COMPARE(source.calls, 2);
    QCOMPARE(source.operation, QStringLiteral("brew-settings-apply"));
    QCOMPARE(source.expectedContextId, QStringLiteral("brew-context"));
    QVariantMap expectedFields = values;
    expectedFields.insert(QStringLiteral("expectedRevision"), revision);
    QCOMPARE(source.fields, expectedFields);
    source.complete(applyId, source.operation, true);
    QTRY_COMPARE(completed.size(), 2);
    QCOMPARE(completed.last().at(0).toString(), applyId);
    QCOMPARE(completed.last().at(1).toString(), QStringLiteral("brew-settings-apply"));

    const QString tempId = service->updateProfileTemperature(revision, 94.0,
                                                              QStringLiteral("brew-context-2"));
    QTRY_COMPARE(source.calls, 3);
    QCOMPARE(source.operation, QStringLiteral("brew-settings-update-profile-temperature"));
    QCOMPARE(source.expectedContextId, QStringLiteral("brew-context-2"));
    QCOMPARE(source.fields,
             (QVariantMap{{QStringLiteral("expectedRevision"), revision},
                          {QStringLiteral("valueC"), 94.0}}));
    source.complete(tempId, source.operation, false, QStringLiteral("revision conflict"));
    QTRY_COMPARE(completed.size(), 3);
    QCOMPARE(completed.last().at(0).toString(), tempId);
    QCOMPARE(completed.last().at(1).toString(),
             QStringLiteral("brew-settings-update-profile-temperature"));
    QVERIFY(!completed.last().at(2).toBool());
    QCOMPARE(completed.last().at(3).toString(), QStringLiteral("revision conflict"));
}

void tst_MachineControlService::espressoActionsCarryContextAndExactPayloads()
{
    PendingMachineControlSource source;
    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("machine-control-test:///espresso-actions")),
                            QRemoteObjectHost::AllowExternalRegistration));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    MachineControlService::setPublishedNode(&host);
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    QTRY_VERIFY(service->ready());
    QSignalSpy completed(service, &MachineControlService::completed);

    const QString context = QStringLiteral("espresso-context-1");
    const QString stopId = service->stopEspresso(context);
    QTRY_COMPARE(source.calls, 1);
    QCOMPARE(source.operation, QStringLiteral("stop-espresso"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, context);
    source.complete(stopId, source.operation, true);
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.last().at(0).toString(), stopId);
    QVERIFY(completed.last().at(2).toBool());

    const QString bumpId = service->bumpTargetWeight(context, -5.0);
    QTRY_COMPARE(source.calls, 2);
    QCOMPARE(source.operation, QStringLiteral("bump-target-weight"));
    QCOMPARE(source.fields,
             (QVariantMap{{QStringLiteral("deltaG"), -5.0}}));
    QCOMPARE(source.expectedContextId, context);
    source.complete(bumpId, source.operation, true);
    QTRY_COMPARE(completed.size(), 2);
    QCOMPARE(completed.last().at(0).toString(), bumpId);

    const QString skipId = service->skipFrame(context);
    QTRY_COMPARE(source.calls, 3);
    QCOMPARE(source.operation, QStringLiteral("skip-frame"));
    QVERIFY(source.fields.isEmpty());
    QCOMPARE(source.expectedContextId, context);
    source.complete(skipId, source.operation, true);
    QTRY_COMPARE(completed.size(), 3);
    QCOMPARE(completed.last().at(0).toString(), skipId);

    const QString flushId = service->bumpFlushSeconds(QStringLiteral("flush-context-1"), 5.0);
    QTRY_COMPARE(source.calls, 4);
    QCOMPARE(source.operation, QStringLiteral("bump-flush-seconds"));
    QCOMPARE(source.fields, (QVariantMap{{QStringLiteral("deltaSec"), 5.0}}));
    QCOMPARE(source.expectedContextId, QStringLiteral("flush-context-1"));
    source.complete(flushId, source.operation, true);
    QTRY_COMPARE(completed.size(), 4);
    QCOMPARE(completed.last().at(0).toString(), flushId);
}

void tst_MachineControlService::presentationEventsBaselineReconnectAndReportGaps()
{
    PendingMachineControlSource source;
    source.setEventSourceId(QStringLiteral("machine-source-a"));
    source.setEventSequence(5);
    source.setPhase(6);
    source.setCurrentFrameIndex(2);
    source.setCurrentFrameName(QStringLiteral("Main pour"));
    source.setPendingDrinkStep(QStringLiteral("hotWater"));

    QRemoteObjectHost host;
    QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
    QVERIFY(host.enableRemoting<MachineControlStateSourceAPI>(&source));
    auto firstNode = std::make_unique<QRemoteObjectNode>();
    QVERIFY(firstNode->connectToNode(host.hostUrl()));
    QQmlEngine engine;
    MachineControlService* service = MachineControlServiceForeign::create(&engine, &engine);
    service->setNode(firstNode.get());
    QTRY_VERIFY(service->ready());
    QCOMPARE(service->phase(), 6);
    QCOMPARE(service->currentFrameIndex(), 2);
    QCOMPARE(service->currentFrameName(), QStringLiteral("Main pour"));
    QCOMPARE(service->pendingDrinkStep(), QStringLiteral("hotWater"));

    QSignalSpy events(service, &MachineControlService::presentationEvent);
    QSignalSpy missed(service, &MachineControlService::presentationEventsMissed);
    QCoreApplication::processEvents();
    QCOMPARE(events.size(), 0); // Hydrated snapshot is not replayed as an event.

    source.publishEvent(QStringLiteral("phaseChanged"),
                        {{QStringLiteral("phase"), 7}});
    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.last().at(0).toString(), QStringLiteral("machine-source-a"));
    QCOMPARE(events.last().at(1).toLongLong(), qint64(6));
    QCOMPARE(events.last().at(2).toString(), QStringLiteral("phaseChanged"));
    QCOMPARE(events.last().at(3).toMap().value(QStringLiteral("phase")).toInt(), 7);

    emit source.presentationEvent(QStringLiteral("machine-source-a"), 6,
                                  QStringLiteral("phaseChanged"),
                                  {{QStringLiteral("phase"), 7}});
    QCoreApplication::processEvents();
    QCOMPARE(events.size(), 1); // Duplicate/out-of-order events are ignored.

    source.setEventSequence(8);
    emit source.presentationEvent(QStringLiteral("machine-source-a"), 8,
                                  QStringLiteral("frameChanged"),
                                  {{QStringLiteral("frameIndex"), 3}});
    QTRY_COMPARE(missed.size(), 1);
    QCOMPARE(missed.first().first().toLongLong(), qint64(1));
    QCOMPARE(events.size(), 1); // A gap is reported; no transition is synthesized.
    source.publishEvent(QStringLiteral("shotStarted"));
    QTRY_COMPARE(events.size(), 2);
    QCOMPARE(events.last().at(1).toLongLong(), qint64(9));

    source.setEventSourceId(QStringLiteral("machine-source-b"));
    source.setEventSequence(0);
    source.publishEvent(QStringLiteral("shotEnded"));
    QCoreApplication::processEvents();
    QCOMPARE(events.size(), 2); // A replacement source hydrates without replay.
    source.publishEvent(QStringLiteral("targetWeightReached"));
    QTRY_COMPARE(events.size(), 3);
    QCOMPARE(events.last().at(0).toString(), QStringLiteral("machine-source-b"));
    QCOMPARE(events.last().at(1).toLongLong(), qint64(2));

    firstNode.reset();
    QTRY_VERIFY(!service->ready());
    source.publishEvent(QStringLiteral("shotEnded")); // sequence 3, missed while detached
    source.publishEvent(QStringLiteral("targetWeightReached")); // sequence 4

    auto secondNode = std::make_unique<QRemoteObjectNode>();
    QVERIFY(secondNode->connectToNode(host.hostUrl()));
    service->setNode(secondNode.get());
    QTRY_VERIFY(service->ready());
    source.publishEvent(QStringLiteral("frameChanged"),
                        {{QStringLiteral("frameIndex"), 4},
                         {QStringLiteral("frameName"), QStringLiteral("Finish")},
                         {QStringLiteral("transitionReason"), QStringLiteral("time")}});
    QTRY_COMPARE(missed.size(), 2);
    QCOMPARE(missed.last().first().toLongLong(), qint64(2));
    QCOMPARE(events.size(), 3); // Reconnect reports skipped event history, never replays it.
    source.publishEvent(QStringLiteral("shotStarted"));
    QTRY_COMPARE(events.size(), 4);
    QCOMPARE(events.last().at(1).toLongLong(), qint64(6));
}

void tst_MachineControlService::espressoActionPolicyRejectsStaleContextAndUnsafePhases()
{
    using MachineControlEspressoPolicy::skipAllowed;
    using MachineControlEspressoPolicy::stopAllowed;
    using MachineControlEspressoPolicy::targetAdjustmentAllowed;

    const QString context = QStringLiteral("live-context");
    QVERIFY(stopAllowed(context, context, MachinePhase::EspressoPreheating, true));
    QVERIFY(stopAllowed(context, context, MachinePhase::Preinfusion, true));
    QVERIFY(stopAllowed(context, context, MachinePhase::Pouring, true));
    QVERIFY(stopAllowed(context, context, MachinePhase::Ending, true));
    QVERIFY(!stopAllowed(context, context, MachinePhase::Heating, true));
    QVERIFY(!stopAllowed(context, context, MachinePhase::Steaming, true));
    QVERIFY(!stopAllowed(context, QStringLiteral("next-context"), MachinePhase::Pouring, true));
    QVERIFY(!stopAllowed(context, context, MachinePhase::Pouring, false));

    for (double delta : {-10.0, -5.0, 5.0, 10.0}) {
        QVERIFY(targetAdjustmentAllowed(context, context, MachinePhase::Preinfusion,
                                        true, 36.0, true, delta));
        QVERIFY(targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                        true, 36.0, true, delta));
    }
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::EspressoPreheating,
                                     true, 36.0, true, 5.0));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Ending,
                                     true, 36.0, true, 5.0));
    QVERIFY(!targetAdjustmentAllowed(context, QStringLiteral("next-context"),
                                     MachinePhase::Pouring, true, 36.0, true, 5.0));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                     false, 36.0, true, 5.0));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                     true, 0.0, true, 5.0));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                     true, 1.0, false, -5.0));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                     true, 36.0, true, 2.5));
    QVERIFY(!targetAdjustmentAllowed(context, context, MachinePhase::Pouring,
                                     true, 36.0, true,
                                     std::numeric_limits<double>::quiet_NaN()));

    QVERIFY(skipAllowed(context, context, MachinePhase::Preinfusion, true));
    QVERIFY(skipAllowed(context, context, MachinePhase::Pouring, true));
    QVERIFY(!skipAllowed(context, context, MachinePhase::EspressoPreheating, true));
    QVERIFY(!skipAllowed(context, context, MachinePhase::Ending, true));
    QVERIFY(!skipAllowed(context, QStringLiteral("next-context"), MachinePhase::Pouring, true));
    QVERIFY(!skipAllowed(context, context, MachinePhase::Pouring, false));
}

QTEST_GUILESS_MAIN(tst_MachineControlService)
#include "tst_machinecontrolservice.moc"
