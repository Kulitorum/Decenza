#include <QTest>
#include "mcp/mcptools_dialing_helpers.h"

using namespace McpDialingHelpers;

class TstMcpToolsDialingHelpers : public QObject
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

    // buildCurrentBean (issue #1019)
    void buildCurrentBean_dyePopulated_noInference();
    void buildCurrentBean_grinderBlank_inferredFromShot();
    void buildCurrentBean_doseBlank_inferredFromShot();
    void buildCurrentBean_partialFallback_listsOnlyInferredFields();
    void buildCurrentBean_beanFieldsNeverInferred();
    void buildCurrentBean_bothBlank_omitsInferredMeta();
    void buildCurrentBean_dyeWins_overShotValues();
};

void TstMcpToolsDialingHelpers::emptyInput_returnsNoSessions()
{
    QVERIFY(groupSessions({}).isEmpty());
}

void TstMcpToolsDialingHelpers::singleShot_returnsOneSessionOfOne()
{
    const auto sessions = groupSessions({1000});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
}

void TstMcpToolsDialingHelpers::twoAdjacentShots_inSameSession()
{
    // Two shots 7 minutes apart — well within the 60-min threshold.
    const auto sessions = groupSessions({2000, 2000 - 7 * 60});
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[0][1], qsizetype(1));
}

void TstMcpToolsDialingHelpers::twoFarApartShots_inSeparateSessions()
{
    // Two shots 24 hours apart.
    const auto sessions = groupSessions({100000, 100000 - 24 * 3600});
    QCOMPARE(sessions.size(), qsizetype(2));
    QCOMPARE(sessions[0].size(), qsizetype(1));
    QCOMPARE(sessions[1].size(), qsizetype(1));
    QCOMPARE(sessions[0][0], qsizetype(0));
    QCOMPARE(sessions[1][0], qsizetype(1));
}

void TstMcpToolsDialingHelpers::issueExample_threeShots_twoSessions()
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

void TstMcpToolsDialingHelpers::exactlyAtThreshold_groupsTogether()
{
    // Exactly at the threshold (gap == threshold) → still same session.
    const auto sessions = groupSessions({3600, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(1));
    QCOMPARE(sessions[0].size(), qsizetype(2));
}

void TstMcpToolsDialingHelpers::justOverThreshold_breaksSession()
{
    // 1 second over the threshold → distinct sessions.
    const auto sessions = groupSessions({3601, 0}, /*thresholdSec=*/3600);
    QCOMPARE(sessions.size(), qsizetype(2));
}

void TstMcpToolsDialingHelpers::thresholdIsConfigurable()
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

void TstMcpToolsDialingHelpers::buildBeanFreshness_emptyRoastDate_returnsEmptyObject()
{
    QCOMPARE(buildBeanFreshness(QString()), QJsonObject());
    QCOMPARE(buildBeanFreshness(QStringLiteral("")), QJsonObject());
}

void TstMcpToolsDialingHelpers::buildBeanFreshness_populatedRoastDate_carriesFreshnessKnownAndInstruction()
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

void TstMcpToolsDialingHelpers::buildBeanFreshness_neverEmitsAnyDayCountField()
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
McpDialingHelpers::ShotIdentity makeIdentity(const QString& gBrand,
                                              const QString& gModel,
                                              const QString& gBurrs,
                                              const QString& bBrand,
                                              const QString& bType)
{
    McpDialingHelpers::ShotIdentity id;
    id.grinderBrand = gBrand;
    id.grinderModel = gModel;
    id.grinderBurrs = gBurrs;
    id.beanBrand = bBrand;
    id.beanType = bType;
    return id;
}
} // namespace

void TstMcpToolsDialingHelpers::hoistSessionContext_emptySession_returnsEmpty()
{
    const auto out = hoistSessionContext({});
    QVERIFY(out.context.grinderBrand.isEmpty());
    QVERIFY(out.context.beanBrand.isEmpty());
    QVERIFY(out.perShotOverrides.isEmpty());
}

void TstMcpToolsDialingHelpers::hoistSessionContext_allShotsShareIdentity_contextHasAllOverridesEmpty()
{
    // Mirrors the Northbound 80's Espresso 4-shot iteration session.
    const auto shot = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm Mazzer Kony conical"),
        QStringLiteral("Northbound Coffee Roasters"),
        QStringLiteral("Spring Tour 2026 #2"));
    const QList<McpDialingHelpers::ShotIdentity> shots{shot, shot, shot, shot};

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

void TstMcpToolsDialingHelpers::hoistSessionContext_oneShotDiffersOnBeanBrand_onlyThatShotOverrides()
{
    const auto a = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const auto b = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QStringLiteral("63mm conical"),
        QStringLiteral("Prodigal"), QStringLiteral("Buenos Aires"));
    const QList<McpDialingHelpers::ShotIdentity> shots{a, b, a};

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

void TstMcpToolsDialingHelpers::hoistSessionContext_singleShotSession_contextCarriesIdentity()
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

void TstMcpToolsDialingHelpers::hoistSessionContext_firstShotEmptyForField_fallsBackToFirstNonEmpty()
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
    const QList<McpDialingHelpers::ShotIdentity> shots{a, b, b};

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

void TstMcpToolsDialingHelpers::hoistSessionContext_allShotsEmptyForField_contextOmitsField()
{
    // No shot has a recorded grinderBurrs — context.grinderBurrs stays
    // empty, and the JSON serializer omits the field from `context`.
    const auto a = makeIdentity(
        QStringLiteral("Niche"), QStringLiteral("Zero"),
        QString(),
        QStringLiteral("Northbound"), QStringLiteral("Spring Tour"));
    const QList<McpDialingHelpers::ShotIdentity> shots{a, a, a};

    const auto out = hoistSessionContext(shots);

    QVERIFY2(out.context.grinderBurrs.isEmpty(),
             "no shot has burrs → context.grinderBurrs stays empty (serializer omits)");
    for (const auto& override : out.perShotOverrides) {
        QVERIFY(override.grinderBurrs.isEmpty());
    }
}

// ---- buildCurrentBean (issue #1019) ----
//
// The DYE block represents what's currently in the grinder/hopper. When
// grinder/dose fields are blank but the resolved shot has them populated,
// fall back to the shot's values and tag them as inferred so the AI knows
// to confirm before recommending a change. Bean fields (brand/type/
// roastLevel) are *never* inferred — those rotate per hopper. roastDate
// is intentionally absent from the helper output; it lives in
// `currentBean.beanFreshness` (composed by the caller) so the freshness
// surface stays in one place.

namespace {
CurrentBeanInputs sampleInputs()
{
    CurrentBeanInputs in;
    in.dyeBeanBrand = QStringLiteral("Northbound");
    in.dyeBeanType = QStringLiteral("Single Origin");
    in.dyeRoastLevel = QStringLiteral("Light");
    in.dyeGrinderBrand = QStringLiteral("Niche");
    in.dyeGrinderModel = QStringLiteral("Zero");
    in.dyeGrinderBurrs = QStringLiteral("63mm Mazzer Kony conical");
    in.dyeGrinderSetting = QStringLiteral("4.5");
    in.dyeDoseWeightG = 18.0;
    in.fallbackGrinderBrand = QStringLiteral("Eureka");
    in.fallbackGrinderModel = QStringLiteral("Mignon");
    in.fallbackGrinderBurrs = QStringLiteral("55mm flat");
    in.fallbackGrinderSetting = QStringLiteral("12");
    in.fallbackDoseWeightG = 20.0;
    in.fallbackShotId = 884;
    return in;
}
} // namespace

void TstMcpToolsDialingHelpers::buildCurrentBean_dyePopulated_noInference()
{
    const QJsonObject bean = buildCurrentBean(sampleInputs());

    QCOMPARE(bean["grinderBrand"].toString(), QStringLiteral("Niche"));
    QCOMPARE(bean["grinderModel"].toString(), QStringLiteral("Zero"));
    QCOMPARE(bean["grinderSetting"].toString(), QStringLiteral("4.5"));
    QCOMPARE(bean["doseWeightG"].toDouble(), 18.0);
    QVERIFY2(!bean.contains("inferredFromShotId"),
             "DYE fully populated must not surface inferredFromShotId");
    QVERIFY2(!bean.contains("inferredFields"),
             "DYE fully populated must not surface inferredFields");
}

void TstMcpToolsDialingHelpers::buildCurrentBean_grinderBlank_inferredFromShot()
{
    CurrentBeanInputs in = sampleInputs();
    in.dyeGrinderBrand.clear();
    in.dyeGrinderModel.clear();
    in.dyeGrinderBurrs.clear();
    in.dyeGrinderSetting.clear();

    const QJsonObject bean = buildCurrentBean(in);

    QCOMPARE(bean["grinderBrand"].toString(), QStringLiteral("Eureka"));
    QCOMPARE(bean["grinderModel"].toString(), QStringLiteral("Mignon"));
    QCOMPARE(bean["grinderBurrs"].toString(), QStringLiteral("55mm flat"));
    QCOMPARE(bean["grinderSetting"].toString(), QStringLiteral("12"));
    QCOMPARE(bean["inferredFromShotId"].toInteger(), qint64(884));

    const QJsonArray inferred = bean["inferredFields"].toArray();
    QCOMPARE(inferred.size(), 4);
    QVERIFY(inferred.contains(QStringLiteral("grinderBrand")));
    QVERIFY(inferred.contains(QStringLiteral("grinderModel")));
    QVERIFY(inferred.contains(QStringLiteral("grinderBurrs")));
    QVERIFY(inferred.contains(QStringLiteral("grinderSetting")));
    QVERIFY2(bean.contains("inferredNote"),
             "inferred fields must come with an explanatory note");
}

void TstMcpToolsDialingHelpers::buildCurrentBean_doseBlank_inferredFromShot()
{
    CurrentBeanInputs in = sampleInputs();
    in.dyeDoseWeightG = 0;

    const QJsonObject bean = buildCurrentBean(in);

    QCOMPARE(bean["doseWeightG"].toDouble(), 20.0);
    const QJsonArray inferred = bean["inferredFields"].toArray();
    QCOMPARE(inferred.size(), 1);
    QVERIFY(inferred.contains(QStringLiteral("doseWeightG")));
}

void TstMcpToolsDialingHelpers::buildCurrentBean_partialFallback_listsOnlyInferredFields()
{
    CurrentBeanInputs in = sampleInputs();
    in.dyeGrinderSetting.clear();   // only this one is blank

    const QJsonObject bean = buildCurrentBean(in);

    const QJsonArray inferred = bean["inferredFields"].toArray();
    QCOMPARE(inferred.size(), 1);
    QCOMPARE(inferred[0].toString(), QStringLiteral("grinderSetting"));
    // Other grinder fields stayed on DYE values
    QCOMPARE(bean["grinderBrand"].toString(), QStringLiteral("Niche"));
}

void TstMcpToolsDialingHelpers::buildCurrentBean_beanFieldsNeverInferred()
{
    // Bean brand/type/roastLevel rotate per hopper — falling back to a
    // stale shot would mislead the AI. Even when DYE is fully empty,
    // these stay blank rather than inheriting the shot's bean. roastDate
    // is owned by `buildBeanFreshness` (composed by the caller) and is
    // intentionally absent from the helper's output.
    CurrentBeanInputs in = sampleInputs();
    in.dyeBeanBrand.clear();
    in.dyeBeanType.clear();
    in.dyeRoastLevel.clear();

    const QJsonObject bean = buildCurrentBean(in);

    QVERIFY2(bean["brand"].toString().isEmpty(),
             "bean brand must never be inferred from shot");
    QVERIFY2(bean["type"].toString().isEmpty(),
             "bean type must never be inferred from shot");
    QVERIFY2(bean["roastLevel"].toString().isEmpty(),
             "roast level must never be inferred from shot");
    QVERIFY2(!bean.contains("roastDate"),
             "roastDate must not appear at currentBean top level — owned by beanFreshness");
    // No grinder/dose blanks → no inferredFields surfaced.
    QVERIFY(!bean.contains("inferredFields"));
}

void TstMcpToolsDialingHelpers::buildCurrentBean_bothBlank_omitsInferredMeta()
{
    // Both DYE and shot grinder are blank — no inference possible.
    CurrentBeanInputs in;
    in.fallbackShotId = 884;
    const QJsonObject bean = buildCurrentBean(in);

    QVERIFY2(!bean.contains("inferredFromShotId"),
             "no fallback data → no inferred meta");
    QVERIFY2(!bean.contains("inferredFields"),
             "no fallback data → no inferred meta");
}

void TstMcpToolsDialingHelpers::buildCurrentBean_dyeWins_overShotValues()
{
    // Sanity: when DYE is populated and the shot has different values, DYE
    // wins. This pins the precedence direction — production code never
    // overrides user-entered DYE with shot data.
    CurrentBeanInputs in = sampleInputs();
    // DYE says Niche, shot says Eureka — Niche must surface.
    QCOMPARE(in.dyeGrinderBrand, QStringLiteral("Niche"));
    QCOMPARE(in.fallbackGrinderBrand, QStringLiteral("Eureka"));

    const QJsonObject bean = buildCurrentBean(in);
    QCOMPARE(bean["grinderBrand"].toString(), QStringLiteral("Niche"));
    QVERIFY(!bean.contains("inferredFields"));
}

QTEST_APPLESS_MAIN(TstMcpToolsDialingHelpers)

#include "tst_mcptools_dialing_helpers.moc"
