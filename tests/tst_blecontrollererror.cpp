#include <QtTest>

#include "ble/blecontrollererror.h"

// Naming and fault classification for QLowEnergyController::Error.
//
// Controller errors are log-only: a reporter with a DE1 they switch off
// overnight got a bare "connection error" box on every single app start
// (#1658), because the direct-wake connect to the saved address beat the
// machine to readiness. In that reporter's own log the reconnect ladder had the
// DE1 back 26–118 s later, every time — so the box only ever interrupted a
// recovery already in progress, and said nothing the user could act on. What
// remains is the debug log, which is where these names have to be right, and
// the teardown-family predicate that drives the wedge detector.
class tst_BleControllerError : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void namesMatchQtEnumValues_data() {
        QTest::addColumn<int>("rawValue");
        QTest::addColumn<QString>("expectedName");

        // Values are Qt's, asserted numerically on purpose: a debug log only
        // ever shows what the mapping produced, so the number a future Qt hands
        // us has to keep meaning the same thing.
        QTest::newRow("0 NoError") << 0 << "NoError";
        QTest::newRow("1 UnknownError") << 1 << "UnknownError";
        QTest::newRow("2 UnknownRemoteDeviceError") << 2 << "UnknownRemoteDeviceError";
        QTest::newRow("3 NetworkError") << 3 << "NetworkError";
        QTest::newRow("4 InvalidBluetoothAdapterError") << 4 << "InvalidBluetoothAdapterError";
        QTest::newRow("5 ConnectionError") << 5 << "ConnectionError";
        QTest::newRow("6 AdvertisingError") << 6 << "AdvertisingError";
        QTest::newRow("7 RemoteHostClosedError") << 7 << "RemoteHostClosedError";
        QTest::newRow("8 AuthorizationError") << 8 << "AuthorizationError";
        QTest::newRow("9 MissingPermissionsError") << 9 << "MissingPermissionsError";
        QTest::newRow("10 RssiReadError") << 10 << "RssiReadError";
    }

    void namesMatchQtEnumValues() {
        QFETCH(int, rawValue);
        QFETCH(QString, expectedName);
        QCOMPARE(bleControllerErrorName(
                     static_cast<QLowEnergyController::Error>(rawValue)),
                 expectedName);
    }

    void stateNamesMatchQtEnumValues_data() {
        QTest::addColumn<int>("rawValue");
        QTest::addColumn<QString>("expectedName");

        QTest::newRow("0 Unconnected") << 0 << "Unconnected";
        QTest::newRow("1 Connecting") << 1 << "Connecting";
        QTest::newRow("2 Connected") << 2 << "Connected";
        QTest::newRow("3 Discovering") << 3 << "Discovering";
        QTest::newRow("4 Discovered") << 4 << "Discovered";
        QTest::newRow("5 Closing") << 5 << "Closing";
        QTest::newRow("6 Advertising") << 6 << "Advertising";
    }

    void stateNamesMatchQtEnumValues() {
        QFETCH(int, rawValue);
        QFETCH(QString, expectedName);
        QCOMPARE(bleControllerStateName(
                     static_cast<QLowEnergyController::ControllerState>(rawValue)),
                 expectedName);
    }

    // The three that feed de1LinkFault, and through it the connection-priority
    // coordinator and the BLE-stack-wedge detector. Each is here because a real
    // report traced back to it: #1176 ConnectionError, #1238
    // RemoteHostClosedError, #1093 AuthorizationError (which is never a pairing
    // failure — the DE1 has no PIN). Dropping one silently starves the wedge
    // detector of the signal it confirms a wedge with.
    void linkTeardownFamilyDrivesTheWedgeDetector_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");

        QTest::newRow("ConnectionError") << QLowEnergyController::ConnectionError;
        QTest::newRow("RemoteHostClosedError") << QLowEnergyController::RemoteHostClosedError;
        QTest::newRow("AuthorizationError") << QLowEnergyController::AuthorizationError;
    }

    void linkTeardownFamilyDrivesTheWedgeDetector() {
        QFETCH(QLowEnergyController::Error, error);
        QVERIFY(bleControllerErrorIsLinkTeardown(error));
    }

    // The other side of the same contract. Widening the family would make the
    // wedge detector fire on failures that are not the contention signature —
    // a missing permission or a dead adapter is a different problem with a
    // different owner (BLEManager, which can actually name it).
    void nonTeardownErrorsAreNotContentionSignature_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");

        QTest::newRow("MissingPermissionsError") << QLowEnergyController::MissingPermissionsError;
        QTest::newRow("InvalidBluetoothAdapterError") << QLowEnergyController::InvalidBluetoothAdapterError;
        QTest::newRow("UnknownRemoteDeviceError") << QLowEnergyController::UnknownRemoteDeviceError;
        QTest::newRow("NetworkError") << QLowEnergyController::NetworkError;
        QTest::newRow("UnknownError") << QLowEnergyController::UnknownError;
        QTest::newRow("AdvertisingError") << QLowEnergyController::AdvertisingError;
        QTest::newRow("RssiReadError") << QLowEnergyController::RssiReadError;
    }

    void nonTeardownErrorsAreNotContentionSignature() {
        QFETCH(QLowEnergyController::Error, error);
        QVERIFY(!bleControllerErrorIsLinkTeardown(error));
    }

    // The number-fallback after each switch is deliberately NOT tested. Reaching
    // it requires an out-of-range enum value, and merely loading one is
    // undefined behaviour — UBSan flags it. macOS runs UBSan in recovering mode,
    // so such a test passes locally and green-lights a change that would fail
    // the halting-mode nightly Linux job. The fallback exists to satisfy the
    // compiler's control-flow analysis, not as reachable behaviour; every value
    // Qt can actually deliver is covered above.
};

QTEST_MAIN(tst_BleControllerError)
#include "tst_blecontrollererror.moc"
