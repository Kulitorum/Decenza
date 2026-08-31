#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QBluetoothUuid>

#include "ble/scales/decentscale.h"
#include "ble/scales/bookooscale.h"
#include "ble/scales/difluidscale.h"
#include "ble/scales/acaiascale.h"
#include "ble/blegattqueue.h"
#include "ble/transport/scalebletransport.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/decentscaleprotocol.h"
#include "messagecapture.h"

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
    explicit MockScaleBleTransport(QObject* parent = nullptr, BleGattQueue* queue = nullptr)
        : ScaleBleTransport(parent, queue) {}

    // The base class's shared-queue plumbing, exposed so the contract every
    // scale and refractometer transport depends on can be asserted once here
    // rather than separately in each of them. Both real implementations call
    // exactly these.
    using ScaleBleTransport::submitGattOperation;
    using ScaleBleTransport::completeGattOperation;
    using ScaleBleTransport::failGattOperation;
    using ScaleBleTransport::releaseGattQueue;
    using ScaleBleTransport::holdsGattSlot;

    void connectToDevice(const QString&, const QString&) override { m_connectCount++; }
    void disconnectFromDevice() override { m_disconnectCount++; }
    void discoverServices() override {}
    void discoverCharacteristics(const QBluetoothUuid& service) override {
        m_characteristicDiscoveries.append(service);
    }
    void enableNotifications(const QBluetoothUuid& service,
                             const QBluetoothUuid& characteristic) override {
        m_notifyEnableCount++;
        m_lastNotifyService = service;
        // The real transports emit this from inside the queued issue callback,
        // when the enable actually reaches the radio. A mock that skips it lets
        // a driver waiting on the signal look broken (or, worse, lets one that
        // should wait look fine). Emitted inline here — the ORDER is what the
        // drivers care about, not the delay.
        //
        // Suppressible, because "the enable never reaches the radio" is a real
        // transport state with four routes into it (link not ready, service
        // missing, characteristic invalid, no CCCD — every one of them calls
        // failGattOperation() and emits nothing), and a driver that only ever
        // sees the happy path cannot be tested against it.
        if (m_suppressNotificationsIssued) return;
        emit notificationsIssued(characteristic);
    }
    bool m_suppressNotificationsIssued = false;
    void writeCharacteristic(const QBluetoothUuid& service, const QBluetoothUuid&,
                             const QByteArray& value, WriteType = WriteType::WithResponse) override {
        m_writes.append(value);
        m_lastWriteService = service;
    }
    void readCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    bool isConnected() const override { return m_isConnected; }

    // Drive the discovery handshake from a test. Qt's `signals` macro expands to
    // `public`, so these are not strictly necessary — they exist because
    // `transport->fakeServiceDiscovered(x)` reads as "the transport reported x",
    // which is the thing under test, rather than as a test emitting a signal.
    void fakeServiceDiscovered(const QBluetoothUuid& s) { emit serviceDiscovered(s); }
    void fakeServicesDiscoveryFinished() { emit servicesDiscoveryFinished(); }
    void fakeCharacteristicsDiscoveryFinished(const QBluetoothUuid& s) {
        emit characteristicsDiscoveryFinished(s);
    }
    void fakeCharacteristicChanged(const QBluetoothUuid& c, const QByteArray& v) {
        emit characteristicChanged(c, v);
    }
    void fakeDisconnected() { emit disconnected(); }

    int m_notifyEnableCount = 0;
    int m_disconnectCount = 0;
    int m_connectCount = 0;
    bool m_isConnected = true;
    QList<QByteArray> m_writes;
    QList<QBluetoothUuid> m_characteristicDiscoveries;
    QBluetoothUuid m_lastNotifyService;
    QBluetoothUuid m_lastWriteService;
};


class tst_ScaleProtocol : public QObject {
    Q_OBJECT

private:
    // Brings a DecentScale to "connected, weight-notify enabled, watchdog
    // running" through the PRODUCTION path.
    //
    // Discovery alone no longer arms the watchdog: it now starts when the
    // notify-enable reaches the radio, because under the shared GATT queue the
    // gap between requesting an enable and it going out can exceed the
    // watchdog's whole budget. In the app the wake sequence issues that enable a
    // few hundred ms after discovery; here we issue the same call, and the mock
    // emits notificationsIssued exactly as the real transports do. Nothing pokes
    // the driver's internals, so the arming code is what is under test.
    static void connectWithWatchdogRunning(DecentScale& scale) {
        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);
        scale.enableWeightNotifications(QStringLiteral("test"));
    }

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
    void init() { QTest::failOnWarning(); }

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
        // QtDebugMsg: a battery byte ticking down is the battery discharging,
        // not a fault. It was at WARN, so routine drain read as a problem in
        // every submitted log.
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte changed:.*0xff.*0x3c.*"));
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

    void hdsBluetoothFirmwareVersionAndUpdateCommand() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);
        scale.m_characteristicsReady = true;

        QSignalSpy versionSpy(&scale, &ScaleDevice::firmwareVersionChanged);
        const auto response = buildDecentLedResponse(50, 3, 1, 13);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 3\\.1\\.13 \\(raw 0x03 0x1d\\).*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, response);

        QCOMPARE(scale.firmwareVersion(), QStringLiteral("3.1.13"));
        QVERIFY(scale.supportsFirmwareUpdate());
        QCOMPARE(versionSpy.count(), 1);

        transport->m_writes.clear();
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateStartingMessage(QStringLiteral("3.1.14")))));
        scale.startFirmwareUpdate(QStringLiteral("3.1.14"));
        // 0x1B plus the target as three 0x80-biased bytes, in the fixed 7-byte
        // packet. The scale's length check for a targeted 0x1B is a minimum, so
        // the pad and checksum are ignored.
        QCOMPARE(transport->m_writes, QList<QByteArray>{QByteArray::fromHex("031B83818E0094")});
    }

    // A bare 0x1B is a valid command that starts the scale's own picker, so an
    // unresolvable target must send nothing rather than fall back to it.
    void hdsBluetoothUpdateCommandRequiresAParsableTarget() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);
        scale.m_characteristicsReady = true;

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Battery byte d\\[4\\]=.*"));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentLedResponse(50, 3, 1, 13));
        transport->m_writes.clear();

        // One bad value: the accepted set is asserted exhaustively by
        // hdsTargetVersionEncoding_data. What this proves is that the driver
        // consults that predicate and writes nothing when it refuses.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateBadTargetMessage(QStringLiteral("3.1.x")))));
        scale.startFirmwareUpdate(QStringLiteral("3.1.x"));
        QVERIFY(transport->m_writes.isEmpty());
    }

    void hdsTargetVersionEncoding_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("3.1.14") << "3.1.14" << QByteArray::fromHex("1B83818E");
        QTest::newRow("zeroes") << "0.0.0" << QByteArray::fromHex("1B808080");
        // Every component byte must keep its high bit, or older firmware would
        // read the payload as a command: 3.10.2 would decode as 03 0A 02, the
        // power-off command.
        QTest::newRow("3.10.2") << "3.10.2" << QByteArray::fromHex("1B838A82");
        // Rejected, not clamped. Clamping would turn a version the user was
        // shown into a different, installable one; the firmware refuses the same
        // input (openscale pullOtaParseTargetVersion).
        QTest::newRow("bad-over-max") << "1.2.128" << QByteArray();
        QTest::newRow("max") << "127.127.127" << QByteArray::fromHex("1BFFFFFF");
        QTest::newRow("bad-negative") << "3.1.-4" << QByteArray();
        QTest::newRow("bad-short") << "3.1" << QByteArray();
        QTest::newRow("bad-empty") << "" << QByteArray();
        // Tolerated, like the firmware's own target parser, so no caller has to
        // know which shape the manifest used.
        QTest::newRow("v-prefix") << "v3.1.14" << QByteArray::fromHex("1B83818E");
    }

    void hdsTargetVersionEncoding() {
        QFETCH(QString, version);
        QFETCH(QByteArray, expected);
        QCOMPARE(DecentScaleProtocol::buildTargetedFirmwareUpdateCommand(version), expected);
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

    // Build the 41-byte ADS debug frame the firmware sends while debug mode is
    // on (openscale include/usbcomm.h buildAdsDebugPacket): type 0x25, checksum
    // over bytes 0-39 in byte 40.
    static QByteArray buildDecentAdsDebugFrame(uint8_t marker = 0x11) {
        QByteArray pkt(41, 0);
        pkt[0] = 0x03;
        pkt[1] = 0x25;
        pkt[5] = static_cast<char>(marker);  // a timestamp byte, i.e. volatile payload
        uint8_t xorSum = 0;
        for (int i = 0; i < 40; i++)
            xorSum ^= static_cast<uint8_t>(pkt[i]);
        pkt[40] = static_cast<char>(xorSum);
        return pkt;
    }

    void decentAdsDebugFrameDoesNotDisableChecksumValidation() {
        // A 41-byte debug frame is not a 7-byte packet, so it must not reach the
        // v1 auto-disable counter. It used to: the checksum was computed over
        // data.size() and compared against byte 6 (a timestamp byte), so five
        // frames retired checksum validation and reported the HDS as an original
        // Decent Scale.
        DecentScale scale(nullptr);
        MessageCapture capture;

        for (int i = 0; i < DecentScale::kChecksumFailureThreshold + 2; i++)
            scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentAdsDebugFrame(uint8_t(i)));

        QCOMPARE(capture.count(QStringLiteral("Checksum validation disabled")), 0);

        // Checksum validation is still live: a corrupt weight packet still drops.
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);
        auto bad = buildDecentWeightPacket(42.0);
        bad[6] = static_cast<char>(static_cast<uint8_t>(bad[6]) ^ 0xFF);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*dropping packet.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, bad);
        QCOMPARE(spy.count(), 0);
    }

    void decentUndecodedFrameLoggedOnceWithItsBytes() {
        // One line per shape, carrying the hex, and nothing on repeats — the
        // payload here changes every frame, which is exactly what defeats a
        // text-keyed suppressor if the hex is left in the repeat line.
        DecentScale scale(nullptr);
        MessageCapture capture;

        for (int i = 0; i < 20; i++)
            scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentAdsDebugFrame(uint8_t(i)));
        // A length no notify produces: a different shape, so one more line.
        scale.onCharacteristicChanged(Scale::Decent::READ, QByteArray::fromHex("030a03"));
        scale.onCharacteristicChanged(Scale::Decent::READ, QByteArray::fromHex("030a04"));

        MessageCapture::Entry entry;
        QVERIFY(capture.single(QStringLiteral("ADS debug frame"), &entry));
        QVERIFY(entry.text.contains(QStringLiteral("03 25")));
        QCOMPARE(capture.count(QStringLiteral("Short frame for type 0x0a")), 1);
    }

    void decentOriginalScaleAutoDisableStillReachedByShortFrames() {
        // The v1 accommodation (#630) must survive the length gate: the original
        // scale only ever sends 7-byte frames, and those still count.
        DecentScale scale(nullptr);
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);

        auto bad = buildDecentWeightPacket(42.0);
        bad[6] = static_cast<char>(static_cast<uint8_t>(bad[6]) ^ 0xFF);
        for (int i = 0; i < DecentScale::kChecksumFailureThreshold - 1; i++) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid checksum.*dropping packet.*"));
            // Interleave a debug frame: it must not reset or advance the counter.
            scale.onCharacteristicChanged(Scale::Decent::READ, buildDecentAdsDebugFrame(uint8_t(i)));
            scale.onCharacteristicChanged(Scale::Decent::READ, bad);
        }
        QCOMPARE(spy.count(), 0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Checksum validation disabled.*"));
        scale.onCharacteristicChanged(Scale::Decent::READ, bad);
        QCOMPARE(spy.last().at(0).toDouble(), 42.0);
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
        // Exhaustion runs the disconnect handling directly (#1519)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));

        int baseNotifyCount = transport->m_notifyEnableCount;

        // Manually fire the watchdog 10 times (faster than waiting 10+ seconds)
        for (int i = 0; i < DecentScale::kWatchdogMaxRetries; i++) {
            scale.onWatchdogFired();
        }

        QCOMPARE(transport->m_disconnectCount, 1);
        // Should have re-enabled notifications for retries 1-9 (10th triggers disconnect)
        QCOMPARE(transport->m_notifyEnableCount - baseNotifyCount, DecentScale::kWatchdogMaxRetries - 1);
    }

    void watchdogExhaustionPropagatesDisconnect() {
        // #1519: the transport never emits disconnected() on the watchdog's
        // forced disconnect, so exhaustion must drive setConnected(false)
        // itself — the auto-reconnect ladder (main.cpp) is gated on
        // connectedChanged, and without it the app parks on a zombie scale.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        // Drive the real connect path so the scale reports connected
        connectWithWatchdogRunning(scale);
        QVERIFY(scale.isConnected());

        QSignalSpy spy(&scale, &ScaleDevice::connectedChanged);

        for (int i = 0; i < DecentScale::kWatchdogMaxRetries + 1; i++)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*DISCONNECTED.*"));

        for (int i = 0; i < DecentScale::kWatchdogMaxRetries; i++)
            scale.onWatchdogFired();

        QCOMPARE(transport->m_disconnectCount, 1);
        QVERIFY(!scale.isConnected());
        QCOMPARE(spy.count(), 1);
    }

    void transientErrorOnLiveLinkKeepsConnection() {
        // A per-operation transport error (e.g. one failed write) on a live
        // link must not flip the scale to disconnected: the transport is
        // still connected and streaming, and the scan-based reconnect ladder
        // cannot recover a connected (non-advertising) peripheral. The
        // watchdog supervises the actual feed.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        connectWithWatchdogRunning(scale);
        QVERIFY(scale.isConnected());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport error.*"));
        emit transport->error("transient write failure");

        QVERIFY(scale.isConnected());
        QVERIFY(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive());
    }

    // The watchdog must not start counting before its notify-enable has reached
    // the radio. It used to arm inline in the wake sequence, which was fine when
    // a write went straight out and wrong once the shared queue could hold it
    // behind the DE1's connect burst: measured on a tablet, enableNotifications
    // called at 7.221 s and dispatched at 8.483 s, so the 1 s watchdog expired
    // 50 ms BEFORE the scale had been asked anything, logged "no initial weight
    // data" and burned a retry.
    //
    // Break the arm-on-issue wiring and this goes red: the wake sequence alone
    // must leave it disarmed.
    // NO path may arm the watchdog before the enable reaches the radio — not the
    // wake sequence, and not wake() itself, which the sequence calls at 200 ms
    // and 500 ms and which used to arm unconditionally.
    //
    // This is the slot that would have caught the incomplete first fix: that one
    // removed the sequence's own inline arm, the sibling below passed, and on
    // hardware wake() armed it anyway 278 ms before the enable went out. Asserts
    // the PROPERTY (nothing is armed yet) rather than one call site.
    // ...but an enable that NEVER issues must still end up armed, or nothing
    // recovers. onNotificationsIssued() is the only other arm, so without this
    // fallback m_watchdogArmPending stays set, wake()'s arm stays gated on it,
    // and the watchdog never fires: no re-enable, no retry, no disconnect, and
    // the app holds a scale that will never report a weight (#1519).
    //
    // Deleting armWatchdogIfEnableNeverIssues() leaves the rest of the suite
    // green — noPathArmsTheWatchdogBeforeTheEnableIsIssued() below asserts the
    // opposite half — so this slot is the only thing standing between that
    // deletion and the field symptom.
    void anEnableThatNeverIssuesStillArmsTheWatchdog() {
        auto* transport = new MockScaleBleTransport;
        transport->m_suppressNotificationsIssued = true;
        DecentScale scale(transport);

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);
        scale.enableWeightNotifications(QStringLiteral("test"));
        scale.m_watchdogArmPending = true;
        scale.armWatchdogIfEnableNeverIssues();

        // Nothing arms it up front — that is the other test's subject.
        QVERIFY(!(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive()));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Notify-enable was never issued to the radio"));
        QTRY_VERIFY_WITH_TIMEOUT(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive(),
                                 DecentScale::kEnableIssueBudgetMs + 2000);
        QVERIFY(!scale.m_watchdogArmPending);
    }

    // A wake() while the watchdog is already running must not re-arm it.
    // startWatchdog() resets m_watchdogRetries and m_watchdogUpdatesSeen, so a
    // second arm hands back the whole retry budget and forgets that weight data
    // had been seen — the next lapse is then timed as a first sight rather than
    // a stall. Build 3574 logged both arms 198 ms apart once the enable started
    // dispatching promptly enough to clear the pending flag before the 500 ms
    // wake().
    void aWakeDoesNotReArmARunningWatchdog() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);

        // onCharacteristicsDiscoveryFinished() set m_watchdogArmPending; the
        // enable then issues (the mock emits notificationsIssued inline), which
        // arms the watchdog and clears the flag — the state the 500 ms wake()
        // runs in.
        scale.enableWeightNotifications(QStringLiteral("test"));
        QVERIFY(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive());
        QVERIFY(!scale.m_watchdogArmPending);

        // Spend part of the budget and see data, then wake.
        scale.m_watchdogRetries = 4;
        scale.m_watchdogUpdatesSeen = true;
        const int remainingBeforeWake = scale.m_watchdogTimer->remainingTime();
        scale.wake();

        QCOMPARE(scale.m_watchdogRetries, 4);
        QVERIFY(scale.m_watchdogUpdatesSeen);
        QVERIFY(scale.m_watchdogTimer->isActive());
        // The countdown is left ALONE, not restarted. Without this a wrong fix
        // that called m_watchdogTimer->start() — "parity with
        // onNotificationsIssued()" — would still pass every assertion above.
        QVERIFY(scale.m_watchdogTimer->remainingTime() <= remainingBeforeWake);
    }

    // main.cpp wakes the scale from its connectedChanged handler when an LCD
    // restore is pending, and connectedChanged is emitted SYNCHRONOUSLY from
    // inside onCharacteristicsDiscoveryFinished(). That re-enters wake() before
    // the connect sequence has submitted its notify-enable, so the arm-pending
    // flag has to be set before setConnected() rather than after it — otherwise
    // the watchdog arms against an LCD command and expires before the enable is
    // ever asked for.
    //
    // noPathArmsTheWatchdogBeforeTheEnableIsIssued() below cannot see this: it
    // wires no connectedChanged observer, so it passes either way.
    void aWakeReEnteredFromConnectedChangedDoesNotArmTheWatchdog() {
        auto* transport = new MockScaleBleTransport;
        transport->m_suppressNotificationsIssued = true;
        DecentScale scale(transport);

        connect(&scale, &ScaleDevice::connectedChanged, &scale, [&scale]() {
            if (scale.isConnected()) scale.wake();
        });

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);

        // The re-entrant wake() must not have armed anything: no enable has been
        // submitted yet, so there is nothing for a watchdog to time.
        QVERIFY(!(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive()));
        QVERIFY(scale.m_watchdogArmPending);
    }

    void noPathArmsTheWatchdogBeforeTheEnableIsIssued() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);

        // Everything the connect sequence does before its enable lands.
        scale.wake();
        scale.wake();
        QVERIFY(!(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive()));

        // And after a sleep, where no enable is pending, wake() must still
        // restore it — the two callers have opposite needs.
        scale.enableWeightNotifications(QStringLiteral("test"));
        QVERIFY(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive());
        scale.stopWatchdog();
        scale.wake();
        QVERIFY(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive());
    }

    void theWatchdogDoesNotArmUntilTheEnableReachesTheRadio() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);

        // Discovery has run and the wake sequence has requested its enables, but
        // none has reached the radio yet. Nothing to time, so nothing running.
        QVERIFY(!(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive()));

        // An enable for a DIFFERENT characteristic says nothing about when
        // weight data should start.
        emit transport->notificationsIssued(Scale::Decent::WRITE);
        QVERIFY(!(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive()));

        // The weight stream's enable reaching the radio is the event it waits
        // for. Issued through the production call, which the mock answers the
        // way the real transports do.
        scale.enableWeightNotifications(QStringLiteral("test"));
        QVERIFY(scale.m_watchdogTimer && scale.m_watchdogTimer->isActive());
    }

    void errorOnDeadLinkCleansUp() {
        // When the transport reports the link is gone, an error must run the
        // full disconnect handling (propagates connectedChanged so the
        // auto-reconnect ladder arms).
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        connectWithWatchdogRunning(scale);
        QVERIFY(scale.isConnected());

        transport->m_isConnected = false;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport error.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*DISCONNECTED.*"));
        emit transport->error("controller error, link dead");

        QVERIFY(!scale.isConnected());
        QVERIFY(!scale.m_watchdogTimer || !scale.m_watchdogTimer->isActive());
        QVERIFY(!scale.m_heartbeatTimer || !scale.m_heartbeatTimer->isActive());
    }

    void strayDataDoesNotRestartStoppedWatchdog() {
        // A late notification arriving after the watchdog was stopped
        // (post-disconnect or sleep()) must not resurrect supervision — a
        // tickle-restarted watchdog on a silent feed exhausts and
        // force-disconnects a deliberately sleeping scale.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_serviceFound = true;
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);
        scale.stopWatchdog();

        auto pkt = buildDecentWeightPacket(21.0);
        scale.onCharacteristicChanged(Scale::Decent::READ, pkt);

        QVERIFY(!scale.m_watchdogTimer || !scale.m_watchdogTimer->isActive());
    }

    void serviceNotFoundPropagatesDisconnect() {
        // #1519: the service-not-found bailout uses the same silent
        // disconnectFromDevice(); the direct disconnect handling is what
        // resets per-connect state that connectToDevice() does not touch —
        // e.g. the #630 checksum auto-disable must not leak from a failed
        // connect into a later session against a different scale.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_checksumDisabled = true;
        scale.m_serviceFound = false;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*service not found.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));
        scale.onServicesDiscoveryFinished();

        QCOMPARE(transport->m_disconnectCount, 1);
        QVERIFY(!scale.m_checksumDisabled);
        QVERIFY(!scale.isConnected());
    }

    void reconnectAfterWatchdogExhaustion() {
        // #1519 round trip: after the watchdog-forced disconnect, a normal
        // connectToDevice() + discovery cycle must bring the scale back —
        // pins that the duplicate-callback guard in
        // onCharacteristicsDiscoveryFinished doesn't swallow the re-setup.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        connectWithWatchdogRunning(scale);
        QVERIFY(scale.isConnected());

        for (int i = 0; i < DecentScale::kWatchdogMaxRetries + 1; i++)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*DISCONNECTED.*"));
        for (int i = 0; i < DecentScale::kWatchdogMaxRetries; i++)
            scale.onWatchdogFired();
        QVERIFY(!scale.isConnected());

        // Reconnect through the same discovery path
        scale.connectToDevice(QBluetoothDeviceInfo());
        scale.onServiceDiscovered(Scale::Decent::SERVICE);
        scale.onServicesDiscoveryFinished();
        scale.onCharacteristicsDiscoveryFinished(Scale::Decent::SERVICE);

        QVERIFY(scale.isConnected());
    }

    void wakeDoesNotReviveDeadLink() {
        // #1519: DE1 wake calls scale wake(); after a watchdog-forced
        // disconnect this must not write to the dead transport or restart
        // the heartbeat/watchdog on a link that no longer exists.
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        // Through the production path, like the sibling watchdog slots: firing
        // the watchdog on a scale whose watchdog was never armed is a state the
        // app cannot reach, and reaching it here made the first retry take the
        // first-arm branch and reset the retry budget, so exhaustion never came.
        connectWithWatchdogRunning(scale);

        for (int i = 0; i < DecentScale::kWatchdogMaxRetries + 1; i++)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Watchdog.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Transport disconnected.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*DISCONNECTED.*"));
        for (int i = 0; i < DecentScale::kWatchdogMaxRetries; i++)
            scale.onWatchdogFired();

        const qsizetype writesBefore = transport->m_writes.size();
        scale.wake();

        QCOMPARE(transport->m_writes.size(), writesBefore);
        QVERIFY(!scale.m_watchdogTimer || !scale.m_watchdogTimer->isActive());
        QVERIFY(!scale.m_heartbeatTimer || !scale.m_heartbeatTimer->isActive());
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

    // Regression for #1317: with the LCD intentionally off (disableLcd() — the
    // DE1-sleep + keepScaleOn=true path) the ~4-min battery refresh must NOT
    // re-send the display-on command, which is the same byte sequence wake()
    // uses and would silently relight the LCD ~4 min into the user's sleep.
    void batteryPollSuppressedAfterDisableLcd() {
        auto* transport = new MockScaleBleTransport;
        DecentScale scale(transport);

        scale.m_characteristicsReady = true;
        scale.startHeartbeat();
        scale.disableLcd();

        // Drop everything written so far (the disableLcd 0A0000 packet) so the
        // assertion below sees only what the next heartbeat tick produces.
        transport->m_writes.clear();
        // Seed the tick counter so one heartbeat tick crosses the battery-poll
        // boundary — avoids a ~240 s real-time wait.
        scale.m_ticksSinceBatteryPoll = DecentScale::kBatteryPollHeartbeatTicks - 1;

        // One heartbeat fires.
        QTest::qWait(1100);

        // The only payload in this window that should NOT appear is the
        // display-on packet (0x03 0x0A 0x01 0x01 0x00 0x01 [xor]) — that's
        // the bug. Heartbeat (0x03 0x0A 0x03 0xFF 0xFF [xor]) is fine.
        for (const auto& w : std::as_const(transport->m_writes)) {
            QVERIFY2(!w.contains(QByteArray::fromHex("0A01010001")),
                     "Battery poll fired while LCD was off — would relight LCD");
        }
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

    // === DiFluid Microbalance / Microbalance Ti ===

    // The service-selection tests below feed a constant in and assert the same
    // constant comes out, and production compares that symbol to itself in between —
    // so they cannot catch a wrong UUID. This is the assertion that ties the suite to
    // the actual protocol fact the Ti support rests on.
    void difluidServiceUuidsMatchTheProtocol() {
        QCOMPARE(Scale::DiFluid::SERVICE.toString(),
                 QString("{000000ee-0000-1000-8000-00805f9b34fb}"));
        QCOMPARE(Scale::DiFluid::SERVICE_TI.toString(),
                 QString("{000000dd-0000-1000-8000-00805f9b34fb}"));
        QCOMPARE(Scale::DiFluid::CHARACTERISTIC.toString(),
                 QString("{0000aa01-0000-1000-8000-00805f9b34fb}"));
        QVERIFY2(Scale::DiFluid::SERVICE != Scale::DiFluid::SERVICE_TI,
                 "the two models must not share a service, or Ti support is a no-op");
    }

    // DiFluid's worked example from protocolMicrobalance.md. Weight is bytes 5-8,
    // signed, x10 grams: 0x000002F8 = 760 -> 76.0 g.
    static QByteArray difluidWeightFrame(qint32 rawWeight) {
        QByteArray f = QByteArray::fromHex("dfdf03000d");
        f.append(static_cast<char>((rawWeight >> 24) & 0xFF));
        f.append(static_cast<char>((rawWeight >> 16) & 0xFF));
        f.append(static_cast<char>((rawWeight >> 8) & 0xFF));
        f.append(static_cast<char>(rawWeight & 0xFF));
        f.append(QByteArray::fromHex("00000000000a27b000a9"));  // timer + trailer
        return f;
    }

    void difluidParsesTheDocumentedWeightFrame() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        // Verbatim from the vendor doc, checksum byte included.
        const QByteArray doc = QByteArray::fromHex(
            "dfdf03000d000002f800000000000a27b000a9");
        QCOMPARE(doc.size(), 19);
        transport->fakeCharacteristicChanged(Scale::DiFluid::CHARACTERISTIC, doc);

        QVERIFY2(!weightSpy.isEmpty(), "the vendor's own weight frame produced no reading");
        QCOMPARE(scale.weight(), 76.0);
    }

    void difluidWeightGoesNegativeAfterTare() {
        // The field is signed. Read unsigned, -0.5 g became 4294967291 and was
        // discarded by the range gate, freezing the display at its last value.
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeCharacteristicChanged(Scale::DiFluid::CHARACTERISTIC,
                                             difluidWeightFrame(-5));
        QCOMPARE(scale.weight(), -0.5);
    }

    void difluidRejectsImplausibleAndShortFrames() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Sensor frame too short"));
        transport->fakeCharacteristicChanged(Scale::DiFluid::CHARACTERISTIC,
                                             QByteArray::fromHex("dfdf030001"));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Weight out of range"));
        transport->fakeCharacteristicChanged(Scale::DiFluid::CHARACTERISTIC,
                                             difluidWeightFrame(999999));

        QVERIFY(weightSpy.isEmpty());
    }

    void difluidIgnoresSettingsEchoesQuietly() {
        // AA01 is also the characteristic the driver WRITES to, and the DF-DF family
        // echoes settings back — so enableNotifications() and setToGrams() each drew a
        // "too short" warning on every connect, calling healthy traffic malformed. At
        // 5Hz a format mismatch would also evict the 1000-entry scale log, destroying
        // the artifact someone would be asked to share.
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        // Func 1 Cmd 0 echo — no warning, and failOnWarning() enforces that.
        transport->fakeCharacteristicChanged(Scale::DiFluid::CHARACTERISTIC,
                                             QByteArray::fromHex("dfdf01000101c1"));
        QVERIFY(weightSpy.isEmpty());
    }

    void difluidExtremeWeightBytesDoNotTripUndefinedBehaviour() {
        // 80 00 00 00 is INT32_MIN, the canonical "invalid reading" sentinel and the
        // shape an unrecognised frame produces. qAbs() asserts on it in debug builds
        // and is signed-overflow UB in release, so the range check must not use it.
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Weight out of range"));
        transport->fakeCharacteristicChanged(
            Scale::DiFluid::CHARACTERISTIC,
            QByteArray::fromHex("dfdf03000d") + QByteArray::fromHex("80000000")
                + QByteArray::fromHex("00000000000a27b000a9"));

        QVERIFY(weightSpy.isEmpty());
    }

    void difluidPrefersTheFirstServiceWhenBothAreAdvertised() {
        // A vendor keeping the old service for compatibility is exactly the shape of
        // this change, and BLE discovery order is not guaranteed — so this must not
        // be a coin toss.
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE);
        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE_TI);
        transport->fakeServicesDiscoveryFinished();

        QCOMPARE(transport->m_characteristicDiscoveries.size(), 1);
        QCOMPARE(transport->m_characteristicDiscoveries.at(0), Scale::DiFluid::SERVICE);
    }

    void difluidCommandsAreRefusedAndLoggedBeforeDiscovery() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Dropping command"));
        scale.tare();

        QVERIFY2(transport->m_writes.isEmpty(), "a command was written before any service was adopted");
    }

    void difluidDisconnectClearsLinkState() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE_TI);
        transport->fakeServicesDiscoveryFinished();
        transport->fakeCharacteristicsDiscoveryFinished(Scale::DiFluid::SERVICE_TI);
        QVERIFY(scale.isConnected());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("DISCONNECTED"));
        transport->fakeDisconnected();

        // The 100ms notification-enable timer armed by characteristic discovery is
        // still pending. It must not enable notifications on a dropped link — and it
        // returns on the characteristicsReady check before reaching the m_service
        // guard, so it does so silently. Let it fire to prove it stays quiet.
        QTest::qWait(200);
        QVERIFY2(transport->m_notifyEnableCount == 0,
                 "notifications were enabled after the link dropped");

        // Without the reset, sendCommand's guards still pass and every later write
        // goes to a torn-down transport.
        transport->m_writes.clear();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Dropping command"));
        scale.tare();
        QVERIFY(transport->m_writes.isEmpty());
    }

    // === DiFluid Microbalance / Microbalance Ti service selection ===
    //
    // The two models are the same protocol on different services (0x00EE vs
    // 0x00DD). The driver must follow whichever the device advertised through
    // characteristic discovery, notifications and writes — matching only 0x00EE
    // left a Ti connected but permanently silent.

    void difluidClassicServiceDrivesDiscovery() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE);
        transport->fakeServicesDiscoveryFinished();

        QCOMPARE(transport->m_characteristicDiscoveries.size(), 1);
        QCOMPARE(transport->m_characteristicDiscoveries.at(0), Scale::DiFluid::SERVICE);
    }

    void difluidTiServiceDrivesDiscovery() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE_TI);
        transport->fakeServicesDiscoveryFinished();

        QCOMPARE(transport->m_characteristicDiscoveries.size(), 1);
        QCOMPARE(transport->m_characteristicDiscoveries.at(0), Scale::DiFluid::SERVICE_TI);
    }

    void difluidTiCommandsUseTiService() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        transport->fakeServiceDiscovered(Scale::DiFluid::SERVICE_TI);
        transport->fakeServicesDiscoveryFinished();
        transport->fakeCharacteristicsDiscoveryFinished(Scale::DiFluid::SERVICE_TI);

        QVERIFY(scale.isConnected());

        // Setup commands are sent from a 100ms singleShot after discovery. Wait
        // on the outcome, not the clock: a fixed qWait races that timer whenever
        // the machine is loaded, which under the parallel sanitizer build is
        // routine rather than exceptional.
        QTRY_VERIFY(!transport->m_writes.isEmpty());
        QCOMPARE(transport->m_lastWriteService, Scale::DiFluid::SERVICE_TI);
        QCOMPARE(transport->m_lastNotifyService, Scale::DiFluid::SERVICE_TI);

        // And an explicit command after setup keeps using the Ti service.
        scale.tare();
        QCOMPARE(transport->m_lastWriteService, Scale::DiFluid::SERVICE_TI);
    }

    void difluidUnknownServiceIsNotAdopted() {
        auto* transport = new MockScaleBleTransport;
        DifluidScale scale(transport);

        // A Decent Scale service on a device we routed here by name must not be
        // mistaken for a DiFluid one.
        transport->fakeServiceDiscovered(Scale::Decent::SERVICE);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("advertises neither DiFluid service"));
        transport->fakeServicesDiscoveryFinished();

        QVERIFY(transport->m_characteristicDiscoveries.isEmpty());
        QVERIFY(!scale.isConnected());
    }

    // --- Acaia (issues #1669, #1670) --------------------------------------
    //
    // Frames are `EF DD <msgType> <length> <payload…>`, length counting from the
    // length byte itself, so a frame occupies ACAIA_METADATA_LEN(5) + length
    // bytes. AcaiaScale defaults to the IPS protocol, so feeding
    // Scale::AcaiaIPS::CHARACTERISTIC reaches parseResponse() with no discovery
    // handshake.
    //
    // Byte layouts are asserted against pyacaia (decode/Settings), Beanconqueror
    // (decoder.ts) and Decaid (acaia_scale.dart), which agree; de1app parses
    // weight only and never reads a 0x08 settings frame.

    // 0x08 settings frame. Battery is payload[1] = buf[4]; buf[5] is the units
    // byte (2 = grams). Defaults are chosen to discriminate: reverting the fix to
    // buf[5] yields 2 — the exact "always 2%" symptom reported in #1670.
    static QByteArray buildAcaiaSettings(uint8_t battery, uint8_t units = 2) {
        QByteArray pkt;
        pkt.append(static_cast<char>(0xEF));
        pkt.append(static_cast<char>(0xDD));
        pkt.append(static_cast<char>(0x08));
        pkt.append(static_cast<char>(0x0D));            // length
        pkt.append(static_cast<char>(battery));         // buf[4]
        pkt.append(static_cast<char>(units));           // buf[5]
        pkt.append(QByteArray(12, '\0'));               // rest of the frame
        return pkt.left(5 + 0x0D);
    }

    // 0x0C / eventType 5 weight frame. Body is 3-byte LE value, then a decimal
    // exponent at payload[4] and a sign flag at payload[5].
    static QByteArray buildAcaiaWeight(double grams) {
        const uint32_t raw = static_cast<uint32_t>(qRound(qAbs(grams) * 10.0));
        QByteArray pkt;
        pkt.append(static_cast<char>(0xEF));
        pkt.append(static_cast<char>(0xDD));
        pkt.append(static_cast<char>(0x0C));
        pkt.append(static_cast<char>(0x06));            // length
        pkt.append(static_cast<char>(0x05));            // eventType
        pkt.append(static_cast<char>(raw & 0xFF));
        pkt.append(static_cast<char>((raw >> 8) & 0xFF));
        pkt.append(static_cast<char>((raw >> 16) & 0xFF));
        pkt.append(static_cast<char>(0x00));
        pkt.append(static_cast<char>(0x01));            // one decimal place
        pkt.append(static_cast<char>(grams < 0 ? 0x02 : 0x00));
        return pkt;
    }

    static void feedAcaia(MockScaleBleTransport* t, const QByteArray& bytes) {
        t->fakeCharacteristicChanged(Scale::AcaiaIPS::CHARACTERISTIC, bytes);
    }

    void acaiaBatteryComesFromPayloadByteOne() {
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy spy(&scale, &ScaleDevice::batteryLevelChanged);

        feedAcaia(transport, buildAcaiaSettings(90));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 90);
        QCOMPARE(scale.batteryLevel(), 90);
    }

    void acaiaBatteryIsNotTheUnitsByte() {
        // The #1670 regression guard. Units is grams(2) here while the battery is
        // 77, so reading the wrong byte cannot coincidentally pass.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);

        feedAcaia(transport, buildAcaiaSettings(77, /*units=*/2));

        QCOMPARE(scale.batteryLevel(), 77);
        QVERIFY(scale.batteryLevel() != 2);
    }

    void acaiaBatteryHighBitIsMasked() {
        // pyacaia and Beanconqueror mask 0x80 off; Decaid does not. Masking
        // keeps a set high bit from reading as an out-of-range battery.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);

        feedAcaia(transport, buildAcaiaSettings(0x80 | 64));

        QCOMPARE(scale.batteryLevel(), 64);
    }

    void acaiaDecodesSecondFrameInSameNotification() {
        // The frame-carrying fix: a settings frame followed by a weight frame in
        // one notification. The old parser decoded the first and cleared the rest.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);
        QSignalSpy batterySpy(&scale, &ScaleDevice::batteryLevelChanged);

        feedAcaia(transport, buildAcaiaSettings(90) + buildAcaiaWeight(100.0));

        QCOMPARE(batterySpy.count(), 1);
        QCOMPARE(batterySpy.at(0).at(0).toInt(), 90);
        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaDecodesSettingsBehindWeight() {
        // Reverse order — the comment in parseResponse claims this is the common
        // shape on the wire, so pin both orderings.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);
        QSignalSpy batterySpy(&scale, &ScaleDevice::batteryLevelChanged);

        feedAcaia(transport, buildAcaiaWeight(18.5) + buildAcaiaSettings(42));

        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 18.5);
        QCOMPARE(batterySpy.count(), 1);
        QCOMPARE(batterySpy.at(0).at(0).toInt(), 42);
    }

    void acaiaShortFrameDoesNotReadIntoItsNeighbour() {
        // A weight frame whose length byte is too short for its own body must be
        // dropped, not completed from the following frame's bytes. Before the
        // frame-bounded reads this decoded `AA BB EF DD 0C 06` as a weight with
        // unit=12 and emitted a spurious sample ahead of the real one.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        QByteArray truncated = QByteArray::fromHex("EFDD0C0205AABB");
        feedAcaia(transport, truncated + buildAcaiaWeight(100.0));

        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaHeartbeatTimerBodyIsNotAWeight() {
        // eventType 11 carries a selector at buf[7]: 5 = weight, 7 = timer.
        // Decoding a timer body as a weight is what the selector check prevents.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        feedAcaia(transport, QByteArray::fromHex("EFDD0C090B000007123456789ABC"));
        QCOMPARE(weightSpy.count(), 0);

        // The weight-bodied variant of the same event still decodes.
        feedAcaia(transport, QByteArray::fromHex("EFDD0C090B000005E80300000100"));
        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaResyncsPastBogusLength() {
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Frame resync"));
        feedAcaia(transport, QByteArray::fromHex("EFDD0CFF0500") + buildAcaiaWeight(100.0));

        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaCarriesFrameSplitAcrossNotifications() {
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        const QByteArray whole = buildAcaiaWeight(100.0);
        feedAcaia(transport, whole.left(6));
        QCOMPARE(weightSpy.count(), 0);
        feedAcaia(transport, whole.mid(6));

        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaCarriesHeaderSplitAcrossNotifications() {
        // The trailing-0xEF hold-back. The old parser found no header, cleared
        // the buffer, and the next notification then began with a stray 0xDD —
        // losing the frame entirely.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        QSignalSpy weightSpy(&scale, &ScaleDevice::weightChanged);

        const QByteArray whole = buildAcaiaWeight(100.0);
        feedAcaia(transport, QByteArray::fromHex("1122") + whole.left(1));
        feedAcaia(transport, whole.mid(1));

        QCOMPARE(weightSpy.count(), 1);
        QCOMPARE(weightSpy.at(0).at(0).toDouble(), 100.0);
    }

    void acaiaOutOfRangeBatteryIsReportedNotSwallowed() {
        // 101..127 survives the 0x7F mask and means buf[4] is not a battery byte
        // — the evidence #1670 lacked. It must warn rather than vanish, and the
        // level must stay at the unknown sentinel.
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Battery byte out of range"));
        feedAcaia(transport, buildAcaiaSettings(120));

        QCOMPARE(scale.batteryLevel(), -1);
    }

    void acaiaReconnectIsNotRefusedAfterAFailedAttempt() {
        // #1669. The first attempt dies without a disconnected() callback — the
        // shape BLEManager's direct-connect abort produces by severing the
        // controller's signals. The next ladder tick must still reach the
        // transport; the old latched m_isConnecting swallowed it forever.
        auto* transport = new MockScaleBleTransport;
        transport->m_isConnected = false;   // mock defaults to connected
        AcaiaScale scale(transport);

        scale.connectToDevice(QBluetoothDeviceInfo());
        QCOMPARE(transport->m_connectCount, 1);

        scale.connectToDevice(QBluetoothDeviceInfo());
        QCOMPARE(transport->m_connectCount, 2);
    }

    void acaiaConnectIsRefusedOnALiveLink() {
        // The complement: a stray scaleDiscovered for a scale we are already
        // talking to must not run the reset block, which would stop the heartbeat
        // and clear m_characteristicsReady (blocking every later write).
        auto* transport = new MockScaleBleTransport;
        AcaiaScale scale(transport);
        feedAcaia(transport, buildAcaiaWeight(12.0));   // first weight → connected
        QVERIFY(scale.isConnected());

        scale.connectToDevice(QBluetoothDeviceInfo());

        QCOMPARE(transport->m_connectCount, 0);
    }

    // ==========================================
    // supportsTimer(): a driver must not claim a timer it does not send
    // ==========================================
    //
    // startTimer/stopTimer/resetTimer are virtual with EMPTY default bodies, so
    // a scale that does not implement them accepts every timer command and does
    // nothing. supportsTimer() is what lets a caller tell those apart, and its
    // failure mode is silent by construction — nothing on the wire, nothing in a
    // log. Acaia is the case that matters: it OVERRIDES all three, as empty
    // bodies, with a comment saying it has no remote timer control, so reading
    // the override list is exactly what gets this wrong.
    void supportsTimerMatchesWhetherTheDriverSendsAnything() {
        DecentScale decent(nullptr);
        BookooScale bookoo(nullptr);
        DifluidScale difluid(nullptr);
        AcaiaScale acaia(nullptr);

        QVERIFY2(decent.supportsTimer(), "DecentScale::startTimer sends 0B0300");
        QVERIFY2(bookoo.supportsTimer(), "BookooScale::startTimer sends 030A0400000A");
        QVERIFY2(difluid.supportsTimer(), "DifluidScale::startTimer sends DFDF03020100C4");
        QVERIFY2(!acaia.supportsTimer(),
                 "AcaiaScale overrides the three timer slots with EMPTY bodies — it must "
                 "report no timer support, not inherit true from the override list");
    }

    // --- The shared GATT queue, from a scale transport's side (#1819) -----
    //
    // Scale and refractometer transports had no concept of an outstanding
    // operation at all — that asymmetry with the DE1 side IS the bug. These pin
    // the contract the base class now supplies to both implementations: a
    // submitted operation is not issued inline, it holds the slot until it ends,
    // and every way it can end releases it. A path that reaches neither the
    // platform nor a release wedges every device on the radio, which is the one
    // failure a shared queue can introduce that separate queues could not.

    void aSubmittedScaleOperationIsPostedRatherThanIssuedInline() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        bool issued = false;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("op"),
                              [&issued]() { issued = true; });

        QVERIFY(!issued);
        QVERIFY(!queue.isBusy());
        QCOMPARE(queue.pendingCount(), qsizetype(1));

        QTRY_VERIFY(issued);
        QVERIFY(t.holdsGattSlot());
    }

    void aScaleOperationHoldsTheSlotUntilItCompletes() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        int issued = 0;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("first"),
                              [&issued]() { ++issued; });
        t.submitGattOperation(DE1::Characteristic::SHOT_SAMPLE, QStringLiteral("second"),
                              [&issued]() { ++issued; });
        QTRY_COMPARE(queue.inFlightKey(), DE1::Characteristic::STATE_INFO);

        // The second is not issued under the first.
        QCOMPARE(issued, 1);

        t.completeGattOperation(DE1::Characteristic::STATE_INFO);
        QTRY_COMPARE(queue.inFlightKey(), DE1::Characteristic::SHOT_SAMPLE);
        QCOMPARE(issued, 2);
    }

    // Failure is terminal, not a retry: these transports carry no retry budget,
    // which is exactly what they did before they were queued. Inheriting the
    // DE1's would let a dead scale hold the shared slot for ~32 s.
    void aFailedScaleOperationReleasesTheSlotWithoutRetrying() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        int issued = 0;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("op"),
                              [&issued]() { ++issued; });
        QTRY_COMPARE(issued, 1);

        t.failGattOperation();
        QTRY_VERIFY(!queue.isBusy());
        QCOMPARE(issued, 1);
    }

    // A guard that returns before reaching the platform must still release. This
    // is the shape every early return in both real transports now follows, and
    // the one that wedges the radio if it is ever forgotten.
    void anOperationThatNeverReachesThePlatformStillReleasesTheSlot() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        bool secondIssued = false;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("guarded"),
                              [&t]() { t.failGattOperation(); });
        t.submitGattOperation(DE1::Characteristic::SHOT_SAMPLE, QStringLiteral("next"),
                              [&secondIssued]() { secondIssued = true; });

        QTRY_VERIFY(secondIssued);
    }

    // A late or duplicate reply for a characteristic the sequence has already
    // moved past must not release whatever is holding the slot NOW. On a shared
    // queue that "whatever" can belong to another device entirely: a stale
    // notify-enable ACK releasing the DE1's in-flight write would let a third
    // operation be issued on top of it.
    void aCompletionForAKeyThatIsNotInFlightIsIgnored() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        int issued = 0;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("first"),
                              [&issued]() { ++issued; });
        t.submitGattOperation(DE1::Characteristic::SHOT_SAMPLE, QStringLiteral("second"),
                              [&issued]() { ++issued; });
        QTRY_COMPARE(queue.inFlightKey(), DE1::Characteristic::STATE_INFO);

        // A reply for the one that has not been issued yet.
        t.completeGattOperation(DE1::Characteristic::SHOT_SAMPLE);
        QTest::qWait(20);   // a NEGATIVE assertion: nothing must change, so this
                            // waits rather than polls — there is no state to
                            // poll toward, and QTRY_ would pass instantly.
        QCOMPARE(issued, 1);
        QCOMPARE(queue.inFlightKey(), DE1::Characteristic::STATE_INFO);
    }

    // The one clock these transports own, and the only thing that can end an
    // operation the platform never answers at all. Without it a scale that goes
    // away mid-write holds the shared slot for the rest of the session — the DE1
    // included. A backoff would be a timer used as a guard; this is not one:
    // there is no event to wait for, which is precisely the condition.
    void anUnansweredScaleOperationReleasesTheSlotAtItsTimeout() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);
        bool secondIssued = false;

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("silent"),
                              []() {}, /*timeoutMs=*/30);
        t.submitGattOperation(DE1::Characteristic::SHOT_SAMPLE, QStringLiteral("next"),
                              [&secondIssued]() { secondIssued = true; });
        QTRY_VERIFY(t.holdsGattSlot());
        QVERIFY(!secondIssued);

        // Self-contained and WARN, per LOGGING.md: the reader has to be told the
        // radio was held for the whole interval, not just that one device failed.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("The BLE radio was held for that whole time")));

        // The slot moved on. Not `!holdsGattSlot()` — this same transport owns
        // the next operation, so it holds the slot again immediately; what must
        // be true is that the unanswered one no longer does.
        //
        // QTRY_, not qWait: the 30 ms timeout only starts the sequence. It calls
        // noteFailed, and the queue POSTS the next dispatch with a queued
        // connection, so the successor is issued on a LATER event-loop turn. A
        // fixed wait asserts at whatever moment it happens to return, and under
        // a loaded parallel run that moment landed between the timeout and the
        // posted dispatch — nothing in flight, and a red test with no bug behind
        // it. Polling waits for the turn instead of betting on it.
        QTRY_COMPARE(queue.inFlightKey(), DE1::Characteristic::SHOT_SAMPLE);
        QVERIFY(secondIssued);
    }

    void aTornDownScaleTransportFreesTheSlotAndItsQueuedWork() {
        BleGattQueue queue;
        MockScaleBleTransport t(nullptr, &queue);

        t.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("held"), []() {});
        t.submitGattOperation(DE1::Characteristic::SHOT_SAMPLE, QStringLiteral("queued"), []() {});
        QTest::qWait(20);
        QVERIFY(t.holdsGattSlot());

        t.releaseGattQueue();

        QVERIFY(!queue.isBusy());
        QCOMPARE(queue.pendingCount(&t), qsizetype(0));
    }

    // One scale's teardown must not drop another device's work. On a shared
    // queue that is no longer a hypothetical distinction.
    void oneTransportsTeardownLeavesAnothersWorkAlone() {
        BleGattQueue queue;
        MockScaleBleTransport mine(nullptr, &queue);
        MockScaleBleTransport other(nullptr, &queue);

        mine.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("mine"), []() {});
        other.submitGattOperation(DE1::Characteristic::STATE_INFO, QStringLiteral("other"), []() {});

        mine.releaseGattQueue();

        QCOMPARE(queue.pendingCount(&other), qsizetype(1));
    }
};

QTEST_GUILESS_MAIN(tst_ScaleProtocol)
#include "tst_scaleprotocol.moc"
