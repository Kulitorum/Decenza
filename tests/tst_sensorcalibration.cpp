#include <QtTest>
#include <QSignalSpy>

#include "ble/de1device.h"
#include "controllers/sensorcalibrationcontroller.h"
#include "core/settings.h"
#include "machine/machinestate.h"

// The capture controller: the object that makes it impossible to submit a
// calibration against a value the machine never reported.
//
// What is actually being defended here is narrow and worth stating. de1app puts
// both halves of the (reported, measured) pair in text fields and sends
// ::settings(espresso_pressure) — a PROFILE parameter — as the machine's half.
// That is only correct for a simple pressure profile; an advanced profile leaves
// a stale scalar behind (A-Flow____default-medium.tcl declares 6.0 beside frames
// that hold 10.0 bar). These tests pin the property that removes the whole class
// of error: a value exists ONLY when this object watched a run that held, and
// never otherwise.
class tst_SensorCalibration : public QObject {
    Q_OBJECT

private:
    using Sensor = SensorCalibrationController::Sensor;
    using State = SensorCalibrationController::State;

    struct TestFixture {
        DE1Device device;
        Settings settings;
        MachineState state{&device};
        SensorCalibrationController controller{&device, &state, nullptr};

        TestFixture() {
            state.setSettings(&settings);
            // isConnected() without a BLE transport.
            device.m_simulationMode = true;
        }

        void setPhase(MachineState::Phase phase) {
            if (state.m_phase == phase) return;
            state.m_phase = phase;
            emit state.phaseChanged();
        }

        // One shot sample at a given pressure and head temperature, delivered the
        // way the machine delivers it. Driving the real parser rather than poking
        // m_pressure keeps these honest about where the reading comes from.
        //
        // `timerS` is the MACHINE's elapsed-time field, which is what the
        // controller measures against — so these tests set it directly and run
        // instantly instead of waiting out real time.
        void pushSample(double pressureBar, double headTempC = 93.0, double timerS = 0.0) {
            QByteArray d(19, '\0');
            const quint16 rawTimer = static_cast<quint16>(qRound(timerS * 100.0));
            d[0] = static_cast<char>((rawTimer >> 8) & 0xFF);
            d[1] = static_cast<char>(rawTimer & 0xFF);
            const quint16 rawPressure = static_cast<quint16>(qRound(pressureBar * 4096.0));
            d[2] = static_cast<char>((rawPressure >> 8) & 0xFF);
            d[3] = static_cast<char>(rawPressure & 0xFF);
            // HeadTemp is U24P16 across bytes 8-10.
            const quint32 rawTemp = static_cast<quint32>(qRound(headTempC * 65536.0));
            d[8]  = static_cast<char>((rawTemp >> 16) & 0xFF);
            d[9]  = static_cast<char>((rawTemp >> 8) & 0xFF);
            d[10] = static_cast<char>(rawTemp & 0xFF);
            device.parseShotSample(d);
        }

        // A steady pour at the DE1's real ~5 Hz sample rate, with `jitter`
        // alternating either side of the value the way the machine's PID
        // actually behaves. The rate test divides by the sample interval, so the
        // interval has to be realistic or the test measures nothing real: at a
        // 20 ms spacing this same jitter reads as ~0.7 bar/s and no hold is ever
        // found, which is exactly how the first draft's invented threshold
        // slipped through.
        void pushSteadyPour(int count, double value, double jitter = 0.02) {
            for (int i = 0; i < count; ++i)
                pushSample(value + (i % 2 == 0 ? jitter : -jitter), 93.0,
                           kSampleIntervalS * i);
        }
    };

    // The DE1 samples at ~5 Hz.
    static constexpr double kSampleIntervalS = 0.2;

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== The per-sensor table =====

    void tableCoversBothSensorsAndNotFlow() {
        TestFixture f;
        QCOMPARE(f.controller.sensorCount(), 2);
        QCOMPARE(f.controller.calibrationTarget(int(Sensor::Pressure)),
                 int(DE1::Calibration::Target::Pressure));
        QCOMPARE(f.controller.calibrationTarget(int(Sensor::Temperature)),
                 int(DE1::Calibration::Target::Temperature));

        // Flow correction is the Flow Calibration card's multiplier plus
        // auto-calibration; a second firmware-side correction would fight it.
        for (int i = 0; i < f.controller.sensorCount(); ++i)
            QVERIFY(f.controller.calibrationTarget(i) != int(DE1::Calibration::Target::Flow));
    }

    void tableNamesAProfileAndAnInstrumentForEverySensor() {
        TestFixture f;
        for (int i = 0; i < f.controller.sensorCount(); ++i) {
            // The Maintenance row shows the instrument text so a user without the
            // hardware can tell before opening the wizard.
            QVERIFY(!f.controller.profileFilename(i).isEmpty());
            QVERIFY(!f.controller.instrumentText(i).isEmpty());
            QVERIFY(!f.controller.label(i).isEmpty());
            QVERIFY(!f.controller.unitLabel(i).isEmpty());
            QVERIFY(f.controller.maxValue(i) > f.controller.minValue(i));
            QVERIFY(f.controller.maxCorrection(i) > 0.0);
        }
    }

    void badSensorIdIsRefusedRatherThanDefaultingToPressure() {
        TestFixture f;
        for (int bad : {-1, 2, 99}) {
            QCOMPARE(f.controller.calibrationTarget(bad), -1);
            QVERIFY(f.controller.profileFilename(bad).isEmpty());
            QVERIFY(!f.controller.rejectionReason(bad, 9.0).isEmpty());
        }
    }

    // ===== Entry guards =====

    void readingOutsidePhysicalRangeIsRefused_data() {
        QTest::addColumn<int>("sensor");
        QTest::addColumn<double>("reading");

        QTest::newRow("pressure negative")   << int(Sensor::Pressure)    << -1.0;
        QTest::newRow("pressure absurd")     << int(Sensor::Pressure)    << 60.0;
        QTest::newRow("temperature absurd")  << int(Sensor::Temperature) << 250.0;
    }

    void readingOutsidePhysicalRangeIsRefused() {
        QFETCH(int, sensor);
        QFETCH(double, reading);
        TestFixture f;
        QVERIFY(!f.controller.rejectionReason(sensor, reading).isEmpty());
    }

    void plausibleReadingPassesTheRangeCheck() {
        TestFixture f;
        QVERIFY(f.controller.rejectionReason(int(Sensor::Pressure), 8.2).isEmpty());
        QVERIFY(f.controller.rejectionReason(int(Sensor::Temperature), 91.0).isEmpty());
    }

    void nonFiniteReadingIsRefused() {
        TestFixture f;
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure),
                                              std::numeric_limits<double>::quiet_NaN()).isEmpty());
    }

    // ===== The core property: no run, no value =====

    void freshControllerHasNothingToOffer() {
        TestFixture f;
        QCOMPARE(f.controller.stateInt(), int(State::Idle));
        QVERIFY(!f.controller.hasMeasurement());
    }

    void armingClearsAnyPreviousMeasurement() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(14, 9.0);
        f.setPhase(MachineState::Phase::Idle);
        QVERIFY(f.controller.hasMeasurement());

        // A value must never outlive the run that produced it.
        f.controller.arm(int(Sensor::Pressure));
        QCOMPARE(f.controller.stateInt(), int(State::Armed));
        QVERIFY(!f.controller.hasMeasurement());
    }

    void aRunThatNeverPouredIsNotAFailedHold() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        // Straight from armed to settled: the user never started anything.
        f.setPhase(MachineState::Phase::Ready);
        QVERIFY(!f.controller.hasMeasurement());
        // Still Armed, not NoHold — "you never ran it" and "it never held" are
        // different answers and the wizard says different things about them.
        QCOMPARE(f.controller.stateInt(), int(State::Armed));
    }

    // ===== Measuring a hold =====

    void steadyHoldYieldsTheMachinesOwnReading() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(16, 9.0);
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::Measured));
        QVERIFY(f.controller.hasMeasurement());
        // The median over the hold, so a single PID excursion cannot move it.
        QVERIFY(qAbs(f.controller.measuredValue() - 9.0) < 0.05);
    }

    void temperatureSessionMeasuresTemperatureNotPressure() {
        TestFixture f;
        f.controller.arm(int(Sensor::Temperature));
        f.setPhase(MachineState::Phase::Pouring);
        for (int i = 0; i < 16; ++i) {
            // Pressure moves a lot while the temperature holds; a session that
            // read the wrong channel would fail to find a hold at all.
            f.pushSample(/*pressureBar=*/1.0 + (i % 5), /*headTempC=*/93.0,
                         kSampleIntervalS * i);
        }
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::Measured));
        QVERIFY(qAbs(f.controller.measuredValue() - 93.0) < 0.2);
    }

    void aRunThatNeverHoldsYieldsNoValue() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        // A continuous ramp: 0.8 bar per 0.2 s sample is 4 bar/s, well over any
        // honest hold ceiling, and it never lets up.
        for (int i = 0; i < 16; ++i)
            f.pushSample(1.0 + i * 0.8, 93.0, kSampleIntervalS * i);
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::NoHold));
        QVERIFY(!f.controller.hasMeasurement());
    }

    void tooFewSamplesIsNoHoldRatherThanAGuess() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(3, 9.0);
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::NoHold));
        QVERIFY(!f.controller.hasMeasurement());
    }

    // A calibration run uses a blind or leaking portafilter and often no scale at
    // all. The auto-flow window finder rejects exactly this shape — it gates on
    // weight flow >= 0.5 g/s and skips windows starting before 10 s — which is
    // why this controller exists rather than reusing it.
    void aScalelessEarlyHoldIsStillMeasured() {
        TestFixture f;
        QVERIFY(f.state.scale() == nullptr);
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(14, 9.0);
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::Measured));
        QVERIFY(qAbs(f.controller.measuredValue() - 9.0) < 0.05);
    }

    // Observing starts at EspressoPreheating so the page can say the run is under
    // way, but that phase covers a group sitting still. For temperature the hold
    // floor cannot exclude it — a stabilising group sits at roughly the hold
    // temperature — so those samples would merge into the window and dilute the
    // median with readings taken while the probe was in still air.
    void samplesBeforeWaterMovesAreNotMeasured() {
        TestFixture f;
        f.controller.arm(int(Sensor::Temperature));

        f.setPhase(MachineState::Phase::EspressoPreheating);
        QCOMPARE(f.controller.stateInt(), int(State::Observing));
        for (int i = 0; i < 20; ++i)
            f.pushSample(0.0, /*headTempC=*/88.0, kSampleIntervalS * i);
        QCOMPARE(f.controller.sampleCount(), 0);

        // Once water moves, samples count and only those reach the median.
        f.setPhase(MachineState::Phase::Pouring);
        for (int i = 0; i < 16; ++i)
            f.pushSample(9.0, /*headTempC=*/93.0, kSampleIntervalS * (20 + i));
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::Measured));
        // 93, not something pulled toward the 88 of the stabilising group.
        QVERIFY(qAbs(f.controller.measuredValue() - 93.0) < 0.2);
    }

    // sample.timer is a 16-bit field wrapping every 655.36 s
    // (maincontroller.h:739-742). A run does not start at zero, so it can
    // straddle a wrap — and unwrapped, the failure is silent: the elapsed value
    // jumps backwards, the window's span goes negative and is discarded, and a
    // run that held perfectly is reported as never having held.
    void aHoldStraddlingTheSampleClockWrapIsStillMeasured() {
        TestFixture f;
        constexpr double kMod = 65536.0 / 100.0;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);

        // Start close enough to the wrap that the run crosses it.
        const double start = kMod - 1.0;
        for (int i = 0; i < 16; ++i) {
            double t = start + kSampleIntervalS * i;
            if (t >= kMod) t -= kMod;
            f.pushSample(9.0 + (i % 2 == 0 ? 0.02 : -0.02), 93.0, t);
        }
        f.setPhase(MachineState::Phase::Idle);

        QCOMPARE(f.controller.stateInt(), int(State::Measured));
        QVERIFY(qAbs(f.controller.measuredValue() - 9.0) < 0.05);
    }

    // ===== Aborts: a dropped link is never a completion =====

    void midRunDisconnectAbortsAndYieldsNothing() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(14, 9.0);

        // updatePhase() forces Disconnected on a BLE drop. Treating that as the
        // run ending would hand back a value measured from a truncated run —
        // the trap TransportPage.qml:47-70 documents for the drain.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("run aborted, machine went away"));
        f.setPhase(MachineState::Phase::Disconnected);

        QCOMPARE(f.controller.stateInt(), int(State::Aborted));
        QVERIFY(!f.controller.hasMeasurement());
    }

    void midRunSleepAbortsAndYieldsNothing() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(14, 9.0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("run aborted, machine went to sleep"));
        f.setPhase(MachineState::Phase::Sleep);

        QCOMPARE(f.controller.stateInt(), int(State::Aborted));
        QVERIFY(!f.controller.hasMeasurement());
    }

    void anAbortedRunCannotBeSalvagedByASettledPhaseAfterwards() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(14, 9.0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("run aborted"));
        f.setPhase(MachineState::Phase::Disconnected);

        // Reconnecting must not resurrect the discarded samples.
        f.setPhase(MachineState::Phase::Ready);
        QCOMPARE(f.controller.stateInt(), int(State::Aborted));
        QVERIFY(!f.controller.hasMeasurement());
    }

    // ===== The distance guard, which needs a measurement to compare against =====

    void correctionFartherThanTheSensorAllowsIsRefused() {
        TestFixture f;
        f.controller.arm(int(Sensor::Pressure));
        f.setPhase(MachineState::Phase::Pouring);
        f.pushSteadyPour(16, 9.0);
        f.setPhase(MachineState::Phase::Idle);
        QVERIFY(f.controller.hasMeasurement());

        // A PSI reading typed into a bar field lands far outside any plausible
        // correction. In range (0-14) but nowhere near what the machine read.
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure), 13.5).isEmpty());
        // A real correction passes.
        QVERIFY(f.controller.rejectionReason(int(Sensor::Pressure), 8.2).isEmpty());
    }

    void distanceGuardIsInactiveBeforeAMeasurementExists() {
        TestFixture f;
        // Nothing to compare against yet, so only the range check applies. The
        // wizard blocks the entry step until a run is measured; this just makes
        // sure the guard does not invent a comparison against zero.
        QVERIFY(f.controller.rejectionReason(int(Sensor::Pressure), 13.5).isEmpty());
    }

    void stateChangesAreSignalled() {
        TestFixture f;
        QSignalSpy stateSpy(&f.controller, &SensorCalibrationController::stateChanged);
        f.controller.arm(int(Sensor::Pressure));
        QCOMPARE(stateSpy.count(), 1);
        f.setPhase(MachineState::Phase::Pouring);
        QCOMPARE(stateSpy.count(), 2);
        f.pushSteadyPour(14, 9.0);
        f.setPhase(MachineState::Phase::Idle);
        QCOMPARE(stateSpy.count(), 3);
    }
};

QTEST_MAIN(tst_SensorCalibration)
#include "tst_sensorcalibration.moc"
