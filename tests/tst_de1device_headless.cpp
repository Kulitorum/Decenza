#include <QtTest>

#include "ble/de1device.h"
#include "mocks/MockTransport.h"

// Guards the GHC "headless" gate default (PR #1470). m_isHeadless means "the
// app may start operations on-screen". It must default TRUE (matching de1app,
// whose ghc_is_installed defaults to 0 → ghc_required()==0 → app can start): a
// false default bricks every start button on the common no-GHC machine until
// (or unless) the GHC MMR read returns. Only a positive GHC read flips it
// false, and a disconnect must restore the permissive default so a GHC
// machine's false cannot bleed into the next connection.

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
};

QTEST_MAIN(tst_DE1DeviceHeadless)
#include "tst_de1device_headless.moc"
