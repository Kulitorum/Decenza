#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QRegularExpression>

#include "ble/de1device.h"
#include "ble/scaledeviceproxy.h"
#include "controllers/preparationcontroller.h"
#include "core/settings.h"
#include "machine/machinestate.h"
#include "mocks/MockScaleDevice.h"

class tst_PreparationController : public QObject
{
    Q_OBJECT

    struct Fixture {
        DE1Device device;
        Settings settings;
        MockScaleDevice scale;
        ScaleDeviceProxy scaleProxy;
        MachineState machine{&device};
        PreparationController preparation{&settings, &machine};
        qint64 nowMs = 100000;

        Fixture()
        {
            settings.brew()->setDoseCupTareWeight(0.0);
            settings.brew()->setMilkAutoCaptureEnabled(false);
            settings.brew()->setSteamSecondsPerGram(0.0);
            settings.brew()->setDoseCaptureSoundEnabled(false);
            settings.brew()->setLastSteamMilkG(0.0);
            settings.brew()->setLastSteamTimeS(0.0);
            while (settings.brew()->steamPitcherCount() > 0)
                settings.brew()->removeSteamPitcherPreset(0);
            device.setSimulationMode(true);
            machine.setSettings(&settings);
            machine.setScale(&scale);
            scaleProxy.setTarget(&scale);
            preparation.setScaleDeviceProxy(&scaleProxy);
            preparation.setClockForTesting([this] { return nowMs; });
            scale.mockSetConnected(true);
            device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
            settleBaseline();
        }

        void setWeight(double grams)
        {
            scale.mockSetWeight(grams);
            preparation.evaluateForTesting();
        }

        void advance(qint64 millis)
        {
            nowMs += millis;
            preparation.evaluateForTesting();
        }

        void settleBaseline()
        {
            setWeight(0.0);
            advance(1000);
        }

        void addTestPitcher()
        {
            settings.brew()->addSteamPitcherPreset(QStringLiteral("Test pitcher"), 20, 20, 160.0);
            settings.brew()->setSteamPitcherWeight(0, 50.0);
            settings.brew()->setSteamPitcherCalibration(0, 100.0);
            settings.brew()->setSteamSecondsPerGram(0.2);
            settings.brew()->setSelectedSteamCup(0);
            QVERIFY(settings.brew()->applySteamPitcherValues(0, 0.0)
                    == SettingsBrew::PitcherApply::Applied);
        }

        void prepareMilkCapture()
        {
            addTestPitcher();
            QVERIFY(preparation.selectMode(QStringLiteral("steam")));
            // Starting a new mode disarms the prior espresso baseline.
            setWeight(0.0);
            advance(1000);
        }

        void captureMilk()
        {
            setWeight(150.0); // 50 g pitcher + 100 g milk
            advance(2500);
        }
    };

private slots:
    void init()
    {
        QTest::failOnWarning();
        AppSettings store;
        store.clear();
        store.sync();
    }

    void modeSelectionDoesNotStartMachineAndOperatingRejectsChanges()
    {
        Fixture f;
        QCOMPARE(f.machine.phase(), MachineState::Phase::Ready);
        QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));
        QCOMPARE(f.preparation.mode(), QStringLiteral("steam"));
        QCOMPARE(f.machine.phase(), MachineState::Phase::Ready);
        QVERIFY(f.preparation.selectMode(QStringLiteral("espresso")));
        QCOMPARE(f.preparation.mode(), QStringLiteral("espresso"));
        QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));

        f.device.setSimulatedState(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QCOMPARE(f.machine.phase(), MachineState::Phase::Preinfusion);
        QVERIFY(!f.preparation.selectMode(QStringLiteral("steam")));
        QTRY_COMPARE(f.preparation.mode(), QStringLiteral("espresso"));
    }

    void beanCaptureWritesDoseOnly()
    {
        Fixture f;
        f.settings.brew()->setDoseCaptureSoundEnabled(true);
        f.settings.brew()->setDoseCupTareWeight(100.0);
        const double oldTare = f.settings.brew()->doseCupTareWeight();
        QSignalSpy ding(&f.preparation, &PreparationController::captureDingRequested);
        f.setWeight(oldTare + 18.0);
        f.advance(2500);
        QCOMPARE(f.settings.dye()->dyeBeanWeight(), 18.0);
        QCOMPARE(f.settings.brew()->doseCupTareWeight(), oldTare);
        QCOMPARE(f.preparation.snapshot().lastBeanGrams, 18.0);
        QCOMPARE(ding.count(), 1);
    }

    void lockedMilkCaptureAppliesScaledSteamSettings()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setDoseCaptureSoundEnabled(true);
        QSignalSpy apply(&f.preparation, &PreparationController::applySteamSettingsRequested);
        QSignalSpy ding(&f.preparation, &PreparationController::captureDingRequested);
        f.captureMilk();
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 100.0);
        QVERIFY(f.preparation.snapshot().milkCaptureSequence == 1);
        QCOMPARE(apply.count(), 1);
        QVERIFY(f.settings.brew()->steamTimeout() > 0);
        QCOMPARE(ding.count(), 1);
    }

    void unlockedMilkCaptureDoesNotChangeMachineTimeout()
    {
        Fixture f;
        f.prepareMilkCapture();
        QVERIFY(f.preparation.setMilkTimeLocked(false));
        const int timeout = f.settings.brew()->steamTimeout();
        QSignalSpy apply(&f.preparation, &PreparationController::applySteamSettingsRequested);
        f.captureMilk();
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 100.0);
        QCOMPARE(f.settings.brew()->steamTimeout(), timeout);
        QCOMPARE(apply.count(), 0);
    }

    void cancelledAttemptRestoresMilkAndSuppressesCapture()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(f.preparation.startMilkAttempt());
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QVERIFY(f.preparation.cancelMilkAttempt());
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(!f.preparation.milk()->active());
        QVERIFY(f.preparation.startMilkAttempt());
        QVERIFY(f.preparation.milk()->active());
    }

    void pitcherChangeClosesAttemptAndKeepsScaleBaseline()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QVERIFY(f.preparation.startMilkAttempt());
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        f.settings.brew()->addSteamPitcherPreset(QStringLiteral("Second pitcher"), 20, 20, 160.0);
        f.settings.brew()->setSteamPitcherWeight(1, 40.0);
        // Editing an unselected pitcher does not restore or replace the attempt's
        // saved amount; selecting it closes the attempt through the cancel path.
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        f.settings.brew()->setSelectedSteamCup(1);
        // A captured net amount remains valid; only the live fallback depends on
        // which empty pitcher is currently selected.
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(!f.preparation.milkAttemptOpen());
        // The prior 150 g sample remains a real load over the original zero
        // baseline; changing the cup tare must not adopt it as a new baseline.
        f.advance(2500);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 2);
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 110.0);
    }

    void pitcherFallbackTracksOnlySelectionAndEmptyWeight()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.setWeight(160.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);

        // Name and steam-flow edits do not change the weight basis for this fallback.
        f.settings.brew()->updateSteamPitcherPreset(0, QStringLiteral("Renamed"),
                                                     25, 35, 160.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);

        // A changed empty weight does change the net amount's meaning.
        f.settings.brew()->setSteamPitcherWeight(0, 60.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        f.setWeight(160.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 100.0);

        f.settings.brew()->addSteamPitcherPreset(QStringLiteral("Second"), 20, 20, 160.0);
        f.settings.brew()->setSteamPitcherWeight(1, 40.0);
        f.settings.brew()->setSelectedSteamCup(1);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
    }

    void targetChangeWaitsForNewScaleSampleBeforeUsingLiveFallback()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.setWeight(160.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);

        MockScaleDevice nextScale;
        nextScale.mockSetConnected(true);
        nextScale.mockSetWeight(160.0);
        f.machine.setScale(&nextScale);
        f.scaleProxy.setTarget(&nextScale);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);

        // setTarget's synthetic weightChanged is not a sample. Only an actual
        // sample from the new target may establish a new live fallback.
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        nextScale.mockSetWeight(160.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
    }

    void disconnectDoesNotRelockManualSteamTime()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(150.0);
        f.advance(2500);
        QVERIFY(f.preparation.snapshot().milk.loadPresent);
        QVERIFY(f.preparation.setMilkTimeLocked(false));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral(".*MockScale DISCONNECTED.*")));
        f.scale.mockSetConnected(false);
        QVERIFY(!f.preparation.milkTimeLocked());
    }

    void connectedFiniteRemovalSampleRelocksManualSteamTime()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(150.0);
        f.advance(2500);
        QVERIFY(f.preparation.snapshot().milk.loadPresent);
        QVERIFY(f.preparation.setMilkTimeLocked(false));

        f.setWeight(0.0);
        QVERIFY(f.preparation.milkTimeLocked());
    }

    void steamEndClearsTransientMilkAndLearnsElapsedPair()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        double observedSteamElapsed = 0.0;
        connect(&f.machine, &MachineState::shotTimeChanged, &f.machine, [&f, &observedSteamElapsed] {
            observedSteamElapsed = f.machine.shotTime();
        });
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_VERIFY(!f.preparation.milk()->active());
        // Wait for the machine's own elapsed-time signal, not a fixed wall-clock
        // sleep. PreparationController consumes the same signal before this observer.
        QTRY_VERIFY(observedSteamElapsed >= 1.0);
        f.device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
        QTRY_COMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QVERIFY(!f.preparation.milkAttemptOpen());
        QCOMPARE(f.settings.brew()->lastSteamMilkG(), 100.0);
        QVERIFY(f.settings.brew()->lastSteamTimeS() >= 1.0);
        QVERIFY(f.preparation.milkTimeLocked());
    }

    void steamStartUsesCapturedMilkBeforeLastLiveFallbackOnce()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);

        // A later live reading is retained as the fallback, but the captured milk
        // from this session takes precedence once the pitcher is off the scale.
        f.setWeight(160.0); // 110 g net
        QTRY_COMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
        f.setWeight(0.0); // Pitcher lifted; scale stays connected.
        f.preparation.evaluateForTesting();
        f.settings.brew()->setSteamSecondsPerGram(0.3);

        QSignalSpy immediate(&f.preparation,
                              &PreparationController::steamTimeoutImmediateRequested);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_COMPARE(immediate.count(), 1);
        QCOMPARE(f.settings.brew()->steamTimeout(), 30);
        QCOMPARE(immediate.count(), 1);
        QCOMPARE(immediate.first().at(0).toInt(), 30);
        QVERIFY(f.preparation.steamTimeoutScaled());

        // Re-evaluation while the same operation remains active cannot reapply it.
        f.preparation.evaluateForTesting();
        QCOMPARE(immediate.count(), 1);
    }

    void steamStartUsesLastLiveMilkWhenThereWasNoCapture()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.setWeight(160.0); // 110 g net; not yet stable enough to capture
        QTRY_COMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
        f.setWeight(0.0); // Pitcher lifted; scale stays connected.

        QSignalSpy immediate(&f.preparation,
                              &PreparationController::steamTimeoutImmediateRequested);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_COMPARE(immediate.count(), 1);
        QCOMPARE(f.settings.brew()->steamTimeout(), 22);
        QCOMPARE(immediate.count(), 1);
    }

    void espressoScaleWeightDoesNotBecomeSteamFallback()
    {
        Fixture f;
        f.prepareMilkCapture();
        QVERIFY(f.preparation.selectMode(QStringLiteral("espresso")));
        QSignalSpy scaleWeightChanged(&f.machine, &MachineState::scaleWeightChanged);
        f.setWeight(160.0);
        QTRY_VERIFY(scaleWeightChanged.count() > 0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);

        f.setWeight(0.0); // Pitcher lifted; scale stays connected.
        QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        const int baselineTimeout = f.settings.brew()->steamTimeout();

        QSignalSpy immediate(&f.preparation,
                              &PreparationController::steamTimeoutImmediateRequested);
        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_VERIFY(phaseChanged.count() > 0);
        QCOMPARE(f.settings.brew()->steamTimeout(), baselineTimeout);
        QCOMPARE(immediate.count(), 0);
    }

    void leavingSteamPreparationForAnotherOperationClearsLiveFallback()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.setWeight(160.0);
        QTRY_COMPARE(f.preparation.lastOnScaleMilkG(), 110.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 110.0);

        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QTRY_VERIFY(phaseChanged.count() > 0);
        QCOMPARE(f.preparation.mode(), QStringLiteral("espresso"));
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        QVERIFY(!f.preparation.milkAttemptOpen());
    }

    void steamStartPreservesManualTimeout()
    {
        Fixture f;
        f.prepareMilkCapture();
        QVERIFY(f.preparation.setMilkTimeLocked(false));
        f.settings.brew()->setSteamTimeout(47);

        QSignalSpy immediate(&f.preparation,
                              &PreparationController::steamTimeoutImmediateRequested);
        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_VERIFY(phaseChanged.count() > 0);
        QCOMPARE(f.settings.brew()->steamTimeout(), 47);
        QCOMPARE(immediate.count(), 0);
    }

    void plannedSteamDurationMatchesTheActualStart_data()
    {
        QTest::addColumn<QString>("milkState");
        QTest::addColumn<int>("expectedSeconds");
        QTest::addColumn<bool>("scaled");
        QTest::newRow("historical-milk-is-not-current") << QStringLiteral("historical") << 65 << false;
        QTest::newRow("manual-time-with-live-milk") << QStringLiteral("manual") << 65 << false;
        QTest::newRow("weight-timing-disabled") << QStringLiteral("disabled") << 65 << false;
        QTest::newRow("live-before-captured") << QStringLiteral("live") << 41 << true;
        QTest::newRow("captured-before-last-live") << QStringLiteral("captured") << 27 << true;
        QTest::newRow("last-live-without-capture") << QStringLiteral("last-live") << 30 << true;
    }

    void plannedSteamDurationMatchesTheActualStart()
    {
        QFETCH(QString, milkState);
        QFETCH(int, expectedSeconds);
        QFETCH(bool, scaled);
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.settings.brew()->setSteamSecondsPerGram(0.27);
        f.settings.brew()->setLastSteamMilkG(220.91);
        if (milkState == QLatin1String("live") || milkState == QLatin1String("captured")) {
            f.captureMilk();
            QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        }
        if (milkState == QLatin1String("manual")) {
            QVERIFY(f.preparation.setMilkTimeLocked(false));
            f.setWeight(150.0);
        } else if (milkState == QLatin1String("disabled")) {
            f.settings.brew()->setMilkAutoCaptureEnabled(false);
            f.setWeight(150.0);
        } else if (milkState == QLatin1String("live")) {
            f.setWeight(200.0); // 150 g fresh milk takes precedence over the captured 100 g.
        } else if (milkState == QLatin1String("captured") || milkState == QLatin1String("last-live")) {
            f.setWeight(160.0); // 110 g cached live milk, then the pitcher is lifted.
            f.setWeight(0.0);
        }
        f.settings.brew()->setSteamTimeout(65);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), expectedSeconds);
        QSignalSpy immediate(&f.preparation, &PreparationController::steamTimeoutImmediateRequested);
        QSignalSpy started(&f.machine, &MachineState::shotStarted);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        // updatePhase changes its getter immediately, then queues phaseChanged
        // and shotStarted. Wait through that delivery so preparation has run its
        // actual start handler, including cases that do not change the timeout.
        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(f.machine.phase(), MachineState::Phase::Steaming);
        QCOMPARE(f.settings.brew()->steamTimeout(), expectedSeconds);
        QCOMPARE(immediate.count(), scaled ? 1 : 0);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), expectedSeconds);
        // Once running, later milk/rate changes must not advertise a different
        // duration from the timeout actually controlling this operation.
        f.settings.brew()->setSteamSecondsPerGram(0.5);
        f.setWeight(250.0);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), expectedSeconds);
        f.settings.brew()->setSteamTimeout(70);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), 70);
    }

    // #2058, on both surfaces that weigh milk: the idle screen's Steam row and the
    // shot review's milk button. The scale reads -450 g empty because nothing tares it,
    // and the pitcher (50 g + 100 g milk) is already on it when capture is armed.
    void pitcherPlacedBeforeArmingIsMeasuredFromTheEmptyReading_data()
    {
        QTest::addColumn<bool>("reviewButton");
        QTest::newRow("idle-steam-row") << false;
        QTest::newRow("review-milk-button") << true;
    }

    void pitcherPlacedBeforeArmingIsMeasuredFromTheEmptyReading()
    {
        QFETCH(bool, reviewButton);
        Fixture f;
        f.addTestPitcher();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(-450.0);
        f.advance(1000);
        f.setWeight(-300.0);
        f.advance(5000);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 0);

        if (reviewButton)
            QVERIFY(f.preparation.startMilkAttempt());
        else
            QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));
        QVERIFY(f.preparation.milk()->active());
        QCOMPARE(f.preparation.milk()->virtualZero(), -450.0);
        QCOMPARE(f.preparation.milkScaleLoadG(), 150.0);
        f.advance(2500);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 100.0);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
    }

    // #2058's log: the pitcher of milk was tared on the scale before the capture
    // settled. In the app the tare signal is raised when the tare is requested,
    // before the scale reports its zero (ShotTimingController::tare()).
    void tareWithThePitcherOnKeepsTheMilk()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(-450.0);
        f.advance(1000);
        f.setWeight(-300.0); // 50 g pitcher + 100 g milk
        f.advance(1000);

        emit f.machine.tareCompleted();
        f.setWeight(0.0);
        QCOMPARE(f.preparation.milk()->virtualZero(), -150.0);
        QCOMPARE(f.preparation.milkScaleLoadG(), 150.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 100.0);

        f.advance(1500);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 100.0);
    }

    // Hot water's tare reads zero at once through its subtracted baseline, whether or
    // not the scale tared. A dropped one must not leave the lifted mug as a load.
    void hotWaterTareThatNeverLandsLeavesNoLoad()
    {
        Fixture f;
        f.setWeight(300.0);
        f.advance(1000);
        f.device.setSimulatedState(DE1::State::HotWater, DE1::SubState::Pouring);
        QTRY_COMPARE(f.machine.phase(), MachineState::Phase::HotWater);
        f.machine.tareScale();
        f.device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
        QTRY_COMPARE(f.machine.phase(), MachineState::Phase::Ready);
        f.setWeight(0.0);
        f.advance(1000);
        QCOMPARE(f.preparation.milkScaleLoadG(), 0.0);
    }

    // The log behind #2058: steam started before the 2.5 s capture dwell, with the
    // pitcher still on an un-zeroed scale. The start-time reading is net of the empty
    // reading, not of the scale's zero.
    void steamStartBeforeCaptureSettlesUsesLoadAboveEmptyReading()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(-450.0);
        f.advance(1000);
        f.setWeight(-300.0);
        f.advance(1000);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 100.0);
        const int expected = f.settings.brew()->scaledSteamTime(0, 100.0);
        QVERIFY(expected > 0);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), expected);

        QSignalSpy immediate(&f.preparation,
                             &PreparationController::steamTimeoutImmediateRequested);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_COMPARE(immediate.count(), 1);
        QCOMPARE(f.settings.brew()->steamTimeout(), expected);
    }

    void steamEndRestoresSelectedPresetAndReleasesPermissionOnce()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QSignalSpy ended(&f.preparation,
                         &PreparationController::steamSessionEndedRequested);
        QSignalSpy immediate(&f.preparation,
                             &PreparationController::steamTimeoutImmediateRequested);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_COMPARE(immediate.count(), 1);

        f.settings.brew()->setSteamTimeout(77);
        f.settings.brew()->setSteamFlow(88);
        f.device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
        QTRY_COMPARE(ended.count(), 1);

        QCOMPARE(f.settings.brew()->steamTimeout(), 20);
        QCOMPARE(f.settings.brew()->steamFlow(), 20);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QCOMPARE(f.preparation.lastOnScaleMilkG(), 0.0);
        QVERIFY(!f.preparation.steamTimeoutScaled());
        QVERIFY(f.preparation.milkTimeLocked());

        // Further idle updates do not release this same session a second time.
        f.preparation.evaluateForTesting();
        QCOMPARE(ended.count(), 1);
    }
};

QTEST_MAIN(tst_PreparationController)
#include "tst_preparationcontroller.moc"
