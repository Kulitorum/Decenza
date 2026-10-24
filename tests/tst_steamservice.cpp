#include <QtTest>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QRegularExpression>
#include <limits>
#include <memory>
#include "remote/steamservice_qml.h"
#include "rep_steamservice_source.h"
#include "models/steamdatamodel.h"
#include "network/steamhealthsnapshot.h"

class TestSteamSource final : public SteamStateSimpleSource
{
    Q_OBJECT
public:
    using SteamStateSimpleSource::setSteamTimeout;
    using SteamStateSimpleSource::setPlannedSteamDurationSec;
    using SteamStateSimpleSource::setEditorRevision;
    using SteamStateSimpleSource::setHostId;
    using SteamStateSimpleSource::setWarningSnapshot;
    using SteamStateSimpleSource::setGraphSnapshot;
    using SteamStateSimpleSource::setSteamPitcherPresets;
    using SteamStateSimpleSource::setSelectedSteamPitcher;
    using SteamStateSimpleSource::setSelectedSteamPitcherDisplayIndex;
    using SteamStateSimpleSource::setMilkAutoCaptureEnabled;
    using SteamStateSimpleSource::setSteamSecondsPerGram;
    using SteamStateSimpleSource::setLastSteamMilkG;
    using SteamStateSimpleSource::setDeviceConnected;
    using SteamStateSimpleSource::setSteamTemperatureActual;
    using SteamStateSimpleSource::setSteamTemperature;
    using SteamStateSimpleSource::setSteamHeaterOn;
    int calls = 0;
    QStringList operations;
    QVariantMap lastFields;
    QVariantMap execute(QString operation, QVariantMap fields,
                        QString revision) override
    {
        ++calls;
        operations.append(operation);
        lastFields = fields;
        if (revision != editorRevision())
            return {{QStringLiteral("success"), false},
                    {QStringLiteral("message"), QStringLiteral("Conflict")},
                    {QStringLiteral("newRevision"), editorRevision()}};
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("operation"), operation}}},
                {QStringLiteral("newRevision"), editorRevision()}};
    }

    void setWarnings(const SteamHealthSnapshot::Snapshot& snapshot)
    {
        setWarningSnapshot(snapshot.toJson().toVariantMap());
    }
};

class tst_SteamService : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); SteamService::setPublishedNode(nullptr); }
    void cleanup() { SteamService::setPublishedNode(nullptr); }

    void liveDetachRejectsAReentrantSteamRequest()
    {
        TestSteamSource source;
        source.setEditorRevision(QStringLiteral("revision-a"));
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        SteamService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &SteamService::completed);
        const QString oldId = service->request(QStringLiteral("old"), {}, QStringLiteral("revision-a"));
        QString reentrantId;
        bool readyDuringFailure = true;
        connect(service, &SteamService::busyChanged, service, [&] {
            if (reentrantId.isEmpty() && !service->busy()) {
                readyDuringFailure = service->ready();
                reentrantId = service->request(QStringLiteral("reentrant"), {}, QStringLiteral("revision-a"));
            }
        });
        service->setNode(nullptr);
        QVERIFY(!readyDuringFailure);
        QVERIFY(!reentrantId.isEmpty());
        QVERIFY(!service->busy());
        QTRY_COMPARE(completed.size(), 2);
        for (const auto& result : completed) {
            QVERIFY(result.at(0).toString() == oldId || result.at(0).toString() == reentrantId);
            QVERIFY(!result.at(2).toBool());
        }
        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        QVERIFY(!source.operations.contains(QStringLiteral("reentrant")));
        QVERIFY(!service->busy());
    }

    void steamAdmissionCannotOutliveItsBinding_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("detach") << 0;
        QTest::newRow("rebind") << 1;
    }

    void steamAdmissionCannotOutliveItsBinding()
    {
        QFETCH(int, change);
        TestSteamSource source;
        source.setEditorRevision(QStringLiteral("revision-a"));
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        SteamService::setPublishedNode(&node);
        QQmlEngine engine;
        QPointer<SteamService> service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service.data(), &SteamService::completed);
        bool changed = false;
        connect(service.data(), &SteamService::busyChanged, service.data(), [&] {
            if (changed || !service || !service->busy()) return;
            changed = true;
            service->setNode(nullptr);
            if (change == 1) service->setNode(&node);
        });
        const QString id = service->request(QStringLiteral("old"), {}, QStringLiteral("revision-a"));
        QVERIFY(changed);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(!service->busy());
        if (change == 1) QTRY_VERIFY(service->ready());
        QCOMPARE(source.calls, 0);
    }

    void generatedStateAndQueuedCompletion()
    {
        TestSteamSource source;
        source.setSteamTimeout(30);
        source.setEditorRevision(QStringLiteral("revision-a"));
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        QRemoteObjectNode clientNode;
        QVERIFY(clientNode.connectToNode(host.hostUrl()));
        SteamService::setPublishedNode(&clientNode);
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->state()->steamTimeout(), 30);
        source.setSteamTimeout(45);
        QTRY_COMPARE(service->state()->steamTimeout(), 45);
        QSignalSpy completed(service, &SteamService::completed);
        const QString id = service->request(QStringLiteral("test"), {}, QStringLiteral("revision-a"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(completed.count(), 0); // Caller can store its correlation ID before completion.
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.at(0).at(0).toString(), id);
        QVERIFY(completed.at(0).at(2).toBool());
        QCOMPARE(source.calls, 1);
        QVERIFY(!service->busy());
    }

    void pitcherReadoutsAndDurationUseTheSharedSteamState()
    {
        const QVariantMap pitcher{{QStringLiteral("name"), QStringLiteral("Small pitcher")},
            {QStringLiteral("duration"), 180}, {QStringLiteral("pitcherWeightG"), 310.0}};
        const QVariantMap off{{QStringLiteral("disabled"), true}, {QStringLiteral("builtin"), true}};
        TestSteamSource source;
        source.setSteamPitcherPresets({pitcher, off});
        source.setSelectedSteamPitcher(0);
        source.setSelectedSteamPitcherDisplayIndex(0);
        source.setMilkAutoCaptureEnabled(true);
        source.setSteamSecondsPerGram(0.15);
        source.setLastSteamMilkG(200.0);
        source.setSteamTimeout(65);
        source.setPlannedSteamDurationSec(65);
        source.setDeviceConnected(true);
        source.setSteamTemperatureActual(130.0);
        source.setSteamTemperature(155.0);
        source.setSteamHeaterOn(true);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QString bindings;
        const QList<QPair<QString, QStringList>> bindingSources{
            {QStringLiteral("/qml/components/SteamPlanText.qml"),
                {QStringLiteral("_preset"), QStringLiteral("_presetOff"), QStringLiteral("_pitcherName"),
                 QStringLiteral("_targetMilk"), QStringLiteral("_duration"), QStringLiteral("_milkStr"), QStringLiteral("_durStr")}},
            {QStringLiteral("/qml/components/layout/items/MilkWeightItem.qml"),
                {QStringLiteral("liveMilkG"), QStringLiteral("milkG")}},
            {QStringLiteral("/qml/components/layout/items/SteamTemperatureItem.qml"),
                {QStringLiteral("currentTemp"), QStringLiteral("targetTemp"),
                 QStringLiteral("heaterOff"), QStringLiteral("readoutText")}}
        };
        for (const auto& entry : bindingSources) {
            QFile file(QStringLiteral(DECENZA_SOURCE_DIR) + entry.first);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString sourceText = QString::fromUtf8(file.readAll());
            for (const QString& name : entry.second) {
                const QRegularExpression expression(QStringLiteral(
                    R"((readonly property \w+ %1:\s*(?:\{[\s\S]*?\n    \}|[^\n]+(?:\n {8,}[^\n]+)*)))").arg(name));
                const auto match = expression.match(sourceText);
                QVERIFY2(match.hasMatch(), qPrintable(name));
                bindings += match.captured(1) + QLatin1Char('\n');
            }
        }
        bindings.replace(QStringLiteral("SteamService."), QStringLiteral("steam."));
        bindings.replace(QStringLiteral("PreparationService.state."), QStringLiteral("preparation."));
        bindings.replace(QStringLiteral("SteamLabels.temperatureText"), QStringLiteral("temperatureText"));
        QQmlComponent planComponent(&engine);
        planComponent.setData(QStringLiteral(R"QML(
            import QtQml
            QtObject {
                id: root
                required property var steam
                property QtObject preparation: QtObject { property real sessionMeasuredMilkG: 0 }
                function temperatureText(off, temperature) { return off ? "Off" : String(temperature) }
                %1
            }
        )QML").arg(bindings).toUtf8(), QUrl());
        std::unique_ptr<QObject> plan(planComponent.createWithInitialProperties(
            {{QStringLiteral("steam"), QVariant::fromValue(service)}}));
        QVERIFY2(plan, qPrintable(planComponent.errorString()));
        QCOMPARE(plan->property("_targetMilk").toDouble(), 0.0);
        QCOMPARE(plan->property("_duration").toInt(), 0);
        QCOMPARE(plan->property("milkG").toDouble(), 0.0);
        QCOMPARE(plan->property("readoutText").toString(), QString(QChar(0x2014)));
        SteamService::setPublishedNode(&node);
        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(plan->property("_pitcherName").toString(), QStringLiteral("Small pitcher"));
        QCOMPARE(plan->property("_targetMilk").toDouble(), 200.0);
        QCOMPARE(plan->property("_duration").toInt(), 65);
        QCOMPARE(plan->property("milkG").toDouble(), 200.0);
        QCOMPARE(plan->property("currentTemp").toDouble(), 130.0);
        QCOMPARE(plan->property("targetTemp").toDouble(), 155.0);
        QCOMPARE(plan->property("readoutText").toString(), QStringLiteral("130"));
        auto* preparation = plan->property("preparation").value<QObject*>();
        QVERIFY(preparation);
        QVERIFY(preparation->setProperty("sessionMeasuredMilkG", 100.0));
        QCOMPARE(plan->property("_targetMilk").toDouble(), 100.0);
        QCOMPARE(plan->property("_duration").toInt(), 65);
        QCOMPARE(plan->property("milkG").toDouble(), 100.0);

        QCOMPARE(service->state()->steamPitcherPresets().size(), 2);
        QCOMPARE(service->state()->selectedSteamPitcher(), 0);
        QCOMPARE(service->getSteamPitcherPreset(0), pitcher);
        QVERIFY(service->steamPitcherNameTaken(QStringLiteral("  SMALL PITCHER ")));
        QVERIFY(service->steamPitcherNameTaken(QStringLiteral("heater OFF")));
        QVERIFY(!service->steamPitcherNameTaken(QStringLiteral("Small pitcher"), 0));
        // The pure preset preview remains available independently of the host
        // operation plan, which also accounts for locks and current milk.
        QCOMPARE(service->state()->lastSteamMilkG(), 200.0);
        QCOMPARE(service->effectiveSteamDurationSec(0, 100.0), 15);
        QCOMPARE(service->effectiveSteamDurationSec(0, 1000.0), 120);
        QCOMPARE(service->effectiveSteamDurationSec(0, std::numeric_limits<double>::max()), 120);
        QCOMPARE(service->effectiveSteamDurationSec(0, 0.0), 180);
        QCOMPARE(service->effectiveSteamDurationSec(0, std::numeric_limits<double>::quiet_NaN()), 180);
        QCOMPARE(service->effectiveSteamDurationSec(-1, 100.0), 0);

        source.setSteamSecondsPerGram(0.25);
        QTRY_COMPARE(service->effectiveSteamDurationSec(0, 100.0), 25);
        // A historical/captured milk label cannot override the host's chosen
        // timeout. Only the host's preparation policy publishes a new duration.
        QCOMPARE(plan->property("_duration").toInt(), 65);
        source.setPlannedSteamDurationSec(25);
        QTRY_COMPARE(plan->property("_duration").toInt(), 25);
        source.setMilkAutoCaptureEnabled(false);
        QTRY_COMPARE(service->effectiveSteamDurationSec(0, 100.0), 180);
        QCOMPARE(plan->property("_duration").toInt(), 25);
        source.setSteamTimeout(47);
        source.setPlannedSteamDurationSec(47);
        QTRY_COMPARE(plan->property("_duration").toInt(), 47);
        QVariantMap renamed = pitcher;
        renamed.insert(QStringLiteral("name"), QStringLiteral("Large pitcher"));
        renamed.insert(QStringLiteral("duration"), 45);
        source.setSteamPitcherPresets({renamed, off});
        QTRY_COMPARE(service->getSteamPitcherPreset(0), renamed);
        QTRY_COMPARE(plan->property("_pitcherName").toString(), QStringLiteral("Large pitcher"));
        QCOMPARE(plan->property("_duration").toInt(), 47);
        QCOMPARE(service->effectiveSteamDurationSec(0, 100.0), 45);
        source.setSelectedSteamPitcher(-1);
        source.setSelectedSteamPitcherDisplayIndex(1);
        source.setPlannedSteamDurationSec(0);
        QTRY_COMPARE(service->state()->selectedSteamPitcherDisplayIndex(), 1);
        QVERIFY(service->getSteamPitcherPreset(-1).value(QStringLiteral("disabled")).toBool());
        QTRY_VERIFY(plan->property("_presetOff").toBool());
        QTRY_COMPARE(plan->property("_duration").toInt(), 0);

        renamed.insert(QStringLiteral("duration"), std::numeric_limits<double>::max());
        source.setSteamPitcherPresets({renamed, off});
        QTRY_COMPARE(service->effectiveSteamDurationSec(0, 100.0), 0);
        QVERIFY(host.disableRemoting(&source));
        QTRY_VERIFY(!service->ready());
        QCOMPARE(plan->property("_targetMilk").toDouble(), 0.0);
        QCOMPARE(plan->property("readoutText").toString(), QString(QChar(0x2014)));
        QCOMPARE(plan->property("milkG").toDouble(), 100.0); // Still owned by PreparationService.
        QVERIFY(preparation->setProperty("sessionMeasuredMilkG", 0.0));
        QCOMPARE(plan->property("milkG").toDouble(), 0.0);
        service->setNode(nullptr);
        QVERIFY(!service->ready());
        QVERIFY(service->getSteamPitcherPreset(0).isEmpty());
        QCOMPARE(service->effectiveSteamDurationSec(0, 100.0), 0);
        QCOMPARE(plan->property("_targetMilk").toDouble(), 0.0);
        QCOMPARE(plan->property("_duration").toInt(), 0);
        QCOMPARE(plan->property("_pitcherName").toString(), QString());
    }

    void conflictAndDisconnectNeverReplay()
    {
        TestSteamSource source;
        source.setEditorRevision(QStringLiteral("revision-b"));
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        QRemoteObjectNode clientNode;
        QVERIFY(clientNode.connectToNode(host.hostUrl()));
        SteamService::setPublishedNode(&clientNode);
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &SteamService::completed);
        service->request(QStringLiteral("test"), {}, QStringLiteral("old-revision"));
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.at(0).at(2).toBool());
        const QString id = service->request(QStringLiteral("test"), {}, QStringLiteral("revision-b"));
        service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.at(1).at(0).toString(), id);
        QVERIFY(!completed.at(1).at(2).toBool());
        QVERIFY(!service->ready());
        QVERIFY(!service->busy());
        // Detaching may race with a request already sent to the host. Let that
        // original request settle, then ensure reconnect does not replay it.
        QTRY_COMPARE(source.calls, 2);
        service->setNode(&clientNode);
        QTRY_VERIFY(service->ready());
        QCoreApplication::sendPostedEvents();
        QCOMPARE(source.calls, 2);
        QCOMPARE(completed.count(), 2);
    }

    void reentrantBindingKeepsTheNewHostsGraphAndConnections_data()
    {
        QTest::addColumn<QString>("trigger");
        for (const char* trigger : {"disconnect-busy", "graph-cleared", "state", "ready"})
            QTest::newRow(trigger) << QString::fromLatin1(trigger);
    }

    void reentrantBindingKeepsTheNewHostsGraphAndConnections()
    {
        QFETCH(QString, trigger);
        TestSteamSource oldSource;
        TestSteamSource nextSource;
        oldSource.setEditorRevision(QStringLiteral("revision-a"));
        nextSource.setEditorRevision(QStringLiteral("revision-b"));
        oldSource.setHostId(QStringLiteral("host-a"));
        nextSource.setHostId(QStringLiteral("host-b"));
        oldSource.setWarnings({});
        nextSource.setWarnings({});
        const QVariant point = QVariant::fromValue(QVariantList{
            QVariant::fromValue(QVariantList{0.0, 6.0})});
        nextSource.setGraphSnapshot({{QStringLiteral("revision"), 8},
            {QStringLiteral("rawTime"), 0.0}, {QStringLiteral("pressure"), point},
            {QStringLiteral("flow"), point}, {QStringLiteral("temperature"), point},
            {QStringLiteral("flowGoalPoints"), QVariantList{}}});
        QRemoteObjectHost oldHost;
        QRemoteObjectHost nextHost;
        QVERIFY(oldHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(nextHost.setHostUrl(QUrl(QStringLiteral("steam-test:///reentrant-graph")),
                                   QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(oldHost.enableRemoting<SteamStateSourceAPI>(&oldSource));
        QVERIFY(nextHost.enableRemoting<SteamStateSourceAPI>(&nextSource));
        QRemoteObjectNode oldNode;
        QVERIFY(oldNode.connectToNode(oldHost.hostUrl()));
        SteamService::setPublishedNode(&oldNode);
        QQmlEngine engine;
        auto* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        bool rebound = false;
        const auto rebind = [&] {
            if (rebound) return;
            rebound = true;
            SteamService::setPublishedNode(&nextHost);
            service->setNode(&nextHost);
        };
        QSignalSpy completed(service, &SteamService::completed);
        bool disconnecting = false;
        if (trigger == QLatin1String("disconnect-busy")) {
            connect(service, &SteamService::busyChanged, this, [&] {
                if (service->busy() && !disconnecting) {
                    disconnecting = true;
                    QVERIFY(oldHost.disableRemoting(&oldSource));
                    QTRY_VERIFY(rebound);
                } else if (!service->busy() && disconnecting) {
                    rebind();
                }
            });
            const QString id = service->request(QStringLiteral("old"), {}, QStringLiteral("revision-a"));
            QTRY_COMPARE(completed.size(), 1);
            QCOMPARE(completed.first().at(0).toString(), id);
            QVERIFY(!completed.first().at(2).toBool());
            QCOMPARE(oldSource.calls, 0);
        } else {
            if (trigger == QLatin1String("graph-cleared"))
                connect(service->graph(), &SteamDataModel::cleared, this, rebind);
            else if (trigger == QLatin1String("state"))
                connect(service, &SteamService::stateChanged, this, rebind);
            else
                connect(service, &SteamService::readyChanged, this, rebind);
            service->setNode(nullptr);
        }
        QVERIFY(rebound);
        QVERIFY(service->ready());
        QCOMPARE(service->state()->hostId(), QStringLiteral("host-b"));
        QCOMPARE(service->graph()->sampleCount(), 1);
        QCOMPARE(service->graph()->pressureData().first(), QPointF(0.0, 6.0));
        QVERIFY(service->errorMessage().isEmpty());
        QCoreApplication::processEvents();
        QSignalSpy stateChanged(service, &SteamService::stateChanged);
        nextSource.setEditorRevision(QStringLiteral("revision-b-next"));
        QTRY_COMPARE(stateChanged.size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(stateChanged.size(), 1);
    }

    void warningObserverCanRebindWithoutAdvancingAnotherHostsCursor_data()
    {
        QTest::addColumn<bool>("fromMissedEvents");
        QTest::newRow("warning-handler") << false;
        QTest::newRow("missed-events-handler") << true;
    }

    void warningObserverCanRebindWithoutAdvancingAnotherHostsCursor()
    {
        QFETCH(bool, fromMissedEvents);
        TestSteamSource oldSource;
        TestSteamSource nextSource;
        oldSource.setHostId(QStringLiteral("host-a"));
        nextSource.setHostId(QStringLiteral("host-b"));
        SteamHealthSnapshot::Snapshot oldWarnings;
        SteamHealthSnapshot::Snapshot nextWarnings;
        const auto append = [](SteamHealthSnapshot::Snapshot* snapshot, qint64 id,
                               SteamHealthSnapshot::Kind kind) {
            SteamHealthSnapshot::Warning event;
            event.id = id;
            event.sessionId = QStringLiteral("c8a5f76f-049b-43fe-9f77-214a5a15eb62");
            event.kind = kind;
            return SteamHealthSnapshot::appendBoundedEvent(snapshot, event);
        };
        QVERIFY(append(&oldWarnings, 1, SteamHealthSnapshot::Kind::PressureTooHigh));
        QVERIFY(append(&nextWarnings, 100, SteamHealthSnapshot::Kind::PressureTooHigh));
        oldSource.setWarnings(oldWarnings);
        nextSource.setWarnings(nextWarnings);
        QRemoteObjectHost oldHost;
        QRemoteObjectHost nextHost;
        QVERIFY(oldHost.setHostUrl(QUrl(QStringLiteral("steam-test:///old")),
                                  QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(nextHost.setHostUrl(QUrl(QStringLiteral("steam-test:///next")),
                                   QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(oldHost.enableRemoting<SteamStateSourceAPI>(&oldSource));
        QVERIFY(nextHost.enableRemoting<SteamStateSourceAPI>(&nextSource));
        SteamService::setPublishedNode(&oldHost);
        QQmlEngine engine;
        auto* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QSignalSpy pressure(service, &SteamService::pressureTooHigh);
        QSignalSpy temperature(service, &SteamService::temperatureTooHigh);
        bool rebound = false;
        const auto rebind = [&] {
            if (rebound) return;
            rebound = true;
            SteamService::setPublishedNode(&nextHost);
            service->setNode(&nextHost);
        };
        if (fromMissedEvents)
            connect(service, &SteamService::warningEventsMissed, this, rebind);
        else
            connect(service, &SteamService::pressureTooHigh, this, rebind);
        if (fromMissedEvents) oldWarnings = {};
        const qint64 firstId = fromMissedEvents ? 10 : 2;
        QVERIFY(append(&oldWarnings, firstId, SteamHealthSnapshot::Kind::PressureTooHigh));
        QVERIFY(append(&oldWarnings, firstId + 1, SteamHealthSnapshot::Kind::TemperatureTooHigh));
        oldSource.setWarnings(oldWarnings);
        QTRY_VERIFY(rebound);
        QTRY_COMPARE(service->state()->hostId(), QStringLiteral("host-b"));
        QCOMPARE(pressure.size(), fromMissedEvents ? 0 : 1);
        QCOMPARE(temperature.size(), 0);
        QVERIFY(append(&nextWarnings, 101, SteamHealthSnapshot::Kind::PressureTooHigh));
        nextSource.setWarnings(nextWarnings);
        QTRY_COMPARE(pressure.size(), fromMissedEvents ? 1 : 2);
        QCOMPARE(temperature.size(), 0);
    }

    void warningLedgerBaselinesAndResumesByHost()
    {
        TestSteamSource source;
        source.setHostId(QStringLiteral("host-a"));
        SteamHealthSnapshot::Snapshot warnings;
        SteamHealthSnapshot::Warning initial;
        initial.id = 1;
        initial.sessionId = QStringLiteral("c8a5f76f-049b-43fe-9f77-214a5a15eb62");
        initial.kind = SteamHealthSnapshot::Kind::PressureTooHigh;
        QVERIFY(SteamHealthSnapshot::appendBoundedEvent(&warnings, initial));
        source.setWarnings(warnings);

        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        auto clientNode = std::make_unique<QRemoteObjectNode>();
        QVERIFY(clientNode->connectToNode(host.hostUrl()));
        SteamService::setPublishedNode(clientNode.get());
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QSignalSpy pressure(service, &SteamService::pressureTooHigh);
        QSignalSpy missed(service, &SteamService::warningEventsMissed);
        QTRY_VERIFY(service->ready());

        auto addPressure = [&warnings](qint64 id) {
            SteamHealthSnapshot::Warning event;
            event.id = id;
            event.sessionId = QStringLiteral("c8a5f76f-049b-43fe-9f77-214a5a15eb62");
            event.kind = SteamHealthSnapshot::Kind::PressureTooHigh;
            QVERIFY(SteamHealthSnapshot::appendBoundedEvent(&warnings, event));
        };

        QCoreApplication::processEvents();
        QCOMPARE(pressure.count(), 0); // First snapshot is a baseline.

        service->setNode(nullptr);
        clientNode.reset();
        addPressure(2);
        source.setWarnings(warnings);
        clientNode = std::make_unique<QRemoteObjectNode>();
        QVERIFY(clientNode->connectToNode(host.hostUrl()));
        service->setNode(clientNode.get());
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(pressure.count(), 1); // Same host resumes after the last cursor.
        QCOMPARE(missed.count(), 0);

        service->setNode(nullptr);
        clientNode.reset();
        addPressure(3);
        source.setWarnings(warnings);
        source.setHostId(QStringLiteral("host-b"));
        clientNode = std::make_unique<QRemoteObjectNode>();
        QVERIFY(clientNode->connectToNode(host.hostUrl()));
        service->setNode(clientNode.get());
        QTRY_VERIFY(service->ready());
        QCoreApplication::processEvents();
        QCOMPARE(pressure.count(), 1); // A new host starts from a clean baseline.

        addPressure(4);
        source.setWarnings(warnings);
        QTRY_COMPARE(pressure.count(), 2);

        warnings.events.clear();
        warnings.oldestEventId = 10;
        warnings.latestEventId = 11;
        for (qint64 id : {10, 11})
            addPressure(id);
        source.setWarnings(warnings);
        QTRY_COMPARE(missed.count(), 1);
        QCOMPARE(missed.at(0).at(0).toInt(), 5);
        QTRY_COMPARE(pressure.count(), 4);
    }

    void graphSnapshotsPreserveNestedSeriesPairs()
    {
        TestSteamSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("steam-test:///service")),
                                QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        SteamService::setPublishedNode(&host);
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        const auto pair = [](double x, double y) {
            return QVariant::fromValue(QVariantList{x, y});
        };
        const QVariantList pressure{pair(0.0, 1.0), pair(0.25, 2.0)};
        const QVariantList flow{pair(0.0, 3.0), pair(0.25, 4.0)};
        const QVariantList temperature{pair(0.0, 90.0), pair(0.25, 91.0)};
        source.setGraphSnapshot({{QStringLiteral("revision"), 2},
                                 {QStringLiteral("rawTime"), 0.25},
                                 {QStringLiteral("pressure"), pressure},
                                 {QStringLiteral("flow"), flow},
                                 {QStringLiteral("temperature"), temperature},
                                 {QStringLiteral("flowGoalPoints"), QVariantList{}}});

        QTRY_COMPARE(service->graph()->sampleCount(), 2);
        QCOMPARE(service->graph()->pressureData().at(1), QPointF(0.25, 2.0));
        QCOMPARE(service->graph()->flowData().at(1), QPointF(0.25, 4.0));
        QCOMPARE(service->graph()->temperatureData().at(1), QPointF(0.25, 91.0));
    }

    void nodeDestructionInvalidatesFacade()
    {
        TestSteamSource source;
        const QVariant points = QVariant::fromValue(
            QVariantList{QVariant::fromValue(QVariantList{0.0, 1.0})});
        source.setGraphSnapshot({{QStringLiteral("revision"), 1},
                                 {QStringLiteral("rawTime"), 0.0},
                                 {QStringLiteral("pressure"), points},
                                 {QStringLiteral("flow"), points},
                                 {QStringLiteral("temperature"), points},
                                 {QStringLiteral("flowGoalPoints"), QVariantList{}}});
        auto host = std::make_unique<QRemoteObjectHost>();
        QVERIFY(host->setHostUrl(QUrl(QStringLiteral("steam-test:///service")),
                                 QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(host->enableRemoting<SteamStateSourceAPI>(&source));
        SteamService::setPublishedNode(host.get());
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(service->graph()->sampleCount(), 1);

        host.reset();
        QCoreApplication::processEvents();
        // A destroyed node must not leave a valid-looking facade backed by its
        // obsolete replica; callers can safely request the disconnected state.
        service->setNode(nullptr);
        QTRY_VERIFY(!service->ready());
        QVERIFY(service->graph()->sampleCount() == 0);
    }

    void hostIdentityChangeIsObservedBeforeNextWarning()
    {
        TestSteamSource source;
        source.setHostId(QStringLiteral("host-a"));
        SteamHealthSnapshot::Snapshot warnings;
        auto addPressure = [&warnings](qint64 id) {
            SteamHealthSnapshot::Warning event;
            event.id = id;
            event.sessionId = QStringLiteral("c8a5f76f-049b-43fe-9f77-214a5a15eb62");
            event.kind = SteamHealthSnapshot::Kind::PressureTooHigh;
            QVERIFY(SteamHealthSnapshot::appendBoundedEvent(&warnings, event));
        };
        addPressure(10);
        source.setWarnings(warnings);

        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("steam-test:///service")),
                                QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(host.enableRemoting<SteamStateSourceAPI>(&source));
        SteamService::setPublishedNode(&host);
        QQmlEngine engine;
        SteamService* service = SteamServiceForeign::create(&engine, &engine);
        QSignalSpy pressure(service, &SteamService::pressureTooHigh);
        QTRY_VERIFY(service->ready());
        QCOMPARE(pressure.count(), 0); // Existing events are not replayed.

        // Change only the host identity first, leaving warningSnapshot intact.
        source.setHostId(QStringLiteral("host-b"));
        QTRY_COMPARE(service->state()->hostId(), QStringLiteral("host-b"));
        addPressure(11);
        source.setWarnings(warnings);
        QTRY_COMPARE(pressure.count(), 1);
    }
};

QTEST_MAIN(tst_SteamService)
#include "tst_steamservice.moc"
