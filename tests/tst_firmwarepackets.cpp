#include <QtTest>

#include "ble/protocol/firmwarepackets.h"

// Tests the pure byte-layout helpers used for DE1 firmware updates over BLE.
// These builders have no Qt runtime dependencies beyond QByteArray, so the
// tests run as a lightweight codec-style unit with no BLE stack, no network,
// and no fixtures. Byte layouts mirror de1app's binary.tcl spec (see the
// firmware-update design doc at docs/plans/2026-04-20-firmware-update-design.md
// section 4.4).

class tst_FirmwarePackets : public QObject {
    Q_OBJECT

private slots:

    // ===== buildFWMapRequest: 7-byte packet written to A009 =====
    // Layout: [WindowIncrement(u16 BE)=0][FWToErase][FWToMap][FirstError×3]

    void buildFWMapRequest_erase() {
        // Phase 1 request: erase=1, map=1, FirstError zero-initialised.
        QByteArray expected = QByteArray::fromHex("00000101000000");
        QCOMPARE(DE1::Firmware::buildFWMapRequest(1, 1), expected);
    }

    void buildFWMapRequest_verify() {
        // Phase 3 request: erase=0, map=1, FirstError = 0xFF,0xFF,0xFF
        // (DE1 fills the response with the first-error location).
        QByteArray expected = QByteArray::fromHex("00000001FFFFFF");
        std::array<uint8_t, 3> seed = {0xFF, 0xFF, 0xFF};
        QCOMPARE(DE1::Firmware::buildFWMapRequest(0, 1, seed), expected);
    }

    void buildFWMapRequest_sizeAlwaysSeven() {
        // Defence: byte length must never drift from the protocol size.
        QCOMPARE(DE1::Firmware::buildFWMapRequest(0, 0).size(), qsizetype(7));
        QCOMPARE(DE1::Firmware::buildFWMapRequest(1, 1).size(), qsizetype(7));
    }

    // ===== buildChunk: 20-byte firmware chunk written to A006 =====
    // Layout: [Opcode=0x10][Addr(u24 LE)][Payload 16 B]

    void buildChunk_layout() {
        // Address 0x00123456 with a 16-byte 0xAA payload.
        QByteArray payload(16, char(0xAA));
        QByteArray packet = DE1::Firmware::buildChunk(0x00123456, payload);

        QCOMPARE(packet.size(), qsizetype(20));
        QCOMPARE(uint8_t(packet[0]), uint8_t(0x10));           // opcode
        QCOMPARE(uint8_t(packet[1]), uint8_t(0x56));           // addr LE byte 0
        QCOMPARE(uint8_t(packet[2]), uint8_t(0x34));           // addr LE byte 1
        QCOMPARE(uint8_t(packet[3]), uint8_t(0x12));           // addr LE byte 2
        for (int i = 0; i < 16; ++i) {
            QCOMPARE(uint8_t(packet[4 + i]), uint8_t(0xAA));   // payload byte i
        }
    }

    void buildChunk_addressZero() {
        QByteArray payload(16, char(0x00));
        QByteArray packet = DE1::Firmware::buildChunk(0, payload);
        QCOMPARE(packet.size(), qsizetype(20));
        QCOMPARE(uint8_t(packet[0]), uint8_t(0x10));
        QCOMPARE(uint8_t(packet[1]), uint8_t(0x00));
        QCOMPARE(uint8_t(packet[2]), uint8_t(0x00));
        QCOMPARE(uint8_t(packet[3]), uint8_t(0x00));
    }

    void buildChunk_rejectsWrongSize() {
        // Payload must be exactly 16 bytes; anything else returns empty so
        // the caller can't silently ship a malformed packet.
        QCOMPARE(DE1::Firmware::buildChunk(0, QByteArray()).size(),       qsizetype(0));
        QCOMPARE(DE1::Firmware::buildChunk(0, QByteArray(15, 0)).size(),  qsizetype(0));
        QCOMPARE(DE1::Firmware::buildChunk(0, QByteArray(17, 0)).size(),  qsizetype(0));
    }

    // ===== parseFWMapNotification: decode A009 notifications =====

    void parseFWMap_verifySuccess() {
        // DE1's "verify passed" reply: FirstError == {FF, FF, FD}.
        QByteArray reply = QByteArray::fromHex("00000001FFFFFD");
        auto parsed = DE1::Firmware::parseFWMapNotification(reply);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->windowIncrement, uint16_t(0));
        QCOMPARE(parsed->fwToErase,       uint8_t(0));
        QCOMPARE(parsed->fwToMap,         uint8_t(1));
        QCOMPARE(parsed->firstError,      DE1::Firmware::VERIFY_SUCCESS);
    }

    void parseFWMap_erasing() {
        // First notification during Phase 1: DE1 confirms it's erasing.
        QByteArray reply = QByteArray::fromHex("00000100000000");
        auto parsed = DE1::Firmware::parseFWMapNotification(reply);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->fwToErase, uint8_t(1));
        QCOMPARE(parsed->fwToMap,   uint8_t(0));
    }

    void parseFWMap_tooShort() {
        // A 6-byte buffer is not a valid FWMapRequest and must be rejected.
        QCOMPARE(DE1::Firmware::parseFWMapNotification(QByteArray(6, 0)).has_value(), false);
        QCOMPARE(DE1::Firmware::parseFWMapNotification(QByteArray()).has_value(),     false);
    }

    void parseFWMap_windowIncrementBigEndian() {
        // WindowIncrement is big-endian on the wire: bytes 0x01,0x02 → 0x0102.
        QByteArray reply = QByteArray::fromHex("01020000000000");
        auto parsed = DE1::Firmware::parseFWMapNotification(reply);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->windowIncrement, uint16_t(0x0102));
    }

    // ===== Invariants on the VERIFY_SUCCESS constant =====

    void verifySuccessConstant() {
        // Hard-coded by DE1 firmware; if this ever changes something is wrong.
        QCOMPARE(DE1::Firmware::VERIFY_SUCCESS[0], uint8_t(0xFF));
        QCOMPARE(DE1::Firmware::VERIFY_SUCCESS[1], uint8_t(0xFF));
        QCOMPARE(DE1::Firmware::VERIFY_SUCCESS[2], uint8_t(0xFD));
    }
};

QTEST_GUILESS_MAIN(tst_FirmwarePackets)
#include "tst_firmwarepackets.moc"
