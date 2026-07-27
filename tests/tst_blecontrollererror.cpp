#include <QtTest>

#include "ble/blecontrollererror.h"

// Naming and fault classification for QLowEnergyController::Error.
//
// Scope, stated honestly: this file covers the extracted helpers, which are a
// pure refactor of two duplicated switches. It does NOT cover #1658's actual
// behaviour change — restore the deleted `emit errorOccurred(...)` in
// BleTransport::onControllerError and every case below still passes. That
// contract lives in tst_bletransporterror.cpp.
//
// What these cases buy is insurance on the debug log, which is the only
// remaining surface for a controller error now that the modal is gone: the
// names are asserted against Qt's numeric enum values so a reordering or an
// inserted enumerator is caught rather than silently re-labelling old logs.
// Qt does not renumber public enums, so this is low-probability/high-cost
// cover, not detection of anything expected.
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

    // The number-fallback after each switch, exercised deliberately.
    //
    // An earlier revision claimed this could not be tested because loading an
    // out-of-range enumerator is undefined behaviour that UBSan would flag. That
    // was wrong for THESE enums. Both are unscoped with no fixed underlying
    // type, so per [dcl.enum]/8 their value range is the smallest bit-field
    // holding all enumerators: Error has 11 enumerators (0-10) → range 0-15, and
    // ControllerState has 7 (0-6) → range 0-7. 11 and 7 are therefore
    // representable and well-defined, and -fsanitize=enum does not fire.
    //
    // Worth testing rather than merely permitted: 11 is exactly what Qt's next
    // added error value will be, and this asserts such a value degrades to a
    // readable number in the log instead of an empty string.
    void unrecognizedValuesDegradeToTheirNumber() {
        QCOMPARE(bleControllerErrorName(static_cast<QLowEnergyController::Error>(11)),
                 QStringLiteral("11"));
        QCOMPARE(bleControllerStateName(
                     static_cast<QLowEnergyController::ControllerState>(7)),
                 QStringLiteral("7"));
    }
};

QTEST_MAIN(tst_BleControllerError)
#include "tst_blecontrollererror.moc"
