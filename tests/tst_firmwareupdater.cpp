#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>

#include "controllers/firmwareupdater.h"
#include "core/firmwareassetcache.h"
#include "core/firmwareheader.h"
#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/firmwarepackets.h"
#include "mocks/MockTransport.h"

// FirmwareUpdater state-machine tests (§4).
//
// Drives a real DE1Device (MockTransport) + real FirmwareAssetCache
// (QTemporaryDir root pre-filled with a synthetic 320-byte .dat) through
// the three-phase flow. Timers are injected at 0 ms intervals so the
// test runs in < 100 ms instead of the real 45-second flash.
//
// This file starts with the happy path only (§4a); error-path tests
// (disconnect, timeout, verify failure, precondition, race guard) land
// in §4b, and retry/dismiss/verify-retroactive-success semantics in §4c.

namespace {

void packU32LE(QByteArray& buf, int off, uint32_t v) {
    buf[off + 0] = char(v       & 0xFF);
    buf[off + 1] = char((v >> 8) & 0xFF);
    buf[off + 2] = char((v >> 16)& 0xFF);
    buf[off + 3] = char((v >> 24)& 0xFF);
}

// Build a synthetic valid firmware file: 64-byte header with the real
// BoardMarker, the requested Version, a ByteCount of 256, and a 256-byte
// zero payload. No encryption or checksum fields are populated — client
// side accepts this per the spec (BoardMarker + size check only).
QByteArray makeFirmwareBlob(uint32_t version, qsizetype payloadSize = 256) {
    QByteArray blob(DE1::Firmware::HEADER_SIZE + payloadSize, char(0));
    packU32LE(blob, 4,  DE1::Firmware::BOARD_MARKER);
    packU32LE(blob, 8,  version);
    packU32LE(blob, 12, static_cast<uint32_t>(payloadSize));
    // IV (offsets 28-59) stays all zeros; DE1 side decrypts in real flash,
    // but tests never reach a real device so content doesn't matter.
    return blob;
}

void writeCachedBlob(const FirmwareUpdater*, DE1::Firmware::FirmwareAssetCache* cache,
                     const QByteArray& blob) {
    QFile f(cache->cachePath());
    QVERIFY2(f.open(QIODevice::WriteOnly), "can't write synthetic firmware cache file");
    f.write(blob);
    f.close();
}

}  // namespace

class tst_FirmwareUpdater : public QObject {
    Q_OBJECT

private:
    struct Fixture {
        QTemporaryDir                    tmp;
        MockTransport                    transport;
        DE1Device                        device;
        DE1::Firmware::FirmwareAssetCache cache;
        FirmwareUpdater                  updater;

        Fixture() : updater(&device, &cache) {
            device.setTransport(&transport);
            cache.setCacheRoot(tmp.path());

            // Fast timings so the test runs in milliseconds.
            updater.setPostEraseWaitMs(0);
            updater.setChunkPumpIntervalMs(0);
            updater.setEraseTimeoutMs(5000);
            updater.setVerifyTimeoutMs(5000);

            // Pretend machine is idle and installed firmware is older.
            updater.setMachinePhaseProvider([]{ return static_cast<int>(DE1::State::Idle); });
            updater.setInstalledVersionProvider([]{ return 1200u; });
        }
    };

private slots:

    void happyPath_endToEnd() {
        Fixture f;
        const QByteArray blob = makeFirmwareBlob(/*version*/ 1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        QSignalSpy stateSpy(&f.updater, &FirmwareUpdater::stateChanged);

        // Kick off the update.
        f.updater.startUpdate();

        // Downloading should finish synchronously (cache short-circuits when
        // the file already exists and validates).
        // We then expect the state to progress to Erasing and a
        // writeFWMapRequest(1,1) packet to be queued on A009.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Erasing, 2000);

        // Verify the erase packet landed on A009.
        bool sawEraseReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000101000000")) {
                sawEraseReq = true; break;
            }
        }
        QVERIFY2(sawEraseReq, "erase FWMapRequest was not written to A009");

        // Verify the firmware notifications characteristic was subscribed.
        QVERIFY(f.transport.subscribes.contains(DE1::Characteristic::FW_MAP_REQUEST));

        // Simulate the DE1's Phase 1 notifications.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));  // erase started
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));  // erase done

        // Post-erase wait is 0 ms → pumps the event loop, reaches Uploading.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Uploading, 2000);

        // Wait for all chunks to drain, then updater enters Verifying.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Verifying, 5000);

        // Confirm the exact number of 16-byte chunks went out on A006.
        qsizetype chunkCount = 0;
        bool       sawVerifyReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::WRITE_TO_MMR &&
                w.second.size() == 20 && uint8_t(w.second[0]) == 0x10) {
                chunkCount++;
            }
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000001FFFFFF")) {
                sawVerifyReq = true;
            }
        }
        // de1app streams the ENTIRE file starting at offset 0 (header + payload):
        // the DE1 itself parses the 64-byte header during flash. A 320-byte
        // synthetic blob therefore produces 20 chunks, not 16.
        const qsizetype expectedChunks = (blob.size() + 15) / 16;
        QCOMPARE(chunkCount, expectedChunks);
        QVERIFY2(sawVerifyReq, "verify FWMapRequest was not written to A009");

        // Simulate Phase 3 success notification.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Succeeded, 2000);
        QCOMPARE(f.updater.progress(), 1.0);
        QVERIFY(!f.updater.updateAvailable());

        // stateSpy fired at least once per transition; sanity check the count.
        QVERIFY2(stateSpy.count() >= 4,
                 qPrintable(QStringLiteral("only %1 state transitions").arg(stateSpy.count())));
    }
};

QTEST_GUILESS_MAIN(tst_FirmwareUpdater)
#include "tst_firmwareupdater.moc"
