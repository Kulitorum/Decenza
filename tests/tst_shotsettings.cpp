#include <QtTest>

#include "ble/de1device.h"
#include "ble/protocol/binarycodec.h"
#include "mocks/MockTransport.h"

// Test DE1Device::setShotSettings BLE wire format.
// Expected byte values from de1app binary.tcl return_de1_packed_steam_hotwater_settings.
//
// de1app wire format (8 bytes written to SHOT_SETTINGS characteristic):
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
};

QTEST_GUILESS_MAIN(tst_ShotSettings)
#include "tst_shotsettings.moc"
