#include <QtTest>
#include <QSignalSpy>

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

private slots:

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
};

QTEST_GUILESS_MAIN(tst_MachineState)
#include "tst_machinestate.moc"
