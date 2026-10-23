#include "../src/remote/ghcsimulatoroperationpolicy.h"

#include <QtTest>

class GHCSimulatorOperationPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void controlsRequireAHostSimulatorAndSimulationMode()
    {
        QVERIFY(GHCSimulatorOperationPolicy::simulatorAvailable(true, true));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(false, true));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(true, false));
        QVERIFY(!GHCSimulatorOperationPolicy::simulatorAvailable(false, false));

        // A real-device connection is insufficient: the remote service must
        // remain unavailable unless this host owns a DE1Simulator instance.
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(
            QStringLiteral("pressEspresso"), false, false));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(
            QStringLiteral("pressEspresso"), true, false));
    }

    void onlyGroupHeadButtonActionsAreExposed()
    {
        for (const QString& operation : {
                 QStringLiteral("pressEspresso"), QStringLiteral("pressSteam"),
                 QStringLiteral("pressHotWater"), QStringLiteral("pressFlush"),
                 QStringLiteral("pressStop")}) {
            QVERIFY(GHCSimulatorOperationPolicy::supported(operation));
            QVERIFY(GHCSimulatorOperationPolicy::canExecute(operation, true, true));
        }
        QVERIFY(!GHCSimulatorOperationPolicy::supported(QStringLiteral("setState")));
        QVERIFY(!GHCSimulatorOperationPolicy::supported(QStringLiteral("releaseStop")));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(QStringLiteral("setState"), true, true));
        QVERIFY(!GHCSimulatorOperationPolicy::canExecute(QStringLiteral("pressStop"), false, true));
    }

    void snapshotSeparatesAvailabilityFromControllerState()
    {
        const QVariantList leds{QStringLiteral("off"), QStringLiteral("lit")};
        const QVariantMap active = GHCSimulatorOperationPolicy::snapshot(true, true, leds, true, 2);
        QVERIFY(active.value(QStringLiteral("available")).toBool());
        QCOMPARE(active.value(QStringLiteral("ledColors")).toList(), leds);
        QVERIFY(active.value(QStringLiteral("stopPressed")).toBool());
        QCOMPARE(active.value(QStringLiteral("activeFunction")).toInt(), 2);

        const QVariantMap physical = GHCSimulatorOperationPolicy::snapshot(false, false, leds, false, 0);
        QVERIFY(!physical.value(QStringLiteral("available")).toBool());
        QCOMPARE(physical.value(QStringLiteral("ledColors")).toList(), leds);
    }
};

QTEST_APPLESS_MAIN(GHCSimulatorOperationPolicyTest)
#include "tst_ghcsimulatoroperationpolicy.moc"
