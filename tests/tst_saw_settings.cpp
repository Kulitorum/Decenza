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
        // After both have graduated, sawLearnedLagFor(A) and sawLearnedLagFor(B)
        // should reflect their own batches, not the global average.
        commitBatch(kProfileA, 0.6, 1.5);   // lag 0.4s
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileA, 0.6, 1.5);   // 3 medians → graduated
        commitBatch(kProfileB, 3.0, 1.5);   // lag 2.0s
        commitBatch(kProfileB, 3.0, 1.5);
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

        // 3. C graduates → uses its own data.
        commitBatch(kProfileC, 1.2, 1.5);
        QCOMPARE(m_settings.sawModelSource(kProfileC, kScale), QString("perProfile"));
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
        // Three batches at consistent lag = 0.4s should yield expected drip ≈ flow * 0.4
        commitBatch(kProfileA, 0.6, 1.5);
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
