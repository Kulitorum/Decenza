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

// Drive the test through the upload phase by waiting for all chunks to be
// queued into MockTransport, then simulating BLE ACKs for each. Returns
// after the state has advanced to Verifying (or whatever follows, depending
// on timings in the specific test).
void simulateFullUpload(MockTransport& transport, const QByteArray& blob,
                        int ackWaitTimeoutMs = 5000) {
    const qsizetype expectedChunks = (blob.size() + 15) / 16;
    // Expected writes at this point: 1 erase FWMapRequest + N chunks.
    QTRY_VERIFY_WITH_TIMEOUT(
        transport.writes.size() >= 1 + expectedChunks, ackWaitTimeoutMs);
    transport.ackAllWritesInOrder();
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
            updater.setPostUploadSettleMs(0);
            updater.setEraseTimeoutMs(5000);
            updater.setVerifyTimeoutMs(5000);

            // Pretend machine is idle and installed firmware is older.
            updater.setPreconditionProvider([]{ return true; });
            updater.setInstalledVersionProvider([]{ return 1200u; });
        }
    };

private slots:

    // ===== §4b: error paths =====

    void eraseTimeout_failsRetryable() {
        Fixture f;
        f.updater.setEraseTimeoutMs(50);                // short timeout
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        // Deliberately never send the erase-done notification.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Erase"));
    }

    void disconnectDuringUpload_failsRetryable() {
        Fixture f;
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352, 4096));  // more chunks
        f.updater.setChunkPumpIntervalMs(5);            // slow enough to catch mid-upload
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Uploading);
        // Yank the BLE transport mid-upload.
        f.transport.setConnectedSim(false);
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(), FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("disconnected", Qt::CaseInsensitive));
    }

    void verifyFailure_reportsErrorOffsetRetryable() {
        Fixture f;
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);
        // Non-success firstError: {0x12, 0x34, 0x56} (arbitrary non-success)
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001123456"));
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Failed);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Verification"));
    }

    void preconditionRefuses_duringShot() {
        Fixture f;
        // Override: machine is pulling a shot — Update must refuse.
        f.updater.setPreconditionProvider([]{ return false; });
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();

        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Failed);
        QVERIFY(f.updater.retryAvailable());
        QVERIFY(f.updater.errorMessage().contains("Finish", Qt::CaseInsensitive));
        // No BLE writes — refused before any firmware transaction.
        bool sawFirmwareWrite = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST ||
                (w.first == DE1::Characteristic::WRITE_TO_MMR &&
                 w.second.size() == 20 && uint8_t(w.second[0]) == 0x10)) {
                sawFirmwareWrite = true; break;
            }
        }
        QVERIFY(!sawFirmwareWrite);
    }

    void raceGuardAlreadyUpdated_jumpsToSucceeded() {
        Fixture f;
        // Installed version already equals what the file contains.
        f.updater.setInstalledVersionProvider([]{ return 1352u; });
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();

        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Succeeded);
        // Race guard fired before any BLE write: no erase, no chunks.
        bool sawEraseReq = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST) {
                sawEraseReq = true; break;
            }
        }
        QVERIFY(!sawEraseReq);
    }

    // ===== §4b+: firmware-flash MMR-write guard =====

    void firmwareGuard_engagedDuringFlash_andClearedOnSuccess() {
        Fixture f;
        const QByteArray blob = makeFirmwareBlob(/*version*/ 1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        QVERIFY(!f.device.firmwareFlashInProgress());
        f.updater.startUpdate();

        // Guard should be engaged as soon as we hit Erasing.
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        QVERIFY(f.device.firmwareFlashInProgress());

        // Drive the flash to completion.
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001FFFFFD"));
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Succeeded);

        // Guard must be cleared once we reach Succeeded — otherwise every
        // subsequent MMR write would be silently dropped.
        QVERIFY(!f.device.firmwareFlashInProgress());
    }

    void firmwareGuard_clearedOnFailure() {
        Fixture f;
        f.updater.setEraseTimeoutMs(50);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        QVERIFY(f.device.firmwareFlashInProgress());

        // Let the erase timeout fire → failWith must clear the guard.
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(!f.device.firmwareFlashInProgress());
    }

    void firmwareGuard_dropsMMRWrites() {
        Fixture f;
        // Baseline: a normal writeMMR goes through to the transport.
        f.device.writeMMR(0x80000C, 42, QStringLiteral("baseline"));
        const qsizetype baselineWrites = f.transport.writes.size();
        QVERIFY(baselineWrites > 0);

        // Engage the guard directly (avoid the full flash flow here — this
        // test targets only the DE1Device write gate).
        f.device.setFirmwareFlashInProgress(true);

        // All three MMR write paths must now drop their packets. We call
        // each with a distinct address so dedup can't mask the drop via an
        // unchanged-value short-circuit.
        f.device.writeMMR(0x80000D, 42, QStringLiteral("blocked"));
        f.device.writeMMRUrgent(0x80000E, 42, QStringLiteral("blocked"));
        f.device.writeMMRVerified(0x80000F, 42, QStringLiteral("blocked"));

        QCOMPARE(f.transport.writes.size(), baselineWrites);

        // Once the guard clears, writes go through again.
        f.device.setFirmwareFlashInProgress(false);
        f.device.writeMMR(0x800010, 42, QStringLiteral("post-flash"));
        QVERIFY(f.transport.writes.size() > baselineWrites);
    }

    void startUpdate_isNoOpOnSimulator() {
        Fixture f;
        f.device.setSimulationMode(true);
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));

        f.updater.startUpdate();

        // Sim-mode gate in startUpdate() means we never leave Idle and no
        // BLE traffic is issued.
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Idle);
        QVERIFY(!f.device.firmwareFlashInProgress());
        for (const auto& w : f.transport.writes) {
            QVERIFY2(w.first != DE1::Characteristic::FW_MAP_REQUEST,
                     "simulator must not have issued any FWMapRequest");
        }
    }

    // ===== §4c: retry, dismiss, verify-disconnect retroactive success =====

    void retryAfterFailure_restartsFromErase() {
        Fixture f;
        writeCachedBlob(&f.updater, &f.cache, makeFirmwareBlob(1352));
        // Short erase timeout so the first attempt fails fast.
        f.updater.setEraseTimeoutMs(50);
        f.updater.startUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());

        // Restore a sensible erase timeout so retry has room to succeed.
        f.updater.setEraseTimeoutMs(5000);
        f.transport.clearWrites();

        f.updater.retry();
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Erasing, 2000);

        // A fresh erase packet (the same 7-byte "00000101000000") must
        // appear on the wire — retry restarts from Phase 1, not from wherever
        // the previous attempt left off.
        bool sawFreshErase = false;
        for (const auto& w : f.transport.writes) {
            if (w.first == DE1::Characteristic::FW_MAP_REQUEST &&
                w.second == QByteArray::fromHex("00000101000000")) {
                sawFreshErase = true; break;
            }
        }
        QVERIFY2(sawFreshErase, "retry did not re-issue the erase request");
    }

    void dismissAvailability_suppressesBannerForSameVersion() {
        Fixture f;
        // Drive the availability signal directly — no startUpdate, no
        // Erasing state to get in the way.
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind = DE1::Firmware::FirmwareAssetCache::CheckResult::Newer;
        r.remoteVersion = 1352;

        emit f.cache.checkFinished(r);
        QVERIFY(f.updater.updateAvailable());

        f.updater.dismissAvailability();
        QVERIFY(!f.updater.updateAvailable());

        // Same remote version again → stays dismissed.
        emit f.cache.checkFinished(r);
        QVERIFY(!f.updater.updateAvailable());
    }

    void olderRemoteSetsDowngradeAvailability() {
        // Mirrors de1app's "Firmware downgrade available" UX: when the
        // cached firmware is strictly older than what's on the DE1 (e.g.
        // user flipped channel nightly → stable), updateAvailable still
        // becomes true and isDowngrade is set.
        Fixture f;
        f.updater.setInstalledVersionProvider([]{ return uint32_t(1352); });
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind          = DE1::Firmware::FirmwareAssetCache::CheckResult::Older;
        r.remoteVersion = 1333;
        emit f.cache.checkFinished(r);

        QVERIFY(f.updater.updateAvailable());
        QVERIFY(f.updater.isDowngrade());
        QCOMPARE(f.updater.availableVersion(), 1333);
    }

    void dismissedBannerReappearsOnNewerVersion() {
        Fixture f;
        DE1::Firmware::FirmwareAssetCache::CheckResult r;
        r.kind = DE1::Firmware::FirmwareAssetCache::CheckResult::Newer;

        r.remoteVersion = 1352;
        emit f.cache.checkFinished(r);
        f.updater.dismissAvailability();
        QVERIFY(!f.updater.updateAvailable());

        // A strictly newer version clears the dismissal → banner returns.
        r.remoteVersion = 1353;
        emit f.cache.checkFinished(r);
        QVERIFY(f.updater.updateAvailable());
    }

    void verifyDisconnectRetroactive_succeedsOnVersionMatch() {
        Fixture f;
        auto installed = std::make_shared<uint32_t>(1200);
        f.updater.setInstalledVersionProvider([installed]{ return *installed; });
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);

        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // Disconnect mid-verify — ambiguous: could be a real failure, could
        // be the DE1's post-flash reboot.
        f.transport.setConnectedSim(false);
        // State should NOT flip to Failed immediately; it waits in the
        // grace window.
        QCOMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        // Post-reboot: DE1 comes back reporting the new firmware version.
        *installed = 1352;
        f.transport.setConnectedSim(true);

        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Succeeded, 2000);
    }

    void verifyDisconnectGraceTimeout_failsRetryable() {
        Fixture f;
        f.updater.setVerifyDisconnectGraceMs(50);   // short grace for test
        const QByteArray blob = makeFirmwareBlob(1352);
        writeCachedBlob(&f.updater, &f.cache, blob);
        f.updater.startUpdate();
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Erasing);
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000100000000"));
        emit f.transport.dataReceived(DE1::Characteristic::FW_MAP_REQUEST,
                                      QByteArray::fromHex("00000001000000"));
        simulateFullUpload(f.transport, blob);
        QTRY_COMPARE(f.updater.state(), FirmwareUpdater::State::Verifying);

        f.transport.setConnectedSim(false);
        // Never reconnects; grace timer fires → Failed (retryable).
        QTRY_COMPARE_WITH_TIMEOUT(f.updater.state(),
                                  FirmwareUpdater::State::Failed, 2000);
        QVERIFY(f.updater.retryAvailable());
    }

    // ===== §4a: happy path =====

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

        // Wait for all chunks to land in MockTransport (the pump queues them
        // synchronously with a 0 ms interval). The expected write count is
        // 1 (erase FWMapRequest) + N (firmware chunks).
        const qsizetype expectedChunksQueued = (blob.size() + 15) / 16;
        QTRY_VERIFY_WITH_TIMEOUT(
            f.transport.writes.size() >= 1 + expectedChunksQueued, 5000);

        // Now simulate BLE ACKing each write. The updater counts
        // WRITE_TO_MMR ACKs with length==16; after the last chunk's ACK
        // it schedules beginVerifyPhase() after postUploadSettleMs (0 in
        // test mode).
        f.transport.ackAllWritesInOrder();

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
