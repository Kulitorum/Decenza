#include <QtTest>
#include <QRegularExpression>

#include <utility>

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

    // Counts emitted debug lines containing `needle`, for the cases whose whole point is HOW MANY
    // lines came out.
    //
    // QTest::ignoreMessage cannot express that. It consumes one message per call, so "exactly one"
    // has to be written as one ignoreMessage plus a silent hope that a second never arrived —
    // unhandled debug output is not a failure. burstDeduplication was written the other way round,
    // queueing 29 ignoreMessages, and that shape only ever fails when TOO FEW lines arrive: it
    // pinned the noisy behaviour as the expected one and would have passed unchanged had the count
    // gone to 300. Counting fails in both directions.
    class MessageCounter {
    public:
        explicit MessageCounter(QString needle) : m_needle(std::move(needle)) {
            s_active = this;
            m_prev = qInstallMessageHandler(&MessageCounter::handle);
        }
        ~MessageCounter() {
            qInstallMessageHandler(m_prev);
            s_active = nullptr;
        }
        MessageCounter(const MessageCounter&) = delete;
        MessageCounter& operator=(const MessageCounter&) = delete;
        int count() const { return m_count; }

    private:
        static void handle(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
            if (s_active && msg.contains(s_active->m_needle)) {
                s_active->m_count++;
                return;  // swallowed, so it cannot also read as unexpected output
            }
            if (s_active && s_active->m_prev)
                s_active->m_prev(type, ctx, msg);
        }
        QString m_needle;
        int m_count = 0;
        QtMessageHandler m_prev = nullptr;
        static inline MessageCounter* s_active = nullptr;
    };

private slots:
    void init() { QTest::failOnWarning(); }

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
        // Emulate the applyFlushSettings burst: 30 identical calls, one BLE write.
        //
        // And ONE log line, not 29. The elided-write line answers "the machine ignored my setting
        // because the value was already that" — true and useful the first time, and unchanged by
        // being restated 28 more times in the same burst. A field session carried 65 of these in
        // bursts of four, every burst byte-identical to the last.
        //
        // Both counts are asserted because the two failure modes are opposite and a test that
        // checks one hides the other: collapsing too little puts the noise back, and collapsing
        // the WRITE would break the machine.
        TestFixture f;
        MessageCounter skips(QStringLiteral("[MMR] write skipped"));

        for (int i = 0; i < 30; ++i) {
            f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);
        }
        QCOMPARE(f.transport.writes.size(), 1);
        QCOMPARE(skips.count(), 1);
    }

    // The collapse is per (register, value, caller), not per register: a DIFFERENT caller eliding
    // the same value is a different answer to "who tried and was ignored", and that is the whole
    // content of the line. Losing it would make the surviving line name whichever caller happened
    // to be first.
    void skipLineDistinguishesCallers() {
        TestFixture f;
        MessageCounter skips(QStringLiteral("[MMR] write skipped"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150, QStringLiteral("applySteamSettings"));
        for (int i = 0; i < 5; ++i)
            f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150, QStringLiteral("applySteamSettings"));
        for (int i = 0; i < 5; ++i)
            f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150, QStringLiteral("steampage-activated"));

        QCOMPARE(f.transport.writes.size(), 1);
        QCOMPARE(skips.count(), 2);  // one per caller, not one per call and not one overall
    }

    // One caller re-applying a GROUP of registers spells one fact, not one per
    // register. wake-steam-reassert re-applies steam flow, flush flow and flush
    // timeout together; when all three are already current it used to print
    // three consecutive skip lines on every single wake.
    void oneCallersGroupOfSkippedRegistersIsOneLine() {
        TestFixture f;
        MessageCounter skips(QStringLiteral("[MMR] write skipped"));

        const QString reassert = QStringLiteral("wake-steam-reassert");
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 80, reassert);
        f.device.writeMMR(DE1::MMR::FLUSH_FLOW_RATE, 80, reassert);
        f.device.writeMMR(DE1::MMR::FLUSH_TIMEOUT, 350, reassert);
        QCOMPARE(f.transport.writes.size(), 3);   // first time: all three are real writes
        QCOMPARE(skips.count(), 0);

        // Second pass: nothing changed, so nothing goes on the wire and the
        // caller reports once rather than three times.
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 80, reassert);
        f.device.writeMMR(DE1::MMR::FLUSH_FLOW_RATE, 80, reassert);
        f.device.writeMMR(DE1::MMR::FLUSH_TIMEOUT, 350, reassert);
        QCOMPARE(f.transport.writes.size(), 3);
        QCOMPARE(skips.count(), 1);
    }

    // An anonymous skip has no group to speak for it, so it keeps the register
    // name — which is then the entire content of the line.
    void anUntaggedSkipStillNamesItsRegister() {
        TestFixture f;
        MessageCounter skips(QStringLiteral("[MMR] write skipped: SteamFlow 0x803828"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 80);
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 80);
        QCOMPARE(skips.count(), 1);
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

    // ===== Command queue clear invalidates cache =====

    void clearCommandQueueDropsInvalidateCache() {
        // When clearCommandQueue actually drops pending writes, those
        // writes never reached the DE1 — but m_lastMMRValues already
        // recorded their values. If the cache survived, the next identical
        // writeMMR would dedup and the DE1 would never see the dropped
        // value. Callers: MainController::onEspressoCycleStarted at every
        // espresso start, DE1Device::stopOperationUrgent on SAW trigger,
        // MachineState on non-espresso flow-begin.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);

        // Simulate a non-empty transport queue — clearQueue reports >0.
        f.transport.pendingQueueSize = 1;
        f.device.clearCommandQueue();

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // same value

        QCOMPARE(f.transport.writes.size(), 1);  // fired despite same value
    }

    void clearCommandQueueWithEmptyQueuePreservesCache() {
        // Non-espresso flow-begin defensively calls clearCommandQueue even
        // when nothing is pending (guarding against stale profile-upload
        // frames that in practice aren't there). If we invalidated the
        // MMR cache on every such call, every steam/hot-water session
        // would end with three spurious re-sends of the three MMR writes
        // sendMachineSettings emits (steam flow 0x803828, flush flow
        // 0x803840, flush timeout 0x803848). When clearQueue reports
        // nothing dropped, the cache is still in sync with what reached
        // the wire — preserve it.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);

        // pendingQueueSize defaults to 0 — clearQueue is a no-op drop.
        f.device.clearCommandQueue();

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // same value

        QCOMPARE(f.transport.writes.size(), 0);  // deduped — cache held
    }

    // ===== goToSleep: same conditional-invalidation contract =====

    void goToSleepDropsInvalidateCache() {
        // goToSleep bypasses DE1Device::clearCommandQueue() and calls
        // m_transport->clearQueue() directly. Same invariant applies: if
        // a queued write was dropped, the cache might be ahead of the DE1
        // and must be invalidated.
        TestFixture f;
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);

        f.transport.pendingQueueSize = 1;
        f.device.goToSleep();

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // same value

        QCOMPARE(f.transport.writes.size(), 1);  // fired despite same value
    }

    void goToSleepWithEmptyQueuePreservesCache() {
        // Symmetric to clearCommandQueueWithEmptyQueuePreservesCache — if
        // nothing was queued, the cache is still in sync and must survive.
        TestFixture f;
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("\\[MMR\\] write skipped"));

        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);

        f.device.goToSleep();  // pendingQueueSize defaults to 0

        f.transport.clearWrites();
        f.device.writeMMR(DE1::MMR::STEAM_FLOW, 150);  // same value

        QCOMPARE(f.transport.writes.size(), 0);  // deduped — cache held
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
    // An abandoned MMR write must not leave the dedup cache asserting the DE1
    // holds a value it never received. Without this, the next write of the same
    // value is elided as unchanged and the setting is unreachable for the rest
    // of the connection — a permanent loss dressed as a transient one. That used
    // to be caught for one register by read-back verification, which is gone.
    void anAbandonedMmrWriteDropsItsDedupCacheEntry() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        device.writeMMR(DE1::MMR::STEAM_FLOW, 8, QStringLiteral("first"));
        const qsizetype afterFirst = transport.writes.size();
        QVERIFY(afterFirst > 0);

        // Same value again is deduped while the cache still claims it landed.
        device.writeMMR(DE1::MMR::STEAM_FLOW, 8, QStringLiteral("deduped"));
        QCOMPARE(transport.writes.size(), afterFirst);

        // The transport gives up on it.
        // Name AND address: the line is the only record that a specific setting
        // did not reach the machine, and a bare address is not a record a reader
        // (or the AI reading their log) can act on.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("write ABANDONED for SteamFlow 0x803828"));
        emit transport.writeAbandoned(DE1::Characteristic::WRITE_TO_MMR,
                                      transport.writes.last().second);

        // Now the same value is actually re-sent rather than skipped.
        device.writeMMR(DE1::MMR::STEAM_FLOW, 8, QStringLiteral("after-abandon"));
        QCOMPARE(transport.writes.size(), afterFirst + 1);
    }

    // The register table's scale, asserted where it is easiest to get wrong.
    //
    // STEAM_FLOW is the case this table exists for: it reads like tenths (every
    // neighbouring flow register IS tenths) and it is hundredths — SteamPage.qml's
    // flowToDisplay divides by 100. A wrong divisor here does not fail anything,
    // it just prints a confident wrong number into a log a user's AI assistant
    // then reasons from, so pin the two scales against each other.
    void describeRegisterAppliesEachRegistersOwnScale() {
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::STEAM_FLOW, 80),
                 QStringLiteral("SteamFlow 0x803828 = 80 (0.80 mL/s)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::FLUSH_FLOW_RATE, 80),
                 QStringLiteral("FlushFlowRate 0x803840 = 80 (8.0 mL/s)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::HOT_WATER_IDLE_TEMP, 990),
                 QStringLiteral("HeaterIdleTemp 0x803818 = 990 (99.0 \u00B0C)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::FLUSH_TIMEOUT, 350),
                 QStringLiteral("FlushTimeout 0x803848 = 350 (35.0 s)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::FLOW_CALIBRATION, 879),
                 QStringLiteral("FlowCalibration 0x80383c = 879 (\u00D70.879)"));
    }

    // A zero that means "disabled" must not render as a commanded quantity:
    // "0 °C" reads as an instruction to hold the tank at freezing.
    void describeRegisterRendersModeZerosAsModes() {
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::FAN_THRESHOLD, 0),
                 QStringLiteral("FanThreshold 0x803808 = 0 (always on)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::FAN_THRESHOLD, 60),
                 QStringLiteral("FanThreshold 0x803808 = 60 (60 \u00B0C)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::TANK_TEMP_THRESHOLD, 0),
                 QStringLiteral("TankTempThreshold 0x80380c = 0 (preheat off)"));
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::USB_CHARGER, 1),
                 QStringLiteral("UsbCharger 0x803854 = 1 (on)"));
    }

    // An unknown address, and a known one with no evidenced scale, both fall back
    // to the raw value rather than inventing a unit. STEAM_HIGHFLOW_START has no
    // documented scale anywhere in the repo — it must stay bare until it does.
    void describeRegisterNeverInventsAScale() {
        QCOMPARE(DE1::MMR::describeRegister(DE1::MMR::STEAM_HIGHFLOW_START, 70),
                 QStringLiteral("SteamHighFlowStart 0x80382c = 70"));
        QCOMPARE(DE1::MMR::describeRegister(0x809999, 5),
                 QStringLiteral("0x809999 = 5"));
    }

    // A non-MMR abandonment must not disturb the cache.
    void anAbandonedNonMmrWriteLeavesTheCacheAlone() {
        MockTransport transport;
        DE1Device device;
        device.setTransport(&transport);

        device.writeMMR(DE1::MMR::STEAM_FLOW, 8, QStringLiteral("first"));
        const qsizetype afterFirst = transport.writes.size();

        emit transport.writeAbandoned(DE1::Characteristic::FRAME_WRITE,
                                      QByteArray(20, 0));

        device.writeMMR(DE1::MMR::STEAM_FLOW, 8, QStringLiteral("still-deduped"));
        QCOMPARE(transport.writes.size(), afterFirst);
    }

};

QTEST_GUILESS_MAIN(tst_MMRWrite)
#include "tst_mmrwrite.moc"
