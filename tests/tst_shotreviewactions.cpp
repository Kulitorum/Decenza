#include <QtTest>

#include <QQmlEngine>
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
