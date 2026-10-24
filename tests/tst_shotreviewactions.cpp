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
#include "remote/shotreviewsessions.h"
#include "rep_shotreviewactions_source.h"

class ReviewActionFixtureSource final : public ShotReviewActionStateSimpleSource
{
    Q_OBJECT
public:
    using ShotReviewActionStateSimpleSource::setDecentLastSerial;
    using ShotReviewActionStateSimpleSource::setRefractometerOwnerReviewId;
    using ShotReviewActionStateSimpleSource::setRefractometerMeasuring;
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
private:
    static QString id(quint64 value)
    {
        return QStringLiteral("00000000-0000-4000-8000-%1")
            .arg(value, 12, 16, QLatin1Char('0'));
    }

    static QString sourceFunction(const QString& source, const QString& name)
    {
        const QString needle = QStringLiteral("function ") + name + QLatin1Char('(');
        const qsizetype start = source.indexOf(needle);
        if (start < 0 || source.indexOf(needle, start + 1) >= 0)
            return {};
        int depth = 0;
        for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i >= 0 && i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{'))
                ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0)
                return source.mid(start, i - start + 1);
        }
        return {};
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void reviewLeasesAreBoundToClientViewAndShot()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(2), id(102), 22), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::Existing);
        QCOMPARE(sessions.hold(id(1), id(101), 22), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(2), id(101), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(sessions.owns(id(1), id(101), 11));
        QVERIFY(!sessions.owns(id(1), id(101), 22));
        QVERIFY(!sessions.owns(id(2), id(101), 11));
        QVERIFY(!sessions.hasActiveReviews());

        QVERIFY(sessions.setActive(id(1), id(101), true));
        QCOMPARE(sessions.measurementOwner(), id(101));
        QVERIFY(sessions.setActive(id(2), id(102), true));
        QCOMPARE(sessions.measurementOwner(), id(101));
        QVERIFY(sessions.setActive(id(1), id(101), false));
        QCOMPARE(sessions.measurementOwner(), id(102));
        QCOMPARE(sessions.release(id(2), id(101)), qint64(0));
        QCOMPARE(sessions.release(id(1), id(101)), qint64(11));
        QCOMPARE(sessions.release(id(2), id(102)), qint64(22));
        QVERIFY(!sessions.hasActiveReviews());
        QVERIFY(sessions.measurementOwner().isEmpty());
    }

    void sharedShotReleasesOnlyAfterTheFinalView()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(102), 11), ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.hold(id(2), id(103), 11), ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.release(id(1), id(101)), qint64(0));
        QVERIFY(sessions.closeClient(id(1)).isEmpty());
        QVERIFY(sessions.owns(id(2), id(103), 11));
        QCOMPARE(sessions.closeClient(id(2)), QList<qint64>{11});
        QVERIFY(sessions.closeClient(id(2)).isEmpty());
        QCOMPARE(sessions.hold(id(1), id(104), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.setActive(id(1), id(102), true));
        QVERIFY(!sessions.beginMeasurement(id(1), id(102)));
    }

    void nativeReviewHasIdlePreferenceButExplicitMeasurementClaimsItsView()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2), true);
        QCOMPARE(sessions.hold(id(1), id(300), 11), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(1), id(200), 22), ShotReviewSessions::Hold::FirstForShot);
        QCOMPARE(sessions.hold(id(2), id(400), 33), ShotReviewSessions::Hold::FirstForShot);
        QVERIFY(sessions.setActive(id(1), id(300), true));
        QVERIFY(sessions.setActive(id(1), id(200), true));
        QCOMPARE(sessions.measurementOwner(), id(300));
        QVERIFY(sessions.setActive(id(2), id(400), true));
        QCOMPARE(sessions.measurementOwner(), id(400));

        QVERIFY(sessions.beginMeasurement(id(1), id(200)));
        QCOMPARE(sessions.measurementOwner(), id(200));
        QVERIFY(sessions.measuring());
        sessions.noteMeasurementStarted();
        QVERIFY(!sessions.beginMeasurement(id(2), id(400)));
        QVERIFY(!sessions.beginMeasurement(id(1), id(200)));
        QVERIFY(sessions.setActive(id(1), id(200), false));
        QCOMPARE(sessions.measurementOwner(), id(200));
        sessions.finishMeasurement();
        QVERIFY(!sessions.measuring());
        QCOMPARE(sessions.measurementOwner(), id(400));
        QVERIFY(!sessions.beginMeasurement(id(1), id(200)));
    }

    void leavingObserversCannotRetargetAnInflightReading()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        sessions.openClient(id(2));
        sessions.hold(id(1), id(101), 11);
        sessions.hold(id(2), id(102), 22);
        QVERIFY(sessions.setActive(id(1), id(101), true));
        QVERIFY(sessions.setActive(id(2), id(102), true));
        QVERIFY(sessions.beginMeasurement(id(1), id(101)));
        QVERIFY(sessions.setActive(id(2), id(102), false));
        QCOMPARE(sessions.release(id(2), id(102)), qint64(22));
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.hold(id(2), id(103), 33);
        QVERIFY(sessions.setActive(id(2), id(103), true));

        QCOMPARE(sessions.closeClient(id(1)), QList<qint64>{11});
        QVERIFY(!sessions.owns(id(1), id(101)));
        QVERIFY(sessions.measuring());
        QCOMPARE(sessions.measurementOwner(), id(101));
        // Reserving the departed owner ID also prevents a new shot in that view
        // from inheriting a result already being measured for the old shot.
        QCOMPARE(sessions.hold(id(2), id(101), 44), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.beginMeasurement(id(2), id(103)));
        sessions.finishMeasurement();
        QCOMPARE(sessions.measurementOwner(), id(103));
        QVERIFY(sessions.beginMeasurement(id(2), id(103)));
    }

    void physicalMeasurementWithoutAnOwnerStaysUnassignedUntilItFinishes()
    {
        ShotReviewSessions sessions;
        sessions.noteMeasurementStarted();
        QVERIFY(sessions.measuring());
        QVERIFY(sessions.measurementOwner().isEmpty());
        sessions.openClient(id(1), true);
        sessions.hold(id(1), id(101), 11);
        QVERIFY(sessions.setActive(id(1), id(101), true));
        QVERIFY(sessions.measurementOwner().isEmpty());
        sessions.finishMeasurement();
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.noteMeasurementStarted();
        QCOMPARE(sessions.closeClient(id(1)), QList<qint64>{11});
        QCOMPARE(sessions.measurementOwner(), id(101));
        sessions.finishMeasurement();
        QVERIFY(sessions.measurementOwner().isEmpty());
    }

    void replacementOwnerIsDeterministic()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        for (quint64 view : {300, 200, 100}) {
            sessions.hold(id(1), id(view), static_cast<qint64>(view));
            QVERIFY(sessions.setActive(id(1), id(view), true));
        }
        QCOMPARE(sessions.measurementOwner(), id(300));
        QCOMPARE(sessions.release(id(1), id(300)), qint64(300));
        QCOMPARE(sessions.measurementOwner(), id(100));
    }

    void leaseLimitsRejectFreshRequestsAndAllowCapacityReuse()
    {
        ShotReviewSessions sessions;
        sessions.openClient(id(1));
        for (quint64 i = 0; i < 16; ++i)
            QCOMPARE(sessions.hold(id(1), id(100 + i), 11),
                     i == 0 ? ShotReviewSessions::Hold::FirstForShot
                            : ShotReviewSessions::Hold::AdditionalForShot);
        QCOMPARE(sessions.hold(id(1), id(200), 11), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(100), 11), ShotReviewSessions::Hold::Existing);
        QCOMPARE(sessions.release(id(1), id(100)), qint64(0));
        QCOMPARE(sessions.hold(id(1), id(200), 11), ShotReviewSessions::Hold::AdditionalForShot);

        ShotReviewSessions full;
        for (quint64 client = 1; client <= 8; ++client) {
            full.openClient(id(client));
            for (quint64 i = 0; i < 16; ++i)
                QVERIFY(full.hold(id(client), id(1000 + client * 16 + i), 11)
                        != ShotReviewSessions::Hold::Rejected);
        }
        full.openClient(id(9));
        QCOMPARE(full.hold(id(9), id(2000), 11), ShotReviewSessions::Hold::Rejected);
        QVERIFY(full.closeClient(id(1)).isEmpty());
        QCOMPARE(full.hold(id(9), id(2000), 11), ShotReviewSessions::Hold::AdditionalForShot);
    }

    void malformedAndUnregisteredReviewRequestsAreRejected()
    {
        ShotReviewSessions sessions;
        sessions.openClient(QStringLiteral("not-a-client"));
        QCOMPARE(sessions.hold(id(1), id(101), 11), ShotReviewSessions::Hold::Rejected);
        sessions.openClient(id(1));
        QCOMPARE(sessions.hold(id(1), QStringLiteral("not-a-view"), 11), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(101), 0), ShotReviewSessions::Hold::Rejected);
        QCOMPARE(sessions.hold(id(1), id(101), -1), ShotReviewSessions::Hold::Rejected);
        QVERIFY(!sessions.setActive(id(1), id(101), true));
        QVERIFY(!sessions.beginMeasurement(id(1), id(101)));
    }

    void shippingReadingHandlersConsumeOnlyTheirTargetView()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/pages/PostShotReviewPage.qml"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
        const QString source = QString::fromUtf8(file.readAll());
        QJSEngine engine;
        for (const QString& name : {QStringLiteral("onRefractometerReading"),
                                    QStringLiteral("onRefractometerAverageProgress"),
                                    QStringLiteral("onRefractometerMeasurementComplete")}) {
            const QString function = sourceFunction(source, name);
            QVERIFY2(!function.isEmpty(), qPrintable(name));
            const QJSValue loaded = engine.evaluate(function);
            QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        }
        const qsizetype handlerAt = source.indexOf(QStringLiteral("function onRefractometerReading("));
        const qsizetype enabledAt = source.lastIndexOf(QStringLiteral("enabled:"), handlerAt);
        QVERIFY(enabledAt >= 0);
        const QString enabled = source.mid(enabledAt + 8, handlerAt - enabledAt - 8).trimmed();
        QJSValue result = engine.evaluate(QStringLiteral("function deliverTds(tds) { if (")
            + enabled + QStringLiteral(") onRefractometerReading(ShotReviewActions.refractometerOwnerReviewId, tds); }"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        result = engine.evaluate(QStringLiteral(R"JS(
            var ShotReviewActions = {
                refractometerOwnerReviewId: "view-a", refractometerMeasuring: true,
                refractometerConnected: true
            };
            var WebDebugLogger = { debug: function() {} };
            function makeView(viewId, shotId) {
                return {
                    _reviewSessionId: viewId, editShotId: shotId,
                    visible: true, isEditMode: true,
                    kMinimumPlausibleTds: 3, kMaximumPlausibleTds: 35,
                    editDrinkTds: 0, r2CommitPending: false,
                    avgDone: 0, avgTotal: 0, commitCalls: 0, writes: 0,
                    calculateEy: function() {},
                    commitPendingR2Reading: function() {
                        ++this.commitCalls;
                        if (this.r2CommitPending) { ++this.writes; this.r2CommitPending = false; }
                    }
                };
            }
            var views = [makeView("view-a", 101), makeView("view-b", 202)];
            var postShotReviewPage;
            function both(action) {
                views.forEach(function(view) { postShotReviewPage = view; action(); });
            }
            both(function() { deliverTds(7.5); });
            both(function() { onRefractometerAverageProgress("view-a", 1, 3); });
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        const auto run = [&engine](const QString& script) { return engine.evaluate(script); };
        QCOMPARE(run(QStringLiteral("views[0].editDrinkTds")).toNumber(), 7.5);
        QCOMPARE(run(QStringLiteral("views[1].editDrinkTds")).toNumber(), 0.0);
        QCOMPARE(run(QStringLiteral("views[0].avgDone")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].avgDone")).toInt(), 0);
        result = run(QStringLiteral("both(function() { onRefractometerMeasurementComplete('view-a'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].writes")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].commitCalls")).toInt(), 0);

        result = run(QStringLiteral("ShotReviewActions.refractometerOwnerReviewId = 'view-b'; both(function() { deliverTds(8.0); }); both(function() { onRefractometerMeasurementComplete('view-a'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].editDrinkTds")).toNumber(), 7.5);
        QCOMPARE(run(QStringLiteral("views[1].editDrinkTds")).toNumber(), 8.0);
        QCOMPARE(run(QStringLiteral("views[1].writes")).toInt(), 0);
        QVERIFY(run(QStringLiteral("views[1].r2CommitPending")).toBool());
        result = run(QStringLiteral("both(function() { onRefractometerMeasurementComplete('view-b'); });"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(run(QStringLiteral("views[0].writes")).toInt(), 1);
        QCOMPARE(run(QStringLiteral("views[1].writes")).toInt(), 1);
    }

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
            var ShotReviewActions = {
                releaseShotEdits: function() {}, holdShotEdits: function() {},
                newRequestId: function() { return "next-review"; }
            };
            var _ensureReviewLease = function() {};
            var _reviewSessionId = "old-review", _leasedClientId = "client";
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

    void leaseCommandsCarryTheirConnectionAndViewIdentity()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        const QString clientId = id(1);
        const QString reviewId = id(101);
        client.setProperty("decenzaClientId", clientId);
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QTRY_VERIFY(actions->ready());
        QCOMPARE(actions->clientId(), clientId);

        actions->holdShotEdits(11, reviewId);
        actions->expectHeldShotEdit(11, reviewId);
        actions->releaseShotEdits(11, reviewId);
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(0).operation, QStringLiteral("hold"));
        QCOMPARE(source.calls.at(1).operation, QStringLiteral("expectHeldEdit"));
        QCOMPARE(source.calls.at(2).operation, QStringLiteral("release"));
        const QVariantMap expected{{QStringLiteral("clientId"), clientId},
                                   {QStringLiteral("reviewId"), reviewId},
                                   {QStringLiteral("shotId"), qint64(11)}};
        for (const auto& call : std::as_const(source.calls)) {
            QCOMPARE(call.arguments, expected);
            source.complete(call);
        }
        QTRY_COMPARE(completed.size(), 3);
    }

    void refractometerCallsStayCorrelatedAndHostOwned()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        const QString clientId = id(1);
        const QString reviewId = id(101);
        client.setProperty("decenzaClientId", clientId);
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
        actions->setRefractometerHunt(true, reviewId);
        actions->scanForDevices(reviewId);
        actions->requestRefractometerMeasurement(reviewId);
        QTRY_COMPARE(source.calls.size(), 3);
        QCOMPARE(source.calls.at(0).operation, QStringLiteral("refractometerHunt"));
        QCOMPARE(source.calls.at(0).arguments,
                 (QVariantMap{{QStringLiteral("active"), true},
                              {QStringLiteral("reviewId"), reviewId},
                              {QStringLiteral("clientId"), clientId}}));
        QCOMPARE(source.calls.at(1).operation, QStringLiteral("scanForDevices"));
        QCOMPARE(source.calls.at(1).arguments,
                 (QVariantMap{{QStringLiteral("reviewId"), reviewId},
                              {QStringLiteral("clientId"), clientId}}));
        QCOMPARE(source.calls.at(2).operation,
                 QStringLiteral("requestRefractometerMeasurement"));
        QCOMPARE(source.calls.at(2).arguments, source.calls.at(1).arguments);

        source.setRefractometerOwnerReviewId(reviewId);
        QTRY_COMPARE(actions->refractometerOwnerReviewId(), reviewId);
        QSignalSpy measurementComplete(actions, &ShotReviewActions::refractometerMeasurementComplete);
        QSignalSpy progress(actions, &ShotReviewActions::refractometerAverageProgress);
        emit source.refractometerAverageProgress(reviewId, 1, 3);
        emit source.refractometerMeasurementComplete(reviewId);
        QTRY_COMPARE(progress.size(), 1);
        QTRY_COMPARE(measurementComplete.size(), 1);
        QCOMPARE(progress.first().at(0).toString(), reviewId);
        QCOMPARE(progress.first().at(1).toInt(), 1);
        QCOMPARE(measurementComplete.first().at(0).toString(), reviewId);

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
        client.setProperty("decenzaClientId", id(1));
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QSignalSpy completed(actions, &ShotReviewActions::actionCompleted);
        QTRY_VERIFY(actions->ready());

        actions->scanForDevices(id(101));
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

    void deletingFacadeFromStateNotificationStopsMeasuringContinuation()
    {
        ReviewActionFixtureSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        client.setProperty("decenzaClientId", id(1));
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ShotReviewActionStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ShotReviewActions::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ShotReviewActions::setPublishedNode(nullptr); } } resetNode;
        auto* actions = ShotReviewActionsForeign::create(&engine, &engine);
        QTRY_VERIFY(actions->ready());

        QPointer<ShotReviewActions> guard(actions);
        connect(actions, &ShotReviewActions::stateChanged, actions,
                [actions] { delete actions; }, Qt::DirectConnection);
        source.setRefractometerMeasuring(true);
        QTRY_VERIFY(guard.isNull());
    }
};

QTEST_GUILESS_MAIN(ShotReviewActionsTest)
#include "tst_shotreviewactions.moc"
