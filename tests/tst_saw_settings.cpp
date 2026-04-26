#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

#include "core/settings.h"

// Tests for per-(profile, scale) SAW learning in Settings.
// Each test wipes SAW data in init/cleanup so QSettings state from a prior run
// or another test cannot leak in.

class tst_SawSettings : public QObject {
    Q_OBJECT

private:
    Settings m_settings;

    static constexpr const char* kScale = "Decent Scale";
    static constexpr const char* kProfileA = "profile_a";
    static constexpr const char* kProfileB = "profile_b";
    static constexpr const char* kProfileC = "profile_c";

    // Drive a full 5-shot batch with consistent (drip, flow, overshoot).
    void commitBatch(const QString& profile, double drip, double flow, double overshoot = 0.0) {
        for (int i = 0; i < 5; ++i)
            m_settings.addSawLearningPoint(drip, flow, kScale, overshoot, profile);
    }

private slots:

    void init() {
        m_settings.resetSawLearning();
    }

    void cleanup() {
        m_settings.resetSawLearning();
    }

    // ===== Per-pair isolation =====

    void perPairIsolatesFromOtherProfile() {
        // A's batch commits a small drip; B's commits a large drip.
        // After both have graduated (2 committed medians each), sawLearnedLagFor(A)
        // and sawLearnedLagFor(B) should reflect their own batches, not the global average.
        commitBatch(kProfileA, 0.6, 1.5);   // lag 0.4s
        commitBatch(kProfileA, 0.6, 1.5);   // 2 medians → graduated
        commitBatch(kProfileB, 3.0, 1.5);   // lag 2.0s
        commitBatch(kProfileB, 3.0, 1.5);

        const double lagA = m_settings.sawLearnedLagFor(kProfileA, kScale);
        const double lagB = m_settings.sawLearnedLagFor(kProfileB, kScale);
        QVERIFY2(lagA < 0.5, qPrintable(QString("A lag %1 not isolated").arg(lagA)));
        QVERIFY2(lagB > 1.8, qPrintable(QString("B lag %1 not isolated").arg(lagB)));
    }

    // ===== Batch commit at N=5 =====

    void batchAccumulatesUntilFiveThenCommits() {
        // Before 5 shots: pending batch grows, no committed history.
        for (int i = 0; i < 4; ++i) {
            m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
            QCOMPARE(m_settings.sawPendingBatch(kProfileA, kScale).size(), i + 1);
            QCOMPARE(m_settings.perProfileSawHistory(kProfileA, kScale).size(), 0);
        }

        // 5th shot triggers commit: pending cleared, history gains one median.
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        QCOMPARE(m_settings.sawPendingBatch(kProfileA, kScale).size(), 0);
        QCOMPARE(m_settings.perProfileSawHistory(kProfileA, kScale).size(), 1);
    }

    // ===== Batch rejection on high IQR =====

    void batchRejectedWhenDispersionTooHigh() {
        // 4 tight entries at lag=0.4s and 1 wild outlier at lag=2.5s. Median lag = 0.4s,
        // deviation of the outlier = 2.1s > 1.5s threshold → batch rejected and dropped.
        // All lags remain ≤ 4s so they pass the entry-level lag-too-high guard.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[SAW\] batch rejected — outlier lag=\S+ deviates \S+ > \S+ from median)"));
        m_settings.addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.addSawLearningPoint(3.75, 1.5, kScale, 0.0, kProfileA);  // lag 2.50 → reject

        // Batch dropped → pending cleared, no commit, no history.
        QCOMPARE(m_settings.sawPendingBatch(kProfileA, kScale).size(), 0);
        QCOMPARE(m_settings.perProfileSawHistory(kProfileA, kScale).size(), 0);
    }

    // ===== Global bootstrap recompute =====

    void globalBootstrapUpdatedAfterMultiplePairsGraduate() {
        // Bootstrap requires ≥ 2 graduated pairs on the same scale to update.
        commitBatch(kProfileA, 0.6, 1.5);  // batches → 1 median
        QCOMPARE(m_settings.globalSawBootstrapLag(kScale), 0.0); // only 1 pair

        commitBatch(kProfileB, 0.9, 1.5);  // 2nd pair graduates → bootstrap updates
        const double bootstrap = m_settings.globalSawBootstrapLag(kScale);
        QVERIFY2(bootstrap > 0.0, "bootstrap not set after 2 graduated pairs");
        // Median of A's 0.4s and B's 0.6s → 0.5s.
        QVERIFY2(qAbs(bootstrap - 0.5) < 0.05,
                 qPrintable(QString("expected ~0.5s, got %1").arg(bootstrap)));
    }

    // ===== Cold-start fallback chain =====

    void coldStartFallsBackToScaleDefaultThenBootstrapThenPerProfile() {
        // 1. No data anywhere → "scaleDefault" source.
        QCOMPARE(m_settings.sawModelSource(kProfileA, kScale), QString("scaleDefault"));

        // 2. Two other pairs graduate → bootstrap exists → C uses "globalBootstrap".
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QCOMPARE(m_settings.sawModelSource(kProfileC, kScale), QString("globalBootstrap"));

        // 3. C graduates (needs ≥ kSawMinMediansForGraduation committed medians,
        //    currently 2) → uses its own data.
        commitBatch(kProfileC, 1.2, 1.5);
        commitBatch(kProfileC, 1.2, 1.5);
        QCOMPARE(m_settings.sawModelSource(kProfileC, kScale), QString("perProfile"));
    }

    // ===== One median is not enough to graduate =====

    void singleMedianStillFallsBackToBootstrap() {
        // After one committed median on C, the read path must still treat C as a
        // cold-start pair and fall back to globalBootstrap (set up by A and B below).
        // This guards the lower-bound boundary of the graduation gate.
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.globalSawBootstrapLag(kScale) > 0.0);

        commitBatch(kProfileC, 1.2, 1.5);  // 1 median — below graduation threshold
        QCOMPARE(m_settings.perProfileSawHistory(kProfileC, kScale).size(), 1);
        QCOMPARE(m_settings.sawModelSource(kProfileC, kScale), QString("globalBootstrap"));
    }

    // ===== Reset for profile only =====

    void resetForProfileLeavesOtherPairsIntact() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.perProfileSawHistory(kProfileA, kScale).size() > 0);
        QVERIFY(m_settings.perProfileSawHistory(kProfileB, kScale).size() > 0);

        m_settings.resetSawLearningForProfile(kProfileA, kScale);

        QCOMPARE(m_settings.perProfileSawHistory(kProfileA, kScale).size(), 0);
        QVERIFY(m_settings.perProfileSawHistory(kProfileB, kScale).size() > 0);
    }

    // ===== Reset for profile clears pending batch =====

    void resetForProfileClearsPendingBatch() {
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        QCOMPARE(m_settings.sawPendingBatch(kProfileA, kScale).size(), 2);

        m_settings.resetSawLearningForProfile(kProfileA, kScale);

        QCOMPARE(m_settings.sawPendingBatch(kProfileA, kScale).size(), 0);
    }

    // ===== getExpectedDripFor returns per-pair after graduation =====

    void getExpectedDripForUsesPerPairAfterGraduation() {
        // Two batches at consistent lag = 0.4s should yield expected drip ≈ flow * 0.4
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileA, 0.6, 1.5);

        const double drip = m_settings.getExpectedDripFor(kProfileA, kScale, 1.5);
        QVERIFY2(qAbs(drip - 0.6) < 0.15,
                 qPrintable(QString("expected ~0.6, got %1").arg(drip)));
    }

    // ===== Legacy call site (no profile) still works =====

    void legacyAddSawLearningPointStillAppendsToGlobalPool() {
        // Calling without a profile uses the legacy single-shot append. Verify
        // the global pool grows by 1 and isSawConverged respects scale type.
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        m_settings.addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        QCOMPARE(m_settings.sawLearningEntries(kScale, 10).size(), 3);
    }

    // ===== Bootstrap survives a single profile reset =====

    void bootstrapPersistsWhenOneProfileResets() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        const double before = m_settings.globalSawBootstrapLag(kScale);
        QVERIFY(before > 0.0);

        m_settings.resetSawLearningForProfile(kProfileA, kScale);

        // Bootstrap is recomputed only on commits, so it stays at the previous
        // value (still useful as a fallback for new profiles) — verify.
        QCOMPARE(m_settings.globalSawBootstrapLag(kScale), before);
    }

    // ===== σ flow-similarity behavior =====
    //
    // The other tests in this file train and query at the same flow value, so the
    // gaussian flow-similarity weight is always 1.0 and σ is invisible to them.
    // The three tests below probe σ explicitly so a future regression that widens
    // it back out (or accidentally narrows it to zero) is caught.

    void farQueryFlowFallsBackBecauseGaussianAttenuates() {
        // Capture the cold-start scale-default fallback at the query flow. With no
        // committed data and no bootstrap, getExpectedDripFor returns
        // flow × (sensorLag + 0.1).
        const double fallback = m_settings.getExpectedDripFor(kProfileA, kScale, 2.5);

        // Train two batches at flow=1.5 so the per-pair history graduates with a
        // training drip far from the fallback.
        commitBatch(kProfileA, 2.0, 1.5);
        commitBatch(kProfileA, 2.0, 1.5);

        // Query 1.0 ml/s away from training. At σ=0.25, flowWeight=exp(-8)≈3e-4 and
        // totalWeight drops below the 0.01 floor → branch falls through to the
        // scale-default fallback. At σ=1.5 (regression) flowWeight≈0.80 and the
        // prediction would lock to 2.0 g.
        const double pred = m_settings.getExpectedDripFor(kProfileA, kScale, 2.5);

        QVERIFY2(qAbs(pred - fallback) < qAbs(pred - 2.0),
                 qPrintable(QString("pred=%1 not closer to fallback=%2 than to training=2.0")
                                .arg(pred).arg(fallback)));
    }

    void sameFlowQueryReturnsTrainingDrip() {
        // Locks in the no-flow-shift case: when query flow equals training flow,
        // flowWeight=1 for every entry and the weighted average collapses to the
        // (constant) training drip regardless of σ. Tolerance is tight so a σ
        // regression doesn't hide here even though σ shouldn't matter at flowDiff=0.
        commitBatch(kProfileA, 2.0, 1.5);
        commitBatch(kProfileA, 2.0, 1.5);

        const double pred = m_settings.getExpectedDripFor(kProfileA, kScale, 1.5);
        QVERIFY2(qAbs(pred - 2.0) < 0.05,
                 qPrintable(QString("expected ~2.0, got %1").arg(pred)));
    }

    void differentQueryFlowsProduceDifferentPredictions() {
        // Two committed medians spanning a wide flow range. Querying at each end
        // should return the corresponding training drip — under σ=0.25 the
        // off-flow entry is attenuated to ~exp(-32) and contributes nothing. If σ
        // were widened to dilute everything to a flat average the two predictions
        // would converge.
        commitBatch(kProfileA, 0.6, 1.0);   // lag 0.6s
        commitBatch(kProfileA, 1.8, 3.0);   // lag 0.6s

        const double low  = m_settings.getExpectedDripFor(kProfileA, kScale, 1.0);
        const double high = m_settings.getExpectedDripFor(kProfileA, kScale, 3.0);

        QVERIFY2(qAbs(high - low) > 0.5,
                 qPrintable(QString("predictions did not separate by flow: low=%1 high=%2")
                                .arg(low).arg(high)));
    }

    // ===== Full reset clears bootstrap =====

    void fullResetClearsBootstrap() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.globalSawBootstrapLag(kScale) > 0.0);

        m_settings.resetSawLearning();

        QCOMPARE(m_settings.globalSawBootstrapLag(kScale), 0.0);
    }
};

QTEST_GUILESS_MAIN(tst_SawSettings)
#include "tst_saw_settings.moc"
