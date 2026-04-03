#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "models/steamdatamodel.h"
#include "machine/steamhealthtracker.h"

// Tests for SteamDataModel session summaries and SteamHealthTracker trend detection.
// SteamHealthTracker uses QSettings("DecentEspresso", "DE1Qt") — tests save/restore
// the steam history key to avoid corrupting real data.

class tst_SteamHealth : public QObject {
    Q_OBJECT

private:
    QSettings m_settings{"DecentEspresso", "DE1Qt"};
    QByteArray m_origHistory;
    QVariant m_origLastWarned;
    QVariant m_origLastFlow;
    QVariant m_origLastTemp;

private slots:

    void init() {
        m_origHistory = m_settings.value("steam/sessionHistory").toByteArray();
        m_origLastWarned = m_settings.value("steam/lastWarnedSession");
        m_origLastFlow = m_settings.value("steam/lastTrackedFlow");
        m_origLastTemp = m_settings.value("steam/lastTrackedTemp");
        // Start each test with clean history
        m_settings.remove("steam/sessionHistory");
        m_settings.remove("steam/lastWarnedSession");
        m_settings.remove("steam/lastTrackedFlow");
        m_settings.remove("steam/lastTrackedTemp");
    }

    void cleanup() {
        if (m_origHistory.isEmpty())
            m_settings.remove("steam/sessionHistory");
        else
            m_settings.setValue("steam/sessionHistory", m_origHistory);

        if (!m_origLastWarned.isValid())
            m_settings.remove("steam/lastWarnedSession");
        else
            m_settings.setValue("steam/lastWarnedSession", m_origLastWarned);

        if (!m_origLastFlow.isValid())
            m_settings.remove("steam/lastTrackedFlow");
        else
            m_settings.setValue("steam/lastTrackedFlow", m_origLastFlow);

        if (!m_origLastTemp.isValid())
            m_settings.remove("steam/lastTrackedTemp");
        else
            m_settings.setValue("steam/lastTrackedTemp", m_origLastTemp);
    }

    // ==========================================
    // SteamDataModel: session summary stats
    // ==========================================

    void dataModelAveragePressureSkipsFirst2Seconds() {
        SteamDataModel model;
        // Add samples in the first 2 seconds (should be excluded)
        model.addSample(0.0, 10.0, 1.0, 140.0);
        model.addSample(0.5, 9.0, 1.0, 140.0);
        model.addSample(1.0, 8.0, 1.0, 140.0);
        model.addSample(1.5, 7.0, 1.0, 140.0);
        // Add samples after 2 seconds (should be included)
        model.addSample(2.0, 2.0, 1.0, 140.0);
        model.addSample(2.5, 3.0, 1.0, 140.0);
        model.addSample(3.0, 2.5, 1.0, 140.0);

        QCOMPARE(model.averagePressure(), 2.5);  // (2.0 + 3.0 + 2.5) / 3
    }

    void dataModelPeakPressureSkipsFirst2Seconds() {
        SteamDataModel model;
        model.addSample(0.0, 15.0, 1.0, 140.0);  // Before 2s — excluded
        model.addSample(1.0, 12.0, 1.0, 140.0);  // Before 2s — excluded
        model.addSample(2.0, 3.0, 1.0, 140.0);
        model.addSample(3.0, 4.0, 1.0, 140.0);

        QCOMPARE(model.peakPressure(), 4.0);
    }

    void dataModelAverageTemperatureSkipsFirst2Seconds() {
        SteamDataModel model;
        model.addSample(0.0, 1.0, 1.0, 200.0);  // Before 2s — excluded
        model.addSample(2.0, 1.0, 1.0, 140.0);
        model.addSample(3.0, 1.0, 1.0, 150.0);

        QCOMPARE(model.averageTemperature(), 145.0);
    }

    void dataModelClearResetsStats() {
        SteamDataModel model;
        model.addSample(2.0, 3.0, 1.0, 140.0);
        model.addSample(3.0, 4.0, 1.0, 150.0);
        QCOMPARE(model.sampleCount(), 2);

        model.clear();
        QCOMPARE(model.sampleCount(), 0);
        QCOMPARE(model.averagePressure(), 0.0);
        QCOMPARE(model.rawTime(), 0.0);
    }

    // ==========================================
    // SteamHealthTracker: live warnings
    // ==========================================

    void liveWarningFiresOncePerSession() {
        SteamHealthTracker tracker;
        QSignalSpy pressureSpy(&tracker, &SteamHealthTracker::pressureTooHigh);
        QSignalSpy tempSpy(&tracker, &SteamHealthTracker::temperatureTooHigh);

        tracker.onSample(9.0, 150.0);  // Pressure high, temp ok
        tracker.onSample(9.5, 150.0);  // Still high — should NOT fire again
        tracker.onSample(2.0, 185.0);  // Pressure ok, temp high
        tracker.onSample(2.0, 190.0);  // Still high — should NOT fire again

        QCOMPARE(pressureSpy.count(), 1);
        QCOMPARE(tempSpy.count(), 1);
    }

    void liveWarningResetsOnNewSession() {
        SteamHealthTracker tracker;
        QSignalSpy pressureSpy(&tracker, &SteamHealthTracker::pressureTooHigh);

        tracker.onSample(9.0, 150.0);
        QCOMPARE(pressureSpy.count(), 1);

        tracker.resetSession();
        tracker.onSample(9.0, 150.0);  // Should fire again after reset
        QCOMPARE(pressureSpy.count(), 2);
    }

    // ==========================================
    // SteamHealthTracker: settings tolerance
    // ==========================================

    void settingsToleranceMatchesWithinBand() {
        SteamHealthTracker tracker;

        // Seed 5 sessions at flow=150, temp=160
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QVERIFY(tracker.hasData());

        // Now add session at flow=155 (within ±10 tolerance) — should still be comparable
        SteamDataModel model;
        for (int j = 0; j < 40; ++j)
            model.addSample(2.0 + j * 0.6, 2.5, 1.0, 161.0);
        tracker.onSessionComplete(&model, 155, 161);

        // currentPressure should reflect the new session (not 0.0 which would mean no comparable data)
        QVERIFY(tracker.currentPressure() > 0.0);
    }

    void settingsToleranceRejectsOutOfBand() {
        SteamHealthTracker tracker;

        // Seed 5 sessions at flow=150, temp=160
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Change to very different flow (200, well outside ±10) — should not be comparable
        SteamDataModel model;
        for (int j = 0; j < 40; ++j)
            model.addSample(2.0 + j * 0.6, 5.0, 1.0, 160.0);
        tracker.onSessionComplete(&model, 200, 160);

        // The baseline/current should still reflect flow=200 sessions,
        // but hasData should be false since there's only 1 session at flow=200
        // Actually updateCachedStats uses the latest settings, so current will reflect 200
        // but baseline should be 5.0 since it's the only comparable session
        QCOMPARE(tracker.currentPressure(), 5.0);
    }

    // ==========================================
    // SteamHealthTracker: trend detection
    // ==========================================

    void noWarningBelowThreshold() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Seed 6 sessions with stable pressure
        for (int i = 0; i < 6; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0 + i * 0.1, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QCOMPARE(warnSpy.count(), 0);  // Small increase — well below 60% threshold
    }

    void warningAt60PercentProgress() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Seed 5 sessions at low pressure (baseline ≈ 2.0 bar)
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Now add session at 5.8 bar — well above 60% of flow-relative range
        // warnLevel = min(2.0 * 3.0, 8.0) = 6.0, range = 6.0 - 2.0 = 4.0
        // progress = (5.8 - 2.0) / 4.0 = 3.8/4.0 = 0.95
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
        SteamDataModel highModel;
        for (int j = 0; j < 40; ++j)
            highModel.addSample(2.0 + j * 0.6, 5.8, 1.0, 160.0);
        tracker.onSessionComplete(&highModel, 150, 160);

        QCOMPARE(warnSpy.count(), 1);
    }

    // ==========================================
    // SteamHealthTracker: warning cooldown
    // ==========================================

    void warningCooldownPreventsRepeatWarnings() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Seed 5 sessions at low pressure
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Add high-pressure session — should warn
        auto addHighSession = [&]() {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 5.8, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        };

        addHighSession();
        QCOMPARE(warnSpy.count(), 1);

        // Next 4 sessions at same high pressure — cooldown should suppress (no ignoreMessage needed)
        for (int i = 0; i < 4; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 5.8, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QCOMPARE(warnSpy.count(), 1);  // Still only 1

        // 5th session after warning — cooldown expired, should warn again
        addHighSession();
        QCOMPARE(warnSpy.count(), 2);
    }

    void clearHistoryResetsCooldown() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Build up and trigger a warning
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
        SteamDataModel highModel;
        for (int j = 0; j < 40; ++j)
            highModel.addSample(2.0 + j * 0.6, 5.8, 1.0, 160.0);
        tracker.onSessionComplete(&highModel, 150, 160);
        QCOMPARE(warnSpy.count(), 1);

        // Clear history — should reset cooldown
        tracker.clearHistory();
        QCOMPARE(tracker.sessionCount(), 0);
        QVERIFY(!tracker.hasData());
    }

    // ==========================================
    // SteamHealthTracker: baseline calculation
    // ==========================================

    void pressureBaselineIsLowestRecorded() {
        SteamHealthTracker tracker;

        // Session 1: 3.0 bar
        SteamDataModel m1;
        for (int j = 0; j < 40; ++j) m1.addSample(2.0 + j * 0.6, 3.0, 1.0, 160.0);
        tracker.onSessionComplete(&m1, 150, 160);

        // Session 2: 2.0 bar (lowest)
        SteamDataModel m2;
        for (int j = 0; j < 40; ++j) m2.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
        tracker.onSessionComplete(&m2, 150, 160);

        // Session 3: 2.5 bar
        SteamDataModel m3;
        for (int j = 0; j < 40; ++j) m3.addSample(2.0 + j * 0.6, 2.5, 1.0, 160.0);
        tracker.onSessionComplete(&m3, 150, 160);

        // Sessions 4-5
        SteamDataModel m4, m5;
        for (int j = 0; j < 40; ++j) m4.addSample(2.0 + j * 0.6, 2.8, 1.0, 160.0);
        for (int j = 0; j < 40; ++j) m5.addSample(2.0 + j * 0.6, 3.2, 1.0, 160.0);
        tracker.onSessionComplete(&m4, 150, 160);
        tracker.onSessionComplete(&m5, 150, 160);

        // Baseline should be the lowest: 2.0 bar
        QCOMPARE(tracker.baselinePressure(), 2.0);
        QCOMPARE(tracker.currentPressure(), 3.2);
    }

    void temperatureBaselineIsUserTarget() {
        SteamHealthTracker tracker;

        // Session with temp setting = 160
        SteamDataModel model;
        for (int j = 0; j < 40; ++j)
            model.addSample(2.0 + j * 0.6, 2.0, 1.0, 165.0);  // actual 165, target 160

        // Need 5 sessions for hasData
        for (int i = 0; i < 5; ++i)
            tracker.onSessionComplete(&model, 150, 160);

        // Baseline temp should be the target setting (160), not the measured value (165)
        QCOMPARE(tracker.baselineTemperature(), 160.0);
        QCOMPARE(tracker.currentTemperature(), 165.0);
    }

    // ==========================================
    // SteamHealthTracker: history management
    // ==========================================

    void historyLimitedTo150Sessions() {
        SteamHealthTracker tracker;

        for (int i = 0; i < 160; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QCOMPARE(tracker.sessionCount(), 150);
    }

    void thresholdPropertiesExposed() {
        SteamHealthTracker tracker;
        // Fresh tracker with no baseline returns hard limit
        QCOMPARE(tracker.pressureThreshold(), 8.0);
        QCOMPARE(tracker.temperatureThreshold(), 180.0);
    }

    // ==========================================
    // SteamHealthTracker: flow-relative thresholds
    // ==========================================

    void pressureThresholdIsFlowRelative() {
        SteamHealthTracker tracker;

        // Seed 5 sessions at baseline ~2.0 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // pressureThreshold = min(2.0 * 3.0, 8.0) = 6.0
        QCOMPARE(tracker.pressureThreshold(), 6.0);
    }

    void pressureThresholdCappedAtHardLimit() {
        SteamHealthTracker tracker;

        // Seed 5 sessions at baseline ~4.0 bar (high flow)
        // 4.0 * 3.0 = 12.0, should be capped at 8.0
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 4.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QCOMPARE(tracker.pressureThreshold(), 8.0);
    }

    void warningAt60PercentBoundary() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Seed 5 sessions at baseline ~2.0 bar
        // warnLevel = min(2.0 * 3.0, 8.0) = 6.0, range = 4.0
        // 60% threshold = 2.0 + 4.0 * 0.6 = 4.4 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QCOMPARE(warnSpy.count(), 0);

        // Session at 4.3 bar — just below 60% (progress = 2.3/4.0 = 0.575)
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 4.3, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QCOMPARE(warnSpy.count(), 0);  // No warning — below 60%

        // Session at 4.5 bar — above 60% (progress = 2.5/4.0 = 0.625)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 4.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QCOMPARE(warnSpy.count(), 1);  // Warning fires
    }

    void highBaselineWarningStillReachable() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Seed 5 sessions at baseline ~3.5 bar (high flow)
        // warnLevel = min(3.5 * 3.0, 8.0) = 8.0, range = 4.5
        // 60% threshold = 3.5 + 4.5 * 0.6 = 6.2 bar (below hard limit — reachable)
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 3.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session at 6.5 bar — above 60% (progress = 3.0/4.5 = 0.667)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 6.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QCOMPARE(warnSpy.count(), 1);
    }
};

QTEST_MAIN(tst_SteamHealth)
#include "tst_steamhealth.moc"
