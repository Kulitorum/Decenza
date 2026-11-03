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

namespace {
const QString kIdleRow = QStringLiteral("client-a/idleRow");
}

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
            // What opening the idle steam row does: select steam, then offer a sample.
            QVERIFY(preparation.selectMode(QStringLiteral("steam")));
            QVERIFY(preparation.startMilkAttempt(kIdleRow));
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

    // The plan does not change until a new capture: offering and withdrawing keep
    // the earlier one.
    void withdrawnOfferKeepsEarlierCaptureAndStopsCapture()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(f.preparation.startMilkAttempt(kIdleRow));
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(f.preparation.cancelMilkAttempt(kIdleRow));
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(!f.preparation.milk()->active());
        QVERIFY(f.preparation.startMilkAttempt(kIdleRow));
        QVERIFY(f.preparation.milk()->active());
    }

    // Tapping the review button again, or another idle pill, cycles the pitcher
    // within the same offer. The old capture was the other pitcher's net.
    void pitcherChangeKeepsAttemptOpenAndScaleBaseline()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QVERIFY(f.preparation.startMilkAttempt(kIdleRow));
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        f.settings.brew()->addSteamPitcherPreset(QStringLiteral("Second pitcher"), 20, 20, 160.0);
        f.settings.brew()->setSteamPitcherWeight(1, 40.0);
        f.settings.brew()->setSelectedSteamCup(1);
        QVERIFY(f.preparation.milkAttemptOpen());
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        // The prior 150 g sample remains a real load over the original zero
        // baseline; changing the cup tare must not adopt it as a new baseline.
        f.advance(2500);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 2);
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 110.0);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 110.0);
        QVERIFY(!f.preparation.milkAttemptOpen());
    }

    // Field log, 2026-11-01/02: after a steam the mode stayed "steam", and the
    // espresso cup set down next was captured as 206.8 g / 211.8 g of milk,
    // rewriting the next steam's time. Only an offered sample may be taken.
    void milkIsCapturedOnlyForOneOfferedSample()
    {
        Fixture f;
        f.addTestPitcher();
        QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));
        f.setWeight(400.0);
        f.advance(5000);
        QVERIFY(!f.preparation.milk()->active());
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 0);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), f.settings.brew()->steamTimeout());

        f.settleBaseline();
        QVERIFY(f.preparation.startMilkAttempt(kIdleRow));
        f.captureMilk();
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);
        QVERIFY(!f.preparation.milkAttemptOpen());
        f.setWeight(0.0);
        f.advance(1000);
        f.setWeight(400.0);
        f.advance(5000);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);

        // phaseChanged is queued behind the getter, so wait for each delivery.
        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_VERIFY(phaseChanged.count() > 0);
        const qsizetype steamChanges = phaseChanged.count();
        f.device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
        QTRY_VERIFY(phaseChanged.count() > steamChanges);
        QCOMPARE(f.machine.phase(), MachineState::Phase::Ready);
        QCOMPARE(f.preparation.mode(), QStringLiteral("steam"));
        f.setWeight(0.0);
        f.advance(1000);
        f.setWeight(400.0);
        f.advance(5000);
        QVERIFY(!f.preparation.milk()->active());
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), f.settings.brew()->steamTimeout());
    }

    // The host holds one attempt for every surface and client that offered it.
    void milkOfferIsHeldPerOwnerAndReleasedWithItsClient()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
        QVERIFY(f.preparation.startMilkAttempt(kIdleRow));
        QVERIFY(f.preparation.startMilkAttempt(QStringLiteral("client-b/review")));
        QVERIFY(!f.preparation.cancelMilkAttempt(QStringLiteral("client-a/idlePopup")));
        QVERIFY(f.preparation.cancelMilkAttempt(kIdleRow));
        QVERIFY(f.preparation.milkAttemptOpen());

        // A browser that closes cannot withdraw; its session close does.
        f.preparation.releaseMilkClient(QStringLiteral("client-a"));
        QVERIFY(f.preparation.milkAttemptOpen());
        f.preparation.releaseMilkClient(QStringLiteral("client-b"));
        QVERIFY(!f.preparation.milkAttemptOpen());
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
    }

    void offeredSampleNeedsWeightTimedPitcherWithEmptyWeight_data()
    {
        QTest::addColumn<QString>("missing");
        QTest::newRow("weight-timed-steaming-off") << QStringLiteral("toggle");
        QTest::newRow("no-empty-pitcher-weight") << QStringLiteral("weight");
    }

    void offeredSampleNeedsWeightTimedPitcherWithEmptyWeight()
    {
        QFETCH(QString, missing);
        Fixture f;
        f.prepareMilkCapture();
        if (missing == QLatin1String("toggle"))
            f.settings.brew()->setMilkAutoCaptureEnabled(false);
        else
            f.settings.brew()->setSteamPitcherWeight(0, 0.0);
        f.captureMilk();
        QVERIFY(f.preparation.milkAttemptOpen());
        QVERIFY(!f.preparation.milk()->active());
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 0);
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

    void connectedFiniteRemovalSampleRelocksManualSteamTime_data()
    {
        QTest::addColumn<bool>("offered");
        QTest::newRow("after-the-sample") << true;
        QTest::newRow("never-offered") << false;
    }

    void connectedFiniteRemovalSampleRelocksManualSteamTime()
    {
        QFETCH(bool, offered);
        Fixture f;
        if (offered) {
            f.prepareMilkCapture();
        } else {
            f.addTestPitcher();
            QVERIFY(f.preparation.selectMode(QStringLiteral("steam")));
        }
        f.setWeight(150.0);
        f.advance(2500);
        QVERIFY(f.preparation.snapshot().milk.loadPresent);
        QVERIFY(!f.preparation.milkAttemptOpen());
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
        QSignalSpy preparationChanges(&f.preparation, &PreparationState::changed);
        emit f.machine.shotTimeChanged();
        QCOMPARE(preparationChanges.size(), 0);
        f.device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
        QTRY_COMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        QVERIFY(!f.preparation.milkAttemptOpen());
        QCOMPARE(f.settings.brew()->lastSteamMilkG(), 100.0);
        QVERIFY(f.settings.brew()->lastSteamTimeS() >= 1.0);
        QVERIFY(f.preparation.milkTimeLocked());
    }

    void steamStartUsesCapturedMilkOnce()
    {
        Fixture f;
        f.prepareMilkCapture();
        f.captureMilk();
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);

        // The sample is taken, so a later reading is not measured as milk.
        f.setWeight(160.0); // 110 g net
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

    void steamStartIgnoresUncapturedLoad_data()
    {
        QTest::addColumn<double>("lastWeight");
        QTest::newRow("lifted-before-it-settled") << 0.0;
        QTest::newRow("resting-at-start") << 200.0;
    }

    // Only a capture sets the time: a pitcher lifted before it settled, or a cup
    // left on the drip-tray scale at steam start, is not milk.
    void steamStartIgnoresUncapturedLoad()
    {
        QFETCH(double, lastWeight);
        Fixture f;
        f.prepareMilkCapture();
        f.setWeight(160.0); // 110 g net; steam starts before it settles
        f.setWeight(lastWeight);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 0.0);
        const int baselineTimeout = f.settings.brew()->steamTimeout();
        QCOMPARE(f.preparation.plannedSteamDurationSec(), baselineTimeout);

        QSignalSpy immediate(&f.preparation,
                              &PreparationController::steamTimeoutImmediateRequested);
        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Steam, DE1::SubState::Steaming);
        QTRY_VERIFY(phaseChanged.count() > 0);
        QCOMPARE(f.settings.brew()->steamTimeout(), baselineTimeout);
        QCOMPARE(immediate.count(), 0);
    }

    void anotherOperationEndsTheOffer()
    {
        Fixture f;
        f.prepareMilkCapture();

        QSignalSpy phaseChanged(&f.machine, &MachineState::phaseChanged);
        f.device.setSimulatedState(DE1::State::Espresso, DE1::SubState::Preinfusion);
        QTRY_VERIFY(phaseChanged.count() > 0);
        QCOMPARE(f.preparation.mode(), QStringLiteral("espresso"));
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
        QTest::newRow("resting-load-does-not-replace-capture") << QStringLiteral("live") << 27 << true;
        QTest::newRow("captured-after-lift") << QStringLiteral("captured") << 27 << true;
        QTest::newRow("uncaptured-load-is-not-milk") << QStringLiteral("last-live") << 65 << false;
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
            f.setWeight(200.0); // 150 g on the scale, not captured
        } else if (milkState == QLatin1String("captured") || milkState == QLatin1String("last-live")) {
            f.setWeight(160.0); // 110 g on the scale, then the pitcher is lifted
            f.setWeight(0.0);
        }
        f.settings.brew()->setSteamTimeout(65);
        QCOMPARE(f.preparation.plannedSteamDurationSec(), expectedSeconds);
        const auto* brew = f.settings.brew();
        const auto cachedPitcher = brew->getSteamPitcherPreset(brew->selectedSteamPitcher());
        QCOMPARE(f.preparation.plannedSteamDurationSec(cachedPitcher,
            brew->milkAutoCaptureEnabled(), brew->steamSecondsPerGram(), brew->steamTimeout()),
            expectedSeconds);
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
        QCOMPARE(f.preparation.plannedSteamDurationSec(cachedPitcher,
            brew->milkAutoCaptureEnabled(), brew->steamSecondsPerGram(), 70), 70);
    }

    // #2058: the scale reads -450 g empty because nothing tares it, and the pitcher
    // (50 g + 100 g milk) is already on it when the sample is offered.
    void pitcherPlacedBeforeArmingIsMeasuredFromTheEmptyReading()
    {
        Fixture f;
        f.addTestPitcher();
        f.settings.brew()->setMilkAutoCaptureEnabled(true);
        f.setWeight(-450.0);
        f.advance(1000);
        f.setWeight(-300.0);
        f.advance(5000);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 0);

        QVERIFY(f.preparation.startMilkAttempt(QStringLiteral("client-a/review")));
        QVERIFY(f.preparation.milk()->active());
        QCOMPARE(f.preparation.milk()->virtualZero(), -450.0);
        QCOMPARE(f.preparation.milkScaleLoadG(), 150.0);
        f.advance(2500);
        QCOMPARE(f.preparation.snapshot().milkCaptureSequence, 1);
        QCOMPARE(f.preparation.snapshot().lastMilkGrams, 100.0);
        QCOMPARE(f.preparation.sessionMeasuredMilkG(), 100.0);
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
        QVERIFY(!f.preparation.steamTimeoutScaled());
        QVERIFY(f.preparation.milkTimeLocked());

        // Further idle updates do not release this same session a second time.
        f.preparation.evaluateForTesting();
        QCOMPARE(ended.count(), 1);
    }
};

QTEST_MAIN(tst_PreparationController)
#include "tst_preparationcontroller.moc"
