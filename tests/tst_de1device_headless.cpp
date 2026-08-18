#include <QtTest>

#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "mocks/MockTransport.h"

// Guards the GHC "headless" gate default (PR #1470). m_isHeadless means "the
// app may start operations on-screen". It must default TRUE (matching de1app,
// whose ghc_is_installed defaults to 0 → ghc_required()==0 → app can start): a
// false default bricks every start button on the common no-GHC machine until
// (or unless) the GHC MMR read returns. Only a positive GHC read flips it
// false, and a disconnect must restore the permissive default so a GHC
// machine's false cannot bleed into the next connection.
//
// Also guards DE1::SubState reset on disconnect (de1app commit 04d3b02e): a
// substate is never reset by de1app's own disconnect handler, so a stale
// Error_NoAC can pin the front-standby-switch warning up forever, even after
// the switch is flipped back and the app reconnects. Same shape as the
// headless reset above, different field.

class tst_DE1DeviceHeadless : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device device;
        TestFixture() { device.setTransport(&transport); }
    };

private slots:
    void init() { QTest::failOnWarning(); }
    void defaultsToHeadless() {
        // A freshly constructed device (no GHC read yet) must allow app starts.
        DE1Device device;
        QCOMPARE(device.isHeadless(), true);
    }

    void disconnectRestoresHeadless() {
        TestFixture f;
        // Simulate a positive GHC read having marked the machine GHC-controlled.
        f.device.setIsHeadless(false);
        QCOMPARE(f.device.isHeadless(), false);

        QSignalSpy spy(&f.device, &DE1Device::isHeadlessChanged);

        // A connect/disconnect cycle must restore the permissive default so the
        // next connection isn't left blocking start until its own GHC read.
        f.transport.setConnectedSim(true);
        f.transport.setConnectedSim(false);

        QCOMPARE(f.device.isHeadless(), true);
        QCOMPARE(spy.count(), 1);  // flipped once (setIsHeadless dedups), not a no-op
    }

    void disconnectResetsSubState() {
        TestFixture f;
        f.transport.setConnectedSim(true);
        f.device.m_subState = DE1::SubState::Error_NoAC;

        QSignalSpy spy(&f.device, &DE1Device::subStateChanged);
        f.transport.setConnectedSim(false);

        QCOMPARE(f.device.subState(), DE1::SubState::Ready);
        QCOMPARE(spy.count(), 1);
    }

    void disconnectIsANoOpWhenSubStateAlreadyReady() {
        // onTransportDisconnected() guards the reset on a value check, so an
        // already-quiet disconnect (the overwhelmingly common case) does not spam
        // subStateChanged.
        TestFixture f;
        f.transport.setConnectedSim(true);

        QSignalSpy spy(&f.device, &DE1Device::subStateChanged);
        f.transport.setConnectedSim(false);

        QCOMPARE(f.device.subState(), DE1::SubState::Ready);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(tst_DE1DeviceHeadless)
#include "tst_de1device_headless.moc"
