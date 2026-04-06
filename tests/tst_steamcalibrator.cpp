#include <QtTest>

#include "machine/steamcalibrator.h"

// Tests for SteamCalibrator stability analysis, dryness estimation, and recommendation.

class tst_SteamCalibrator : public QObject {
    Q_OBJECT

private:
    static QVector<QPointF> stablePressure(double meanBar = 3.0, double noise = 0.05,
                                           int samples = 150, double hz = 5.0) {
        QVector<QPointF> data;
        for (int i = 0; i < samples; i++) {
            double t = i / hz;
            double v = meanBar + noise * qSin(i * 0.7) * qCos(i * 1.3);
            data.append(QPointF(t, v));
        }
        return data;
    }

    static QVector<QPointF> oscillatingPressure(double meanBar = 3.0, double amplitude = 0.8,
                                                 double freqHz = 3.0, int samples = 150,
                                                 double hz = 5.0) {
        QVector<QPointF> data;
        for (int i = 0; i < samples; i++) {
            double t = i / hz;
            double v = meanBar + amplitude * qSin(2.0 * M_PI * freqHz * t);
            data.append(QPointF(t, v));
        }
        return data;
    }

private slots:

    // --- Stability analysis ---

    void stableLowerCVThanOscillating()
    {
        auto stableData = stablePressure(3.0, 0.05, 150, 5.0);
        auto oscData = oscillatingPressure(3.0, 0.8, 3.0, 150, 5.0);

        auto stableResult = SteamCalibrator::analyzeStability(stableData, 80, 160, 1500.0, 2.0);
        auto oscResult = SteamCalibrator::analyzeStability(oscData, 80, 160, 1500.0, 2.0);

        qDebug() << "Stable CV:" << stableResult.pressureCV << "Osc CV:" << oscResult.pressureCV;
        QVERIFY(stableResult.pressureCV < oscResult.pressureCV);
    }

    void trimSkipsEarlySpike()
    {
        QVector<QPointF> data;
        for (int i = 0; i < 10; i++)
            data.append(QPointF(i / 5.0, 8.0));
        for (int i = 10; i < 160; i++)
            data.append(QPointF(i / 5.0, 3.0 + 0.02 * qSin(i)));

        auto result = SteamCalibrator::analyzeStability(data, 80, 160, 1500.0, 2.0);

        QVERIFY(result.avgPressure < 3.5);
        QVERIFY(result.avgPressure > 2.5);
        QVERIFY(result.pressureCV < 0.05);
    }

    void tooShortReturnsLowSamples()
    {
        QVector<QPointF> data;
        for (int i = 0; i < 15; i++)
            data.append(QPointF(i / 5.0, 3.0));

        auto result = SteamCalibrator::analyzeStability(data, 80, 160, 1500.0, 2.0);
        QVERIFY(result.sampleCount < 10);
    }

    void emptyDataHandled()
    {
        QVector<QPointF> data;
        auto result = SteamCalibrator::analyzeStability(data, 80, 160, 1500.0, 2.0);
        QCOMPARE(result.sampleCount, 0);
    }

    void heaterExhaustionTrimmed()
    {
        QVector<QPointF> data;
        // Good data for 20s
        for (int i = 0; i < 100; i++)
            data.append(QPointF(i / 5.0, 3.0 + 0.1 * qSin(i)));
        // Heater dies — pressure drops to near zero
        for (int i = 100; i < 130; i++)
            data.append(QPointF(i / 5.0, 0.1));

        auto result = SteamCalibrator::analyzeStability(data, 80, 160, 1500.0, 2.0);

        // Should not include the 0.1 bar tail
        QVERIFY2(result.avgPressure > 2.5,
                 qPrintable(QString("avg %1 — exhaustion not trimmed").arg(result.avgPressure)));
    }

    // --- Dryness and dilution ---

    void drynessFullAtLowFlow()
    {
        double dryness = SteamCalibrator::estimateDryness(1500.0, 0.4, 160.0);
        QVERIFY(dryness >= 0.99);
    }

    void drynessDropsAtHighFlow()
    {
        double dryness = SteamCalibrator::estimateDryness(1500.0, 2.0, 160.0);
        QVERIFY(dryness < 0.5);
    }

    void drynessHigherWithMorePower()
    {
        double drynessXXL = SteamCalibrator::estimateDryness(2200.0, 1.0, 160.0);
        double drynessPro = SteamCalibrator::estimateDryness(1500.0, 1.0, 160.0);
        QVERIFY(drynessXXL > drynessPro);
    }

    void dilutionMatchesDamianMath()
    {
        // Damian: 180g milk, 60°C rise, 224g pitcher → ~11.3%
        double dilution = SteamCalibrator::estimateDilution(1.0, 180.0, 60.0, 224.0);
        QVERIFY2(dilution > 10.0 && dilution < 13.0,
                 qPrintable(QString("Expected ~11.3%, got %1%").arg(dilution)));
    }

    void dilutionHigherWithWetSteam()
    {
        double dry = SteamCalibrator::estimateDilution(1.0);
        double wet = SteamCalibrator::estimateDilution(0.7);
        QVERIFY(wet > dry);
    }

    // --- Recommendation algorithm ---

    void recommendsHighestFlowInCVBand()
    {
        // Simulate U-shaped CV curve like real DE1+ data
        QVector<CalibrationStepResult> steps;
        auto makeStep = [](int flow, double cv) {
            CalibrationStepResult s;
            s.flowRate = flow;
            s.pressureCV = cv;
            s.sampleCount = 100;
            return s;
        };

        steps.append(makeStep(40, 0.30));   // worst
        steps.append(makeStep(60, 0.23));   // good
        steps.append(makeStep(80, 0.21));   // best CV
        steps.append(makeStep(100, 0.24));  // good (within 20% of 0.21)
        steps.append(makeStep(120, 0.32));  // worst

        int rec = SteamCalibrator::findRecommendedFlow(steps);

        // 0.21 * 1.20 = 0.252 threshold. Flows 60 (0.23), 80 (0.21), 100 (0.24) all qualify.
        // Should pick 100 — highest flow in the band.
        QCOMPARE(rec, 100);
    }

    void recommendsOnlyFlowWhenAllSimilar()
    {
        QVector<CalibrationStepResult> steps;
        auto makeStep = [](int flow, double cv) {
            CalibrationStepResult s;
            s.flowRate = flow;
            s.pressureCV = cv;
            s.sampleCount = 100;
            return s;
        };

        // All CVs similar — should pick highest
        steps.append(makeStep(40, 0.25));
        steps.append(makeStep(60, 0.24));
        steps.append(makeStep(80, 0.23));
        steps.append(makeStep(100, 0.25));

        int rec = SteamCalibrator::findRecommendedFlow(steps);
        // 0.23 * 1.20 = 0.276. All qualify. Pick highest = 100.
        QCOMPARE(rec, 100);
    }

    void recommendsLowestWhenOnlyOneGood()
    {
        QVector<CalibrationStepResult> steps;
        auto makeStep = [](int flow, double cv) {
            CalibrationStepResult s;
            s.flowRate = flow;
            s.pressureCV = cv;
            s.sampleCount = 100;
            return s;
        };

        steps.append(makeStep(40, 0.10));  // clear winner
        steps.append(makeStep(60, 0.40));
        steps.append(makeStep(80, 0.50));

        int rec = SteamCalibrator::findRecommendedFlow(steps);
        // 0.10 * 1.20 = 0.12 threshold. Only 40 qualifies.
        QCOMPARE(rec, 40);
    }

    // --- Sweep generation ---

    void sweepGenerationProModel()
    {
        auto steps = SteamCalibrator::generateFlowSweep(3);
        QVERIFY(steps.size() >= 4);
        QVERIFY(steps.first() >= 40);
        QVERIFY(steps.last() <= 160);
    }

    void sweepGenerationXXLModel()
    {
        auto steps = SteamCalibrator::generateFlowSweep(6);
        QVERIFY(steps.size() >= 4);
        QVERIFY(steps.first() >= 50);
    }

    void sweepGeneration110VNarrower()
    {
        auto steps120 = SteamCalibrator::generateFlowSweep(3, 120);
        auto steps220 = SteamCalibrator::generateFlowSweep(3, 220);
        QVERIFY(steps120.last() <= steps220.last());
    }

    // --- Heater wattage ---

    void heaterWattsKnownModels()
    {
        QCOMPARE(SteamCalibrator::heaterWattsForModel(3), 1500.0);
        QCOMPARE(SteamCalibrator::heaterWattsForModel(6), 2200.0);
        QCOMPARE(SteamCalibrator::heaterWattsForModel(7), 3000.0);
    }

    void heaterWattsReducedAt110V()
    {
        double watts220 = SteamCalibrator::heaterWattsForModel(3, 220);
        double watts110 = SteamCalibrator::heaterWattsForModel(3, 110);
        QVERIFY(watts110 < watts220);
    }
};

QTEST_MAIN(tst_SteamCalibrator)
#include "tst_steamcalibrator.moc"
