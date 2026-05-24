#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>

#include "core/settings.h"
#include "core/settings_calibration.h"
#include "ble/scales/scaletypeids.h"

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

    // Drive a full 3-shot batch with consistent (drip, flow, overshoot).
    void commitBatch(const QString& profile, double drip, double flow, double overshoot = 0.0) {
        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(drip, flow, kScale, overshoot, profile);
    }

    // Build a committed-median entry as stored in perProfileHistory.
    static QJsonObject medianEntry(double drip, double flow, const QString& scale) {
        QJsonObject e;
        e["drip"] = drip; e["flow"] = flow; e["overshoot"] = 0.0;
        e["scale"] = scale; e["profile"] = QStringLiteral("p"); e["ts"] = 1000;
        return e;
    }

    // Seed a raw per-profile SAW history map straight into QSettings, bypassing the
    // (now-normalizing) write path — used to simulate legacy display-name-keyed data.
    void seedPerProfileHistory(const QJsonObject& map) {
        QSettings qs("DecentEspresso", "DE1Qt");
        qs.setValue("saw/perProfileHistory", QJsonDocument(map).toJson(QJsonDocument::Compact));
        qs.sync();
        m_settings.calibration()->invalidateCache();
    }

private slots:

    void init() {
        m_settings.calibration()->resetSawLearning();
    }

    void cleanup() {
        m_settings.calibration()->resetSawLearning();
    }

    // ===== Per-pair isolation =====

    void perPairIsolatesFromOtherProfile() {
        // A's batch commits a small drip; B's commits a large drip.
        // After both have graduated (≥ 1 committed median each), sawLearnedLagFor(A)
        // and sawLearnedLagFor(B) should reflect their own batches, not the global average.
        commitBatch(kProfileA, 0.6, 1.5);   // lag 0.4s — 1 median → graduated
        commitBatch(kProfileA, 0.6, 1.5);   // 2nd median, extra stability
        commitBatch(kProfileB, 3.0, 1.5);   // lag 2.0s — graduated
        commitBatch(kProfileB, 3.0, 1.5);

        const double lagA = m_settings.calibration()->sawLearnedLagFor(kProfileA, kScale);
        const double lagB = m_settings.calibration()->sawLearnedLagFor(kProfileB, kScale);
        QVERIFY2(lagA < 0.5, qPrintable(QString("A lag %1 not isolated").arg(lagA)));
        QVERIFY2(lagB > 1.8, qPrintable(QString("B lag %1 not isolated").arg(lagB)));
    }

    // ===== Batch commit at N=3 =====

    void batchAccumulatesUntilThreeThenCommits() {
        // Before 3 shots: pending batch grows, no committed history.
        for (int i = 0; i < 2; ++i) {
            m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
            QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(), i + 1);
            QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(), 0);
        }

        // 3rd shot triggers commit: pending cleared, history gains one median.
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(), 0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(), 1);
    }

    // ===== Batch rejection on high deviation =====

    void batchRejectedWhenDispersionTooHigh() {
        // 2 tight entries at lag=0.4s and 1 wild outlier at lag=2.5s (N=3 batch).
        // Median lag = 0.4s, deviation of the outlier = 2.1s > 1.5s → batch rejected.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[SAW\] batch rejected — outlier lag=\S+ deviates \S+ > \S+ from median)"));
        m_settings.calibration()->addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.calibration()->addSawLearningPoint(0.6, 1.5, kScale, 0.0, kProfileA);   // lag 0.40
        m_settings.calibration()->addSawLearningPoint(3.75, 1.5, kScale, 0.0, kProfileA);  // lag 2.50 → reject

        // Batch dropped → pending cleared, no commit, no history.
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(), 0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(), 0);
    }

    // ===== Global bootstrap recompute =====

    void globalBootstrapUpdatedAfterMultiplePairsGraduate() {
        // Bootstrap requires ≥ 2 graduated pairs on the same scale to update.
        commitBatch(kProfileA, 0.6, 1.5);  // batches → 1 median
        QCOMPARE(m_settings.calibration()->globalSawBootstrapLag(kScale), 0.0); // only 1 pair

        commitBatch(kProfileB, 0.9, 1.5);  // 2nd pair graduates → bootstrap updates
        const double bootstrap = m_settings.calibration()->globalSawBootstrapLag(kScale);
        QVERIFY2(bootstrap > 0.0, "bootstrap not set after 2 graduated pairs");
        // Median of A's 0.4s and B's 0.6s → 0.5s.
        QVERIFY2(qAbs(bootstrap - 0.5) < 0.05,
                 qPrintable(QString("expected ~0.5s, got %1").arg(bootstrap)));
    }

    // ===== Cold-start fallback chain =====

    void coldStartFallsBackToScaleDefaultThenBootstrapThenPerProfile() {
        // 1. No data anywhere → "scaleDefault" source.
        QCOMPARE(m_settings.calibration()->sawModelSource(kProfileA, kScale), QString("scaleDefault"));

        // 2. Two other pairs graduate → bootstrap exists → C uses "globalBootstrap".
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QCOMPARE(m_settings.calibration()->sawModelSource(kProfileC, kScale), QString("globalBootstrap"));

        // 3. C graduates (needs ≥ kSawMinMediansForGraduation = 1 committed median) → uses its own data.
        commitBatch(kProfileC, 1.2, 1.5);
        QCOMPARE(m_settings.calibration()->sawModelSource(kProfileC, kScale), QString("perProfile"));
    }

    // ===== Zero medians still fall back to bootstrap =====

    void zeroMediansStillFallsBackToBootstrap() {
        // Before C has any committed medians, the read path must fall back to
        // globalBootstrap (set up by A and B). Guards the pre-graduation boundary.
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.calibration()->globalSawBootstrapLag(kScale) > 0.0);

        // C has no committed medians yet — should use globalBootstrap.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileC, kScale).size(), 0);
        QCOMPARE(m_settings.calibration()->sawModelSource(kProfileC, kScale), QString("globalBootstrap"));
    }

    // ===== Reset for profile only =====

    void resetForProfileLeavesOtherPairsIntact() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size() > 0);
        QVERIFY(m_settings.calibration()->perProfileSawHistory(kProfileB, kScale).size() > 0);

        m_settings.calibration()->resetSawLearningForProfile(kProfileA, kScale);

        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(), 0);
        QVERIFY(m_settings.calibration()->perProfileSawHistory(kProfileB, kScale).size() > 0);
    }

    // ===== Reset for profile clears pending batch =====

    void resetForProfileClearsPendingBatch() {
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0, kProfileA);
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(), 2);

        m_settings.calibration()->resetSawLearningForProfile(kProfileA, kScale);

        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(), 0);
    }

    // ===== getExpectedDripFor returns per-pair after graduation =====

    void getExpectedDripForUsesPerPairAfterGraduation() {
        // Two batches at consistent lag = 0.4s should yield expected drip ≈ flow * 0.4
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileA, 0.6, 1.5);

        const double drip = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 1.5);
        QVERIFY2(qAbs(drip - 0.6) < 0.15,
                 qPrintable(QString("expected ~0.6, got %1").arg(drip)));
    }

    // ===== Legacy call site (no profile) still works =====

    void legacyAddSawLearningPointStillAppendsToGlobalPool() {
        // Calling without a profile uses the legacy single-shot append. Verify
        // the global pool grows by 1 and isSawConverged respects scale type.
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        m_settings.calibration()->addSawLearningPoint(1.0, 2.0, kScale, 0.0);
        QCOMPARE(m_settings.calibration()->sawLearningEntries(kScale, 10).size(), 3);
    }

    // ===== Bootstrap survives a single profile reset =====

    void bootstrapPersistsWhenOneProfileResets() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        const double before = m_settings.calibration()->globalSawBootstrapLag(kScale);
        QVERIFY(before > 0.0);

        m_settings.calibration()->resetSawLearningForProfile(kProfileA, kScale);

        // Bootstrap is recomputed only on commits, so it stays at the previous
        // value (still useful as a fallback for new profiles) — verify.
        QCOMPARE(m_settings.calibration()->globalSawBootstrapLag(kScale), before);
    }

    // ===== σ flow-similarity behavior =====
    //
    // The other tests in this file train and query at the same flow value, so the
    // gaussian flow-similarity weight is always 1.0 and σ is invisible to them.
    // Two of the three tests below (farQueryFlow, differentQueryFlows) probe σ
    // explicitly so a future regression that widens it back out (or accidentally
    // narrows it to zero) is caught. The third (sameFlowQuery) is a flowDiff=0
    // baseline lock-in — σ is invisible there too, but the test pins the no-flow-
    // shift result so the surrounding weighted-average machinery can't silently break.

    void farQueryFlowFallsBackBecauseGaussianAttenuates() {
        // Capture the cold-start scale-default fallback at the query flow. With no
        // committed data and no bootstrap, getExpectedDripFor returns
        // flow × (sensorLag + 0.1).
        const double fallback = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 2.5);

        // Train two batches at flow=1.5 so the per-pair history graduates with a
        // training drip far from the fallback.
        commitBatch(kProfileA, 2.0, 1.5);
        commitBatch(kProfileA, 2.0, 1.5);

        // Query 1.0 ml/s away from training. At σ=0.25, flowWeight=exp(-8)≈3e-4 and
        // totalWeight drops below the 0.01 floor → branch falls through to the
        // scale-default fallback. At σ=1.5 (regression) flowWeight≈0.80 and the
        // prediction would lock to 2.0 g.
        const double pred = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 2.5);

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

        const double pred = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 1.5);
        QVERIFY2(qAbs(pred - 2.0) < 0.05,
                 qPrintable(QString("expected ~2.0, got %1").arg(pred)));
    }

    void differentQueryFlowsProduceDifferentPredictions() {
        // Two committed medians spanning a wide flow range. Querying at each end
        // should return the corresponding training drip — under σ=0.25 the
        // off-flow entry is attenuated to ~exp(-32) and contributes nothing. If σ
        // were widened to dilute everything to a flat average the two predictions
        // would converge.
        commitBatch(kProfileA, 0.6, 1.0);   // low-flow training: drip=0.6, flow=1.0
        commitBatch(kProfileA, 1.8, 3.0);   // high-flow training: drip=1.8, flow=3.0

        const double low  = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 1.0);
        const double high = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 3.0);

        QVERIFY2(qAbs(high - low) > 0.5,
                 qPrintable(QString("predictions did not separate by flow: low=%1 high=%2")
                                .arg(low).arg(high)));
    }

    // ===== Full reset clears bootstrap =====

    void fullResetClearsBootstrap() {
        commitBatch(kProfileA, 0.6, 1.5);
        commitBatch(kProfileB, 0.9, 1.5);
        QVERIFY(m_settings.calibration()->globalSawBootstrapLag(kScale) > 0.0);

        m_settings.calibration()->resetSawLearning();

        QCOMPARE(m_settings.calibration()->globalSawBootstrapLag(kScale), 0.0);
    }

    // ===== scale-type-identity: canonical id mapping =====

    void scaleTypeIdRoundTripsWithCanonicalAccessors() {
        const ScaleType all[] = {
            ScaleType::DecentScale, ScaleType::DecentScaleWifi, ScaleType::DecentScaleUsb,
            ScaleType::Acaia, ScaleType::AcaiaPyxis, ScaleType::Felicita, ScaleType::Skale,
            ScaleType::HiroiaJimmy, ScaleType::Bookoo, ScaleType::SmartChef,
            ScaleType::Difluid, ScaleType::EurekaPrecisa, ScaleType::SoloBarista,
            ScaleType::AtomheartEclair, ScaleType::VariaAku, ScaleType::Timemore,
        };
        for (ScaleType t : all) {
            const QString id = ScaleTypeIds::scaleTypeId(t);
            QVERIFY2(!id.isEmpty(), "every real scale type has a non-empty id");
            // Display name normalizes to the id...
            QCOMPARE(ScaleTypeIds::normalizeScaleTypeId(ScaleTypeIds::scaleTypeName(t)), id);
            // ...and the id normalizes to itself (idempotent).
            QCOMPARE(ScaleTypeIds::normalizeScaleTypeId(id), id);
        }
        // A genuinely unknown string passes through unchanged.
        QCOMPARE(ScaleTypeIds::normalizeScaleTypeId("Some Future Scale"), QString("Some Future Scale"));
    }

    // ===== scale-type-identity: sensorLag keyed by id =====

    void sensorLagResolvesByIdAndLegacyName() {
        QCOMPARE(SettingsCalibration::sensorLag("decent"), 0.38);
        QCOMPARE(SettingsCalibration::sensorLag("decent-wifi"), 0.38);
        QCOMPARE(SettingsCalibration::sensorLag("decent-usb"), 0.38);
        QCOMPARE(SettingsCalibration::sensorLag("bookoo"), 0.50);
        // Legacy display names still resolve via normalization.
        QCOMPARE(SettingsCalibration::sensorLag("Decent Scale"), 0.38);
        QCOMPARE(SettingsCalibration::sensorLag("Bookoo"), 0.50);
    }

    // ===== scale-type-identity: one-time migration =====

    void migrationRekeysLegacyDisplayNameHistory() {
        QJsonObject map;
        QJsonArray arr; arr.append(medianEntry(0.6, 1.5, "Decent Scale"));
        map["profile_a::Decent Scale"] = arr;
        seedPerProfileHistory(map);

        // Orphaned before migration: reading under the canonical id finds nothing.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory("profile_a", "decent").size(), 0);

        m_settings.calibration()->migrateScaleTypeIds();

        // Rekeyed to "profile_a::decent" with the same median + rewritten scale field.
        const QJsonArray hist = m_settings.calibration()->perProfileSawHistory("profile_a", "decent");
        QCOMPARE(hist.size(), 1);
        QCOMPARE(hist[0].toObject()["drip"].toDouble(), 0.6);
        QCOMPARE(hist[0].toObject()["scale"].toString(), QString("decent"));
    }

    void migrationLeavesIdKeysUnchanged() {
        QJsonObject map;
        QJsonArray arr; arr.append(medianEntry(0.7, 1.5, "decent-wifi"));
        map["profile_a::decent-wifi"] = arr;
        seedPerProfileHistory(map);

        m_settings.calibration()->migrateScaleTypeIds();

        QCOMPARE(m_settings.calibration()->perProfileSawHistory("profile_a", "decent-wifi").size(), 1);
    }

    void migrationIsIdempotent() {
        QJsonObject map;
        QJsonArray arr; arr.append(medianEntry(0.6, 1.5, "Decent Scale"));
        map["profile_a::Decent Scale"] = arr;
        seedPerProfileHistory(map);

        m_settings.calibration()->migrateScaleTypeIds();
        m_settings.calibration()->migrateScaleTypeIds();   // second run is a no-op

        QCOMPARE(m_settings.calibration()->perProfileSawHistory("profile_a", "decent").size(), 1);
    }

    void migrationMergesCollidingBucketsWithoutLoss() {
        QJsonObject map;
        QJsonArray a1; a1.append(medianEntry(0.6, 1.5, "Decent Scale"));
        QJsonArray a2; a2.append(medianEntry(0.9, 1.5, "decent"));
        map["profile_a::Decent Scale"] = a1;
        map["profile_a::decent"] = a2;
        seedPerProfileHistory(map);

        m_settings.calibration()->migrateScaleTypeIds();

        // Both legacy and pre-existing id entries survive under the id key.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory("profile_a", "decent").size(), 2);
    }

    void addKnownScaleStoresCanonicalId() {
        const QString addr = QStringLiteral("TEST:SCALE:IDNORM");
        m_settings.addKnownScale(addr, "Bookoo", "My Bookoo");
        QString storedType, storedName;
        for (const QVariant& v : m_settings.knownScales()) {
            const QVariantMap s = v.toMap();
            if (s["address"].toString() == addr) {
                storedType = s["type"].toString();
                storedName = s["name"].toString();
            }
        }
        m_settings.removeKnownScale(addr);  // clean up before asserting (survives failures)
        QCOMPARE(storedType, QString("bookoo"));   // id, not the "Bookoo" display name
        QCOMPARE(storedName, QString("My Bookoo")); // human label untouched
    }

    // migrateScaleTypeIds() branch A: global pool entry's "scale" field.
    void migrationRewritesGlobalPoolScaleField() {
        QSettings qs("DecentEspresso", "DE1Qt");
        QJsonArray pool; pool.append(medianEntry(1.0, 2.0, "Acaia"));
        qs.setValue("saw/learningHistory", QJsonDocument(pool).toJson());
        qs.sync();
        m_settings.calibration()->invalidateCache();

        // Before: entry keyed on display name "Acaia" — not matched under the id.
        QCOMPARE(m_settings.calibration()->sawLearningEntries("acaia", 10).size(), 0);

        m_settings.calibration()->migrateScaleTypeIds();

        // After: the "scale" field is rewritten to "acaia".
        QCOMPARE(m_settings.calibration()->sawLearningEntries("acaia", 10).size(), 1);
    }

    // migrateScaleTypeIds() branch C: pending-batch map keyed on a display name.
    void migrationRekeysPendingBatch() {
        QSettings qs("DecentEspresso", "DE1Qt");
        QJsonObject batchMap;
        QJsonArray b; b.append(medianEntry(1.0, 2.0, "Bookoo"));
        batchMap["profile_a::Bookoo"] = b;
        qs.setValue("saw/perProfileBatch", QJsonDocument(batchMap).toJson(QJsonDocument::Compact));
        qs.sync();
        m_settings.calibration()->invalidateCache();

        QCOMPARE(m_settings.calibration()->sawPendingBatch("profile_a", "bookoo").size(), 0);  // orphaned
        m_settings.calibration()->migrateScaleTypeIds();
        QCOMPARE(m_settings.calibration()->sawPendingBatch("profile_a", "bookoo").size(), 1);  // rekeyed
    }

    // migrateScaleTypeIds() branch D: globalBootstrapLag sub-keys.
    void migrationRenamesBootstrapSubKeys() {
        QSettings qs("DecentEspresso", "DE1Qt");
        qs.setValue("saw/globalBootstrapLag/Felicita", 0.42);  // legacy display-name key
        qs.setValue("saw/globalBootstrapLag/skale", 0.30);     // already an id (control)
        qs.sync();
        m_settings.calibration()->invalidateCache();

        m_settings.calibration()->migrateScaleTypeIds();

        QCOMPARE(m_settings.calibration()->globalSawBootstrapLag("felicita"), 0.42);  // renamed -> id
        QCOMPARE(m_settings.calibration()->globalSawBootstrapLag("skale"), 0.30);     // id key untouched
        QSettings qs2("DecentEspresso", "DE1Qt");
        QVERIFY(!qs2.contains("saw/globalBootstrapLag/Felicita"));                    // legacy key removed
    }

    // Settings-ctor one-time migration: the actual per-install upgrade path that
    // normalizes scale/type and flag-guards itself. Saves/restores the dev's store
    // (CI runs on a clean one). knownScales are already id-normalized by the member
    // m_settings ctor, so a fresh re-run won't disturb them.
    void ctorMigratesScaleTypeOnce() {
        QSettings qs("DecentEspresso", "DE1Qt");
        const QVariant origType = qs.value("scale/type");
        const QVariant origFlag = qs.value("scale/typeIdsMigrated");

        qs.setValue("scale/type", "Bookoo");   // legacy display name
        qs.remove("scale/typeIdsMigrated");    // force the one-time ctor migration to run
        qs.sync();

        {
            Settings fresh;   // ctor normalizes scale/type and sets the migrated flag
            QCOMPARE(fresh.scaleType(), QString("bookoo"));
        }
        QSettings qs2("DecentEspresso", "DE1Qt");
        QVERIFY(qs2.value("scale/typeIdsMigrated").toBool());

        if (origType.isValid()) qs.setValue("scale/type", origType); else qs.remove("scale/type");
        if (origFlag.isValid()) qs.setValue("scale/typeIdsMigrated", origFlag); else qs.remove("scale/typeIdsMigrated");
        qs.sync();
    }
};

QTEST_GUILESS_MAIN(tst_SawSettings)
#include "tst_saw_settings.moc"
