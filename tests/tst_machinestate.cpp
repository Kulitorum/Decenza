#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "machine/machinestate.h"
#include "ble/de1device.h"
#include "core/settings.h"
#include "mocks/MockScaleDevice.h"

// Test MachineState phase transitions, volume reset, tare lifecycle, and signals.
// Uses DECENZA_TESTING friend access to manipulate private state directly.
// Reuses TestFixture pattern from tst_sav.

class tst_MachineState : public QObject {
    Q_OBJECT

private:
    struct TestFixture {
        DE1Device device;
        Settings settings;
        MockScaleDevice scale;
        MachineState state{&device};

        TestFixture() {
            state.setSettings(&settings);
            state.setScale(&scale);
            // Use simulation mode so isConnected() returns true without a BLE transport
            device.m_simulationMode = true;
        }

        void setDE1State(DE1::State s, DE1::SubState ss) {
            device.m_state = s;
            device.m_subState = ss;
            state.onDE1StateChanged();
        }
    };

    // Puts the fixture in the pre-flow window with a completed tare, which is the
    // only state in which either gate is live.
    void armPreheat(TestFixture& f) {
        f.scale.mockSetConnected(true);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Heating);
        f.state.m_tareCompleted = true;
        f.state.m_waitingForTare = false;
        f.scale.resetTareCount();
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ==========================================
    // Phase Mapping (de1app update_de1_state)
    // ==========================================

    void phaseMapping_data() {
        QTest::addColumn<int>("de1State");
        QTest::addColumn<int>("de1SubState");
        QTest::addColumn<int>("expectedPhase");

        // Sleep
        QTest::newRow("Sleep")
            << int(DE1::State::Sleep) << int(DE1::SubState::Ready)
            << int(MachineState::Phase::Sleep);
        QTest::newRow("GoingToSleep")
            << int(DE1::State::GoingToSleep) << int(DE1::SubState::Ready)
            << int(MachineState::Phase::Sleep);

        // Idle with substates
        QTest::newRow("Idle/Ready")
            << int(DE1::State::Idle) << int(DE1::SubState::Ready)
            << int(MachineState::Phase::Ready);
        QTest::newRow("Idle/Heating")
            << int(DE1::State::Idle) << int(DE1::SubState::Heating)
            << int(MachineState::Phase::Heating);
        QTest::newRow("Idle/Stabilising")
            << int(DE1::State::Idle) << int(DE1::SubState::Stabilising)
            << int(MachineState::Phase::Ready);

        // Espresso substates
        QTest::newRow("Espresso/Heating")
            << int(DE1::State::Espresso) << int(DE1::SubState::Heating)
            << int(MachineState::Phase::EspressoPreheating);
        QTest::newRow("Espresso/Preinfusion")
            << int(DE1::State::Espresso) << int(DE1::SubState::Preinfusion)
            << int(MachineState::Phase::Preinfusion);
        QTest::newRow("Espresso/Pouring")
            << int(DE1::State::Espresso) << int(DE1::SubState::Pouring)
            << int(MachineState::Phase::Pouring);
        QTest::newRow("Espresso/Ending")
            << int(DE1::State::Espresso) << int(DE1::SubState::Ending)
            << int(MachineState::Phase::Ending);

        // Other states
        QTest::newRow("HotWater")
            << int(DE1::State::HotWater) << int(DE1::SubState::Pouring)
            << int(MachineState::Phase::HotWater);
        QTest::newRow("HotWaterRinse/Flush")
            << int(DE1::State::HotWaterRinse) << int(DE1::SubState::Pouring)
            << int(MachineState::Phase::Flushing);
        QTest::newRow("Steam/Steaming")
            << int(DE1::State::Steam) << int(DE1::SubState::Steaming)
            << int(MachineState::Phase::Steaming);
        QTest::newRow("Descale")
            << int(DE1::State::Descale) << int(DE1::SubState::Pouring)
            << int(MachineState::Phase::Descaling);
        QTest::newRow("Clean")
            << int(DE1::State::Clean) << int(DE1::SubState::Pouring)
            << int(MachineState::Phase::Cleaning);
        QTest::newRow("Refill")
            << int(DE1::State::Refill) << int(DE1::SubState::Ready)
            << int(MachineState::Phase::Refill);
        QTest::newRow("AirPurge")
            << int(DE1::State::AirPurge) << int(DE1::SubState::Ready)
            << int(MachineState::Phase::Transport);
    }

    void phaseMapping() {
        QFETCH(int, de1State);
        QFETCH(int, de1SubState);
        QFETCH(int, expectedPhase);

        TestFixture f;
        f.setDE1State(static_cast<DE1::State>(de1State),
                      static_cast<DE1::SubState>(de1SubState));
        QCOMPARE(int(f.state.phase()), expectedPhase);
    }

    // ==========================================
    // isFlowing property
    // ==========================================

    void isFlowing_data() {
        QTest::addColumn<int>("de1State");
        QTest::addColumn<int>("de1SubState");
        QTest::addColumn<bool>("expectedFlowing");

        QTest::newRow("Preinfusion=true")
            << int(DE1::State::Espresso) << int(DE1::SubState::Preinfusion) << true;
        QTest::newRow("Pouring=true")
            << int(DE1::State::Espresso) << int(DE1::SubState::Pouring) << true;
        QTest::newRow("HotWater=true")
            << int(DE1::State::HotWater) << int(DE1::SubState::Pouring) << true;
        QTest::newRow("Steaming=true")
            << int(DE1::State::Steam) << int(DE1::SubState::Steaming) << true;
        QTest::newRow("Flushing=true")
            << int(DE1::State::HotWaterRinse) << int(DE1::SubState::Pouring) << true;
        QTest::newRow("Idle=false")
            << int(DE1::State::Idle) << int(DE1::SubState::Ready) << false;
        QTest::newRow("Sleep=false")
            << int(DE1::State::Sleep) << int(DE1::SubState::Ready) << false;
        QTest::newRow("EspressoPreheating=false")
            << int(DE1::State::Espresso) << int(DE1::SubState::Heating) << false;
    }

    void isFlowing() {
        QFETCH(int, de1State);
        QFETCH(int, de1SubState);
        QFETCH(bool, expectedFlowing);

        TestFixture f;
        f.setDE1State(static_cast<DE1::State>(de1State),
                      static_cast<DE1::SubState>(de1SubState));
        QCOMPARE(f.state.isFlowing(), expectedFlowing);
    }

    // ==========================================
    // Volume reset between extractions (bug #505)
    // ==========================================

    void volumeResetOnNewExtraction() {
        // Bug #505: stale volume counters from first shot caused instant stop on second shot
        TestFixture f;

        // First extraction: accumulate volume
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Pouring);
        f.state.m_pourVolume = 50.0;
        f.state.m_preinfusionVolume = 10.0;
        f.state.m_cumulativeVolume = 60.0;

        // Return to idle
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);

        // Second extraction: volumes must be 0
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QCOMPARE(f.state.m_pourVolume, 0.0);
        QCOMPARE(f.state.m_preinfusionVolume, 0.0);
        QCOMPARE(f.state.m_cumulativeVolume, 0.0);
    }

    void volumeResetOnHotWater() {
        // Hot water also resets volumes
        TestFixture f;

        // First: hot water with volume
        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        f.state.m_pourVolume = 200.0;

        // Return to idle
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);

        // Second hot water: should be reset
        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        QCOMPARE(f.state.m_pourVolume, 0.0);
    }

    // ==========================================
    // Hot water tare
    // ==========================================

    // The tare is scheduled a moment after flow starts, to keep it off the timer
    // commands' heels. Weight samples arrive in between carrying the PREVIOUS pour's
    // zero (a field log read -138.40 g, 56 ms before its own tare fired). Skipping them
    // is right; reporting "Tare not completed" is not — that names a tare that failed,
    // and this one had not been sent yet. init() calls QTest::failOnWarning(), so the
    // silence is asserted.
    void hotWaterTareInFlightIsNotReportedAsAFailedTare() {
        TestFixture f;

        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        QVERIFY(f.state.m_hotWaterTarePending);   // scheduled, not yet sent
        QVERIFY(!f.state.m_tareCompleted);
        f.state.checkStopAtWeightHotWater(-138.4);
    }

    // ...and the warning is still there for a tare that really did not complete: the
    // gate is the pendency, not the phase.
    void hotWaterTareNeverCompletedStillWarns() {
        TestFixture f;

        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        f.state.m_hotWaterTarePending = false;    // the scheduled tare has run
        QVERIFY(!f.state.m_tareCompleted);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Tare not completed"));
        f.state.checkStopAtWeightHotWater(-138.4);
    }

    // ==========================================
    // Stop flags reset
    // ==========================================

    void stopFlagsResetOnNewExtraction() {
        TestFixture f;

        // First extraction triggers all stops
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Pouring);
        f.state.m_stopAtWeightTriggered = true;
        f.state.m_stopAtVolumeTriggered = true;
        f.state.m_stopAtTimeTriggered = true;

        // Return to idle
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);

        // New extraction: all flags cleared
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QVERIFY(!f.state.m_stopAtWeightTriggered);
        QVERIFY(!f.state.m_stopAtVolumeTriggered);
        QVERIFY(!f.state.m_stopAtTimeTriggered);
    }

    // ==========================================
    // Hot water weight (bugs #530, #509)
    // ==========================================

    void hotWaterFrozenWeightClearedOnNewFlow() {
        // Bug #530: hot water frozen weight persists into espresso display
        TestFixture f;

        // Hot water SAW freezes display
        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        f.state.m_hotWaterFrozenWeight = 75.0;

        // Return to idle
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);

        // Start espresso: frozen weight must be cleared
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QCOMPARE(f.state.m_hotWaterFrozenWeight, -1.0);  // -1 means "not frozen"
    }

    void hotWaterBaselineResetOnNewFlow() {
        // Hot water tare baseline must not leak into espresso
        TestFixture f;

        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        f.state.m_hotWaterTareBaseline = 150.0;

        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QCOMPARE(f.state.m_hotWaterTareBaseline, 0.0);
    }

    // ==========================================
    // Signal verification
    // ==========================================

    void phaseChangedSignalEmitted() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::phaseChanged);

        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCoreApplication::processEvents();  // Signals use QueuedConnection
        QVERIFY(spy.count() >= 1);
    }

    void espressoCycleStartedSignal() {
        // espressoCycleStarted should emit when entering espresso from non-espresso
        TestFixture f;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCoreApplication::processEvents();

        QSignalSpy spy(&f.state, &MachineState::espressoCycleStarted);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Heating);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void espressoCycleStartedNotOnSubstateChange() {
        // Should NOT re-emit when transitioning within espresso (e.g., preheating → preinfusion)
        TestFixture f;
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Heating);
        QCoreApplication::processEvents();

        QSignalSpy spy(&f.state, &MachineState::espressoCycleStarted);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);  // Already in espresso cycle
    }

    void shotEndedSignalOnExitToIdle() {
        // shotEnded emits when leaving an active espresso phase to non-espresso
        TestFixture f;
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Pouring);
        QCoreApplication::processEvents();

        QSignalSpy spy(&f.state, &MachineState::shotEnded);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    // ==========================================
    // Tare lifecycle
    // ==========================================

    void tareResetOnNewExtraction() {
        TestFixture f;

        // Simulate completed tare from previous extraction
        f.state.m_tareCompleted = true;
        f.state.m_waitingForTare = false;

        // Return to idle then start new extraction
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Preinfusion);

        // New extraction should have tare cleared
        QVERIFY(!f.state.m_tareCompleted);
        QVERIFY(!f.state.m_waitingForTare);
    }

    // ==========================================
    // Disconnected state
    // ==========================================

    void disconnectedWhenDeviceNotConnected() {
        TestFixture f;
        f.device.m_simulationMode = false;  // No transport + no sim = disconnected
        f.state.onDE1StateChanged();
        QCOMPARE(f.state.phase(), MachineState::Phase::Disconnected);
    }

    void disconnectedOverridesAnyDE1State() {
        TestFixture f;
        // Set to espresso first
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Pouring);
        QCOMPARE(f.state.phase(), MachineState::Phase::Pouring);

        // Disconnect
        f.device.m_simulationMode = false;
        f.state.onDE1StateChanged();
        QCOMPARE(f.state.phase(), MachineState::Phase::Disconnected);
    }

    // ==========================================
    // Standby switch (Error_NoAC) — de1app commit 04d3b02e
    // ==========================================

    void standbySwitchOpenOnSupportedFirmware() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1337;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        elapseNoAcWait(f);
        QVERIFY(f.state.standbySwitchOpen());
    }

    void standbySwitchStaysClosedOnOldFirmware_data() {
        QTest::addColumn<int>("firmwareBuildNumber");
        QTest::newRow("unknown (0)") << 0;
        QTest::newRow("just below the gate") << 1336;
        QTest::newRow("well below the gate") << 1200;
    }

    void standbySwitchStaysClosedOnOldFirmware() {
        QFETCH(int, firmwareBuildNumber);
        TestFixture f;
        f.device.m_firmwareBuildNumber = firmwareBuildNumber;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        QVERIFY(!f.state.standbySwitchOpen());
    }

    void standbySwitchClearsOnDisconnect() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1337;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        elapseNoAcWait(f);
        QVERIFY(f.state.standbySwitchOpen());

        f.device.m_simulationMode = false;  // No transport + no sim = disconnected
        f.state.onDE1StateChanged();
        QVERIFY(!f.state.standbySwitchOpen());
    }

    // Fires the settle wait without the test waiting 6 s of wall clock.
    static void elapseNoAcWait(TestFixture& f) {
        QVERIFY(f.state.m_noAcSettleTimer->isActive());
        f.state.m_noAcSettleTimer->stop();
        f.state.m_noAcSettled = true;
        f.state.updatePhase();
    }

    // A snapshot cannot tell an open switch from the machine's own brief report —
    // both read "Idle, Error_NoAC" — so the warning waits the condition out. Field
    // reports on v1363 (inside the range the 1337 gate trusts) show the blip on every
    // tap-to-wake, clearing untouched after ~3 s.
    void standbySwitchWaitsBeforeWarning() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1363;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        QVERIFY(!f.state.standbySwitchOpen());

        elapseNoAcWait(f);
        QVERIFY(f.state.standbySwitchOpen());
    }

    // The entry point varies — Ready on a tap-to-wake, a heating substate mid warm-up
    // — so none of them may shortcut the wait.
    void standbySwitchWaitsWhateverItArrivedFrom_data() {
        QTest::addColumn<int>("fromSubState");
        QTest::newRow("Ready (tap-to-wake)") << int(DE1::SubState::Ready);
        QTest::newRow("Heating") << int(DE1::SubState::Heating);
        QTest::newRow("FinalHeating") << int(DE1::SubState::FinalHeating);
        QTest::newRow("Stabilising") << int(DE1::SubState::Stabilising);
    }

    void standbySwitchWaitsWhateverItArrivedFrom() {
        QFETCH(int, fromSubState);
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1363;
        f.setDE1State(DE1::State::Idle, static_cast<DE1::SubState>(fromSubState));
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        QVERIFY(!f.state.standbySwitchOpen());
    }

    // The reported defect: the substate clears on its own before the wait elapses.
    void standbySwitchNeverWarnsForAnEpisodeThatClearsItself() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1363;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Heating);

        QVERIFY(!f.state.m_noAcSettleTimer->isActive());
        QVERIFY(!f.state.standbySwitchOpen());
    }

    // The wait must not restart on every re-evaluation, or an episode that outlives it
    // would never reach the user. updatePhase() re-runs on connectedChanged and
    // firmwareVersionChanged with the substate unchanged.
    void standbySwitchWaitIsNotRestartedByAReEvaluation() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1363;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        QTimer* timer = f.state.m_noAcSettleTimer;
        QVERIFY(timer->isActive());

        emit f.device.firmwareVersionChanged();
        QCOMPARE(f.state.m_noAcSettleTimer, timer);
        QVERIFY(timer->isActive());
    }

    // A disconnect ends the episode: the wait must not carry over and fire against a
    // machine we are no longer talking to.
    void standbySwitchWaitIsAbandonedOnDisconnect() {
        TestFixture f;
        f.device.m_firmwareBuildNumber = 1363;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        QVERIFY(f.state.m_noAcSettleTimer->isActive());

        f.device.m_simulationMode = false;  // No transport + no sim = disconnected
        f.state.onDE1StateChanged();
        QVERIFY(!f.state.m_noAcSettleTimer->isActive());
        QVERIFY(!f.state.standbySwitchOpen());
    }

    void standbySwitchRecheckedWhenFirmwareArrivesLate() {
        // The substate STATE_INFO read is queued before sendInitialSettings()'s MMR
        // read populates firmwareBuildNumber(), so a machine already sitting in
        // Error_NoAC when the app connects evaluates the gate against firmware 0
        // (unknown) first. Without a recheck when the firmware number lands,
        // Error_NoAC's latching nature means nothing would ever re-evaluate it for
        // the rest of the session.
        TestFixture f;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Error_NoAC);
        elapseNoAcWait(f);
        QVERIFY(!f.state.standbySwitchOpen());  // firmware still unknown

        f.device.m_firmwareBuildNumber = 1400;
        emit f.device.firmwareVersionChanged();
        QVERIFY(f.state.standbySwitchOpen());
    }

    // ==========================================
    // Steam phase with substates
    // ==========================================

    void steamHeatingPhase() {
        // Steam heating shows as Heating, not Steaming
        TestFixture f;
        f.setDE1State(DE1::State::Steam, DE1::SubState::Heating);
        QCOMPARE(f.state.phase(), MachineState::Phase::Heating);
    }

    void steamPuffingPhase() {
        // Puffing substate stays in Steaming (not Ending)
        TestFixture f;
        f.setDE1State(DE1::State::Steam, DE1::SubState::Puffing);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
    }

    void steamEndingPhase() {
        // Ending substate stays in Steaming
        TestFixture f;
        f.setDE1State(DE1::State::Steam, DE1::SubState::Ending);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
    }

    // ==========================================
    // steamFlowStopped emission (LiveSteamCoach contract)
    // ==========================================

    // Auto-stop cycle: flow ends via the substate change (phase stays Steaming)
    // and the machine later returns to Idle. Exactly ONE emission — at the
    // actual end of flow. The later phase-exit transition also runs the
    // flow-stopped branch (wasFlowing derives from oldPhase alone) but the
    // consumed m_steamFlowStopPending flag keeps it silent.
    void steamFlowStopped_autoStopThenIdle() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::Steam, DE1::SubState::Steaming);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
        QCOMPARE(spy.count(), 0);

        // Firmware auto-stop: substate leaves Steaming, phase stays Steaming.
        // The shot timer is still active from flow start, so the substate-change
        // block stops it and emits.
        f.setDE1State(DE1::State::Steam, DE1::SubState::Puffing);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
        QCOMPARE(spy.count(), 1);

        // Machine returns to idle: no duplicate — the pending flag was consumed.
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 1);
    }

    // Manual stop straight out of flowing steam (e.g. GHC stop -> Idle): the
    // phase-change path is the only emitter, exactly once.
    void steamFlowStopped_manualStopToIdle() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::Steam, DE1::SubState::Steaming);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 1);
    }

    // Other flow types ending must not emit — the signal is steam-only
    // (pins the oldPhase == Steaming guard).
    void steamFlowStopped_notForOtherFlows() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::HotWater, DE1::SubState::Pouring);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 0);
    }

    // A steam whose flow was never observed (entered mid-sequence at Puffing —
    // both flowing-substate notifications dropped) must stay silent: the
    // pending flag never armed, so the phase exit is not a flow-stop event.
    // Guards LiveSteamCoach against a ghost "Steam done" computed off a stale
    // shot clock.
    void steamFlowStopped_notWhenFlowNeverSeen() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::Steam, DE1::SubState::Puffing);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 0);
    }

    // "Flow observed" is the isFlowing() whitelist, which includes Pouring:
    // a steam whose FIRST observed substate is Pouring (only the initial
    // Steaming notification missed) still arms and still gets its stop event.
    // Pins the arming condition's real boundary — only Puffing/Ending-first
    // entries stay silent (test above).
    void steamFlowStopped_armsWhenFirstSeenAtPouring() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::Steam, DE1::SubState::Pouring);
        QCOMPARE(f.state.phase(), MachineState::Phase::Steaming);
        f.setDE1State(DE1::State::Steam, DE1::SubState::Puffing);
        QCOMPARE(spy.count(), 1);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 1);
    }

    // A BLE disconnect mid-steam must disarm WITHOUT emitting (a dropped
    // connection is not a completion), and the stale arm must not leak across
    // the disconnect into a later never-flowed steam's phase exit.
    void steamFlowStopped_disconnectDisarmsWithoutEmitting() {
        TestFixture f;
        QSignalSpy spy(&f.state, &MachineState::steamFlowStopped);

        f.setDE1State(DE1::State::Steam, DE1::SubState::Steaming);  // arms
        // BLE drops mid-steam: updatePhase's disconnect early-return runs.
        f.device.m_simulationMode = false;  // isConnected() -> false
        f.state.onDE1StateChanged();
        QCOMPARE(f.state.phase(), MachineState::Phase::Disconnected);
        QCOMPARE(spy.count(), 0);  // disconnect is not a flow-stop event

        // Reconnect idle, then a steam observed only mid-sequence (never
        // flowing): the stale arm from before the disconnect must not
        // ghost-emit on its phase exit.
        f.device.m_simulationMode = true;
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        f.setDE1State(DE1::State::Steam, DE1::SubState::Puffing);
        f.setDE1State(DE1::State::Idle, DE1::SubState::Ready);
        QCOMPARE(spy.count(), 0);
    }

    // ==========================================
    // activeScaleType / activeScaleName
    //
    // The authority for "which scale is serving", used to key SAW learning, the
    // Calibration tab's model display, and the reset_saw_learning_for_profile MCP
    // default. Its failure mode is a corrupted persisted learning pool that no
    // screen reveals, so it is asserted here rather than trusted to on-device use:
    // the WiFi→BLE fallback that triggers it is rare and its symptom is a slightly
    // wrong SAW stop weeks later.
    // ==========================================

    void activeScaleTypePrefersConnectedScaleOverSavedPrimary() {
        // The bug this exists to fix: WiFi primary preserved on purpose by the
        // WiFi→BLE fallback, BLE actually delivering every weight sample. Four
        // consecutive shots on-device logged scale="decent-wifi" while served by BLE.
        TestFixture f;
        f.settings.setScaleType("decent-wifi");
        f.settings.setScaleName("Half Decent Scale (WiFi)");
        f.scale.setType("decent");
        f.scale.mockSetConnected(true);

        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent"));
        // Canonical name of the scale actually serving — NOT the stale saved label,
        // which would name one scale beside another's learned model.
        QCOMPARE(f.state.activeScaleName(), QStringLiteral("Decent Scale"));
    }

    void activeScaleNameKeepsUserLabelWhenServingScaleIsThePrimary() {
        TestFixture f;
        f.settings.setScaleType("decent");
        f.settings.setScaleName("Kitchen scale");  // user's own label
        f.scale.setType("decent");
        f.scale.mockSetConnected(true);

        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent"));
        QCOMPARE(f.state.activeScaleName(), QStringLiteral("Kitchen scale"));
    }

    void activeScaleTypeFallsBackForNonCanonicalScale() {
        // FlowScale reports "flow" and is permanently isConnected(). Keying on it
        // would open a "flow" pool and make SettingsCalibration::sensorLag() warn
        // about an unknown type on every espresso cycle — which, under
        // QTest::failOnWarning(), would surface as failures elsewhere in the suite.
        TestFixture f;
        f.settings.setScaleType("decent");
        f.scale.setType("flow");
        f.scale.mockSetConnected(true);
        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent"));

        // Base ScaleDevice::type() returns "" — an empty key would write learning
        // entries into a pool nothing ever reads.
        f.scale.setType(QString());
        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent"));
    }

    void activeScaleTypeFallsBackWhenNotConnectedOrAbsent() {
        TestFixture f;
        f.settings.setScaleType("decent-wifi");
        f.scale.setType("decent");

        f.scale.mockSetConnected(false);
        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent-wifi"));

        f.state.setScale(nullptr);
        QCOMPARE(f.state.activeScaleType(), QStringLiteral("decent-wifi"));
    }

    void activeScaleTypeNotifiesOnEverySourceThatCanChangeIt() {
        // Each of these is a one-line wiring that, if dropped, leaves the Calibration
        // tab bound to a stale value while its reset button — an imperative read, not
        // a binding — clears a different pool. Silent, and exactly the read/write
        // divergence activeScaleType exists to prevent.
        TestFixture f;
        f.settings.setScaleType("decent-wifi");
        QSignalSpy spy(&f.state, &MachineState::activeScaleTypeChanged);

        f.scale.setType("decent");
        f.scale.mockSetConnected(true);           // ScaleDevice::connectedChanged
        QVERIFY2(spy.count() >= 1, "connect/disconnect must notify");

        spy.clear();
        MockScaleDevice other;
        other.setType("bookoo");
        f.state.setScale(&other);                  // setScale()
        QVERIFY2(spy.count() >= 1, "swapping the serving scale must notify");

        spy.clear();
        f.state.setScale(nullptr);                 // now on the saved-primary fallback
        spy.clear();
        f.settings.setScaleType("acaia");          // Settings::scaleTypeChanged
        QVERIFY2(spy.count() >= 1,
                 "changing the saved primary must notify while on the fallback path");
        QCOMPARE(f.state.activeScaleType(), QStringLiteral("acaia"));

        spy.clear();
        f.settings.setScaleName("Renamed");        // Settings::scaleNameChanged
        QVERIFY2(spy.count() >= 1, "renaming the saved scale must notify activeScaleName");
    }

    // ==========================================
    // Auto-tare settle gate
    // ==========================================
    //
    // Field case: the 2026-08-20 9:37 AM shot tared while the cell was still moving.
    // The scale acknowledged 0.00 g and then read -20.6 g at rest, so every weight
    // that shot was 20.6 g low and stop-at-weight fired 23 g late, while the log said
    // `tare= true` throughout. Nothing downstream could tell.

    void autoTareWaitsForTheCellToStopRinging() {
        TestFixture f;
        armPreheat(f);

        // The ring from the field log: alternating sign, tens of grams apart. The
        // pre-fix code tared on the first sample over the threshold.
        for (double w : {8.3, -6.8, 9.1, -4.2, 7.7})
            f.scale.mockSetWeight(w);
        QCOMPARE(f.scale.tareCount(), 0);

        // Cup now sitting still: the window fills and the tare goes out.
        for (double w : {302.0, 302.3, 302.1, 302.2, 302.15})
            f.scale.mockSetWeight(w);
        QCOMPARE(f.scale.tareCount(), 1);
    }

    void aSlowRampIsNotMistakenForStillness_data() {
        QTest::addColumn<double>("gramsPerSample");
        QTest::addColumn<int>("expectedTares");
        // A window of N samples spans (N-1) steps, so at N=4 and a 1.0 g band any
        // ramp above 1.0/3 = 0.333 g/sample is rejected. 0.47 is the field rate --
        // the case a per-sample band AND a 3-sample window both let through, which
        // is why this row exists at exactly that number.
        QTest::newRow("field rate 0.47") << 0.47 << 0;
        QTest::newRow("faster 1.0") << 1.0 << 0;
        // Below the bound the ramp is genuinely indistinguishable from a still
        // reading at this window size, and it tares. Asserted rather than left
        // unstated so the limit is on the record instead of being rediscovered.
        QTest::newRow("under the bound 0.2") << 0.2 << 1;
    }

    void aSlowRampIsNotMistakenForStillness() {
        QFETCH(double, gramsPerSample);
        QFETCH(int, expectedTares);
        TestFixture f;
        armPreheat(f);

        double w = 300.0;
        for (int i = 0; i < 10; ++i) { f.scale.mockSetWeight(w); w += gramsPerSample; }
        QCOMPARE(f.scale.tareCount(), expectedTares);
    }

    void aStillCupIsTaredEvenWhenTheReadingNeverChanges() {
        TestFixture f;
        armPreheat(f);

        // ScaleDevice::setWeight dedupes weightChanged on value, so an identical
        // repeated reading emits nothing on that signal. The window would never fill
        // and a perfectly still cup would never be tared at all. This is why the
        // auto-tare is wired to weightSampleReceived instead.
        for (int i = 0; i < 6; ++i)
            f.scale.mockSetWeight(250.0);
        QCOMPARE(f.scale.tareCount(), 1);
    }

    void autoTareIgnoresSamplesOutsideThePreFlowWindow() {
        TestFixture f;
        armPreheat(f);
        f.setDE1State(DE1::State::Espresso, DE1::SubState::Pouring);

        // Deliberately STILL samples, well above AUTO_TARE_THRESHOLD -- a cup mid-pour
        // reads exactly like this. The settle gate is satisfied here, so it is not what
        // rules the tare out; only the pre-flow window check is. An earlier version of
        // this test stepped 5 g at a time, which the settle band rejected on its own --
        // so it passed with the window check deleted and asserted nothing.
        for (int i = 0; i < 6; ++i)
            f.scale.mockSetWeight(25.0 + i * 0.1);
        QCOMPARE(f.scale.tareCount(), 0);
    }

};

QTEST_GUILESS_MAIN(tst_MachineState)
#include "tst_machinestate.moc"
