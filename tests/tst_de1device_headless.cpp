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
//
// And the water-level filter, which is here rather than in a file of its own because a new test
// TU costs ~1.4 s of every clean build forever while a slot in an existing one costs milliseconds
// (see TESTING.md). It shares this file's fixture: a DE1Device on a MockTransport, driven by
// feeding characteristic payloads.

class tst_DE1DeviceHeadless : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device device;
        TestFixture() { device.setTransport(&transport); }
    };

    // Build a WaterLevels payload reading `mm` at the sensor, i.e. before the +5 mm mount offset
    // the parser adds. U16P8 big-endian.
    static QByteArray waterPayload(double sensorMm) {
        const uint16_t raw = static_cast<uint16_t>(sensorMm * 256.0 + 0.5);
        QByteArray d;
        d.append(static_cast<char>((raw >> 8) & 0xFF));
        d.append(static_cast<char>(raw & 0xFF));
        return d;
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void defaultsToHeadless() {
        // A freshly constructed device (no GHC read yet) must allow app starts.
        DE1Device device;
        QCOMPARE(device.isHeadless(), true);
    }

    // ---- Water level filtering (see DE1Device::parseWaterLevel) ----

    // The first sample IS the level. Without the seed the EMA ramps from zero and every connect
    // shows the tank filling over the filter's settling time — visible on screen, not just in
    // the log, because the property is driven from the same value.
    void waterLevelSeedsFromFirstSample() {
        TestFixture f;
        f.device.parseWaterLevel(waterPayload(30.0));
        QCOMPARE(f.device.waterLevelMm(), 35.0);  // 30 at the sensor + 5 mm offset
    }

    // The defect this whole change exists for: under the pump the tank sloshes, a field log showed
    // 5.5 mm to 33.5 mm inside four seconds, and a 2 mm hysteresis on the INSTANTANEOUS reading
    // passed on essentially every sample. Feed that waveform and nothing may reach the log.
    //
    // Deliberately asserted on m_lastLoggedWaterLevelMm rather than on captured output: it is the
    // gate's own state, so the test fails if the gate is removed, widened past the slosh, or fed
    // the raw sample again — and does not care how the line is worded.
    void sloshDoesNotReachTheLog() {
        TestFixture f;
        f.device.parseWaterLevel(waterPayload(20.0));
        const double afterFirst = f.device.m_lastLoggedWaterLevelMm;

        // +/-10 mm about a level that is not actually changing, 60 samples (~15 s at 4 Hz).
        for (int i = 0; i < 60; ++i)
            f.device.parseWaterLevel(waterPayload(i % 2 == 0 ? 30.0 : 10.0));

        QCOMPARE(f.device.m_lastLoggedWaterLevelMm, afterFirst);
        // ...and the filtered level stayed at the true mean rather than chasing either extreme.
        QVERIFY(qAbs(f.device.waterLevelMm() - 25.0) < 2.0);
    }

    // The other half, and the reason a bigger threshold was not the fix: a REAL move still has to
    // arrive. A refill is a sustained step, so the filter must follow it and the gate must fire.
    void aSustainedRefillStillLogs() {
        TestFixture f;
        f.device.parseWaterLevel(waterPayload(10.0));
        const double afterFirst = f.device.m_lastLoggedWaterLevelMm;

        for (int i = 0; i < 60; ++i)
            f.device.parseWaterLevel(waterPayload(40.0));

        QVERIFY(f.device.m_lastLoggedWaterLevelMm > afterFirst + 20.0);
    }

    // A reconnect must not measure its first delta against the previous session, nor ramp from the
    // old average. Both endpoints are cleared with the seed.
    void disconnectResetsTheWaterFilter() {
        TestFixture f;
        f.device.parseWaterLevel(waterPayload(40.0));
        QVERIFY(f.device.m_waterLevelSeeded);

        f.device.onTransportDisconnected();
        QVERIFY(!f.device.m_waterLevelSeeded);
        QCOMPARE(f.device.m_lastLoggedWaterLevelMl, -1);

        // Re-seeds at the new level rather than easing across from 45 mm.
        f.device.parseWaterLevel(waterPayload(10.0));
        QCOMPARE(f.device.waterLevelMm(), 15.0);
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
