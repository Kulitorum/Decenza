#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QBluetoothUuid>

#include "ble/scales/decentscale.h"
#include "ble/scales/bookooscale.h"
#include "ble/transport/scalebletransport.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/decentscaleprotocol.h"

// Test BLE packet parsing for scale implementations.
// Feeds raw byte arrays through onCharacteristicChanged() (public slot)
// and verifies weight/battery signals.
//
// de1app references:
//   Decent: de1plus/scale.tcl proc decent_scale_parse_response
//   Bookoo: de1plus/scale.tcl proc bookoo_parse_response

// Minimal mock transport for watchdog tests
class MockScaleBleTransport : public ScaleBleTransport {
    Q_OBJECT
public:
    explicit MockScaleBleTransport(QObject* parent = nullptr) : ScaleBleTransport(parent) {}

    void connectToDevice(const QString&, const QString&) override {}
    void disconnectFromDevice() override { m_disconnectCount++; }
    void discoverServices() override {}
    void discoverCharacteristics(const QBluetoothUuid&) override {}
    void enableNotifications(const QBluetoothUuid&, const QBluetoothUuid&) override { m_notifyEnableCount++; }
    void writeCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&,
                             const QByteArray&, WriteType = WriteType::WithResponse) override {}
    void readCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    bool isConnected() const override { return true; }

    int m_notifyEnableCount = 0;
    int m_disconnectCount = 0;
};

class tst_ScaleProtocol : public QObject {
    Q_OBJECT

private:
    // Build a 7-byte Decent Scale packet with valid XOR checksum
    static QByteArray buildDecentPacket(uint8_t cmd, uint8_t d2, uint8_t d3,
                                        uint8_t d4, uint8_t d5) {
        QByteArray pkt(7, 0);
        pkt[0] = 0x03;
        pkt[1] = static_cast<char>(cmd);
        pkt[2] = static_cast<char>(d2);
        pkt[3] = static_cast<char>(d3);
        pkt[4] = static_cast<char>(d4);
        pkt[5] = static_cast<char>(d5);
        pkt[6] = static_cast<char>(DecentScaleProtocol::calculateXor(pkt));
        return pkt;
    }

    // Build a Decent weight packet: cmd=0xCE, weight as int16 BE / 10
    static QByteArray buildDecentWeightPacket(double grams) {
        int16_t raw = static_cast<int16_t>(qRound(grams * 10.0));
        uint8_t hi = static_cast<uint8_t>((raw >> 8) & 0xFF);
        uint8_t lo = static_cast<uint8_t>(raw & 0xFF);
        return buildDecentPacket(0xCE, hi, lo, 0x00, 0x00);
    }

    // Build a 20-byte Bookoo weight packet
    static QByteArray buildBookooPacket(double grams, uint8_t battery = 50) {
        QByteArray pkt(20, 0);
        pkt[0] = 0x03;
        pkt[1] = 0x0B;
        // bytes 2-4: timer (0)
        // byte 5: unit (0 = grams)

        bool negative = grams < 0;
        pkt[6] = static_cast<char>(negative ? '-' : '+');

        uint32_t raw = static_cast<uint32_t>(qRound(qAbs(grams) * 100.0));
        pkt[7] = static_cast<char>((raw >> 16) & 0xFF);
        pkt[8] = static_cast<char>((raw >> 8) & 0xFF);
        pkt[9] = static_cast<char>(raw & 0xFF);

        // bytes 10-12: flow (0)
        pkt[13] = static_cast<char>(battery);
        // bytes 14-19: standby, buzzer, XOR (don't matter for parsing)
        return pkt;
    }

private slots:

    // ==========================================
    // DecentScale: weight parsing
    // ==========================================

    void decentWeight100g() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildDecentWeightPacket(100.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), 100.0);
    }

    void decentWeightZero() {
        DecentScale scale(nullptr);
        // First set to non-zero so the 0.0 packet triggers a change
        auto nonZero = buildDecentWeightPacket(10.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, nonZero);

        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);
        auto pkt = buildDecentWeightPacket(0.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), 0.0);
    }

    void decentWeightNegative() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildDecentWeightPacket(-5.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), -5.0);
    }

    void decentWeightMaxInt16() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        // Max int16 = 32767 -> 3276.7g
        auto pkt = buildDecentWeightPacket(3276.7);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QVERIFY(qAbs(spy.last().at(0).toDouble() - 3276.7) < 0.2);
    }

    void decentWeightPrecision() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        // 36.2g should encode as 362 raw -> 36.2g decoded
        auto pkt = buildDecentWeightPacket(36.2);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), 36.2);
    }

    // ==========================================
    // DecentScale: battery parsing
    // ==========================================

    void decentBattery75() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::batteryLevelChanged);

        // LED-response packet (battery 75%, firmware bytes zero). The
        // helper sets bytes [5-6] to 0x00 0x00 so the firmware-version
        // logger emits "Firmware version: 0.0.0 (raw 0x00 0x00)" — ignored
        // here so it doesn't pollute test output. The first LED response
        // per connect also debug-logs the parsed battery byte.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        auto pkt = buildDecentLedResponse(75, 0, 0, 0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 75);
    }

    void decentBatteryCharging() {
        DecentScale scale(nullptr);
        QSignalSpy battSpy(&scale, &ScaleDevice::batteryLevelChanged);
        QSignalSpy chargeSpy(&scale, &ScaleDevice::chargingChanged);

        // Battery charging: d4=0xFF -> batteryLevel=100 AND charging=true.
        // The "battery=100" reporting is preserved so existing UI that reads
        // batteryLevel == 100 keeps working; charging is the new first-class
        // signal that surfaces the underlying state.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        auto pkt = buildDecentLedResponse(0xFF, 0, 0, 0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(battSpy.count() >= 1);
        QCOMPARE(battSpy.last().at(0).toInt(), 100);
        QVERIFY(chargeSpy.count() >= 1);
        QCOMPARE(chargeSpy.last().at(0).toBool(), true);
        QCOMPARE(scale.charging(), true);
    }

    void decentChargingClearedWhenBatteryByteIsPercent() {
        DecentScale scale(nullptr);
        QSignalSpy chargeSpy(&scale, &ScaleDevice::chargingChanged);

        // First a charging response, then a normal battery percent — should
        // flip charging back to false. Firmware version is logged once per
        // connect (same value on both packets → no second log to ignore).
        // First LED response debug-logs the battery byte (0xff); the second
        // (different byte 0x3c=60) warn-logs the transition.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=0xff.*"));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentLedResponse(0xFF, 0, 0, 0));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Battery byte changed:.*0xff.*0x3c.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentLedResponse(60, 0, 0, 0));

        // Two flips: false→true, then true→false.
        QCOMPARE(chargeSpy.count(), 2);
        QCOMPARE(chargeSpy.last().at(0).toBool(), false);
        QCOMPARE(scale.charging(), false);
    }

    // ==========================================
    // DecentScale: firmware version logging
    // ==========================================

    // Build a 7-byte LED-response packet (cmd=0x0A, header=0x03) with the
    // given battery byte and openscale-encoded firmware version. Mirrors
    // openscale include/ble.h:730-731: verHigh = BCD(major), verLow =
    // (minor << 4) | patch. LED responses carry no checksum.
    static QByteArray buildDecentLedResponse(uint8_t battery, int major, int minor, int patch) {
        QByteArray pkt(7, 0);
        pkt[0] = 0x03;
        pkt[1] = 0x0A;
        pkt[2] = 0x00;
        pkt[3] = 0x00;
        pkt[4] = static_cast<char>(battery);
        pkt[5] = static_cast<char>(((major / 10) << 4) | (major % 10));
        pkt[6] = static_cast<char>((minor << 4) | patch);
        return pkt;
    }

    void decentFirmwareVersionLoggedOnceOnFirstLedResponse() {
        // Current openscale source (include/config.h:29) is `FW: 3.0.9` →
        // wire bytes 0x03 0x09. Verify the exact decode, and verify
        // mechanically (via QSignalSpy on logMessage — see SCALE_LOG in
        // scalelogging.h, which emits logMessage alongside qDebug) that
        // the second identical packet does NOT re-log.
        DecentScale scale(nullptr);
        auto pkt = buildDecentLedResponse(50, 3, 0, 9);
        QCOMPARE(static_cast<uint8_t>(pkt[5]), uint8_t(0x03));
        QCOMPARE(static_cast<uint8_t>(pkt[6]), uint8_t(0x09));

        QSignalSpy logSpy(&scale, &ScaleDevice::logMessage);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 3\\.0\\.9 \\(raw 0x03 0x09\\).*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        // Count "Firmware version" log emissions after the first packet —
        // exactly one expected.
        const auto firmwareLogCount = [&logSpy]() {
            int n = 0;
            for (const auto& args : logSpy) {
                if (args.value(0).toString().contains(QStringLiteral("Firmware version:")))
                    ++n;
            }
            return n;
        };
        QCOMPARE(firmwareLogCount(), 1);

        // Second identical packet: no new "Firmware version" log line. If
        // dedup regresses, this QCOMPARE fires (and the unsuppressed qDebug
        // would also appear in test output).
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);
        QCOMPARE(firmwareLogCount(), 1);
    }

    void decentFirmwareVersionBcdMajor() {
        // Verify BCD decode of a major >= 10 — major=12, minor=3, patch=4
        // packs to verHigh=0x12, verLow=0x34. Catches a naive `d[5]` (raw
        // byte) decode that would mis-render as "18.3.4".
        DecentScale scale(nullptr);
        auto pkt = buildDecentLedResponse(80, 12, 3, 4);
        QCOMPARE(static_cast<uint8_t>(pkt[5]), uint8_t(0x12));
        QCOMPARE(static_cast<uint8_t>(pkt[6]), uint8_t(0x34));

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 12\\.3\\.4 \\(raw 0x12 0x34\\).*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);
    }

    void decentFirmwareVersionChangeWarns() {
        // A subsequent LED response carrying a different version is itself
        // diagnostic (shouldn't happen on real hardware) — warn-log the
        // transition rather than silently overwriting.
        DecentScale scale(nullptr);
        auto pkt1 = buildDecentLedResponse(50, 3, 0, 9);
        auto pkt2 = buildDecentLedResponse(50, 3, 1, 0);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 3\\.0\\.9.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt1);

        // pkt2 has the same battery byte (50) so no battery log on the
        // second packet — only the firmware-version warn fires.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Firmware version changed mid-connect.*3\\.0\\.9.*3\\.1\\.0.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt2);
    }

    // ==========================================
    // DecentScale: error handling
    // ==========================================

    void decentTruncatedPacketNoCrash() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        // Only 3 bytes (need 7)
        QByteArray truncated = QByteArray::fromHex("03CE00");
        scale.onCharacteristicChanged(Scale::Decent::READ, truncated);

        // Should not crash. Weight may or may not change.
    }

    void decentEmptyPacketNoCrash() {
        DecentScale scale(nullptr);
        scale.onCharacteristicChanged(Scale::Decent::READ, QByteArray());
    }

    void decentWrongUuidIgnored() {
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildDecentWeightPacket(50.0);
        // Use wrong UUID — should be ignored
        QBluetoothUuid wrongUuid(QString("00001234-0000-1000-8000-00805F9B34FB"));
        scale.onCharacteristicChanged(wrongUuid, pkt);

        QCOMPARE(spy.count(), 0);
    }

    void decentChecksumValidation() {
        // Valid checksum byte matches XOR of bytes 0-5
        auto pkt = buildDecentWeightPacket(50.0);
        uint8_t expected = 0;
        for (int i = 0; i < 6; i++)
            expected ^= static_cast<uint8_t>(pkt[i]);
        QCOMPARE(static_cast<uint8_t>(pkt[6]), expected);
    }

    void decentBadChecksumDropped() {
        // Corrupt checksum byte — weight should NOT be emitted
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildDecentWeightPacket(42.0);
        pkt[6] = static_cast<char>(static_cast<uint8_t>(pkt[6]) ^ 0xFF);  // Flip all bits

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QCOMPARE(spy.count(), 0);
    }

    void decentBadChecksumButtonDropped() {
        // Corrupt button packet should be dropped
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::buttonPressed);

        auto pkt = buildDecentPacket(0xAA, 0x01, 0x00, 0x00, 0x00);
        pkt[6] = static_cast<char>(static_cast<uint8_t>(pkt[6]) ^ 0xFF);  // Flip all bits

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QCOMPARE(spy.count(), 0);
    }

    void decentChecksumAutoDisableAfterConsecutiveFailures() {
        // Original Decent Scale (v1) sends invalid checksums — on the 5th consecutive
        // failure, checksum validation is disabled and the triggering packet is accepted.
        // See: https://github.com/Kulitorum/Decenza/issues/630
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        // Build a weight packet with deliberately wrong checksum
        auto pkt = buildDecentWeightPacket(42.0);
        pkt[6] = static_cast<char>(static_cast<uint8_t>(pkt[6]) ^ 0xFF);

        // First 4 failures should drop the packet
        for (int i = 0; i < 4; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*dropping packet.*"));
            scale.onCharacteristicChanged(Scale::Decent::READ, pkt);
        }
        QCOMPARE(spy.count(), 0);

        // 5th failure should trigger auto-disable and accept the packet
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Checksum validation disabled.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), 42.0);

        // Subsequent bad-checksum packets should also be accepted without warnings
        auto pkt2 = buildDecentWeightPacket(55.0);
        pkt2[6] = static_cast<char>(static_cast<uint8_t>(pkt2[6]) ^ 0xFF);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt2);
        QCOMPARE(spy.last().at(0).toDouble(), 55.0);
    }

    void decentChecksumResetOnGoodPacket() {
        // A valid checksum should reset the consecutive failure counter
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto badPkt = buildDecentWeightPacket(42.0);
        badPkt[6] = static_cast<char>(static_cast<uint8_t>(badPkt[6]) ^ 0xFF);

        // Send 3 bad packets (under threshold)
        for (int i = 0; i < 3; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*dropping packet.*"));
            scale.onCharacteristicChanged(Scale::Decent::READ, badPkt);
        }
        QCOMPARE(spy.count(), 0);

        // Send a good packet — resets counter
        auto goodPkt = buildDecentWeightPacket(10.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, goodPkt);
        QVERIFY(spy.count() >= 1);

        // Send 3 more bad packets — should still drop (counter was reset)
        spy.clear();
        for (int i = 0; i < 3; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*dropping packet.*"));
            scale.onCharacteristicChanged(Scale::Decent::READ, badPkt);
        }
        QCOMPARE(spy.count(), 0);
    }

    void decentLedResponseSkipsChecksum() {
        // LED-response (cmd 0x0A) has no checksum — bytes [5-6] carry the
        // BCD-encoded firmware version, NOT a checksum, and the parser must
        // accept the packet regardless of what those bytes happen to be.
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::batteryLevelChanged);

        // FW: 1.0.2 → wire 0x01 0x02. The exact value doesn't matter for
        // this test — the point is that byte 6 is not validated as a XOR
        // checksum of bytes 0-5 (which it would fail).
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        auto pkt = buildDecentLedResponse(60, 1, 0, 2);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 60);
    }

    // ==========================================
    // BookooScale: weight parsing
    // ==========================================

    void bookooWeight250g() {
        BookooScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildBookooPacket(250.50);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, pkt);

        QVERIFY(spy.count() >= 1);
        QVERIFY(qAbs(spy.last().at(0).toDouble() - 250.50) < 0.02);
    }

    void bookooWeightZero() {
        BookooScale scale(nullptr);
        // Set to non-zero first so 0.0 triggers a change
        auto nonZero = buildBookooPacket(10.0);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, nonZero);

        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);
        auto pkt = buildBookooPacket(0.0);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toDouble(), 0.0);
    }

    void bookooWeightNegative() {
        BookooScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto pkt = buildBookooPacket(-3.5);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, pkt);

        QVERIFY(spy.count() >= 1);
        QVERIFY(qAbs(spy.last().at(0).toDouble() - (-3.5)) < 0.02);
    }

    void bookooBattery80() {
        BookooScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::batteryLevelChanged);

        auto pkt = buildBookooPacket(10.0, 80);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, pkt);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 80);
    }

    // ==========================================
    // BookooScale: error handling
    // ==========================================

    void bookooTruncatedNoCrash() {
        BookooScale scale(nullptr);
        // Only 5 bytes (need 20)
        QByteArray truncated(5, 0);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, truncated);
    }

    void bookooEmptyNoCrash() {
        BookooScale scale(nullptr);
        scale.onCharacteristicChanged(Scale::Bookoo::STATUS, QByteArray());
    }

    // ==========================================
    // Cross-scale boundary tests
    // ==========================================

    void oversizedPacketNoCrash() {
        // 255-byte junk packet should not crash any scale
        QByteArray oversized(255, 0x42);

        DecentScale decent(nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*"));
        decent.onCharacteristicChanged(Scale::Decent::READ, oversized);

        BookooScale bookoo(nullptr);
        bookoo.onCharacteristicChanged(Scale::Bookoo::STATUS, oversized);
    }

    void singleBytePacketNoCrash() {
        QByteArray single(1, 0x03);

        DecentScale decent(nullptr);
        decent.onCharacteristicChanged(Scale::Decent::READ, single);

        BookooScale bookoo(nullptr);
        bookoo.onCharacteristicChanged(Scale::Bookoo::STATUS, single);
    }

    // ==========================================
    // DecentScale: watchdog behavior
    // ==========================================

    void watchdogFiresWhenNoData() {
        // Watchdog should fire and re-enable notifications when no weight data arrives
        auto* transport = new MockScaleBleTransport;  // DecentScale takes ownership via setParent
        DecentScale scale(transport);

        // Simulate characteristics discovered to arm the watchdog
        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Expect watchdog warning after timeout
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));

        // Wait for initial watchdog timeout (1s) + margin
        QTest::qWait(1200);

        // Watchdog should have re-enabled notifications at least once
        QVERIFY(transport->m_notifyEnableCount >= 1);
    }

    void watchdogTickleResetsTimer() {
        // Weight data arriving should prevent the watchdog from firing
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Feed data before the 1s initial timeout
        QTest::qWait(500);
        auto pkt = buildDecentWeightPacket(18.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        int countAfterTickle = transport->m_notifyEnableCount;

        // Wait past the initial timeout — should NOT fire since we tickled
        QTest::qWait(800);

        QCOMPARE(transport->m_notifyEnableCount, countAfterTickle);
    }

    void watchdogDisconnectsAfterMaxRetries() {
        // After 10 failed retries, watchdog should disconnect for reconnection
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Expect watchdog warnings: 10 retry warnings + 1 "max retries exhausted" = 11 total
        for (int i = 0; i < DecentScale::kWatchdogMaxRetries + 1; i++)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));

        int baseNotifyCount = transport->m_notifyEnableCount;

        // Manually fire the watchdog 10 times (faster than waiting 10+ seconds)
        for (int i = 0; i < DecentScale::kWatchdogMaxRetries; i++) {
            scale.onWatchdogFired();
        }

        QCOMPARE(transport->m_disconnectCount, 1);
        // Should have re-enabled notifications for retries 1-9 (10th triggers disconnect)
        QCOMPARE(transport->m_notifyEnableCount - baseNotifyCount, DecentScale::kWatchdogMaxRetries - 1);
    }

    void watchdogRetryCountResetsOnData() {
        // Receiving data should reset the retry counter
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Expect watchdog warnings for retry attempts
        for (int i = 0; i < 10; i++)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));

        // Fire watchdog 5 times (half of max)
        for (int i = 0; i < 5; i++) {
            scale.onWatchdogFired();
        }
        QCOMPARE(transport->m_disconnectCount, 0);

        // Data arrives — resets retry count
        auto pkt = buildDecentWeightPacket(20.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        // Fire 5 more times — should NOT disconnect (retries reset to 0)
        for (int i = 0; i < 5; i++) {
            scale.onWatchdogFired();
        }
        QCOMPARE(transport->m_disconnectCount, 0);
    }

    void watchdogStopsOnDisconnect() {
        // Transport disconnect should cancel the watchdog so it doesn't fire on a dead transport
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Expect the disconnect warning from onTransportDisconnected
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));

        // Simulate transport disconnect (fires onTransportDisconnected via signal)
        emit transport->disconnected();

        // Wait past the initial watchdog timeout — should NOT fire
        QTest::qWait(1500);

        QCOMPARE(transport->m_notifyEnableCount, 0);
        QCOMPARE(transport->m_disconnectCount, 0);
    }

    void watchdogGuardsCharacteristicsReady() {
        // onWatchdogFired should log and stop when characteristics are not ready
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();

        // Characteristics become unready (e.g., during teardown)
        scale.m_characteristicsReady = false;

        // Expect the guard warning
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*not ready.*stopping watchdog.*"));

        // Fire watchdog — should log, stop watchdog, and return without re-enabling or disconnecting
        scale.onWatchdogFired();

        QCOMPARE(transport->m_notifyEnableCount, 0);
        QCOMPARE(transport->m_disconnectCount, 0);
    }

    void wakeRestartsWatchdog() {
        // wake() should restart heartbeat and watchdog after sleep() stopped them
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startWatchdog();
        scale.startHeartbeat();

        // sleep() stops both
        scale.stopWatchdog();
        scale.stopHeartbeat();

        // wake() should restart them
        scale.wake();

        // Expect watchdog warning if no data arrives within 1s
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));

        QTest::qWait(1200);

        // Watchdog should have fired and re-enabled notifications
        QVERIFY(transport->m_notifyEnableCount >= 1);
    }
};

QTEST_GUILESS_MAIN(tst_ScaleProtocol)
#include "tst_scaleprotocol.moc"
