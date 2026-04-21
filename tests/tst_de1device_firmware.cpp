#include <QtTest>
#include <QSignalSpy>

#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "mocks/MockTransport.h"

// Tests for the firmware-update surface on DE1Device (task §3).
//
// Three write methods + one signal + on-demand A009 subscribe:
//   - writeFWMapRequest(fwToErase, fwToMap, firstError) → A009
//   - writeFirmwareChunk(address, payload16)            → A006 (opcode 0x10)
//   - subscribeFirmwareNotifications()                  → subscribe(A009)
//   - fwMapResponse signal                              ← parsed A009 notify
//
// Firmware chunk writes MUST NOT populate the MMR dedupe cache
// (m_lastMMRValues) — doing so would both pollute the cache with ~28,000
// unique addresses and risk colliding with real MMR registers. The friend-
// class access below lets the test verify this invariant directly.

class tst_DE1DeviceFirmware : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device     device;
        TestFixture() { device.setTransport(&transport); }
    };

private slots:

    // ===== writeFWMapRequest → A009 =====

    void writeFWMapRequest_eraseBytes() {
        TestFixture f;
        f.device.writeFWMapRequest(/*erase*/ 1, /*map*/ 1);

        QCOMPARE(f.transport.writes.size(), qsizetype(1));
        QCOMPARE(f.transport.writes.first().first, DE1::Characteristic::FW_MAP_REQUEST);
        QCOMPARE(f.transport.writes.first().second,
                 QByteArray::fromHex("00000101000000"));
    }

    void writeFWMapRequest_verifyBytes() {
        TestFixture f;
        f.device.writeFWMapRequest(/*erase*/ 0, /*map*/ 1, {0xFF, 0xFF, 0xFF});

        QCOMPARE(f.transport.writes.size(), qsizetype(1));
        QCOMPARE(f.transport.writes.first().first, DE1::Characteristic::FW_MAP_REQUEST);
        QCOMPARE(f.transport.writes.first().second,
                 QByteArray::fromHex("00000001FFFFFF"));
    }

    void writeFWMapRequest_noTransport() {
        // Sanity: calling with no transport attached must not crash.
        DE1Device bare;
        bare.writeFWMapRequest(1, 1);  // expect no crash, no writes
    }

    // ===== writeFirmwareChunk → A006 =====

    void writeFirmwareChunk_toA006WithLengthAndBeAddress() {
        TestFixture f;
        QByteArray payload(16, char(0xAA));
        f.device.writeFirmwareChunk(0x00123456, payload);

        QCOMPARE(f.transport.writes.size(), qsizetype(1));
        QCOMPARE(f.transport.writes.first().first, DE1::Characteristic::WRITE_TO_MMR);

        const QByteArray sent = f.transport.writes.first().second;
        QCOMPARE(sent.size(), qsizetype(20));
        QCOMPARE(uint8_t(sent[0]), uint8_t(16));            // length (matches 0x10 incidentally)
        QCOMPARE(uint8_t(sent[1]), uint8_t(0x12));          // addr BE high
        QCOMPARE(uint8_t(sent[2]), uint8_t(0x34));          // addr BE mid
        QCOMPARE(uint8_t(sent[3]), uint8_t(0x56));          // addr BE low
        for (int i = 0; i < 16; ++i) {
            QCOMPARE(uint8_t(sent[4 + i]), uint8_t(0xAA));
        }
    }

    void writeFirmwareChunk_rejectsWrongSize() {
        // A 15- or 17-byte payload is a bug, not a protocol-legal edge case.
        // Guard against it silently shipping a malformed packet.
        TestFixture f;
        f.device.writeFirmwareChunk(0, QByteArray(15, 0));
        f.device.writeFirmwareChunk(0, QByteArray(17, 0));
        f.device.writeFirmwareChunk(0, QByteArray());
        QCOMPARE(f.transport.writes.size(), qsizetype(0));
    }

    void writeFirmwareChunk_doesNotPopulateMMRCache() {
        // Regression guard for the dedupe-bypass invariant. Chunk writes use
        // A006 (same characteristic as writeMMR) but must not be visible to
        // the per-register dedupe cache — otherwise the hash would grow to
        // ~28,000 entries during one flash and could collide with real MMR
        // addresses.
        TestFixture f;
        // Pre-populate: write a real MMR value.
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        const qsizetype writesBefore = f.transport.writes.size();
        QCOMPARE(f.device.m_lastMMRValues.size(), qsizetype(1));

        // Now write a firmware chunk whose "address" happens to collide with
        // a real MMR register address (STEAM_FLOW = 0x803828). Even so, the
        // dedupe cache must be untouched.
        QByteArray payload(16, char(0));
        f.device.writeFirmwareChunk(DE1::MMR::STEAM_FLOW, payload);

        // New write reached the wire.
        QCOMPARE(f.transport.writes.size(), writesBefore + 1);
        // Cache unchanged — still just the one real MMR entry.
        QCOMPARE(f.device.m_lastMMRValues.size(), qsizetype(1));
        QCOMPARE(f.device.m_lastMMRValues.value(DE1::MMR::STEAM_FLOW), uint32_t(150));
    }

    // ===== subscribeFirmwareNotifications → subscribe(A009) =====

    void subscribeFirmwareNotifications_subscribesA009() {
        TestFixture f;
        QCOMPARE(f.transport.subscribes.contains(DE1::Characteristic::FW_MAP_REQUEST), false);
        f.device.subscribeFirmwareNotifications();
        QCOMPARE(f.transport.subscribes.contains(DE1::Characteristic::FW_MAP_REQUEST), true);
    }

    // ===== fwMapResponse signal fires on A009 notification =====

    void fwMapResponseSignal_firesOnA009Notification() {
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::fwMapResponse);

        // Simulate a DE1 "verify passed" notification.
        emit f.transport.dataReceived(
            DE1::Characteristic::FW_MAP_REQUEST,
            QByteArray::fromHex("00000001FFFFFD"));

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toUInt(),         0u);                           // fwToErase
        QCOMPARE(args.at(1).toUInt(),         1u);                           // fwToMap
        QCOMPARE(args.at(2).toByteArray(),    QByteArray::fromHex("FFFFFD"));// firstError
    }

    void fwMapResponseSignal_firesOnEraseNotification() {
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::fwMapResponse);
        emit f.transport.dataReceived(
            DE1::Characteristic::FW_MAP_REQUEST,
            QByteArray::fromHex("00000100000000"));
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toUInt(), 1u);    // fwToErase
        QCOMPARE(args.at(1).toUInt(), 0u);    // fwToMap
    }

    void fwMapResponseSignal_ignoresShortBuffer() {
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::fwMapResponse);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray(6, 0));
        QCOMPARE(spy.count(), 0);
    }

    void fwMapResponseSignal_notFiredForOtherCharacteristics() {
        TestFixture f;
        QSignalSpy spy(&f.device, &DE1Device::fwMapResponse);
        // A payload that would parse as a valid FWMapRequest, but delivered
        // on the wrong characteristic — must not emit the firmware signal.
        emit f.transport.dataReceived(
            DE1::Characteristic::READ_FROM_MMR,
            QByteArray::fromHex("00000001FFFFFD"));
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(tst_DE1DeviceFirmware)
#include "tst_de1device_firmware.moc"
