#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/de1device.h"
#include "ble/protocol/binarycodec.h"
#include "ble/protocol/de1characteristics.h"
#include "mocks/MockTransport.h"

// Test DE1Device::setShotSettings BLE wire format.
// Expected byte values from de1app binary.tcl return_de1_packed_steam_hotwater_settings.
//
// de1app wire format (9 bytes written to SHOT_SETTINGS characteristic: 7 x U8 + 1 x U16):
//   [0] SteamSettings = 0 (flags, unused)
//   [1] TargetSteamTemp = U8P0(steamTemp)
//   [2] TargetSteamLength = U8P0(steamDuration)
//   [3] TargetHotWaterTemp = U8P0(hotWaterTemp)
//   [4] TargetHotWaterVol = U8P0(hotWaterVolume)
//   [5] TargetHotWaterLength = U8P0(60)  -- de1app: water_time_max
//   [6] TargetEspressoVol = U8P0(200)  -- de1app: espresso_typical_volume = 200
//   [7:8] TargetGroupTemp = U16P8(groupTemp) big-endian

class tst_ShotSettings : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device device;

        TestFixture() {
            device.setTransport(&transport);
        }

        QByteArray callAndCapture(double steamTemp, int steamDuration,
                                  double hotWaterTemp, int hotWaterVolume,
                                  double groupTemp) {
            transport.clearWrites();
            device.setShotSettings(steamTemp, steamDuration, hotWaterTemp, hotWaterVolume, groupTemp);
            return transport.lastWriteData();
        }
    };

private slots:

    // ===== Wire format length =====

    void shotSettingsLength() {
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        // Decenza writes 9 bytes (includes header byte)
        QCOMPARE(data.size(), 9);
    }

    // ===== Individual field encoding =====

    void shotSettingsHeaderByte() {
        // de1app: SteamSettings = 0 & 0x80 & 0x40 = 0
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[0]), uint8_t(0));
    }

    void shotSettingsSteamTemp() {
        // de1app: TargetSteamTemp = U8P0(steam_temperature)
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[1]), BinaryCodec::encodeU8P0(160));
        QCOMPARE(uint8_t(data[1]), uint8_t(160));
    }

    void shotSettingsSteamDuration() {
        // de1app: TargetSteamLength = U8P0(steam_timeout)
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[2]), BinaryCodec::encodeU8P0(120));
        QCOMPARE(uint8_t(data[2]), uint8_t(120));
    }

    void shotSettingsHotWaterTemp() {
        // de1app: TargetHotWaterTemp = U8P0(water_temperature)
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[3]), BinaryCodec::encodeU8P0(80));
        QCOMPARE(uint8_t(data[3]), uint8_t(80));
    }

    void shotSettingsHotWaterVolume() {
        // de1app: TargetHotWaterVol = U8P0(water_volume) when no scale
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[4]), BinaryCodec::encodeU8P0(200));
        QCOMPARE(uint8_t(data[4]), uint8_t(200));
    }

    void shotSettingsHotWaterLength() {
        // de1app: TargetHotWaterLength = U8P0(water_time_max), default 60
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[5]), uint8_t(60));
    }

    void shotSettingsTargetEspressoVol() {
        // de1app: TargetEspressoVol = U8P0(espresso_typical_volume) = 200
        // Bug #556: was hardcoded to 36 instead of 200
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        QCOMPARE(uint8_t(data[6]), uint8_t(200));  // 0xC8
    }

    void shotSettingsGroupTemp() {
        // de1app: TargetGroupTemp = U16P8(espresso_temperature), big-endian
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);
        uint16_t expected = BinaryCodec::encodeU16P8(93.0);  // 93*256 = 23808 = 0x5D00
        QCOMPARE(uint8_t(data[7]), uint8_t((expected >> 8) & 0xFF));  // 0x5D
        QCOMPARE(uint8_t(data[8]), uint8_t(expected & 0xFF));          // 0x00
    }

    // ===== Full byte comparison with de1app expected output =====

    void shotSettingsFullByteArray() {
        // de1app return_de1_packed_steam_hotwater_settings with:
        //   steam=160, duration=120, water_temp=80, water_vol=200, group_temp=93.0
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.0);

        QByteArray expected(9, 0);
        expected[0] = 0;                                    // SteamSettings
        expected[1] = char(160);                            // TargetSteamTemp
        expected[2] = char(120);                            // TargetSteamLength
        expected[3] = char(80);                             // TargetHotWaterTemp
        expected[4] = char(200);                            // TargetHotWaterVol
        expected[5] = char(60);                             // TargetHotWaterLength
        expected[6] = char(200);                            // TargetEspressoVol (de1app: 200)
        uint16_t groupTemp = BinaryCodec::encodeU16P8(93.0);
        expected[7] = char((groupTemp >> 8) & 0xFF);        // TargetGroupTemp high
        expected[8] = char(groupTemp & 0xFF);                // TargetGroupTemp low

        QCOMPARE(data, expected);
    }

    // ===== Edge case: different temperature =====

    void shotSettingsGroupTempPrecision() {
        // Verify fractional temperature encodes correctly
        TestFixture f;
        QByteArray data = f.callAndCapture(160, 120, 80, 200, 93.5);
        uint16_t expected = BinaryCodec::encodeU16P8(93.5);  // 93.5*256 = 23936
        QCOMPARE(uint8_t(data[7]), uint8_t((expected >> 8) & 0xFF));
        QCOMPARE(uint8_t(data[8]), uint8_t(expected & 0xFF));
    }

    // ===== Commanded-value tracking (issue #746 drift detection) =====

    void commandedValuesInitiallyUnset() {
        // Before any write, commanded values report -1 so MainController's
        // drift check can ignore pre-commanded indications (DE1's power-on
        // state isn't something we should "fix").
        TestFixture f;
        QCOMPARE(f.device.commandedSteamTargetC(), -1.0);
        QCOMPARE(f.device.commandedSteamDurationSec(), -1);
        QCOMPARE(f.device.commandedHotWaterTempC(), -1.0);
        QCOMPARE(f.device.commandedHotWaterVolMl(), -1);
        QCOMPARE(f.device.commandedGroupTargetC(), -1.0);
        QCOMPARE(f.device.lastShotSettingsWriteMs(), qint64(0));
    }

    void commandedValuesTrackedOnWrite() {
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QCOMPARE(f.device.commandedSteamTargetC(), 160.0);
        QCOMPARE(f.device.commandedSteamDurationSec(), 120);
        QCOMPARE(f.device.commandedHotWaterTempC(), 80.0);
        QCOMPARE(f.device.commandedHotWaterVolMl(), 200);
        QCOMPARE(f.device.commandedGroupTargetC(), 93.0);
        QVERIFY(f.device.lastShotSettingsWriteMs() > 0);

        // Subsequent write overwrites — latest command wins.
        f.device.setShotSettings(0, 60, 90, 150, 88.0);
        QCOMPARE(f.device.commandedSteamTargetC(), 0.0);
        QCOMPARE(f.device.commandedSteamDurationSec(), 60);
        QCOMPARE(f.device.commandedHotWaterTempC(), 90.0);
        QCOMPARE(f.device.commandedHotWaterVolMl(), 150);
        QCOMPARE(f.device.commandedGroupTargetC(), 88.0);
    }

    // ===== ShotSettings indication parsing (verifies DE1's reported state) =====

    void parseShotSettingsValidPayload() {
        // Construct the 9-byte de1app wire format and feed it to the device
        // as if the DE1 had indicated its current state.
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::shotSettingsReported);

        QByteArray payload(9, 0);
        payload[0] = 0;                                    // SteamSettings flags
        payload[1] = char(145);                            // TargetSteamTemp
        payload[2] = char(120);                            // TargetSteamLength
        payload[3] = char(80);                             // TargetHotWaterTemp
        payload[4] = char(200);                            // TargetHotWaterVol
        payload[5] = char(60);                             // TargetHotWaterLength
        payload[6] = char(200);                            // TargetEspressoVol
        uint16_t groupRaw = BinaryCodec::encodeU16P8(90.25);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);

        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 145.0);   // steam temp
        QCOMPARE(spy.at(0).at(1).toInt(), 120);          // steam duration
        QCOMPARE(spy.at(0).at(2).toDouble(), 80.0);      // hot water temp
        QCOMPARE(spy.at(0).at(3).toInt(), 200);           // hot water vol
        QCOMPARE(spy.at(0).at(4).toDouble(), 90.25);     // group temp
        QCOMPARE(f.device.deviceSteamTargetC(), 145.0);
        QCOMPARE(f.device.deviceSteamDurationSec(), 120);
        QCOMPARE(f.device.deviceHotWaterTempC(), 80.0);
        QCOMPARE(f.device.deviceHotWaterVolMl(), 200);
        QCOMPARE(f.device.deviceGroupTargetC(), 90.25);
    }

    void parseShotSettingsShortPayloadIgnored() {
        // A truncated payload (short BLE frame) must not corrupt state or
        // fire a misleading "reported=0" signal. This guards against
        // mis-parsing a stray 8-byte frame.
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::shotSettingsReported);

        // Expected qWarning from parseShotSettings — silence so the test
        // passes cleanly (otherwise QTest flags it as "passed with warning").
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("\\[BLE DE1\\] parseShotSettings: short payload"));

        QByteArray truncated(8, 0);
        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, truncated);

        QCOMPARE(spy.count(), 0);
        QCOMPARE(f.device.deviceSteamTargetC(), -1.0);
        QCOMPARE(f.device.deviceGroupTargetC(), -1.0);
    }

    void parseShotSettingsHeaterOff() {
        // DE1 reporting steam=0 is how "heater off" looks on the wire.
        // Parser must return 0 exactly (not the 74°C physical idle temp),
        // which is how MainController distinguishes "user wants off" from
        // "heater hasn't heated yet".
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::shotSettingsReported);

        QByteArray payload(9, 0);
        // All zeros except a plausible group temp.
        uint16_t groupRaw = BinaryCodec::encodeU16P8(93.0);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);

        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 0.0);   // steam temp off
        QCOMPARE(spy.at(0).at(1).toInt(), 0);          // duration
        QCOMPARE(spy.at(0).at(2).toDouble(), 0.0);     // hot water temp
        QCOMPARE(spy.at(0).at(3).toInt(), 0);           // hot water vol
    }

    void disconnectResetsCommanded() {
        // On BLE disconnect, the tracker must clear so a reconnect doesn't
        // compare the DE1's post-reconnect indication against a stale
        // commanded value (which would log a spurious drift).
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QVERIFY(f.device.commandedSteamTargetC() > 0);

        f.transport.setConnectedSim(false);

        QCOMPARE(f.device.commandedSteamTargetC(), -1.0);
        QCOMPARE(f.device.commandedSteamDurationSec(), -1);
        QCOMPARE(f.device.commandedHotWaterTempC(), -1.0);
        QCOMPARE(f.device.commandedHotWaterVolMl(), -1);
        QCOMPARE(f.device.commandedGroupTargetC(), -1.0);
        QCOMPARE(f.device.lastShotSettingsWriteMs(), qint64(0));
        QCOMPARE(f.device.deviceSteamTargetC(), -1.0);
        QCOMPARE(f.device.deviceSteamDurationSec(), -1);
        QCOMPARE(f.device.deviceHotWaterTempC(), -1.0);
        QCOMPARE(f.device.deviceHotWaterVolMl(), -1);
        QCOMPARE(f.device.deviceGroupTargetC(), -1.0);
    }

    void disconnectEmitsSentinelSignal() {
        // Q_PROPERTY(deviceSteamTargetC NOTIFY shotSettingsReported) requires
        // an emission whenever the value changes — including the -1 reset on
        // disconnect, otherwise QML bindings would keep showing the previous
        // session's value.
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QSignalSpy spy(&f.device, &DE1Device::shotSettingsReported);

        f.transport.setConnectedSim(false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), -1.0);   // steam temp
        QCOMPARE(spy.at(0).at(1).toInt(), -1);          // duration
        QCOMPARE(spy.at(0).at(2).toDouble(), -1.0);     // hot water temp
        QCOMPARE(spy.at(0).at(3).toInt(), -1);           // hot water vol
        QCOMPARE(spy.at(0).at(4).toDouble(), -1.0);     // group temp
    }

    // ===== Indication-pending flag (event-based stale-indication detection) =====

    void indicationPendingSetOnWriteClearedOnMatch() {
        TestFixture f;
        QVERIFY(!f.device.shotSettingsIndicationPending());

        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QVERIFY(f.device.shotSettingsIndicationPending());

        // A matching indication arrives — flag clears.
        QByteArray payload(9, 0);
        payload[1] = char(160);
        payload[2] = char(120);
        payload[3] = char(80);
        payload[4] = char(200);
        payload[5] = char(60);
        payload[6] = char(200);
        uint16_t groupRaw = BinaryCodec::encodeU16P8(93.0);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);
        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        QVERIFY(!f.device.shotSettingsIndicationPending());
    }

    void indicationPendingStaysOnMismatch() {
        // A stale indication (non-matching value arrives before the DE1 has
        // processed our write) must NOT clear the flag — otherwise a
        // subsequent matching indication would be misinterpreted, and the
        // drift handler would incorrectly treat the stale one as real drift.
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QVERIFY(f.device.shotSettingsIndicationPending());

        // Stale indication with the previous (0) value.
        QByteArray payload(9, 0);
        uint16_t groupRaw = BinaryCodec::encodeU16P8(90.0);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);
        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        QVERIFY(f.device.shotSettingsIndicationPending());
    }

    // ===== resendLastShotSettings: repeats the last payload exactly =====

    void resendLastShotSettingsRepeatsPayload() {
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QByteArray originalWrite = f.transport.lastWriteData();

        f.transport.clearWrites();
        f.device.resendLastShotSettings();

        QCOMPARE(f.transport.writes.size(), 1);
        QCOMPARE(f.transport.lastWriteData(), originalWrite);
        // Resend should arm the pending flag too — we're waiting for a new
        // confirming indication.
        QVERIFY(f.device.shotSettingsIndicationPending());
    }

    void resendLastShotSettingsNoOpBeforeFirstWrite() {
        // Calling resend before any setShotSettings must not write anything.
        TestFixture f;
        f.transport.clearWrites();
        f.device.resendLastShotSettings();
        QCOMPARE(f.transport.writes.size(), 0);
    }

    // ===== Partial-match: pending flag stays set if only some fields match =====

    void indicationPendingPartialMatchStaysPending() {
        // Validates the "lost steam timeout" scenario: steam temp matches but
        // duration doesn't. The pending flag must stay set so MainController
        // detects drift and triggers a resend.
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QVERIFY(f.device.shotSettingsIndicationPending());

        // Build indication that matches steam+group temp but has wrong duration.
        QByteArray payload(9, 0);
        payload[1] = char(160);   // steam temp matches
        payload[2] = char(60);    // duration MISMATCHES (120 commanded)
        payload[3] = char(80);    // hot water temp matches
        payload[4] = char(200);   // hot water vol matches
        payload[5] = char(60);
        payload[6] = char(200);
        uint16_t groupRaw = BinaryCodec::encodeU16P8(93.0);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);
        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        // Pending should stay set because duration doesn't match.
        QVERIFY(f.device.shotSettingsIndicationPending());
    }

    void indicationPendingPartialMatchHotWaterStaysPending() {
        // Hot water volume mismatch: everything else matches but vol doesn't.
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        QVERIFY(f.device.shotSettingsIndicationPending());

        QByteArray payload(9, 0);
        payload[1] = char(160);   // steam temp matches
        payload[2] = char(120);   // duration matches
        payload[3] = char(80);    // hot water temp matches
        payload[4] = char(150);   // hot water vol MISMATCHES (200 commanded)
        payload[5] = char(60);
        payload[6] = char(200);
        uint16_t groupRaw = BinaryCodec::encodeU16P8(93.0);
        payload[7] = char((groupRaw >> 8) & 0xFF);
        payload[8] = char(groupRaw & 0xFF);
        emit f.transport.dataReceived(DE1::Characteristic::SHOT_SETTINGS, payload);

        QVERIFY(f.device.shotSettingsIndicationPending());
    }

    // ===== Dedup: identical writes are skipped =====

    void duplicateWriteSkipped() {
        // Multiple QML signals (state change, phase change, page open, isSteaming
        // change) all fire startSteamHeating() with the same values. The second
        // identical write should be skipped to avoid wasted BLE traffic.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[ShotSettings\\] write skipped"));

        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        f.transport.clearWrites();
        f.device.setShotSettings(160, 120, 80, 200, 93.0);  // identical

        QCOMPARE(f.transport.writes.size(), 0);
    }

    void changedWriteFiresAfterDuplicate() {
        // After a duplicate is skipped, a genuinely different write must still
        // fire. Dedup compares against the LAST sent payload, not historical.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[ShotSettings\\] write skipped"));

        f.device.setShotSettings(160, 120, 80, 200, 93.0);
        f.device.setShotSettings(160, 120, 80, 200, 93.0);  // skipped
        f.transport.clearWrites();
        f.device.setShotSettings(160, 60, 80, 200, 93.0);   // different duration

        QCOMPARE(f.transport.writes.size(), 1);
    }

    void duplicateWriteAfterDisconnectFires() {
        // Disconnect clears m_lastShotSettingsPayload. A subsequent identical
        // write after reconnect must not be deduped against the pre-disconnect
        // value.
        TestFixture f;
        f.device.setShotSettings(160, 120, 80, 200, 93.0);

        f.transport.setConnectedSim(false);
        f.transport.setConnectedSim(true);

        f.transport.clearWrites();
        f.device.setShotSettings(160, 120, 80, 200, 93.0);  // same values
        QCOMPARE(f.transport.writes.size(), 1);  // fired despite same values
    }
};

QTEST_GUILESS_MAIN(tst_ShotSettings)
#include "tst_shotsettings.moc"
