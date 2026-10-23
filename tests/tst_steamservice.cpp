#include <QtTest>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
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
    using SteamStateSimpleSource::setEditorRevision;
    using SteamStateSimpleSource::setHostId;
    using SteamStateSimpleSource::setWarningSnapshot;
    using SteamStateSimpleSource::setGraphSnapshot;
    int calls = 0;
    QVariantMap lastFields;
    QVariantMap execute(QString operation, QVariantMap fields,
                        QString revision) override
    {
        ++calls;
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
