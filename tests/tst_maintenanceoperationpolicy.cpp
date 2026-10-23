#include "../src/remote/maintenanceoperationpolicy.h"

#include <QtTest>

class MaintenanceOperationPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void startRequiresConnectedSafePhaseOrSimulation()
    {
        constexpr int idle = 2;
        constexpr int heating = 3;
        constexpr int ready = 4;
        QVERIFY(MaintenanceOperationPolicy::startAllowed(false, true, idle, idle, heating, ready));
        QVERIFY(MaintenanceOperationPolicy::startAllowed(false, true, heating, idle, heating, ready));
        QVERIFY(MaintenanceOperationPolicy::startAllowed(false, true, ready, idle, heating, ready));
        QVERIFY(MaintenanceOperationPolicy::startAllowed(true, false, 0, idle, heating, ready));
        QVERIFY(!MaintenanceOperationPolicy::startAllowed(false, false, idle, idle, heating, ready));
        QVERIFY(!MaintenanceOperationPolicy::startAllowed(false, true, 7, idle, heating, ready));
    }

    void startsUseRevisionCompareAndSwap()
    {
        QVERIFY(MaintenanceOperationPolicy::revisionMatches(9, 9));
        QVERIFY(!MaintenanceOperationPolicy::revisionMatches(8, 9));
        QVERIFY(!MaintenanceOperationPolicy::revisionMatches(-1, 9));
    }

    void hotDescaleNeedsExplicitAcknowledgement()
    {
        QVERIFY(MaintenanceOperationPolicy::hotDescaleAllowed(60.0, false));
        QVERIFY(MaintenanceOperationPolicy::hotDescaleAllowed(75.0, true));
        QVERIFY(!MaintenanceOperationPolicy::hotDescaleAllowed(60.1, false));
    }

    void disconnectCannotCompleteAMaintenanceWorkflow()
    {
        MaintenanceWorkflowCompletion descale;
        descale.observe(true, false, false);
        QVERIFY(descale.observedActive());
        descale.observe(false, true, false); // BLE link drops while running
        QVERIFY(descale.interrupted());
        QVERIFY(!descale.needsFinish());
        descale.observe(false, false, true); // reconnect reports Idle
        QVERIFY(!descale.needsFinish());
        descale.observe(true, false, false); // machine reports the operation resumed
        QVERIFY(!descale.interrupted());
        descale.observe(false, false, true);
        QVERIFY(descale.needsFinish());

        MaintenanceWorkflowCompletion transport;
        transport.observe(true, false, false);
        transport.observe(false, true, false);
        transport.observe(false, false, true);
        QVERIFY(!transport.needsFinish());
    }

    void explicitStopRemainsDistinctFromNaturalCompletion()
    {
        MaintenanceWorkflowCompletion workflow;
        workflow.observe(true, false, false);
        workflow.end(true);
        QVERIFY(workflow.stopped());
        QVERIFY(!workflow.observedActive());
        QVERIFY(!workflow.needsFinish());
        QVERIFY(!workflow.interrupted());

        workflow.reset();
        QVERIFY(!workflow.stopped());
        workflow.observe(true, false, false);
        workflow.observe(false, false, true);
        QVERIFY(workflow.needsFinish());
        QVERIFY(!workflow.stopped());
    }

    void explicitStopStaysLatchedUntilDoneOrNewWorkflow()
    {
        MaintenanceWorkflowCompletion workflow;
        workflow.observe(true, false, false);
        workflow.end(true);

        // An asynchronous device phase update may still report the old active
        // phase after the stop command has been accepted.
        workflow.observe(true, false, false);
        QVERIFY(workflow.stopped());
        QVERIFY(!workflow.needsFinish());
        workflow.observe(false, false, true);
        QVERIFY(workflow.stopped());
        QVERIFY(!workflow.needsFinish());

        // Done acknowledges the stopped outcome even though stop cleanup already
        // made the workflow inactive.
        workflow.end(false);
        QVERIFY(!workflow.stopped());
        QVERIFY(!workflow.needsFinish());

        // Reopening and starting a fresh workflow resets the old outcome.
        workflow.reset();
        workflow.observe(true, false, false);
        QVERIFY(!workflow.stopped());
        workflow.observe(false, false, true);
        QVERIFY(workflow.needsFinish());
        QVERIFY(!workflow.stopped());
    }

    void viewExitIsNotAnOperationAndExplicitControlsAreAllowlisted()
    {
        QVERIFY(!MaintenanceOperationPolicy::supported(QStringLiteral("back")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("finishDescale")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("cancelDescale")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("stopDescale")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("finishTransport")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("stopTransport")));
        QVERIFY(!MaintenanceOperationPolicy::supported(QStringLiteral("releaseTransport")));
    }

    void operationsAreExplicitlyAllowlisted()
    {
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("startDescale")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("startTransport")));
        QVERIFY(MaintenanceOperationPolicy::supported(QStringLiteral("setHeadless")));
        QVERIFY(!MaintenanceOperationPolicy::supported(QStringLiteral("setProperty")));
        QVERIFY(!MaintenanceOperationPolicy::supported(QStringLiteral("writeMMR")));
    }
};

QTEST_APPLESS_MAIN(MaintenanceOperationPolicyTest)
#include "tst_maintenanceoperationpolicy.moc"
