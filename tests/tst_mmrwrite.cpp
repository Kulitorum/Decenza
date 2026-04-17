#include <QtTest>
#include <QRegularExpression>

#include "ble/de1device.h"
#include "ble/protocol/de1characteristics.h"
#include "mocks/MockTransport.h"

// Verifies DE1Device::writeMMR per-register dedup (issue #783), modelled on
// the setShotSettings dedup (#773). The session log captured after #780
// showed ~30 identical flush-flow MMR bursts in 2.5 s on FlushPage — one
// slider change fanned out through multiple convergent QML signals into
// applyFlushSettings → sendMachineSettings → 3× writeMMR each. With dedup
// only the first real write goes out on the wire.

class tst_MMRWrite : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        MockTransport transport;
        DE1Device device;

        TestFixture() {
            device.setTransport(&transport);
        }
    };

private slots:

    // ===== Basic accept path =====

    void firstWriteFires() {
        // A fresh register with no prior cache must reach the BLE transport.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        QCOMPARE(f.transport.writes.size(), 1);
        QCOMPARE(f.transport.writes.first().first, DE1::Characteristic::WRITE_TO_MMR);
    }

    void differentValuesFire() {
        // Distinct values to the same register must each produce a BLE write.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 175);
        QCOMPARE(f.transport.writes.size(), 2);
    }

    void differentAddressesNotCollapsed() {
        // Dedup is per-address: writing the same value to two different
        // registers must not collapse — the hash keys on address.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 100);
        f.device.writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, 100);
        QCOMPARE(f.transport.writes.size(), 2);
    }

    // ===== Dedup skip path =====

    void duplicateWriteSkipped() {
        // This is the issue #783 regression: applyFlushSettings fires 30+
        // times in 2.5 s with identical values. Only the first write goes out.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // identical

        QCOMPARE(f.transport.writes.size(), 0);
    }

    void changedWriteFiresAfterDuplicate() {
        // Dedup compares against the LAST sent value, not historical. After a
        // skip, a genuinely different value must still go through.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // skipped
        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 200);

        QCOMPARE(f.transport.writes.size(), 1);
    }

    void burstDeduplication() {
        // Emulate the applyFlushSettings burst: 30 identical calls should
        // produce exactly 1 BLE write + 29 skip log lines.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        for (int i = 0; i < 30; ++i) {
            f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        }
        QCOMPARE(f.transport.writes.size(), 1);
    }

    // ===== Force path (USB charger keepalive semantics) =====

    void forceBypassesDedup() {
        // The DE1's USB-charger register has a 10-minute auto-enable timeout
        // that forces us to keep reasserting the commanded value even when
        // unchanged. BatteryManager::tick() relies on setUsbChargerOn(on,
        // force=true) — which must reach the wire regardless of cache.
        TestFixture f;

        f.device.writeMMR(DE1::MMR::USB_CHARGER, 1);
        QCOMPARE(f.transport.writes.size(), 1);

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::USB_CHARGER, 1, QString(), /*force=*/true);
        QCOMPARE(f.transport.writes.size(), 1);
    }

    void setUsbChargerOnForceReachesWire() {
        // Integration check at the higher-level API. setUsbChargerOn forwards
        // its `force` argument through writeMMR, so the keepalive path must
        // produce a wire write every call even when the commanded value is
        // unchanged from the last one.
        TestFixture f;

        f.device.setUsbChargerOn(true, /*force=*/true);
        QCOMPARE(f.transport.writes.size(), 1);

        f.transport.clearWrites();
        f.device.setUsbChargerOn(true, /*force=*/true);  // unchanged value
        QCOMPARE(f.transport.writes.size(), 1);
    }

    // ===== Urgent path =====

    void urgentAlwaysFires() {
        // writeMMRUrgent is for time-critical writes (e.g. ensureChargerOn on
        // app suspend). It must never dedup — if we skipped an urgent write
        // because the cache happened to match, the DE1 might never learn
        // about it before iOS freezes us.
        TestFixture f;

        f.device.writeMMR(DE1::MMR::USB_CHARGER, 1);
        QCOMPARE(f.transport.writes.size(), 1);

        f.transport.clearWrites();
        f.device.writeMMRUrgent(DE1::MMR::USB_CHARGER, 1);  // same value
        QCOMPARE(f.transport.writes.size(), 1);
    }

    void urgentUpdatesCache() {
        // Urgent writes must still populate the cache so a subsequent
        // non-urgent writeMMR with the same value is correctly skipped —
        // otherwise the urgent/non-urgent split would leak extra writes.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        f.device.writeMMRUrgent(DE1::MMR::STEAM_FLOW, 150);
        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // should dedup

        QCOMPARE(f.transport.writes.size(), 0);
    }

    // ===== Disconnect invalidates cache =====

    void disconnectClearsCache() {
        // On disconnect the cache must clear, matching setShotSettings
        // behaviour — the DE1 may power-cycle or lose state between
        // sessions, so a stale cache would silently drop real writes after
        // reconnect.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);

        f.transport.setConnectedSim(false);
        f.transport.setConnectedSim(true);

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // same value

        QCOMPARE(f.transport.writes.size(), 1);  // fired despite same value
    }

    // ===== Log line tagging =====

    void reasonTagAppearsInSkipLog() {
        // Reason strings passed by convergent callers (applyFlushSettings,
        // startSteamHeating, sendMachineSettings, …) must appear in the skip
        // log so post-hoc analysis of captured sessions can attribute
        // redundant traffic to its origin.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150,
                          QStringLiteral("applyFlushSettings"));

        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped:.*\\[applyFlushSettings\\]"));
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150,
                          QStringLiteral("applyFlushSettings"));
    }
};

QTEST_GUILESS_MAIN(tst_MMRWrite)
#include "tst_mmrwrite.moc"
