#include <QtTest>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>
#include <QHash>

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
    // Already-normalized basket keys. Tests pass these explicitly because the resolving
    // path reads the active equipment package, which no test installs.
    static constexpr const char* kBasketOne = "decent-18g-ridged";
    static constexpr const char* kBasketTwo = "graph-coffee-stepped-58-46mm";

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
        QSettings qs(Settings::testQSettingsPath(), QSettings::IniFormat);
        qs.setValue("saw/perProfileHistory", QJsonDocument(map).toJson(QJsonDocument::Compact));
        qs.sync();
        m_settings.calibration()->invalidateCache();
    }

private slots:

    void init() { QTest::failOnWarning();
        m_settings.calibration()->resetSawLearning();
    }

    void cleanup() {
        m_settings.calibration()->resetSawLearning();
        // Must be here, not at the end of each test that installs one. Settings is a
        // class member, so a provider left installed leaks into every later test in the
        // file — and a test that installs one then fails a QCOMPARE returns from the
        // slot without reaching its own teardown, burying the real failure under
        // cascading ones.
        m_settings.calibration()->setServingScaleTypeProvider(nullptr);
    }

    // ===== ScaleTypeIds::kAll completeness =====

    void everyScaleTypeIsInTheCanonicalVocabulary() {
        // kAll is a hand-maintained array and gets NO compiler help. The switches in
        // scaleTypeId()/scaleTypeName() have no `default:`, so -Wswitch (with -Werror)
        // forces a new enumerator to be handled there — but nothing forces it into kAll.
        //
        // Miss it and the failure is silent and expensive: isCanonicalScaleTypeId()
        // returns false for the new scale, SettingsCalibration::currentScaleType() falls
        // through to the SAVED primary, and that scale's shots train another scale's SAW
        // pool. Identical to the decent-wifi/BLE corruption this whole change set exists
        // to fix, for a scale nobody is looking at yet.
        //
        // Bound: iterates the real types, Unknown excluded (it has no id by design).
        // NOTE this catches "added an enumerator, forgot kAll" — the likely mistake —
        // but not "added an enumerator AFTER Timemore", which also needs this bound
        // moved. See the note on the enum's last entry in scaletypeids.h.
        // ScaleType is at global scope; only the helper functions are namespaced.
        int checked = 0;
        for (int t = int(ScaleType::Unknown) + 1; t <= int(ScaleType::Timemore); ++t) {
            const ScaleType type = static_cast<ScaleType>(t);
            const QString id = ScaleTypeIds::scaleTypeId(type);
            QVERIFY2(!id.isEmpty(),
                     qPrintable(QString("ScaleType %1 has no canonical id").arg(t)));
            QVERIFY2(ScaleTypeIds::isCanonicalScaleTypeId(id),
                     qPrintable(QString("id '%1' (ScaleType %2) is missing from kAll — SAW "
                                        "would key its shots on the saved primary").arg(id).arg(t)));
            // The round trips the three kAll consumers rely on.
            QCOMPARE(ScaleTypeIds::normalizeScaleTypeId(id), id);
            QCOMPARE(ScaleTypeIds::normalizeScaleTypeId(ScaleTypeIds::scaleTypeName(type)), id);
            QCOMPARE(ScaleTypeIds::scaleTypeNameForId(id), ScaleTypeIds::scaleTypeName(type));
            ++checked;
        }
        QVERIFY2(checked >= 16, "enum shrank unexpectedly — is the bound still right?");
    }

    // ===== Scale-key resolution (omitted scaleType) =====
    //
    // The whole point of the optional parameter: a call site that omits the scale
    // gets the SAME key the learner writes under. Four consumers previously derived
    // it three ways, so these pin the resolution itself rather than any one caller.

    void omittedScaleTypeResolvesToTheServingScale() {
        // Saved primary is one scale, a different one is actually serving. Learning
        // under the serving scale must be readable by a caller that names nothing —
        // this is the decent-wifi/BLE split that started all of this.
        m_settings.setScaleType(QStringLiteral("decent-wifi"));
        m_settings.calibration()->setServingScaleTypeProvider(
            []() { return QStringLiteral("bookoo"); });

        // One batch of 3 graduates the pair (kSawMinMediansForGraduation == 1).
        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(3.0, 1.5, QStringLiteral("bookoo"),
                                                          0.0, kProfileA);

        // Cover ALL FOUR optional-key entry points, not just one. Dropping the
        // resolveScaleKey() call from any single reader must fail this — sawModelSource()
        // especially, since that is the Q_INVOKABLE the Calibration tab reads, so its
        // mutation is literally "the tab reports a pool the learner is not training".
        //
        // Each assertion is two-sided: the omitted-key answer must MATCH the serving
        // scale's explicit answer and DIFFER from the saved primary's. The second half is
        // what makes it discriminating — matching alone would also pass if both pools
        // happened to be empty.
        auto* cal = m_settings.calibration();
        const QString kServing = QStringLiteral("bookoo");
        const QString kSaved = QStringLiteral("decent-wifi");

        // Match-the-serving-scale holds for every reader.
        QCOMPARE(cal->sawLearnedLagFor(kProfileA), cal->sawLearnedLagFor(kProfileA, kServing));
        QCOMPARE(cal->sawModelSource(kProfileA), cal->sawModelSource(kProfileA, kServing));
        QCOMPARE(cal->perProfileSawHistory(kProfileA).size(),
                 cal->perProfileSawHistory(kProfileA, kServing).size());
        QCOMPARE(cal->sawPendingBatch(kProfileA).size(),
                 cal->sawPendingBatch(kProfileA, kServing).size());

        // Differ-from-the-saved-primary is asserted on the DIRECT pool reads only.
        // sawLearnedLagFor/sawModelSource deliberately fall back (per-pair → global
        // bootstrap → global pool) when a pair has not graduated, and with only one
        // scale's data present those fallbacks land on the same number — so "must
        // differ" is not a property of those readers and asserting it there fails for
        // a reason that has nothing to do with key resolution. perProfileSawHistory
        // reads one pool with no fallback, which is what makes it discriminating.
        QVERIFY2(cal->perProfileSawHistory(kProfileA).size()
                     != cal->perProfileSawHistory(kProfileA, kSaved).size(),
                 "resolved key must not reach the saved primary's pool");
        QCOMPARE(cal->perProfileSawHistory(kProfileA, kSaved).size(), qsizetype(0));

        QVERIFY2(cal->sawLearnedLagFor(kProfileA) > 1.8,
                 "omitting the scale must reach the serving scale's pool");
    }

    void omittedScaleTypeFallsBackToSavedWhenNothingIsServing() {
        // No physical scale connected — the provider reports empty. The saved primary
        // is the only answer left, and it must be NORMALIZED: "Decent Scale" and
        // "decent" keying different pools is the same split under another spelling.
        m_settings.setScaleType(QStringLiteral("Decent Scale"));
        m_settings.calibration()->setServingScaleTypeProvider([]() { return QString(); });

        QCOMPARE(m_settings.calibration()->currentScaleType(), QStringLiteral("decent"));

    }

    void nonCanonicalServingScaleFallsBackToSaved() {
        // FlowScale reports "flow" and is permanently connected. Without the canonical
        // check every scale-less shot would open a "flow" pool and make sensorLag()
        // warn about an unknown type on every cycle (init()'s failOnWarning would
        // catch that here).
        m_settings.setScaleType(QStringLiteral("bookoo"));
        m_settings.calibration()->setServingScaleTypeProvider(
            []() { return QStringLiteral("flow"); });

        QCOMPARE(m_settings.calibration()->currentScaleType(), QStringLiteral("bookoo"));

    }

    void explicitScaleTypeOverridesTheServingScale() {
        // The learning path latches the key at shot start and passes it ~40 s later,
        // so a scale swapped mid-shot still trains the pool that made the prediction.
        // An explicit key must therefore beat live resolution, not be overridden by it.
        m_settings.setScaleType(QStringLiteral("decent"));
        m_settings.calibration()->setServingScaleTypeProvider(
            []() { return QStringLiteral("bookoo"); });

        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(0.6, 1.5, QStringLiteral("acaia"),
                                                          0.0, kProfileA);
        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(0.6, 1.5, QStringLiteral("acaia"),
                                                          0.0, kProfileA);

        // acaia's pool has the graduated data; bookoo (resolved) does not.
        QVERIFY2(m_settings.calibration()
                     ->perProfileSawHistory(kProfileA, QStringLiteral("acaia")).size() > 0,
                 "precondition: explicit-key learning landed in acaia's pool");
        QCOMPARE(m_settings.calibration()
                     ->perProfileSawHistory(kProfileA, QStringLiteral("bookoo")).size(),
                 qsizetype(0));

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

    // Device transfer / backup (finish-recipes-first-class): the whole SAW
    // learning state exports and re-imports losslessly, so learned per-(profile,
    // scale) lag survives a device change.
    void exportImportRoundTripsLearning() {
        commitBatch(kProfileA, 0.6, 1.5);   // graduate a per-pair lag
        commitBatch(kProfileA, 0.6, 1.5);
        const double lagBefore = m_settings.calibration()->sawLearnedLagFor(kProfileA, kScale);
        QVERIFY2(lagBefore < 0.5, "precondition: A graduated to its own lag");

        const QJsonObject exported = m_settings.calibration()->sawLearningExport();
        QVERIFY(!exported.isEmpty());

        m_settings.calibration()->resetSawLearning();
        m_settings.calibration()->sawLearningImport(exported);

        QCOMPARE(m_settings.calibration()->sawLearnedLagFor(kProfileA, kScale), lagBefore);
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

    void committedEntryIsOneShotsOwnDripAndFlow() {
        // A batch whose median-drip shot is NOT its median-flow shot. Real numbers from
        // this maintainer's device: drips 1.30/1.90/1.53 against flows 1.87/2.10/1.60.
        //
        // Per-shot lags are 0.695, 0.905 and 0.956 s, so the batch's median lag is 0.905 s
        // and it belongs to the second shot, (1.90 g, 2.10 g/s).
        //
        // Independent medians instead give drip 1.53 over flow 1.87 = 0.818 s — a lag no
        // shot in the batch had, from a pair that never happened. Four readers inherit
        // whichever pair is stored: sawLearningEntriesFor hands it to the WeightProcessor
        // snapshot that fires the stop, getExpectedDripFor smooths it, sawLearnedLagFor
        // divides it, recomputeGlobalSawBootstrap medians it.
        //
        // This fixture assumes kBatchSize == 3. If that changes the test goes red rather
        // than silently passing: a larger batch never commits and the size assertion
        // fails; a smaller one commits the wrong shot and the drip assertion fails.
        const double drips[] = {1.30, 1.90, 1.53};
        const double flows[] = {1.87, 2.10, 1.60};
        for (int i = 0; i < 3; ++i) {
            m_settings.calibration()->addSawLearningPoint(drips[i], flows[i], kScale, 0.0,
                                                          kProfileA);
        }

        const QJsonArray history =
            m_settings.calibration()->perProfileSawHistory(kProfileA, kScale);
        QCOMPARE(history.size(), qsizetype(1));
        const QJsonObject committed = history.first().toObject();
        const double drip = committed["drip"].toDouble();
        const double flow = committed["flow"].toDouble();

        // The pair describes one of the three shots, not a per-field composite.
        bool isARealShot = false;
        for (int i = 0; i < 3; ++i) {
            if (qFuzzyCompare(drip, drips[i]) && qFuzzyCompare(flow, flows[i]))
                isARealShot = true;
        }
        QVERIFY2(isARealShot, qPrintable(QStringLiteral("committed (%1, %2) is not any "
                                                        "shot in the batch")
                                             .arg(drip).arg(flow)));

        // And it is specifically the median-lag shot: 1.90 g at 2.10 g/s. The pair the old
        // code stored was 1.53/1.87 — 0.818 s against this shot's 0.905 s.
        QCOMPARE(drip, 1.90);
        QCOMPARE(flow, 2.10);
    }

    void batchWithNoUsableFlowIsDroppedNotCommitted() {
        // Every shot below the 0.5 g/s floor, so no shot has a usable lag. The batch used
        // to commit medianOf(drips) over medianOf(flows) — the composite the change above
        // exists to stop storing — with a printed lag of 0.000 s and no warning.
        //
        // ShotTimingController returns before emitting under 0.5 g/s, so the app cannot
        // produce this; sawLearningImport() writes a pending batch wholesale with no
        // validation, so a transferred device can.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("no shot had a usable flow")));
        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(0.2, 0.4, kScale, 0.0, kProfileA);

        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(),
                 qsizetype(0));
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale).size(),
                 qsizetype(0));
    }

    void dispersionGateMeasuresAgainstTheMedianLagNotTheMedianOfMedians() {
        // Every other batch fixture in this file feeds one (drip, flow) three times, which
        // makes medianOf(drips)/medianOf(flows) identically medianOf(lags) — so none of
        // them can see which reference the gate uses. This one can:
        //
        //   A 0.6 g @ 1.50 g/s -> 0.400 s
        //   B 1.0 g @ 0.90 g/s -> 1.111 s
        //   C 3.6 g @ 1.44 g/s -> 2.500 s
        //
        // Median drip 1.0 over median flow 1.44 is 0.694 s, which lies OUTSIDE the lags it
        // is being compared against; C then deviates 1.806 s and the batch is rejected,
        // discarding three shots of learning. Against the median lag of 1.111 s the worst
        // deviation is 1.389 s and the batch commits.
        const double drips[] = {0.6, 1.0, 3.6};
        const double flows[] = {1.50, 0.90, 1.44};
        for (int i = 0; i < 3; ++i) {
            m_settings.calibration()->addSawLearningPoint(drips[i], flows[i], kScale, 0.0,
                                                          kProfileA);
        }

        // init()'s QTest::failOnWarning() means the old code's rejection warning fails this
        // on its own; the assertions below say what should have happened instead.
        const QJsonArray history =
            m_settings.calibration()->perProfileSawHistory(kProfileA, kScale);
        QCOMPARE(history.size(), qsizetype(1));
        const QJsonObject committed = history.first().toObject();
        QCOMPARE(committed["drip"].toDouble(), 1.0);
        QCOMPARE(committed["flow"].toDouble(), 0.90);
    }

    void evenLagCountStillCommitsOneShotsPair() {
        // The closest-lag search exists for the case where medianOf() averages the middle
        // two and no shot owns the result. That is reachable without kBatchSize changing:
        // sawLearningImport() writes a pending batch with no size validation, so a
        // transferred device can present three entries that one more shot then completes
        // as a batch of four.
        QJsonObject batchMap;
        QJsonArray seeded;
        const double drips[] = {0.60, 1.00, 1.40};
        const double flows[] = {1.50, 1.25, 1.40};  // lags 0.400 / 0.800 / 1.000
        for (int i = 0; i < 3; ++i) {
            QJsonObject e;
            e["drip"] = drips[i]; e["flow"] = flows[i]; e["overshoot"] = 0.0;
            e["scale"] = QString(kScale); e["profile"] = QString(kProfileA);
            e["basket"] = QStringLiteral("(none)"); e["ts"] = 1000;
            seeded.append(e);
        }
        batchMap[QStringLiteral("profile_a::decent::(none)")] = seeded;
        {
            QSettings qs(Settings::testQSettingsPath(), QSettings::IniFormat);
            qs.setValue("saw/perProfileBatch",
                        QJsonDocument(batchMap).toJson(QJsonDocument::Compact));
            qs.sync();
            m_settings.calibration()->invalidateCache();
        }

        // Fourth shot completes the batch: lags 0.400 / 0.800 / 1.000 / 1.200, whose median
        // is 0.900 — a value no shot produced. The nearest are 0.800 and 1.000, tied at
        // 0.100; the search keeps the first it meets.
        m_settings.calibration()->addSawLearningPoint(1.20, 1.00, kScale, 0.0, kProfileA);

        const QJsonArray history =
            m_settings.calibration()->perProfileSawHistory(kProfileA, kScale);
        QCOMPARE(history.size(), qsizetype(1));
        const QJsonObject committed = history.first().toObject();
        QCOMPARE(committed["drip"].toDouble(), 1.00);
        QCOMPARE(committed["flow"].toDouble(), 1.25);
    }

    void twoConsecutiveBadOvershootBatchesClearCommittedHistory() {
        // medianOver is the one median this change deliberately left alone, on the grounds
        // that it gates this auto-reset. Nothing asserted that, so the grounds were
        // unverified.
        commitBatch(kProfileA, 1.0, 2.0, -7.0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(),
                 qsizetype(1));

        // Second consecutive batch with median overshoot < -6 g wipes the history and
        // leaves only the new median. The reset announces itself, and init()'s
        // failOnWarning would otherwise make that announcement the failure.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("clearing committed history")));
        commitBatch(kProfileA, 1.0, 2.0, -7.0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(),
                 qsizetype(1));

        // A third batch with a normal overshoot appends rather than replacing, which is
        // what makes the two above a reset and not just a trim.
        commitBatch(kProfileA, 1.0, 2.0, 0.0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(),
                 qsizetype(2));
    }

    // ===== Basket dimension of the key =====

    void twoBasketsOnOneProfileAndScaleLearnIndependently() {
        // The defect this whole change exists for: one basket's drip training the model
        // another basket predicts from. Two baskets, deliberately 2x apart in drip (the
        // measured Graph-vs-Decent gap), each graduated on its own three shots.
        for (int i = 0; i < 3; ++i) {
            m_settings.calibration()->addSawLearningPoint(1.35, 1.5, kScale, 0.0, kProfileA, kBasketOne);
            m_settings.calibration()->addSawLearningPoint(0.65, 1.5, kScale, 0.0, kProfileA, kBasketTwo);
        }

        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketOne).size(), 1);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketTwo).size(), 1);

        const double lagOne = m_settings.calibration()->sawLearnedLagFor(kProfileA, kScale, kBasketOne);
        const double lagTwo = m_settings.calibration()->sawLearnedLagFor(kProfileA, kScale, kBasketTwo);
        QVERIFY2(qAbs(lagOne - 0.90) < 0.02, qPrintable(QString("basket one lag %1").arg(lagOne)));
        QVERIFY2(qAbs(lagTwo - 0.433) < 0.02, qPrintable(QString("basket two lag %1").arg(lagTwo)));

        // And the prediction each basket gets is its own, not a blend.
        const double dripOne = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 1.5, kBasketOne);
        const double dripTwo = m_settings.calibration()->getExpectedDripFor(kProfileA, kScale, 1.5, kBasketTwo);
        QVERIFY2(dripOne - dripTwo > 0.5,
                 qPrintable(QString("baskets not isolated: %1 vs %2").arg(dripOne).arg(dripTwo)));
    }

    void noBasketIsItsOwnBucket() {
        commitBatch(kProfileA, 1.35, 1.5);  // no basket argument → resolves to "(none)"
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale).size(), 1);
        // A real basket must not see the no-basket bucket's history.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketOne).size(), 0);
    }

    void basketSwitchMidBatchLeavesPendingBatchWithItsOriginalBasket() {
        m_settings.calibration()->addSawLearningPoint(1.35, 1.5, kScale, 0.0, kProfileA, kBasketOne);
        m_settings.calibration()->addSawLearningPoint(1.35, 1.5, kScale, 0.0, kProfileA, kBasketOne);
        // User swaps basket with the batch one shot short.
        m_settings.calibration()->addSawLearningPoint(0.65, 1.5, kScale, 0.0, kProfileA, kBasketTwo);

        // No commit anywhere: neither batch reached three, and the switch did not tip the
        // first over by donating the second basket's shot.
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale, kBasketOne).size(), 2);
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale, kBasketTwo).size(), 1);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketOne).size(), 0);
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketTwo).size(), 0);
    }

    void basketKeyNormalizationCannotCollideWithTheNoBasketSentinel() {
        QCOMPARE(SettingsCalibration::sawBasketKey("Graph Coffee", "Stepped 58→46mm"),
                 QString("graph-coffee-stepped-58-46mm"));
        // Case and punctuation differences fold together...
        QCOMPARE(SettingsCalibration::sawBasketKey("  decent ", "18g Ridged"),
                 SettingsCalibration::sawBasketKey("DECENT", "18g_Ridged"));
        // ...and both empty, or punctuation-only, land on the sentinel rather than on an
        // empty segment that would key a bucket no reader looks in.
        QCOMPARE(SettingsCalibration::sawBasketKey("", ""), QString("(none)"));
        QCOMPARE(SettingsCalibration::sawBasketKey("()", "-"), QString("(none)"));
        // A basket literally named "(none)" normalizes to letters, so it cannot alias.
        QCOMPARE(SettingsCalibration::sawBasketKey("(none)", ""), QString("none"));
    }

    // ===== Scoped resets =====

    void resetForProfileClearsPendingBatchesToo() {
        for (int i = 0; i < 3; ++i)
            m_settings.calibration()->addSawLearningPoint(1.35, 1.5, kScale, 0.0, kProfileA, kBasketOne);
        m_settings.calibration()->addSawLearningPoint(0.65, 1.5, kScale, 0.0, kProfileA, kBasketTwo);  // pending only

        m_settings.calibration()->resetSawLearningForProfile(kProfileA, kScale);

        // Committed medians AND a part-filled batch in another basket both go: a reset that
        // left the pending entries would commit them into the "cleared" profile three shots later.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, kScale, kBasketOne).size(), 0);
        QCOMPARE(m_settings.calibration()->sawPendingBatch(kProfileA, kScale, kBasketTwo).size(), 0);
    }

    void hasSawLearningForProfileSpansBasketsAndIgnoresPrefixSiblings() {
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile(kProfileA, kScale), false);

        // A single pending entry counts: there is something to clear even pre-graduation, and
        // gating the button on graduation would hide it while a batch was accumulating.
        m_settings.calibration()->addSawLearningPoint(1.35, 1.5, kScale, 0.0, kProfileA, kBasketOne);
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile(kProfileA, kScale), true);

        // Another profile's data does not count, and a profile whose name merely PREFIXES
        // this one must not claim it — the free-form-filename trap the reset path guards.
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile(kProfileB, kScale), false);
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile("profile", kScale), false);
    }

    // ===== Buckets the 2.0.3 pre-basket seed left behind =====

    void inheritedMediansDoNotVoteInTheBootstrap() {
        // One batch of shots, copied into three baskets by the 2.0.3 seed, must not count
        // three times toward the cross-basket bootstrap median, and neither may the frozen
        // two-segment bucket it was copied from. A recompute with two real contributors
        // makes the exclusion observable.
        QJsonObject map;
        QJsonArray source; source.append(medianEntry(1.35, 1.5, "decent"));   // lag 0.90
        map[QStringLiteral("profile_a::decent")] = source;
        for (const QString& basket : {QString(kBasketOne), QString(kBasketTwo), QStringLiteral("third-basket")}) {
            QJsonObject inherited = medianEntry(1.35, 1.5, "decent");
            inherited["inherited"] = true;
            QJsonArray copy; copy.append(inherited);
            map[QStringLiteral("profile_a::decent::") + basket] = copy;
        }
        seedPerProfileHistory(map);

        // Two OTHER profiles earn real medians, which is what triggers the recompute.
        commitBatch(kProfileB, 0.60, 1.5);   // lag 0.40
        commitBatch(kProfileC, 0.75, 1.5);   // lag 0.50

        // Contributors must be the two real medians only -> median 0.45. If the inherited
        // copies voted (three of them at 0.90, plus the frozen two-segment source), the
        // median would be dragged to 0.90.
        const double bootstrap = m_settings.calibration()->globalSawBootstrapLag(kScale);
        QVERIFY2(qAbs(bootstrap - 0.45) < 0.03,
                 qPrintable(QString("inherited or frozen medians voted: bootstrap %1").arg(bootstrap)));
    }

    void aCorruptHistoryBlobIsQuarantined() {
        // The store used to reset a bad blob to "{}" and log "history lost" — destroying the
        // only copy of bytes that truncation often leaves partly salvageable.
        const QByteArray badBytes = QByteArrayLiteral(R"({"profile_a::decent": [{"drip": 1.35,)");
        {
            QSettings qs(Settings::testQSettingsPath(), QSettings::IniFormat);
            qs.setValue("saw/perProfileHistory", badBytes);
            qs.remove("saw/perProfileHistory.corrupt");
            qs.sync();
        }
        m_settings.calibration()->invalidateCache();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[SAW\]\[Learning\] Corrupt saw/perProfileHistory JSON: .* quarantined)"));
        QCOMPARE(m_settings.calibration()->allPerProfileSawHistory().size(), 0);   // triggers the read

        // The raw bytes survive for recovery, with a timestamp.
        QSettings probe(Settings::testQSettingsPath(), QSettings::IniFormat);
        QCOMPARE(probe.value("saw/perProfileHistory.corrupt").toByteArray(), badBytes);
        QVERIFY(!probe.value("saw/perProfileHistory.corruptAt").toString().isEmpty());
    }

    void resetDropsTheQuarantine() {
        {
            QSettings qs(Settings::testQSettingsPath(), QSettings::IniFormat);
            qs.setValue("saw/perProfileHistory.corrupt", QByteArrayLiteral("bad"));
            qs.setValue("saw/perProfileHistory.corruptAt", "2026-01-01T00:00:00Z");
            qs.sync();
        }
        m_settings.calibration()->resetSawLearning();

        // "Wipes EVERY saw/* key" has to include the quarantine.
        QSettings probe(Settings::testQSettingsPath(), QSettings::IniFormat);
        QVERIFY(!probe.contains("saw/perProfileHistory.corrupt"));
        QVERIFY(!probe.contains("saw/perProfileHistory.corruptAt"));
    }

    void scopedResetDoesNotTouchAnotherTransportOfTheSameScale() {
        // The reachable prefix trap, and the destructive one: "p::decent" IS a prefix of
        // "p::decent-wifi::<basket>", so a bare startsWith in the scoped reset would wipe the
        // WiFi and USB scales' learning when clearing the BLE scale. The earlier assertion
        // here used a profile-name prefix, which cannot collide ("d_flow::decent" is not a
        // prefix of "d_flow_q::decent") and so passed with the rule removed.
        QJsonObject map;
        QJsonArray bt;   bt.append(medianEntry(1.35, 1.5, "decent"));
        QJsonArray wifi; wifi.append(medianEntry(0.65, 1.5, "decent-wifi"));
        map[QStringLiteral("profile_a::decent")] = bt;                                   // legacy arm
        map[QStringLiteral("profile_a::decent-wifi::") + QString(kBasketOne)] = wifi;    // sibling transport
        seedPerProfileHistory(map);

        m_settings.calibration()->resetSawLearningForProfile(kProfileA, QStringLiteral("decent"));

        // The BLE scale's legacy bucket is gone...
        QVERIFY(!m_settings.calibration()->allPerProfileSawHistory().contains("profile_a::decent"));
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile(kProfileA, QStringLiteral("decent")), false);
        // ...and the WiFi scale is untouched.
        QCOMPARE(m_settings.calibration()->perProfileSawHistory(kProfileA, QStringLiteral("decent-wifi"),
                                                               kBasketOne).size(), 1);
        QCOMPARE(m_settings.calibration()->hasSawLearningForProfile(kProfileA, QStringLiteral("decent-wifi")), true);
    }

    // ===== Batch rejection on high deviation =====

    void batchRejectedWhenDispersionTooHigh() {
        // 2 tight entries at lag=0.4s and 1 wild outlier at lag=2.5s (N=3 batch).
        // Median lag = 0.4s, deviation of the outlier = 2.1s > 1.5s → batch rejected.
        QTest::ignoreMessage(QtWarningMsg,
            // [SAW] now carries a source tag — [SAW][Learning] — so the marker is
            // no longer immediately followed by the message.
            QRegularExpression(R"(\[SAW\]\[Learning\] batch rejected — outlier lag=\S+ deviates \S+ > \S+ from median)"));
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

};

QTEST_GUILESS_MAIN(tst_SawSettings)
#include "tst_saw_settings.moc"
