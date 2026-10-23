#include <QtTest>

#include <QQmlEngine>
#include <QFile>
#include <QJSEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QUuid>
#include <QUrl>

#include <utility>

#include "remote/shotreviewactions.h"
#include "remote/shotreviewactions_qml.h"
#include "rep_shotreviewactions_source.h"

class ReviewActionFixtureSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    using ShotReviewActionStateSimpleSource::setDecentLastSerial;
    struct Call {
        QString requestId;
        QString operation;
        QVariantMap arguments;
    };

    QList<Call> calls;

    void execute(QString requestId, QString operation, QVariantMap arguments) override
    {
        calls.append({std::move(requestId), std::move(operation), std::move(arguments)});
    }

    void complete(const Call& call, bool success = true)
    {
        emit actionCompleted(call.requestId, call.operation, success, {},
                             success ? QString() : QStringLiteral("fixture rejection"));
    }
};

class ShotReviewActionsTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void queuedAnchorCannotFollowNavigationToAnotherShot()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        // Execute the shipping page functions, with only their service and page
        // state supplied by the fixture, as the source-function tests elsewhere do.
        const auto extract = [&source](const QString& name) {
            const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
            const qsizetype start = source.indexOf(needle);
            if (start < 0 || source.indexOf(needle, start + 1) >= 0) return QString();
            int depth = 0;
            for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
                if (source[i] == QLatin1Char('{')) ++depth;
                else if (source[i] == QLatin1Char('}') && --depth == 0)
                    return source.mid(start, i - start + 1);
            }
            return QString();
        };
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("markGrindAnchor"),
                                    QStringLiteral("_sendPendingGrindAnchor"),
                                    QStringLiteral("_loadSteppedShot")}) {
            const QString function = extract(name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const QJSValue fixture = engine.evaluate(QStringLiteral(R"JS(
            var EquipmentService = {
                calls: [],
                requestSetGrindAnchor: function(shot, side, pkg) { this.calls.push([shot, side, pkg]); }
            };
            var ShotReviewActions = { releaseShotEdits: function() {}, holdShotEdits: function() {} };
            var loadShotForEditing = function() {};
            var hasUnsavedChanges = false, _saveFailed = false;
            var editShotId = 101, editEquipmentId = 7, _heldShotId = 101;
            var _pendingAnchorSide = "", _pendingAnchorShotId = -1, _pendingAnchorPackageId = -1;
            var _pendingMetadataWrites = { save: { shotId: 101 } };
        )JS"));
        QVERIFY2(!fixture.isError(), qPrintable(fixture.toString()));
        const auto run = [&engine](const QString& script) { return engine.evaluate(script); };

        QJSValue result = run(QStringLiteral("markGrindAnchor('fine'); _loadSteppedShot(102); _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 0);
        QCOMPARE(run(QStringLiteral("_pendingAnchorSide")).toString(), QString());
        QCOMPARE(run(QStringLiteral("_pendingAnchorShotId")).toInt(), -1);

        // Dispatch itself also rejects a changed selection, independently of the
        // normal stepping path clearing the queued intent.
        result = run(QStringLiteral("editShotId = 101; _pendingMetadataWrites = { save: {} }; markGrindAnchor('coarse'); editShotId = 102; _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 0);

        // Staying on the shot sends exactly the captured shot/package after save.
        result = run(QStringLiteral("editShotId = 101; _pendingMetadataWrites = { save: {} }; markGrindAnchor('fine'); _pendingMetadataWrites = {}; _sendPendingGrindAnchor(); JSON.stringify(EquipmentService.calls)"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toString(), QStringLiteral("[[101,\"fine\",7]]"));
        result = run(QStringLiteral("_loadSteppedShot(102); EquipmentService.calls.length"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(result.toInt(), 1); // Navigation leaves the already-sent host write alone.
    }

    void refractometerCallsStayCorrelatedAndHostOwned()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QSignalSpy stateChanges(actions, &ShotReviewActions::stateChanged);
        QTRY_VERIFY(actions->ready());

        const qsizetype stateChangeBaseline = stateChanges.count();
        source.setDecentLastSerial(QStringLiteral("updated-serial"));
        QTRY_COMPARE(actions->decentLastSerial(), QStringLiteral("updated-serial"));
        QVERIFY(stateChanges.count() > stateChangeBaseline);

        // These calls travel to the host as correlated commands. The test source
        // records them but does not scan or connect any physical BLE device.
        actions->setRefractometerHunt(true);
        actions->scanForDevices();
        actions->requestRefractometerMeasurement();
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(0).operation, QStringLiteral("refractometerHunt"));
        QCOMPARE(source.calls.at(0).arguments,
                 (QVariantMap{{QStringLiteral("active"), true}}));
        QCOMPARE(source.calls.at(1).operation, QStringLiteral("scanForDevices"));
        QVERIFY(source.calls.at(1).arguments.isEmpty());
        QCOMPARE(source.calls.at(2).operation,
                 QStringLiteral("requestRefractometerMeasurement"));
        QVERIFY(source.calls.at(2).arguments.isEmpty());

        // Completing out of order cannot cross-route the operation or its ID.
        source.complete(source.calls.at(2));
        source.complete(source.calls.at(0));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.at(0).at(0).toString(), source.calls.at(2).requestId);
        QCOMPARE(completed.at(0).at(1).toString(),
                 QStringLiteral("requestRefractometerMeasurement"));
        QCOMPARE(completed.at(1).at(0).toString(), source.calls.at(0).requestId);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("refractometerHunt"));

        // A response for an unknown request is ignored; it cannot complete the
        // still-pending scan command.
        emit source.actionCompleted(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                    QStringLiteral("scanForDevices"), true, {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 2);
        source.complete(source.calls.at(1), false);
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), source.calls.at(1).requestId);
        QVERIFY(!completed.last().at(2).toBool());

    }

    void disconnectCompletesPendingActionWithoutReplay()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QTRY_VERIFY(actions->ready());

        actions->scanForDevices();
        QTRY_COMPARE(source.calls.size(), 1);
        const auto call = source.calls.first();
        actions->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), call.requestId);
        QVERIFY(!completed.first().at(2).toBool());

        source.complete(call);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(source.calls.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ShotReviewActionsTest)
#include "tst_shotreviewactions.moc"
