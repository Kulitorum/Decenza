#include <QtTest>

#include <cmath>

#include "ble/de1device.h"
#include "controllers/sensorcalibrationcontroller.h"

// Sensor calibration the way Decent's test profiles do it: the machine-reported
// half of the correction is the profile's DECLARED hold — the number on screen
// the user compares against their gauge — not a value derived from telemetry.
//
// What these defend is that the declared value can only be read from the profile
// that declares it, and that the pair reaching the machine is assembled in one
// place. An earlier design measured the hold from shot samples; that is gone
// (see the header for why), so there is nothing here about windows or medians.
class tst_SensorCalibration : public QObject {
    Q_OBJECT

private:
    using Sensor = SensorCalibrationController::Sensor;

    struct TestFixture {
        DE1Device device;
        SensorCalibrationController controller{&device, nullptr};
        SensorCalibrationController::ProfileContext ctx;

        TestFixture() {
            device.m_simulationMode = true;
            controller.setProfileContextProvider(
                [this]() { return ctx; });
        }

        // Stands in for a loaded profile. Only the final frame's declared holds
        // reach the controller, which is what main() hands it.
        void loadProfile(const QString& filename, double holdPressure, double holdTemp) {
            ctx.filename = filename;
            ctx.holdPressure = holdPressure;
            ctx.holdTemperature = holdTemp;
            controller.noteProfileChanged();
        }
    };

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
            // The Calibration-card row shows the instrument text, so a user
            // without the hardware can tell before opening the wizard.
            QVERIFY(!f.controller.profileFilename(i).isEmpty());
            QVERIFY(!f.controller.instrumentText(i).isEmpty());
            QVERIFY(!f.controller.label(i).isEmpty());
            QVERIFY(!f.controller.unitLabel(i).isEmpty());
            QVERIFY(f.controller.maxValue(i) > f.controller.minValue(i));
            QVERIFY(f.controller.maxCorrection(i) > 0.0);
        }
    }

    // The filenames must be the profiles Decenza actually ships, or the wizard
    // loads nothing and the guard below can never pass.
    void tableNamesTheShippedProfiles() {
        TestFixture f;
        QCOMPARE(f.controller.profileFilename(int(Sensor::Pressure)),
                 QStringLiteral("test_pressure_calibration"));
        QCOMPARE(f.controller.profileFilename(int(Sensor::Temperature)),
                 QStringLiteral("test_temperature_calibration"));
        for (int i = 0; i < f.controller.sensorCount(); ++i) {
            const QString path = QStringLiteral(DECENZA_SOURCE_DIR "/resources/profiles/%1.json")
                                     .arg(f.controller.profileFilename(i));
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
    }

    void badSensorIdIsRefusedRatherThanDefaultingToPressure() {
        TestFixture f;
        for (int bad : {-1, 2, 99}) {
            QCOMPARE(f.controller.calibrationTarget(bad), -1);
            QVERIFY(f.controller.profileFilename(bad).isEmpty());
            QVERIFY(!f.controller.rejectionReason(bad, 9.0).isEmpty());
            // A degenerate 0.0 range would read as a real bound.
            QVERIFY(std::isnan(f.controller.minValue(bad)));
            QVERIFY(std::isnan(f.controller.maxValue(bad)));
        }
    }

    // ===== The declared hold =====

    void theDeclaredHoldIsTheProfilesFinalFrame() {
        TestFixture f;
        // Mirrors test_pressure_calibration: a 7 bar lead-in, then the hold.
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);

        QVERIFY(f.controller.isTestProfileActive(int(Sensor::Pressure)));
        // 9, not the 7 bar lead-in — the hold is what the machine displays while
        // the user reads their gauge.
        QVERIFY(qAbs(f.controller.declaredHoldValue(int(Sensor::Pressure)) - 9.0) < 1e-6);
    }

    void theTemperatureHoldComesFromTheFramesTemperature() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_temperature_calibration"), 0.0, 90.0);
        QVERIFY(qAbs(f.controller.declaredHoldValue(int(Sensor::Temperature)) - 90.0) < 1e-6);
    }

    // The guard that keeps this honest: a declared hold only means something
    // while the machine is running the profile that declares it.
    void anotherProfileYieldsNoDeclaredHold() {
        TestFixture f;
        f.loadProfile(QStringLiteral("my_everyday_espresso"), 9.0, 93.0);

        QVERIFY(!f.controller.isTestProfileActive(int(Sensor::Pressure)));
        QVERIFY(std::isnan(f.controller.declaredHoldValue(int(Sensor::Pressure))));
    }

    // Loading the PRESSURE test profile must not hand a value to the
    // temperature sensor, which would be read off the wrong profile entirely.
    void oneSensorsProfileDoesNotSatisfyTheOther() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(f.controller.isTestProfileActive(int(Sensor::Pressure)));
        QVERIFY(!f.controller.isTestProfileActive(int(Sensor::Temperature)));
        QVERIFY(std::isnan(f.controller.declaredHoldValue(int(Sensor::Temperature))));
    }

    void aHoldOutsideThePhysicalRangeIsNotUsed() {
        TestFixture f;
        // A profile declaring something impossible must not become a correction
        // baseline.
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 99.0, 93.0);
        QVERIFY(std::isnan(f.controller.declaredHoldValue(int(Sensor::Pressure))));
    }

    // ===== Entry guards =====

    void readingOutsidePhysicalRangeIsRefused_data() {
        QTest::addColumn<double>("reading");
        QTest::newRow("negative") << -1.0;
        QTest::newRow("absurd")   << 60.0;
    }

    void readingOutsidePhysicalRangeIsRefused() {
        QFETCH(double, reading);
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure), reading).isEmpty());
    }

    void plausibleReadingPasses() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(f.controller.rejectionReason(int(Sensor::Pressure), 8.2).isEmpty());
    }

    void nonFiniteReadingIsRefused() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure),
                                              std::numeric_limits<double>::quiet_NaN()).isEmpty());
    }

    // A PSI reading typed into a bar field is in range (0-14) but nowhere near
    // what the machine holds. So is a gauge read against the 7 bar lead-in
    // instead of the 9 bar hold — exactly maxCorrection away, which is why the
    // guard rejects AT the limit rather than beyond it.
    void aCorrectionTooFarFromTheHoldIsRefused_data() {
        QTest::addColumn<double>("reading");
        QTest::newRow("psi-shaped")        << 13.5;
        QTest::newRow("read the lead-in")  << 7.0;
    }

    void aCorrectionTooFarFromTheHoldIsRefused() {
        QFETCH(double, reading);
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure), reading).isEmpty());
    }

    void withoutTheTestProfileNothingIsAccepted() {
        TestFixture f;
        f.loadProfile(QStringLiteral("my_everyday_espresso"), 9.0, 93.0);
        // Even a perfectly plausible reading: there is nothing to compare it to.
        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure), 8.2).isEmpty());
    }

    // ===== The write chokepoint =====

    // The check succeeding is not a correction. Sending a reported == measured
    // pair is a WRITE whose firmware effect is unverified — it may zero an
    // offset the user already had — so "they agree" must not reach the machine.
    void agreementIsReportedNotWritten() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);

        QVERIFY(!f.controller.rejectionReason(int(Sensor::Pressure), 9.0).isEmpty());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("correction refused:"));
        QVERIFY(!f.controller.applyCorrection(int(Sensor::Pressure), 9.0));
        QVERIFY(!f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)));
    }

    // But a real difference still applies, however small.
    void aSmallRealDifferenceStillApplies() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QVERIFY(f.controller.rejectionReason(int(Sensor::Pressure), 8.9).isEmpty());
    }

    void aCorrectionCarriesTheDeclaredHoldAsTheMachinesHalf() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("applying Pressure Calibration"));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("simulated machine stored"));
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression("stored pressure calibration"));
        QVERIFY(f.controller.applyCorrection(int(Sensor::Pressure), 8.2));

        // The machine applies a tenth of (measured - reported), so a correct pair
        // shows up as -0.08. A wrong pair — reported left at zero — would land on
        // +0.82 and fail here.
        QVERIFY(qAbs(f.device.storedCalibration(int(DE1::Calibration::Target::Pressure)) + 0.08) < 1e-4);
    }

    void aCorrectionWithoutTheTestProfileSendsNothing() {
        TestFixture f;
        f.loadProfile(QStringLiteral("my_everyday_espresso"), 9.0, 93.0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("correction refused:"));
        QVERIFY(!f.controller.applyCorrection(int(Sensor::Pressure), 8.2));
        QVERIFY(!f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)));
    }

    void aCorrectionFailingItsGuardsSendsNothing() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("correction refused:"));
        QVERIFY(!f.controller.applyCorrection(int(Sensor::Pressure), 60.0));
        QVERIFY(!f.device.hasStoredCalibration(int(DE1::Calibration::Target::Pressure)));
    }

    void aBadSensorIdSendsNothing() {
        TestFixture f;
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("sensor out of range"));
        QVERIFY(!f.controller.applyCorrection(7, 8.2));
    }

    void switchingProfileBumpsTheContextVersion() {
        TestFixture f;
        const int before = f.controller.contextVersion();
        QSignalSpy spy(&f.controller, &SensorCalibrationController::contextChanged);
        f.loadProfile(QStringLiteral("test_pressure_calibration"), 9.0, 93.0);
        QCOMPARE(spy.count(), 1);
        QVERIFY(f.controller.contextVersion() > before);
    }
};

QTEST_MAIN(tst_SensorCalibration)
#include "tst_sensorcalibration.moc"
