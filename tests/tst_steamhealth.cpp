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
    QVariant m_origPendingAutoReset;
    QVariant m_origEstablishingAfterReset;

private slots:

    void init() {
        m_origHistory = m_settings.value("steam/sessionHistory").toByteArray();
        m_origLastWarned = m_settings.value("steam/lastWarnedSession");
        m_origLastFlow = m_settings.value("steam/lastTrackedFlow");
        m_origLastTemp = m_settings.value("steam/lastTrackedTemp");
        m_origPendingAutoReset = m_settings.value("steam/pendingAutoReset");
        m_origEstablishingAfterReset = m_settings.value("steam/establishingAfterReset");
        // Start each test with clean history
        m_settings.remove("steam/sessionHistory");
        m_settings.remove("steam/lastWarnedSession");
        m_settings.remove("steam/lastTrackedFlow");
        m_settings.remove("steam/lastTrackedTemp");
        m_settings.remove("steam/pendingAutoReset");
        m_settings.remove("steam/establishingAfterReset");
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

        if (!m_origPendingAutoReset.isValid())
            m_settings.remove("steam/pendingAutoReset");
        else
            m_settings.setValue("steam/pendingAutoReset", m_origPendingAutoReset);

        if (!m_origEstablishingAfterReset.isValid())
            m_settings.remove("steam/establishingAfterReset");
        else
            m_settings.setValue("steam/establishingAfterReset", m_origEstablishingAfterReset);
    }

    // Helper: feed the tracker a single session at given raw pressure / temp.
    static void addSession(SteamHealthTracker& tracker, double rawPressure,
                           int steamFlow = 150, int steamTemp = 160,
                           double measuredTemp = 160.0) {
        SteamDataModel model;
        for (int j = 0; j < 40; ++j)
            model.addSample(2.0 + j * 0.6, rawPressure, 1.0, measuredTemp);
        tracker.onSessionComplete(&model, steamFlow, steamTemp);
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
    // SteamHealthTracker: flow normalization
    // ==========================================

    void normalizationProducesConsistentBaseline() {
        SteamHealthTracker tracker;

        // Mix sessions at different flow settings — all on a clean machine.
        // Flow 75 (0.75 mL/s): avg pressure ~1.4 bar
        // Flow 120 (1.20 mL/s): avg pressure ~2.0 bar
        // Flow 150 (1.50 mL/s, reference): avg pressure ~2.4 bar (extrapolated)
        // Normalization: measuredP - 0.012 * (flow - 150)
        //   flow 75: 1.4 - 0.012*(75-150) = 1.4 + 0.9 = 2.3 bar normalized
        //   flow 120: 2.0 - 0.012*(120-150) = 2.0 + 0.36 = 2.36 bar normalized
        //   flow 150: 2.4 - 0.012*(150-150) = 2.4 bar normalized

        // 2 sessions at flow=75, raw pressure 1.4 bar
        for (int i = 0; i < 2; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 1.4, 1.0, 160.0);
            tracker.onSessionComplete(&model, 75, 160);
        }

        // 2 sessions at flow=120, raw pressure 2.0 bar
        for (int i = 0; i < 2; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 120, 160);
        }

        // 1 session at flow=150, raw pressure 2.4 bar
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.4, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QVERIFY(tracker.hasData());  // 5 sessions total

        // All normalized pressures should be close (~2.3-2.4 bar)
        // Baseline = lowest normalized = 2.3 (from flow=75 sessions)
        QCOMPARE(tracker.baselinePressure(), 2.3);
        // Current = most recent normalized = 2.4 (flow=150 session)
        QCOMPARE(tracker.currentPressure(), 2.4);
    }

    void sessionsAtDifferentFlowAllCountToward5() {
        SteamHealthTracker tracker;

        // 3 sessions at flow=80, 2 at flow=150 — all should count
        for (int i = 0; i < 3; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 1.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 80, 160);
        }
        QVERIFY(!tracker.hasData());  // Only 3

        for (int i = 0; i < 2; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QVERIFY(tracker.hasData());  // Now 5 total
        QCOMPARE(tracker.sessionCount(), 5);
    }

    // ==========================================
    // SteamHealthTracker: clean reference floor
    // ==========================================

    void cleanReferenceFloorDetectsScaledMachine() {
        SteamHealthTracker tracker;
        QSignalSpy warnSpy(&tracker, &SteamHealthTracker::scaleBuildupWarning);

        // Simulate a machine that has never been seen clean — all sessions
        // at high normalized pressure (5.0 bar at reference flow).
        // Without the clean reference floor, baseline would be 5.0 and
        // no warning would ever fire.
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 5.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QVERIFY(tracker.hasData());
        // Baseline should be the clean reference (3.0), not the measured 5.0
        QCOMPARE(tracker.baselinePressure(), 3.0);

        // Progress should be significant: (5.0 - 3.0) / (min(3.0*3,8.0) - 3.0) = 2.0/5.0 = 0.4
        // That's above 0.3 (monitor) but below 0.6 (warning)
        // The 5th session should NOT have triggered a warning (only 40% progress)
        QCOMPARE(warnSpy.count(), 0);
    }

    void cleanMachineUsesOwnBaseline() {
        SteamHealthTracker tracker;

        // Sessions at 2.0 bar (below clean reference of 3.0) — machine is genuinely clean
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Baseline should be the measured 2.0, not the clean reference 3.0
        QCOMPARE(tracker.baselinePressure(), 2.0);
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

    void temperatureBaselineIsLowestMeasured() {
        SteamHealthTracker tracker;

        // Sessions with varying measured temperatures
        // Session 1-3: measured 165°C
        for (int i = 0; i < 3; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 165.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session 4: measured 158°C (lowest)
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 158.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session 5: measured 162°C
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 162.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        QVERIFY(tracker.hasData());
        // Baseline temp = lowest measured (158), not user setting (160)
        QCOMPARE(tracker.baselineTemperature(), 158.0);
        QCOMPARE(tracker.currentTemperature(), 162.0);
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

    // ==========================================
    // MCP steam health status derivation
    // ==========================================

    void mcpStatusInsufficientDataWhenLessThan5Sessions() {
        SteamHealthTracker tracker;
        QVERIFY(!tracker.hasData());
        QCOMPARE(tracker.sessionCount(), 0);

        // Add 4 sessions — still not enough
        for (int i = 0; i < 4; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QVERIFY(!tracker.hasData());
        QCOMPARE(tracker.sessionCount(), 4);
    }

    void mcpStatusHealthyWhenProgressBelow30Percent() {
        SteamHealthTracker tracker;

        // 5 sessions at baseline ~2.0 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }
        QVERIFY(tracker.hasData());

        // Add session at 2.5 bar — threshold = 6.0, progress = 0.5/4.0 = 0.125 (< 0.3 = healthy)
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        double range = tracker.pressureThreshold() - tracker.baselinePressure();
        double progress = (tracker.currentPressure() - tracker.baselinePressure()) / range;
        QVERIFY(progress < 0.3);
    }

    void mcpStatusMonitorWhenProgressBetween30And60Percent() {
        SteamHealthTracker tracker;

        // 5 sessions at baseline ~2.0 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session at 3.5 bar — threshold = 6.0, progress = 1.5/4.0 = 0.375 (0.3-0.6 = monitor)
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 3.5, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        double range = tracker.pressureThreshold() - tracker.baselinePressure();
        double progress = (tracker.currentPressure() - tracker.baselinePressure()) / range;
        QVERIFY(progress >= 0.3);
        QVERIFY(progress < 0.6);
    }

    void mcpStatusWarningWhenProgressAbove60Percent() {
        SteamHealthTracker tracker;

        // 5 sessions at baseline ~2.0 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session at 5.0 bar — threshold = 6.0, progress = 3.0/4.0 = 0.75 (>= 0.6 = warning)
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SteamHealth \\[warn\\].*pressure"));
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 5.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        double range = tracker.pressureThreshold() - tracker.baselinePressure();
        double progress = (tracker.currentPressure() - tracker.baselinePressure()) / range;
        QVERIFY(progress >= 0.6);
    }

    void mcpProgressCalculationMatchesExpected() {
        SteamHealthTracker tracker;

        // 5 sessions at baseline ~2.0 bar
        for (int i = 0; i < 5; ++i) {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 2.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // Session at 4.0 bar
        {
            SteamDataModel model;
            for (int j = 0; j < 40; ++j)
                model.addSample(2.0 + j * 0.6, 4.0, 1.0, 160.0);
            tracker.onSessionComplete(&model, 150, 160);
        }

        // baseline = 2.0, threshold = min(2.0*3, 8.0) = 6.0
        // current = 4.0, progress = (4.0-2.0)/(6.0-2.0) = 0.5
        QCOMPARE(tracker.baselinePressure(), 2.0);
        QCOMPARE(tracker.pressureThreshold(), 6.0);
        QCOMPARE(tracker.currentPressure(), 4.0);
        double range = tracker.pressureThreshold() - tracker.baselinePressure();
        double progress = (tracker.currentPressure() - tracker.baselinePressure()) / range;
        QCOMPARE(progress, 0.5);
    }

    // ==========================================
    // Auto-reset two-step confirmation (issue #752)
    // ==========================================
    //
    // A real descale/steam-wand clean produces a persistent pressure drop
    // across multiple sessions. A single low reading (partial boiler,
    // normalization edge case, etc.) is an outlier and must NOT trim
    // history on its own. The trim only fires when two consecutive
    // sessions both drop >= AUTO_RESET_DROP_THRESHOLD below the rolling
    // recent average. After trimming, only the two triggering sessions
    // are retained — older scaled-up sessions would pollute the new
    // baseline's recent-average window.

    void singleLowSessionArmsButDoesNotTrim() {
        SteamHealthTracker tracker;

        // Seed a scaled-up baseline: 6 sessions at 5.0 bar raw (normalized ~5.0).
        for (int i = 0; i < 6; ++i)
            addSession(tracker, 5.0);

        int countBeforeDrop = tracker.sessionCount();
        QCOMPARE(countBeforeDrop, 6);

        // Single low session — raw 1.0 bar. Big drop, should arm but not trim.
        addSession(tracker, 1.0);

        QCOMPARE(tracker.sessionCount(), countBeforeDrop + 1);
        QVERIFY2(tracker.baselineState() == SteamHealthTracker::Ready,
                 "single low session must not trim history — trend detection stays live");
    }

    void twoConsecutiveLowSessionsTrimHistoryToJustThem() {
        SteamHealthTracker tracker;

        // Seed a scaled-up baseline.
        for (int i = 0; i < 6; ++i)
            addSession(tracker, 5.0);

        // First low session — arms.
        addSession(tracker, 1.0);
        QCOMPARE(tracker.sessionCount(), 7);

        // Second low session — confirms, triggers trim to exactly 2 sessions
        // (the two triggering low ones; all older scaled-up sessions discarded).
        addSession(tracker, 1.0);

        QCOMPARE(tracker.sessionCount(), 2);
        // State flips to EstablishingAfterReset (2 < MIN_SESSIONS_FOR_TREND=5)
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingAfterReset);
    }

    void pressureRecoveryDisarmsPendingReset() {
        SteamHealthTracker tracker;

        // Use raw 5.0 / 3.0 so the baseline doesn't drop so far that the
        // recovery session looks like massive scale buildup and fires a
        // spurious scaleBuildupWarning after disarming.
        //   baseline=3.0, warn=min(9,8)=8, range=5, threshold=1.5
        //   arming drop = 5.0 - 3.0 = 2.0 (armed)
        //   recovery drop = avg(3,5,5,5,5)=4.6 - 5.0 = -0.4 (disarm)
        for (int i = 0; i < 6; ++i)
            addSession(tracker, 5.0);

        // Armed by a low session...
        addSession(tracker, 3.0);
        QCOMPARE(tracker.sessionCount(), 7);

        // ...but next session recovers to the original range. No trim.
        addSession(tracker, 5.0);

        QCOMPARE(tracker.sessionCount(), 8);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::Ready);
    }

    void establishingAfterResetClearsOnceFullWindow() {
        SteamHealthTracker tracker;

        // Seed, trigger reset, then refill past the 5-session threshold.
        for (int i = 0; i < 6; ++i)
            addSession(tracker, 5.0);
        addSession(tracker, 1.0);  // arm
        addSession(tracker, 1.0);  // confirm — trim
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingAfterReset);

        // 3 more sessions at the new (low) range — baseline state should
        // flip back to Ready once sessionCount == 5.
        addSession(tracker, 1.0);
        addSession(tracker, 1.0);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingAfterReset);
        addSession(tracker, 1.0);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::Ready);
    }

    void pendingAutoResetSurvivesRestart() {
        {
            SteamHealthTracker tracker;
            for (int i = 0; i < 6; ++i)
                addSession(tracker, 5.0);
            addSession(tracker, 1.0);  // arms — m_pendingAutoReset persisted to QSettings
            QCOMPARE(tracker.sessionCount(), 7);
        }

        // Simulate app restart: a new tracker instance inherits persisted state.
        SteamHealthTracker tracker2;
        QCOMPARE(tracker2.sessionCount(), 7);

        // Immediately trigger a confirming low session — if the pending flag
        // wasn't persisted, this would merely arm again and NOT trim.
        addSession(tracker2, 1.0);

        QCOMPARE(tracker2.sessionCount(), 2);
        QCOMPARE(tracker2.baselineState(), SteamHealthTracker::EstablishingAfterReset);
    }

    void establishingAfterResetSurvivesRestart() {
        {
            SteamHealthTracker tracker;
            for (int i = 0; i < 6; ++i)
                addSession(tracker, 5.0);
            addSession(tracker, 1.0);
            addSession(tracker, 1.0);  // trim fires
            QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingAfterReset);
        }

        SteamHealthTracker tracker2;
        QCOMPARE(tracker2.baselineState(), SteamHealthTracker::EstablishingAfterReset);
        QCOMPARE(tracker2.sessionCount(), 2);
    }

    void baselineStateReflectsSessionCount() {
        SteamHealthTracker tracker;
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::Empty);

        addSession(tracker, 2.0);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingInitial);

        for (int i = 0; i < 3; ++i)
            addSession(tracker, 2.0);
        QCOMPARE(tracker.sessionCount(), 4);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingInitial);

        addSession(tracker, 2.0);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::Ready);
    }

    void clearHistoryResetsAutoResetFlags() {
        SteamHealthTracker tracker;
        for (int i = 0; i < 6; ++i)
            addSession(tracker, 5.0);
        addSession(tracker, 1.0);  // arm
        addSession(tracker, 1.0);  // confirm — trim fires
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingAfterReset);

        tracker.clearHistory();
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::Empty);

        // After a manual clear, a fresh new run should behave like a
        // clean install: initial establishing state, not after-reset.
        addSession(tracker, 2.0);
        QCOMPARE(tracker.baselineState(), SteamHealthTracker::EstablishingInitial);
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
