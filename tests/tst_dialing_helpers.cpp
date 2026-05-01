#include <QTest>
#include "ai/dialing_helpers.h"

using namespace DialingHelpers;

class TstDialingHelpers : public QObject
{
    Q_OBJECT

private slots:
    void emptyInput_returnsNoSessions();
    void singleShot_returnsOneSessionOfOne();
    void twoAdjacentShots_inSameSession();
    void twoFarApartShots_inSeparateSessions();
    void issueExample_threeShots_twoSessions();
    void exactlyAtThreshold_groupsTogether();
    void justOverThreshold_breaksSession();
    void thresholdIsConfigurable();

    // buildBeanFreshness (openspec optimize-dialing-context-payload, task 6)
    void buildBeanFreshness_emptyRoastDate_returnsEmptyObject();
    void buildBeanFreshness_populatedRoastDate_carriesFreshnessKnownAndInstruction();
    void buildBeanFreshness_neverEmitsAnyDayCountField();

    // hoistSessionContext (openspec optimize-dialing-context-payload, task 1)
    void hoistSessionContext_emptySession_returnsEmpty();
    void hoistSessionContext_allShotsShareIdentity_contextHasAllOverridesEmpty();
    void hoistSessionContext_oneShotDiffersOnBeanBrand_onlyThatShotOverrides();
    void hoistSessionContext_singleShotSession_contextCarriesIdentity();
    void hoistSessionContext_firstShotEmptyForField_fallsBackToFirstNonEmpty();
    void hoistSessionContext_allShotsEmptyForField_contextOmitsField();

    // buildShotChangeDiff (issue #1020 — also drives changeFromPrev)
    void buildShotChangeDiff_identicalShots_emptyDiff();
    void buildShotChangeDiff_directionIsFromTo();
    void buildShotChangeDiff_zeroFieldsSkipped();
    void buildShotChangeDiff_emptyStringsSkipped();
    void buildShotChangeDiff_changeFromBestExample();

    // estimateFlowAtCutoff (issue #1021)
    void estimateFlowAtCutoff_emptySamples_returnsZero();
    void estimateFlowAtCutoff_zeroDuration_returnsZero();
    void estimateFlowAtCutoff_averagesLastTwoSeconds();
    void estimateFlowAtCutoff_skipsZeroFlowSamples();
    void estimateFlowAtCutoff_shortShot_clampsWindowToZero();
    void estimateFlowAtCutoff_customWindow();
};

void TstDialingHelpers::emptyInput_returnsNoSessions()
{
    QVERIFY(groupSessions({}).isEmpty());
}

void TstDialingHelpers::singleShot_returnsOneSessionOfOne()
{
    const auto sessions = groupSessions({1000});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
}

void TstDialingHelpers::twoAdjacentShots_inSameSession()
{
    // Two shots 7 minutes apart — well within the 60-min threshold.
    const auto sessions = groupSessions({2000, 2000 - 7 * 60});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[0][1], qsizetype(1));
}

void TstDialingHelpers::twoFarApartShots_inSeparateSessions()
{
    // Two shots 24 hours apart.
    const auto sessions = groupSessions({100000, 100000 - 24 * 3600});
    QCOMPARE(sessions.size(), qsizetype(2));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[1].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[1][0], qsizetype(1));
}

void TstDialingHelpers::issueExample_threeShots_twoSessions()
{
    // Mirrors the issue #1009 example: shots 884 (today 9:36), 883 (today 9:29),
    // 882 (yesterday 10:09). 884 and 883 are 7 min apart — same session.
    // 883 and 882 are ~23 hours apart — separate sessions.
    const qint64 t884 = 1000000;
    const qint64 t883 = t884 - 7 * 60;
    const qint64 t882 = t883 - 23 * 3600;
    const auto sessions = groupSessions({t884, t883, t882});

    QCOMPARE(sessions.size(), qsizetype(2));
    QCOMPARE(sessions[0].size(), qsizetype(2));
    QCOMPARE(sessions[1].size(), qsizetype(1));
    // First session has indices [0, 1] (884 + 883), second has [2] (882).
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[0][1], qsizetype(1));
    QCOMPARE(sessions[1][0], qsizetype(2));
}

void TstDialingHelpers::exactlyAtThreshold_groupsTogether()
{
    // Exactly at the threshold (gap == threshold) → still same session.
    const auto sessions = groupSessions({3600, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
}

void TstDialingHelpers::justOverThreshold_breaksSession()
{
    // 1 second over the threshold → distinct sessions.
    const auto sessions = groupSessions({3601, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(2));
}

void TstDialingHelpers::thresholdIsConfigurable()
{
    // With a 30-min threshold, a 45-min gap should split.
    const auto sessions = groupSessions({3600, 3600 - 45 * 60}, /*thresholdSec=*/30 * 60);
    QCOMPARE(sessions.size(), qsizetype(2));
}

// ---- buildBeanFreshness (openspec optimize-dialing-context-payload, task 6) ----
//
// The block replaces the precomputed `daysSinceRoast` + advisory note with a
// structured shape that forces the AI to ASK about storage before quoting
// age. Calendar age without storage context (frozen, thawed weekly,
// vacuum-sealed) is misleading data dressed as precise data — many users
// freeze beans, and the previous advisory note was demonstrably skimmed.

void TstDialingHelpers::buildBeanFreshness_emptyRoastDate_returnsEmptyObject()
{
    QCOMPARE(buildBeanFreshness(QString()), QJsonObject());
    QCOMPARE(buildBeanFreshness(QStringLiteral("")), QJsonObject());
}

void TstDialingHelpers::buildBeanFreshness_populatedRoastDate_carriesFreshnessKnownAndInstruction()
{
    const QJsonObject block = buildBeanFreshness(QStringLiteral("2026-04-15"));
    QCOMPARE(block["roastDate"].toString(), QStringLiteral("2026-04-15"));
    QCOMPARE(block["freshnessKnown"].toBool(), false);
    QVERIFY2(block.contains("instruction"),
             "block must carry the imperative storage instruction");
    const QString instruction = block["instruction"].toString();
    QVERIFY2(instruction.contains(QStringLiteral("ASK")),
             "instruction must include an explicit ASK directive");
    QVERIFY2(instruction.contains(QStringLiteral("storage")),
             "instruction must reference storage as the missing variable");
    QVERIFY2(instruction.contains(QStringLiteral("freeze"))
             || instruction.contains(QStringLiteral("frozen")),
             "instruction must surface the freezing pattern that breaks calendar-age reasoning");
}

void TstDialingHelpers::buildBeanFreshness_neverEmitsAnyDayCountField()
{
    // The block intentionally contains NO precomputed day count under any
    // field name. This is the load-bearing contract — adding back any
    // *days* field undoes the change. Pin it here so a future refactor
    // can't accidentally reintroduce one.
    const QJsonObject block = buildBeanFreshness(QStringLiteral("2026-04-15"));
    const QStringList forbiddenKeys = {
        QStringLiteral("daysSinceRoast"),
        QStringLiteral("calendarDaysSinceRoast"),
        QStringLiteral("effectiveAgeDays"),
        QStringLiteral("ageDays"),
        QStringLiteral("days")
    };
    for (const QString& key : forbiddenKeys) {
        QVERIFY2(!block.contains(key),
                 qPrintable(QString("beanFreshness must not contain a precomputed day-count field: %1").arg(key)));
    }
    // Also verify no key contains the substring "Day" (case-sensitive
    // since field naming is conventionally camelCase here).
    for (const QString& key : block.keys()) {
        QVERIFY2(!key.contains(QStringLiteral("Day")),
                 qPrintable(QString("unexpected day-related key in beanFreshness: %1").arg(key)));
    }
}

// ---- hoistSessionContext (openspec optimize-dialing-context-payload, task 1) ----
//
// Empirical anchor: the Northbound 80's Espresso conversation has a 4-shot
// session where every shot shares the same Niche Zero / 63mm Mazzer Kony /
// Northbound bean. The pre-change payload repeats those five fields on
// every shot in `dialInSessions[].shots[]`. After hoisting, the fields
// surface once on `session.context` and zero times on per-shot entries.

namespace {
DialingHelpers::ShotIdentity makeIdentity(const QString& gBrand,
                                              const QString& gModel,
                                              const QString& gBurrs,
                                              const QString& bBrand,
                                              const QString& bType)
{
    DialingHelpers::ShotIdentity id;
    id.grinderBrand = gBrand;
    id.grinderModel = gModel;
    id.grinderBurrs = gBurrs;
    id.beanBrand = bBrand;
    id.beanType = bType;
    return id;
}
} // namespace

void TstDialingHelpers::hoistSessionContext_emptySession_returnsEmpty()
{
    const auto out = hoistSessionContext({});
    QVERIFY(out.context.grinderBrand.isEmpty());
    QVERIFY(out.context.beanBrand.isEmpty());
    QVERIFY(out.perShotOverrides.isEmpty());
}

void TstDialingHelpers::hoistSessionContext_allShotsShareIdentity_contextHasAllOverridesEmpty()
{
    // Mirrors the Northbound 80's Espresso 4-shot iteration session.
    const auto shot = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm Mazzer Kony conical"),
        QStringLiteral("Northbound Coffee Roasters"),
        QStringLiteral("Spring Tour 2026 #2"));
    const QList<DialingHelpers::ShotIdentity> shots{shot, shot, shot, shot};

    const auto out = hoistSessionContext(shots);

    QCOMPARE(out.context.grinderBrand, QStringLiteral("Niche"));
    QCOMPARE(out.context.grinderModel, QStringLiteral("Zero"));
    QCOMPARE(out.context.grinderBurrs, QStringLiteral("63mm Mazzer Kony conical"));
    QCOMPARE(out.context.beanBrand, QStringLiteral("Northbound Coffee Roasters"));
    QCOMPARE(out.context.beanType, QStringLiteral("Spring Tour 2026 #2"));
    QCOMPARE(out.perShotOverrides.size(), shots.size());
    for (const auto& override : out.perShotOverrides) {
        QVERIFY2(override.grinderBrand.isEmpty()
                 && override.grinderModel.isEmpty()
                 && override.grinderBurrs.isEmpty()
                 && override.beanBrand.isEmpty()
                 && override.beanType.isEmpty(),
                 "all shots share identity → no per-shot overrides");
    }
}

void TstDialingHelpers::hoistSessionContext_oneShotDiffersOnBeanBrand_onlyThatShotOverrides()
{
    const auto a = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const auto b = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Prodigal"), QStringLiteral("Buenos Aires"));
    const QList<DialingHelpers::ShotIdentity> shots{a, b, a};

    const auto out = hoistSessionContext(shots);

    // Grinder fields uniform across all shots → all hoisted, no overrides.
    QCOMPARE(out.context.grinderBrand, QStringLiteral("Niche"));
    QCOMPARE(out.context.grinderBurrs, QStringLiteral("63mm conical"));
    // Bean fields differ on shot[1]; context = shots[0]'s value.
    QCOMPARE(out.context.beanBrand, QStringLiteral("Northbound"));
    QCOMPARE(out.context.beanType, QStringLiteral("Spring Tour"));

    QCOMPARE(out.perShotOverrides.size(), 3);
    // Shot[0] matches context → no override.
    QVERIFY(out.perShotOverrides[0].beanBrand.isEmpty());
    QVERIFY(out.perShotOverrides[0].beanType.isEmpty());
    QVERIFY(out.perShotOverrides[0].grinderBrand.isEmpty());
    // Shot[1] differs on bean fields → override carries them.
    QCOMPARE(out.perShotOverrides[1].beanBrand, QStringLiteral("Prodigal"));
    QCOMPARE(out.perShotOverrides[1].beanType, QStringLiteral("Buenos Aires"));
    // Shot[1]'s grinder still matches context → no grinder override.
    QVERIFY(out.perShotOverrides[1].grinderBrand.isEmpty());
    // Shot[2] matches context → no override.
    QVERIFY(out.perShotOverrides[2].beanBrand.isEmpty());
}

void TstDialingHelpers::hoistSessionContext_singleShotSession_contextCarriesIdentity()
{
    const auto only = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const auto out = hoistSessionContext({only});

    QCOMPARE(out.context.grinderBrand, QStringLiteral("Niche"));
    QCOMPARE(out.context.beanBrand, QStringLiteral("Northbound"));
    QCOMPARE(out.perShotOverrides.size(), 1);
    QVERIFY(out.perShotOverrides[0].grinderBrand.isEmpty());
    QVERIFY(out.perShotOverrides[0].beanBrand.isEmpty());
}

void TstDialingHelpers::hoistSessionContext_firstShotEmptyForField_fallsBackToFirstNonEmpty()
{
    // Shot[0] has no recorded grinderBurrs (legacy import); shots[1+] do.
    // Context should pick the first non-empty value rather than leave it
    // blank — otherwise shots[1+] would all carry burrs as overrides
    // even though they agree with each other.
    const auto a = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QString(),  // empty burrs on legacy shot
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const auto b = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const QList<DialingHelpers::ShotIdentity> shots{a, b, b};

    const auto out = hoistSessionContext(shots);

    QCOMPARE(out.context.grinderBurrs, QStringLiteral("63mm conical"));
    // Shot[0] has empty burrs while context has a value — by the spec,
    // the override carries the field iff `shot[i].field != context.field`.
    // Empty != "63mm conical", so... but the override emits via
    // `!override.field.isEmpty()`, so the empty value won't surface in
    // JSON. This is the correct behavior: legacy shots inherit the
    // session's modern burr identification rather than blanking it.
    // The override IS empty for shots[1] and [2] (they match context).
    QVERIFY(out.perShotOverrides[1].grinderBurrs.isEmpty());
    QVERIFY(out.perShotOverrides[2].grinderBurrs.isEmpty());
}

void TstDialingHelpers::hoistSessionContext_allShotsEmptyForField_contextOmitsField()
{
    // No shot has a recorded grinderBurrs — context.grinderBurrs stays
    // empty, and the JSON serializer omits the field from `context`.
    const auto a = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QString(),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const QList<DialingHelpers::ShotIdentity> shots{a, a, a};

    const auto out = hoistSessionContext(shots);

    QVERIFY2(out.context.grinderBurrs.isEmpty(),
             "no shot has burrs → context.grinderBurrs stays empty (serializer omits)");
    for (const auto& override : out.perShotOverrides) {
        QVERIFY(override.grinderBurrs.isEmpty());
    }
}

// ---- buildShotChangeDiff (issue #1020) ----
//
// Drives both changeFromPrev (within a session) and changeFromBest
// (current vs best-rated past shot). Direction is `from -> to`; pin that
// here so the AI's "what moved" envelope reads consistently.

void TstDialingHelpers::buildShotChangeDiff_identicalShots_emptyDiff()
{
    ShotDiffInputs s;
    s.grinderSetting = QStringLiteral("4.5");
    s.beanBrand = QStringLiteral("Northbound");
    s.doseWeightG = 18.0;
    s.finalWeightG = 36.0;
    s.durationSec = 30.0;
    s.enjoyment0to100 = 70;

    const QJsonObject diff = buildShotChangeDiff(s, s);
    QVERIFY2(diff.isEmpty(),
             "two shots with identical fields must produce no diff");
}

void TstDialingHelpers::buildShotChangeDiff_directionIsFromTo()
{
    // changeFromBest example from #1020 issue: best=18g/9-grind, current=20g/4.5-grind
    ShotDiffInputs best;
    best.grinderSetting = QStringLiteral("9");
    best.doseWeightG = 18.0;
    best.finalWeightG = 40.2;

    ShotDiffInputs current;
    current.grinderSetting = QStringLiteral("4.5");
    current.doseWeightG = 20.0;
    current.finalWeightG = 35.9;

    const QJsonObject diff = buildShotChangeDiff(best, current);

    QCOMPARE(diff["grinderSetting"].toString(), QStringLiteral("9 -> 4.5"));
    // Numeric format: "<from> -> <to> <unit> (<sign><delta>)"
    QCOMPARE(diff["doseG"].toString(), QStringLiteral("18.0 -> 20.0 g (+2.0)"));
    QCOMPARE(diff["yieldG"].toString(), QStringLiteral("40.2 -> 35.9 g (-4.3)"));
}

void TstDialingHelpers::buildShotChangeDiff_zeroFieldsSkipped()
{
    // When either side has 0 for a numeric field, skip the diff — there's
    // no meaningful comparison to be made (e.g. legacy shots without TDS).
    ShotDiffInputs from;
    from.doseWeightG = 18.0;
    from.finalWeightG = 0;  // missing
    from.durationSec = 30.0;

    ShotDiffInputs to;
    to.doseWeightG = 20.0;
    to.finalWeightG = 36.0;
    to.durationSec = 31.0;

    const QJsonObject diff = buildShotChangeDiff(from, to);
    QVERIFY2(!diff.contains("yieldG"),
             "a zero on either side must skip the numeric diff");
    QVERIFY(diff.contains("doseG"));
    QVERIFY(diff.contains("durationSec"));
}

void TstDialingHelpers::buildShotChangeDiff_emptyStringsSkipped()
{
    // Same rule for strings: blank on either side = no diff.
    ShotDiffInputs from;
    from.grinderSetting = QStringLiteral("4.5");
    from.beanBrand.clear();

    ShotDiffInputs to;
    to.grinderSetting = QStringLiteral("4.5");
    to.beanBrand = QStringLiteral("Prodigal");

    const QJsonObject diff = buildShotChangeDiff(from, to);
    QVERIFY2(!diff.contains("beanBrand"),
             "a blank string on either side must skip the diff");
    QVERIFY2(!diff.contains("grinderSetting"),
             "identical strings must not produce a diff entry");
}

void TstDialingHelpers::buildShotChangeDiff_changeFromBestExample()
{
    // Mirrors the issue #1020 example: best is shot 802 on Prodigal Buenos
    // Aires at grind 9 / 18g / 40.2g, current is shot 884 on Northbound at
    // grind 4.5 / 20g / 35.9g.
    ShotDiffInputs best;
    best.grinderSetting = QStringLiteral("9");
    best.beanBrand = QStringLiteral("Prodigal");
    best.doseWeightG = 18.0;
    best.finalWeightG = 40.2;

    ShotDiffInputs current;
    current.grinderSetting = QStringLiteral("4.5");
    current.beanBrand = QStringLiteral("Northbound Coffee Roasters");
    current.doseWeightG = 20.0;
    current.finalWeightG = 35.9;

    const QJsonObject diff = buildShotChangeDiff(best, current);
    QVERIFY(diff.contains("grinderSetting"));
    QCOMPARE(diff["beanBrand"].toString(),
             QStringLiteral("Prodigal -> Northbound Coffee Roasters"));
    QCOMPARE(diff["doseG"].toString(), QStringLiteral("18.0 -> 20.0 g (+2.0)"));
    QCOMPARE(diff["yieldG"].toString(), QStringLiteral("40.2 -> 35.9 g (-4.3)"));
}

// ---- estimateFlowAtCutoff (issue #1021) ----
//
// SAW prediction needs a representative flow rate at the moment of cutoff.
// We approximate by averaging the tail of the recorded flow curve.

namespace {
QVariantMap makeSample(double t, double y)
{
    QVariantMap m;
    m["x"] = t;
    m["y"] = y;
    return m;
}
} // namespace

void TstDialingHelpers::estimateFlowAtCutoff_emptySamples_returnsZero()
{
    QCOMPARE(estimateFlowAtCutoff({}, 30.0), 0.0);
}

void TstDialingHelpers::estimateFlowAtCutoff_zeroDuration_returnsZero()
{
    // No duration → no window. Defensive: legacy shots with bogus durations.
    QVariantList samples;
    samples.append(makeSample(10.0, 1.5));
    QCOMPARE(estimateFlowAtCutoff(samples, 0.0), 0.0);
}

void TstDialingHelpers::estimateFlowAtCutoff_averagesLastTwoSeconds()
{
    // Shot duration = 30s; default window = 2s → samples in [28, 30] count.
    QVariantList samples;
    samples.append(makeSample(10.0, 0.5));   // outside window
    samples.append(makeSample(20.0, 1.0));   // outside window
    samples.append(makeSample(28.0, 1.6));   // inside (boundary)
    samples.append(makeSample(29.0, 1.8));
    samples.append(makeSample(30.0, 2.0));

    const double avg = estimateFlowAtCutoff(samples, 30.0);
    // Average of 1.6, 1.8, 2.0 = 1.8
    QVERIFY(qFuzzyCompare(avg, 1.8));
}

void TstDialingHelpers::estimateFlowAtCutoff_skipsZeroFlowSamples()
{
    // y == 0 (drip detected as zero, scale rounding) shouldn't drag the avg
    // down — the SAW prediction wants the flow regime that drove pour
    // pressure, not stale zero readings between drops.
    QVariantList samples;
    samples.append(makeSample(28.0, 1.5));
    samples.append(makeSample(29.0, 0.0));   // skip
    samples.append(makeSample(30.0, 1.5));

    const double avg = estimateFlowAtCutoff(samples, 30.0);
    QVERIFY(qFuzzyCompare(avg, 1.5));
}

void TstDialingHelpers::estimateFlowAtCutoff_shortShot_clampsWindowToZero()
{
    // Shot shorter than the window — window must clamp to t >= 0 so we
    // average the whole shot rather than emit garbage.
    QVariantList samples;
    samples.append(makeSample(0.5, 1.0));
    samples.append(makeSample(1.0, 2.0));

    // duration=1.0, default window=2.0 → window starts at max(0, 1.0-2.0) = 0
    const double avg = estimateFlowAtCutoff(samples, 1.0);
    QVERIFY(qFuzzyCompare(avg, 1.5));  // (1.0 + 2.0) / 2
}

void TstDialingHelpers::estimateFlowAtCutoff_customWindow()
{
    QVariantList samples;
    samples.append(makeSample(25.0, 1.0));
    samples.append(makeSample(28.0, 2.0));
    samples.append(makeSample(30.0, 3.0));

    // 5s window → [25, 30] all three samples → (1+2+3)/3 = 2.0
    QVERIFY(qFuzzyCompare(estimateFlowAtCutoff(samples, 30.0, 5.0), 2.0));
    // 1s window → [29, 30] only the t=30 sample → 3.0
    QVERIFY(qFuzzyCompare(estimateFlowAtCutoff(samples, 30.0, 1.0), 3.0));
}

QTEST_APPLESS_MAIN(TstDialingHelpers)

#include "tst_dialing_helpers.moc"
