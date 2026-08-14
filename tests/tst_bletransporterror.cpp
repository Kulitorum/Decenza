#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/bletransport.h"
#include "ble/blecontrollererror.h"

// The #1658 contract, pinned at the one place that enforces it.
//
// This is the test the rest of that change was missing. tst_blecontrollererror
// covers the extracted name/classification helpers, but those were a pure
// refactor — put `emit errorOccurred(...)` back into onControllerError and every
// one of its cases still passes. The actual fix is the ABSENCE of an emit, and
// absence is exactly what a reviewer skims past and a future change silently
// undoes.
//
// The seam is real, not contrived: BleTransport's constructor only configures
// QTimers, onControllerError is null-safe on m_controller by construction
// (`m_controller ? m_controller->state() : UnconnectedState`), and
// bletransport.cpp already carries #ifndef DECENZA_TESTING guards around its
// BLEManager dependencies precisely so test targets can link it without one.
//
// The two Linux-only diagnostics in onControllerError are guarded out of test
// builds under that same #ifndef — see the comment there. Without that, the
// UnknownRemoteDeviceError rows below would emit two extra qWarnings on an
// unprivileged CI runner (failing init()'s failOnWarning) and leak a detached
// diagnostics thread into the nightly Linux ASan job. Both are invisible on
// macOS, so a green local run proves nothing about either.
class tst_BleTransportError : public QObject {
    Q_OBJECT

private:
    // Every value QLowEnergyController::Error can take in Qt 6.11.
    static QList<QLowEnergyController::Error> allErrors() {
        return {
            QLowEnergyController::NoError,
            QLowEnergyController::UnknownError,
            QLowEnergyController::UnknownRemoteDeviceError,
            QLowEnergyController::NetworkError,
            QLowEnergyController::InvalidBluetoothAdapterError,
            QLowEnergyController::ConnectionError,
            QLowEnergyController::AdvertisingError,
            QLowEnergyController::RemoteHostClosedError,
            QLowEnergyController::AuthorizationError,
            QLowEnergyController::MissingPermissionsError,
            QLowEnergyController::RssiReadError,
        };
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void noControllerErrorRaisesAUserFacingError_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");
        for (const auto err : allErrors())
            QTest::newRow(qPrintable(bleControllerErrorName(err))) << err;
    }

    // The headline assertion: whatever the radio reports, the user is not
    // interrupted. A reporter who switches their DE1 off overnight got this box
    // on every single app start while the reconnect ladder was already fixing it.
    void noControllerErrorRaisesAUserFacingError() {
        QFETCH(QLowEnergyController::Error, error);

        BleTransport transport;
        QSignalSpy modal(&transport, &DE1Transport::errorOccurred);
        QVERIFY(modal.isValid());

        // The warn line is the whole remaining surface for these errors, so it
        // is asserted rather than merely tolerated — init()'s failOnWarning
        // would fail this test if the log line ever went away silently.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("!!! CONTROLLER ERROR: %1 \\(state=Unconnected\\) !!!")
                                   .arg(bleControllerErrorName(error))));

        transport.onControllerError(error);

        QCOMPARE(modal.count(), 0);
    }

    void linkTeardownFamilyStillFiresDe1LinkFault_data() {
        QTest::addColumn<QLowEnergyController::Error>("error");
        for (const auto err : allErrors())
            QTest::newRow(qPrintable(bleControllerErrorName(err))) << err;
    }

    // The other half of the contract, and the reason this change is a narrowing
    // rather than a deletion: silencing the dialog must not also silence the
    // fault feed. de1LinkFault drives the connection-priority coordinator and
    // the BLE-stack-wedge detector (#1309), both of which would quietly stop
    // working if someone "simplified" the remaining branch away.
    void linkTeardownFamilyStillFiresDe1LinkFault() {
        QFETCH(QLowEnergyController::Error, error);

        BleTransport transport;
        QSignalSpy fault(&transport, &DE1Transport::de1LinkFault);
        QVERIFY(fault.isValid());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("!!! CONTROLLER ERROR: %1")
                                   .arg(bleControllerErrorName(error))));

        transport.onControllerError(error);

        const int expected = bleControllerErrorIsLinkTeardown(error) ? 1 : 0;
        QCOMPARE(fault.count(), expected);
        if (expected == 1)
            QCOMPARE(fault.first().at(0).toString(), QStringLiteral("controller-error"));
    }

    // The write-retry budget is a TIME bound, and that is the half that broke.
    //
    // A timing-out write occupies the link for
    //   (MAX_WRITE_RETRIES + 1) × WRITE_TIMEOUT_MS + MAX_WRITE_RETRIES × WRITE_RETRY_DELAY_MS
    // and the app issues periodic writes on a 60 s cadence (BatteryManager's
    // charger keepalive, batterymanager.cpp:26). At the old budget of 10 that
    // came to exactly 60.0 s, so on a degraded link the next keepalive was
    // queued before the previous one was abandoned and the link never went
    // idle — measured dispatch→abandonment in #1691 at 60.06 s, with keepalive
    // exhaustions 60.0 s apart.
    //
    // This asserts the relationship, not the constants: raising either the
    // retry count or the timeout without re-checking the cadence puts the link
    // back into permanent occupancy, and nothing else in the suite would say
    // so. Verified to fail by restoring MAX_WRITE_RETRIES to 10.
    void writeRetryBudgetLeavesTheLinkIdleBetweenPeriodicWrites() {
        constexpr int kShortestPeriodicWriteMs = 60000;  // batterymanager.cpp:26

        constexpr int worstCaseMs =
            (BleTransport::MAX_WRITE_RETRIES + 1) * BleTransport::WRITE_TIMEOUT_MS
            + BleTransport::MAX_WRITE_RETRIES * BleTransport::WRITE_RETRY_DELAY_MS;

        QVERIFY2(worstCaseMs < kShortestPeriodicWriteMs,
                 qPrintable(QStringLiteral(
                     "a timing-out write occupies the link for %1 ms, which is not "
                     "shorter than the %2 ms periodic-write cadence — the link never "
                     "goes idle on a degraded connection (#1691)")
                         .arg(worstCaseMs).arg(kShortestPeriodicWriteMs)));
    }

    // Guards the retry budget itself against being raised back without the
    // reasoning being re-read. 434 retry cycles measured across the #1176-#1810
    // debug-log corpus: every recovery happened by retry 9, exactly one write
    // recovered at retry 10, and 380 cycles ran the full budget and failed.
    //
    // The upper bound is the real assertion — the lower one only catches a
    // budget cut so far it would abandon writes that routinely recover.
    void writeRetryBudgetStaysInTheMeasuredBand() {
        QVERIFY(BleTransport::MAX_WRITE_RETRIES >= 3);
        QVERIFY(BleTransport::MAX_WRITE_RETRIES <= 5);
    }
};

QTEST_MAIN(tst_BleTransportError)
#include "tst_bletransporterror.moc"
