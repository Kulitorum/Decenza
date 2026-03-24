#include <QtTest>
#include <QSignalSpy>

#include "machine/machinestate.h"
#include "ble/de1device.h"
#include "core/settings.h"
#include "mocks/MockScaleDevice.h"

// Test SAV (stop-at-volume) logic across all 4 profile types.
// Uses DECENZA_TESTING friend access to manipulate private state directly.

class tst_SAV : public QObject {
    Q_OBJECT

private:
    // Helper: set up a MachineState ready for SAV testing.
    // Returns the MachineState with espresso-like state configured.
    struct TestFixture {
        DE1Device device;
        Settings settings;
        MockScaleDevice scale;
        MachineState state{&device};

        TestFixture() {
            state.setSettings(&settings);
            state.setScale(&scale);
        }

        // Set MachineState into a SAV-testable condition:
        // - Espresso state, Pouring substate
        // - Tare completed
        // - Not yet triggered
        void prepareForSAV(const QString& profileType, double targetVolume) {
            // Set DE1 into espresso/pouring state
            device.m_state = DE1::State::Espresso;
            device.m_subState = DE1::SubState::Pouring;

            // Trigger phase update via the state change handler
            state.onDE1StateChanged();

            state.setProfileType(profileType);
            state.setTargetVolume(targetVolume);
            state.m_tareCompleted = true;
            state.m_stopAtVolumeTriggered = false;
            state.m_pourVolume = 0.0;
            state.m_preinfusionVolume = 0.0;
            state.m_cumulativeVolume = 0.0;
        }

        // Set MachineState into hot water state
        void prepareForHotWaterSAV() {
            device.m_state = DE1::State::HotWater;
            device.m_subState = DE1::SubState::Pouring;
            state.onDE1StateChanged();
            state.m_tareCompleted = true;
            state.m_stopAtVolumeTriggered = false;
            state.m_pourVolume = 0.0;
        }

        // Simulate flow samples to accumulate pour volume
        void addPourVolume(double ml) {
            state.m_pourVolume = ml;
            state.m_cumulativeVolume = state.m_preinfusionVolume + ml;
        }
    };

private slots:

    // ===== SAV fires when pourVolume >= targetVolume =====

    void savFiresAtTarget_data() {
        QTest::addColumn<QString>("profileType");
        QTest::newRow("settings_2a") << "settings_2a";
        QTest::newRow("settings_2b") << "settings_2b";
        QTest::newRow("settings_2c") << "settings_2c";
        QTest::newRow("settings_2c2") << "settings_2c2";
    }

    void savFiresAtTarget() {
        QFETCH(QString, profileType);
        TestFixture f;
        f.prepareForSAV(profileType, 36.0);
        // No scale configured — SAV should fire for all profile types
        f.settings.setScaleAddress("");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(36.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);
    }

    // ===== SAV disabled when targetVolume == 0 =====

    void savDisabledWhenTargetZero_data() {
        QTest::addColumn<QString>("profileType");
        QTest::newRow("settings_2a") << "settings_2a";
        QTest::newRow("settings_2b") << "settings_2b";
        QTest::newRow("settings_2c") << "settings_2c";
        QTest::newRow("settings_2c2") << "settings_2c2";
    }

    void savDisabledWhenTargetZero() {
        QFETCH(QString, profileType);
        TestFixture f;
        f.prepareForSAV(profileType, 0.0);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(100.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 0);
    }

    // ===== SAV disabled before tare completes =====

    void savBlockedBeforeTare() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.state.m_tareCompleted = false;  // Tare not done yet
        f.settings.setScaleAddress("");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 0);
    }

    // ===== SAV fires only once =====

    void savFiresOnlyOnce() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.settings.setScaleAddress("");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();
        f.state.checkStopAtVolume();  // Second call — should not fire again

        QCOMPARE(spy.count(), 1);
    }

    // ===== SAV uses raw comparison, no lag compensation =====

    void savNoLagCompensation() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.settings.setScaleAddress("");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        // Just below target — should NOT fire (no lag compensation to trigger early)
        f.addPourVolume(35.9);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 0);

        // At target — should fire
        f.addPourVolume(36.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);
    }

    // ===== Basic profiles: SAV skipped when scale configured =====

    void savSkippedForBasicWithScale_data() {
        QTest::addColumn<QString>("profileType");
        QTest::newRow("settings_2a") << "settings_2a";
        QTest::newRow("settings_2b") << "settings_2b";
    }

    void savSkippedForBasicWithScale() {
        QFETCH(QString, profileType);
        TestFixture f;
        f.prepareForSAV(profileType, 36.0);
        f.settings.setScaleAddress("AA:BB:CC:DD:EE:FF");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 0);  // Skipped for basic profile + scale
    }

    // ===== Basic profiles: SAV active when no scale configured =====

    void savActiveForBasicWithoutScale_data() {
        QTest::addColumn<QString>("profileType");
        QTest::newRow("settings_2a") << "settings_2a";
        QTest::newRow("settings_2b") << "settings_2b";
    }

    void savActiveForBasicWithoutScale() {
        QFETCH(QString, profileType);
        TestFixture f;
        f.prepareForSAV(profileType, 36.0);
        f.settings.setScaleAddress("");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);  // Active — no scale to rely on
    }

    // ===== Advanced profiles: SAV active even with scale (unless ignoreVolumeWithScale) =====

    void savActiveForAdvancedWithScale_data() {
        QTest::addColumn<QString>("profileType");
        QTest::newRow("settings_2c") << "settings_2c";
        QTest::newRow("settings_2c2") << "settings_2c2";
    }

    void savActiveForAdvancedWithScale() {
        QFETCH(QString, profileType);
        TestFixture f;
        f.prepareForSAV(profileType, 36.0);
        f.settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        f.settings.setIgnoreVolumeWithScale(false);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);  // Advanced profiles: SAV active with scale
    }

    // ===== ignoreVolumeWithScale: SAV skipped when ON + scale configured =====

    void ignoreVolumeWithScaleSkipsSAV() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        f.settings.setIgnoreVolumeWithScale(true);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 0);  // User opted out of SAV
    }

    // ===== ignoreVolumeWithScale: SAV active when ON but no scale configured =====

    void ignoreVolumeWithScaleNoEffect() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.settings.setScaleAddress("");
        f.settings.setIgnoreVolumeWithScale(true);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);  // No scale configured — ignoreVolume has no effect
    }

    // ===== ignoreVolumeWithScale: SAV active when OFF + scale configured =====

    void ignoreVolumeOffSavActive() {
        TestFixture f;
        f.prepareForSAV("settings_2c", 36.0);
        f.settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        f.settings.setIgnoreVolumeWithScale(false);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(40.0);
        f.state.checkStopAtVolume();

        QCOMPARE(spy.count(), 1);  // Setting OFF — SAV still active
    }

    // ===== Hot Water SAV: 250 ml safety net with scale =====

    void hotWaterSavSafetyNetWithScale() {
        TestFixture f;
        f.prepareForHotWaterSAV();
        f.settings.setScaleAddress("AA:BB:CC:DD:EE:FF");

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);

        // Below 250 ml — should NOT fire
        f.addPourVolume(249.0);
        f.state.checkStopAtVolumeHotWater();
        QCOMPARE(spy.count(), 0);

        // At 250 ml — should fire
        f.addPourVolume(250.0);
        f.state.checkStopAtVolumeHotWater();
        QCOMPARE(spy.count(), 1);
    }

    // ===== Hot Water SAV: waterVolume target without scale =====

    void hotWaterSavTargetWithoutScale() {
        TestFixture f;
        f.prepareForHotWaterSAV();
        f.settings.setScaleAddress("");
        f.settings.setWaterVolume(200);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);

        f.addPourVolume(199.0);
        f.state.checkStopAtVolumeHotWater();
        QCOMPARE(spy.count(), 0);

        f.addPourVolume(200.0);
        f.state.checkStopAtVolumeHotWater();
        QCOMPARE(spy.count(), 1);
    }

    // ===== Hot Water SAV: tare guard =====

    void hotWaterSavBlockedBeforeTare() {
        TestFixture f;
        f.prepareForHotWaterSAV();
        f.state.m_tareCompleted = false;
        f.settings.setScaleAddress("");
        f.settings.setWaterVolume(200);

        QSignalSpy spy(&f.state, &MachineState::targetVolumeReached);
        f.addPourVolume(300.0);
        f.state.checkStopAtVolumeHotWater();

        QCOMPARE(spy.count(), 0);
    }

    // ===== Volume bucketing: preinfusion phase → preinfusion volume =====

    void volumeBucketingPreinfusion() {
        TestFixture f;
        // Must go through proper phase transitions for isFlowing() to return true:
        // Sleep → Espresso/Preinfusion triggers the flow-start path
        f.device.m_state = DE1::State::Espresso;
        f.device.m_subState = DE1::SubState::Preinfusion;
        f.state.onDE1StateChanged();
        f.state.onDE1SubStateChanged();
        // Set phase directly since the state change may not fully propagate
        // without a complete signal chain
        f.state.m_phase = MachineState::Phase::Preinfusion;

        // Feed flow samples during preinfusion
        f.state.onFlowSample(4.0, 0.5);  // 2 ml
        f.state.onFlowSample(4.0, 0.5);  // 2 ml

        QCOMPARE(f.state.m_preinfusionVolume, 4.0);
        QCOMPARE(f.state.m_pourVolume, 0.0);
    }

    // ===== Volume bucketing: pouring phase → pour volume =====

    void volumeBucketingPouring() {
        TestFixture f;
        f.device.m_state = DE1::State::Espresso;
        f.device.m_subState = DE1::SubState::Pouring;
        f.state.onDE1StateChanged();
        f.state.onDE1SubStateChanged();
        f.state.m_phase = MachineState::Phase::Pouring;

        f.state.onFlowSample(3.0, 1.0);  // 3 ml

        QCOMPARE(f.state.m_preinfusionVolume, 0.0);
        QCOMPARE(f.state.m_pourVolume, 3.0);
    }
};

QTEST_GUILESS_MAIN(tst_SAV)
#include "tst_sav.moc"
