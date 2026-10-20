// Tests for CommunityAggregator::summarize (src/core/communityaggregator.{h,cpp}) —
// Bean Search Phase 2, the pure networkless summariser over community shots.
//
// The ranking is a Normal lower credible bound (mean − 1.64·sd/√n) over each
// profile family's rated shots, requiring n≥2 to rank. The headline property the
// feature promises — "a lone stranger's 100 must not outrank ten people's 80" —
// is a confidence-bound property, not a point-estimate one, so the first cases
// pin exactly that. The rest cover the cleaning/grouping/window/grinder paths.

#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include "core/communityaggregator.h"
#include "core/grinderaliases.h"

using CommunityAggregator::Params;
using CommunityAggregator::summarize;
using GrinderAliases::MicronScale;
using GrinderAliases::micronsFromLinear;
using GrinderAliases::linearFromMicrons;

namespace {

constexpr qint64 DAY = 86400;
constexpr qint64 NOW = 2000000000;

// A valid baseline shot; `over` replaces/removes keys. Set a key to QVariant() to
// make it "missing" (the cleaner then drops the row or treats it as unrated).
QVariantMap shot(const QVariantMap& over = {})
{
    QVariantMap m{
        {QStringLiteral("bean_weight"), 18.0},
        {QStringLiteral("drink_weight"), 36.0},
        {QStringLiteral("duration"), 30.0},
        {QStringLiteral("espresso_enjoyment"), 80.0},
        {QStringLiteral("profile_title"), QStringLiteral("Prof")},
        {QStringLiteral("user_id"), QStringLiteral("u")},
        {QStringLiteral("grinder_model"), QString()},
        {QStringLiteral("grinder_setting"), QString()},
    };
    for (auto it = over.cbegin(); it != over.cend(); ++it)
        m.insert(it.key(), it.value());
    return m;
}

QVariantMap profileByTitle(const QVariantMap& out, const QString& title)
{
    const QVariantList ps = out.value(QStringLiteral("profiles")).toList();
    for (const QVariant& pv : ps)
        if (pv.toMap().value(QStringLiteral("title")).toString() == title)
            return pv.toMap();
    return {};
}

} // namespace

class TstCommunityAggregator : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void empty_allKeysPresent();
    void allUnrated_shotsOnlyFallback();
    void loneHundred_excludedByNGate_tenEightiesSoleRanked();      // case 3
    void credibleBound_twoConfidentHundredsBeatTenEighties();      // two-table (1)
    void credibleBound_noisySplitLosesToTight();                  // two-table (2)
    void tieBreak_deterministic();
    void grouping_versionSuffixesMerge();
    void grouping_bareTrailingNumberNotStripped();
    void cleaning_outlierTrimming();
    void cleaning_localeAndShapeDuality();
    void minUsers_boundary();
    void window_recentFallbackAndEdges();
    void median_evenCounts();
    void grinder_matchVariantsAndThreshold();
    void grinder_compoundDial();
    void grinder_unparseableExcludedFromGrindOnly();
    void unratedShotsFeedTopFamilyMedians();
    void minEnjoymentFloor_filtersRankingNotFallback();
    void zeroRatedFamilyExcludedEvenIfMostShots();
    void topShotId_bestRatedAboveFloorWithTiebreaks();

    // DARK cross-grinder grind primitive (ADR-0020). The consumer wiring is
    // deferred until there is real micron data; these cover the pure math and
    // every nullopt guard so the mechanism is honest the day data arrives.
    void micronScale_affineAndRoundTrip();
    void micronScale_unusableReturnsNullopt();
};

// 1. Empty input → every key present, zero counts, no suggestion, no crash.
void TstCommunityAggregator::empty_allKeysPresent()
{
    const QVariantMap out = summarize({}, Params{});
    for (const char* k : {"shotCount", "userCount", "ratedShotCount", "hasSuggestion",
                          "isAllTime", "profiles", "shots"})
        QVERIFY2(out.contains(QString::fromLatin1(k)), k);
    QCOMPARE(out.value("shotCount").toInt(), 0);
    QCOMPARE(out.value("userCount").toInt(), 0);
    QCOMPARE(out.value("ratedShotCount").toInt(), 0);
    QCOMPARE(out.value("hasSuggestion").toBool(), false);
    QVERIFY(out.value("profiles").toList().isEmpty());
    QVERIFY(out.value("shots").toList().isEmpty());
}

// 2. All shots unrated → shots-only fallback, no ranking, no NaN.
void TstCommunityAggregator::allUnrated_shotsOnlyFallback()
{
    QVariantList in;
    for (int i = 0; i < 4; ++i)
        in << shot({{"espresso_enjoyment", 0.0}, {"user_id", QStringLiteral("u%1").arg(i)}});
    const QVariantMap out = summarize(in, Params{});
    QCOMPARE(out.value("shotCount").toInt(), 4);
    QCOMPARE(out.value("ratedShotCount").toInt(), 0);
    QVERIFY(out.value("profiles").toList().isEmpty());
    QCOMPARE(out.value("hasSuggestion").toBool(), false);
    QCOMPARE(out.value("shots").toList().size(), 4);
}

// 3. The motivating case: one shot rated 100 vs ten shots rated 80. The lone 100
//    is unrankable (n<2) and B is the sole ranked profile.
void TstCommunityAggregator::loneHundred_excludedByNGate_tenEightiesSoleRanked()
{
    QVariantList in;
    in << shot({{"profile_title", QStringLiteral("Alpha")}, {"espresso_enjoyment", 100.0}, {"user_id", QStringLiteral("ua")}});
    for (int i = 0; i < 10; ++i)
        in << shot({{"profile_title", QStringLiteral("Beta")}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("ub%1").arg(i)}});
    const QVariantMap out = summarize(in, Params{});
    const QVariantList ps = out.value("profiles").toList();
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Beta"));
    QCOMPARE(ps.first().toMap().value("rankScore").toDouble(), 80.0);
    QVERIFY(profileByTitle(out, QStringLiteral("Alpha")).isEmpty());
    QCOMPARE(out.value("hasSuggestion").toBool(), true); // 11 distinct users
}

// two-table (1): two confident 100s SHOULD beat ten 80s — big-n doesn't just win.
void TstCommunityAggregator::credibleBound_twoConfidentHundredsBeatTenEighties()
{
    QVariantList in;
    for (int i = 0; i < 2; ++i)
        in << shot({{"profile_title", QStringLiteral("Alpha")}, {"espresso_enjoyment", 100.0}, {"user_id", QStringLiteral("ua%1").arg(i)}});
    for (int i = 0; i < 10; ++i)
        in << shot({{"profile_title", QStringLiteral("Beta")}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("ub%1").arg(i)}});
    const QVariantList ps = summarize(in, Params{}).value("profiles").toList();
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Alpha"));
    QCOMPARE(ps.first().toMap().value("rankScore").toDouble(), 100.0);
}

// two-table (2): same mean (80) but a noisy [100,60] pair loses to a tight ten-80s.
void TstCommunityAggregator::credibleBound_noisySplitLosesToTight()
{
    QVariantList in;
    in << shot({{"profile_title", QStringLiteral("Gamma")}, {"espresso_enjoyment", 100.0}, {"user_id", QStringLiteral("ug0")}});
    in << shot({{"profile_title", QStringLiteral("Gamma")}, {"espresso_enjoyment", 60.0}, {"user_id", QStringLiteral("ug1")}});
    for (int i = 0; i < 10; ++i)
        in << shot({{"profile_title", QStringLiteral("Beta")}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("ub%1").arg(i)}});
    const QVariantMap out = summarize(in, Params{});
    const QVariantList ps = out.value("profiles").toList();
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Beta"));
    // Gamma: mean 80, sample sd sqrt(800)=28.284..., score = 80 − 1.64·sqrt(800)/sqrt(2) = 47.2
    const double gScore = profileByTitle(out, QStringLiteral("Gamma")).value("rankScore").toDouble();
    QVERIFY2(std::abs(gScore - 47.2) < 1e-4, qPrintable(QString::number(gScore)));
}

// 4. Deterministic tie-break: equal score & ratedCount → title ascending.
void TstCommunityAggregator::tieBreak_deterministic()
{
    QVariantList in;
    for (int i = 0; i < 2; ++i)
        in << shot({{"profile_title", QStringLiteral("Zebra")}, {"user_id", QStringLiteral("uz%1").arg(i)}});
    for (int i = 0; i < 2; ++i)
        in << shot({{"profile_title", QStringLiteral("Apple")}, {"user_id", QStringLiteral("ua%1").arg(i)}});
    const QVariantList ps = summarize(in, Params{}).value("profiles").toList();
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps.at(0).toMap().value("title").toString(), QStringLiteral("Apple"));
    QCOMPARE(ps.at(1).toMap().value("title").toString(), QStringLiteral("Zebra"));
}

// 5a. Version suffixes (v2 / V3 / case / whitespace) collapse to one family; the
//     display title is the modal raw title.
void TstCommunityAggregator::grouping_versionSuffixesMerge()
{
    QVariantList in;
    in << shot({{"profile_title", QStringLiteral("Extractamundo Dos v2")}, {"user_id", QStringLiteral("u1")}});
    in << shot({{"profile_title", QStringLiteral("Extractamundo Dos")}, {"user_id", QStringLiteral("u2")}});
    in << shot({{"profile_title", QStringLiteral("Extractamundo Dos")}, {"user_id", QStringLiteral("u3")}});
    in << shot({{"profile_title", QStringLiteral("extractamundo  DOS v3")}, {"user_id", QStringLiteral("u4")}});
    const QVariantMap out = summarize(in, Params{});
    const QVariantList ps = out.value("profiles").toList();
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().toMap().value("shotCount").toInt(), 4);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Extractamundo Dos")); // modal
}

// 5b. Neither a bare trailing integer ("Blooming 88"/"93") nor a v-word stem
//     ("Negev 3"/"4" — the \b-less regression) is a version marker; each stays
//     its own family.
void TstCommunityAggregator::grouping_bareTrailingNumberNotStripped()
{
    const char* titles[] = {"Blooming 88", "Blooming 93", "Negev 3", "Negev 4"};
    QVariantList in;
    for (const char* t : titles)
        for (int i = 0; i < 2; ++i)
            in << shot({{"profile_title", QString::fromLatin1(t)}, {"user_id", QStringLiteral("%1-%2").arg(QString::fromLatin1(t)).arg(i)}});
    const QVariantMap out = summarize(in, Params{});
    QCOMPARE(out.value("profiles").toList().size(), 4);
    for (const char* t : titles)
        QVERIFY2(!profileByTitle(out, QString::fromLatin1(t)).isEmpty(), t);
}

// 6. Cleaning: dose 0 / missing yield dropped; ratio bounds inclusive (1.0, 6.0
//    kept, 6.01 dropped); time bounds inclusive (10, 120 kept, 9/121 dropped);
//    enjoyment > 100 dropped as corrupt.
void TstCommunityAggregator::cleaning_outlierTrimming()
{
    QVariantList in;
    in << shot({{"user_id", QStringLiteral("u1")}});                                          // keep
    in << shot({{"bean_weight", 0.0}, {"user_id", QStringLiteral("u2")}});                     // drop: dose 0
    in << shot({{"drink_weight", QVariant()}, {"user_id", QStringLiteral("u3")}});             // drop: yield missing
    in << shot({{"bean_weight", 10.0}, {"drink_weight", 60.1}, {"user_id", QStringLiteral("u4")}}); // drop: ratio 6.01
    in << shot({{"bean_weight", 10.0}, {"drink_weight", 60.0}, {"user_id", QStringLiteral("u5")}}); // keep: ratio 6.0
    in << shot({{"bean_weight", 30.0}, {"drink_weight", 30.0}, {"user_id", QStringLiteral("u6")}}); // keep: ratio 1.0
    in << shot({{"duration", 9.0}, {"user_id", QStringLiteral("u7")}});                        // drop: time 9
    in << shot({{"duration", 121.0}, {"user_id", QStringLiteral("u8")}});                      // drop: time 121
    in << shot({{"duration", 10.0}, {"user_id", QStringLiteral("u9")}});                       // keep: time 10
    in << shot({{"duration", 120.0}, {"user_id", QStringLiteral("u10")}});                     // keep: time 120
    in << shot({{"espresso_enjoyment", 101.0}, {"user_id", QStringLiteral("u11")}});           // drop: corrupt
    QCOMPARE(summarize(in, Params{}).value("shotCount").toInt(), 5);
}

// 7. A field may be a double or free text ("18,5" with a comma); both parse.
void TstCommunityAggregator::cleaning_localeAndShapeDuality()
{
    QVariantList in;
    in << shot({{"bean_weight", 18.0}, {"drink_weight", 36.0}, {"duration", 30.0}, {"user_id", QStringLiteral("u1")}});
    in << shot({{"bean_weight", QStringLiteral("18,5")}, {"drink_weight", QStringLiteral("37")},
                {"duration", QStringLiteral("30")}, {"user_id", QStringLiteral("u2")}});
    const QVariantMap out = summarize(in, Params{});
    QCOMPARE(out.value("shotCount").toInt(), 2);
    bool sawComma = false;
    for (const QVariant& sv : out.value("shots").toList())
        if (std::abs(sv.toMap().value("doseG").toDouble() - 18.5) < 1e-9)
            sawComma = true;
    QVERIFY(sawComma);
}

// 8. The ≥3-distinct-users gate counts users, not shots.
void TstCommunityAggregator::minUsers_boundary()
{
    auto build = [](int shots, int users) {
        QVariantList in;
        for (int i = 0; i < shots; ++i)
            in << shot({{"user_id", QStringLiteral("u%1").arg(i % users)}});
        return in;
    };
    QCOMPARE(summarize(build(3, 2), Params{}).value("hasSuggestion").toBool(), false); // 3 shots, 2 users
    QCOMPARE(summarize(build(3, 3), Params{}).value("hasSuggestion").toBool(), true);  // 3 shots, 3 users
    QCOMPARE(summarize(build(10, 1), Params{}).value("hasSuggestion").toBool(), false); // 10 shots, 1 user
}

// 9. 12-month window: recent kept, older-than-window + missing start_time dropped
//    from the windowed pass; an all-old set falls back to all-time and flags it.
void TstCommunityAggregator::window_recentFallbackAndEdges()
{
    Params p;
    p.nowEpochSec = NOW;

    // In-window: 364d (×3) and exactly 365d (boundary is inclusive). Out: 366d and
    // a missing start_time.
    QVariantList edges;
    edges << shot({{"user_id", QStringLiteral("u1")}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 364 * DAY))}});
    edges << shot({{"user_id", QStringLiteral("u2")}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 364 * DAY))}});
    edges << shot({{"user_id", QStringLiteral("u3")}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 364 * DAY))}});
    edges << shot({{"user_id", QStringLiteral("u6")}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 365 * DAY))}}); // exact boundary, kept
    edges << shot({{"user_id", QStringLiteral("u4")}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 366 * DAY))}});
    edges << shot({{"user_id", QStringLiteral("u5")}}); // no start_time
    const QVariantMap wOut = summarize(edges, p);
    QCOMPARE(wOut.value("shotCount").toInt(), 4);       // 364×3 + 365 boundary; 366d + missing dropped
    QCOMPARE(wOut.value("isAllTime").toBool(), false);

    // nowEpochSec==0 disables the window entirely → nothing dropped for time, all-time.
    const QVariantMap disabled = summarize(edges, Params{});
    QCOMPARE(disabled.value("shotCount").toInt(), 6);
    QCOMPARE(disabled.value("isAllTime").toBool(), true);

    // All older than 12 months → windowed set fails the user gate → all-time fallback.
    QVariantList allOld;
    for (int i = 0; i < 3; ++i)
        allOld << shot({{"user_id", QStringLiteral("o%1").arg(i)}, {"start_time", QVariant(static_cast<qlonglong>(NOW - 400 * DAY))}});
    const QVariantMap oOut = summarize(allOld, p);
    QCOMPARE(oOut.value("shotCount").toInt(), 3);
    QCOMPARE(oOut.value("isAllTime").toBool(), true);
}

// 10. Median parity: interpolated for display stats, observed (lower-middle) for grind.
void TstCommunityAggregator::median_evenCounts()
{
    const double doses[] = {16, 18, 20, 22};
    const char* settings[] = {"10", "12", "14", "16"};
    QVariantList in;
    for (int i = 0; i < 4; ++i)
        in << shot({{"bean_weight", doses[i]}, {"drink_weight", doses[i] * 2.0}, {"user_id", QStringLiteral("u%1").arg(i)},
                    {"grinder_model", QStringLiteral("DF64")}, {"grinder_setting", QString::fromLatin1(settings[i])}});
    Params p;
    p.myGrinder = QStringLiteral("DF64");
    const QVariantMap prof = summarize(in, p).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("dose").toMap().value("median").toDouble(), 19.0); // (18+20)/2 interpolated
    QCOMPARE(prof.value("grinderMatched").toBool(), true);
    QCOMPARE(prof.value("grindSampleCount").toInt(), 4);
    QCOMPARE(prof.value("grindRepresentative").toString(), QStringLiteral("12")); // observed lower-middle
}

// 11. Grinder matching: alias/case/substring variants all match the user's grinder;
//     a different model doesn't; an empty grinder doesn't; ≥3 parseable samples
//     needed to quote a grind.
void TstCommunityAggregator::grinder_matchVariantsAndThreshold()
{
    const char* variants[] = {"df64", "DF64", "Df64 (modded)"};
    const char* sets[] = {"2.4", "2.5", "2.6"};
    QVariantList in;
    for (int i = 0; i < 3; ++i)
        in << shot({{"user_id", QStringLiteral("u%1").arg(i)}, {"grinder_model", QString::fromLatin1(variants[i])},
                    {"grinder_setting", QString::fromLatin1(sets[i])}});
    Params p;
    p.myGrinder = QStringLiteral("DF64");
    const QVariantMap prof = summarize(in, p).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("grinderMatched").toBool(), true);
    QCOMPARE(prof.value("grindSampleCount").toInt(), 3);
    QCOMPARE(prof.value("grindRepresentative").toString(), QStringLiteral("2.5")); // observed median of 2.4/2.5/2.6

    // Different model: no match, no grind quoted.
    QVariantList other;
    for (int i = 0; i < 3; ++i)
        other << shot({{"user_id", QStringLiteral("n%1").arg(i)}, {"grinder_model", QStringLiteral("Niche Zero")}, {"grinder_setting", QStringLiteral("15")}});
    const QVariantMap np = summarize(other, p).value("profiles").toList().first().toMap();
    QCOMPARE(np.value("grinderMatched").toBool(), false);
    QCOMPARE(np.value("grindSampleCount").toInt(), 0);
    QVERIFY(np.value("grindRepresentative").toString().isEmpty());

    // Empty user grinder: nothing matches.
    const QVariantMap ep = summarize(in, Params{}).value("profiles").toList().first().toMap();
    QCOMPARE(ep.value("grinderMatched").toBool(), false);

    // Only two parseable matched rows → grinderMatched true, but no grind quoted.
    QVariantList two;
    for (int i = 0; i < 2; ++i)
        two << shot({{"user_id", QStringLiteral("t%1").arg(i)}, {"grinder_model", QStringLiteral("DF64")}, {"grinder_setting", QStringLiteral("2.5")}});
    const QVariantMap tp = summarize(two, p).value("profiles").toList().first().toMap();
    QCOMPARE(tp.value("grinderMatched").toBool(), true);
    QCOMPARE(tp.value("grindSampleCount").toInt(), 2);
    QVERIFY(tp.value("grindRepresentative").toString().isEmpty());
}

// 11b. A compound dial ("1+4") parses via parseGrinderSetting (leadingDialNumber
//      would reject it) and round-trips through formatGrinderSetting.
void TstCommunityAggregator::grinder_compoundDial()
{
    QVariantList in;
    for (int i = 0; i < 3; ++i)
        in << shot({{"user_id", QStringLiteral("u%1").arg(i)}, {"grinder_model", QStringLiteral("Eureka Specialita")}, {"grinder_setting", QStringLiteral("1+4")}});
    Params p;
    p.myGrinder = QStringLiteral("specialita");
    const QVariantMap prof = summarize(in, p).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("grindSampleCount").toInt(), 3);
    QCOMPARE(prof.value("grindRepresentative").toString(), QStringLiteral("1+4"));
}

// 11c. An unparseable matched-grinder setting drops from the grind median only —
//      the shot still counts and still feeds dose/yield stats.
void TstCommunityAggregator::grinder_unparseableExcludedFromGrindOnly()
{
    QVariantList in;
    in << shot({{"user_id", QStringLiteral("u0")}, {"grinder_model", QStringLiteral("DF64")}, {"grinder_setting", QStringLiteral("fine-ish")}});
    in << shot({{"user_id", QStringLiteral("u1")}, {"grinder_model", QStringLiteral("DF64")}, {"grinder_setting", QStringLiteral("2.5")}});
    in << shot({{"user_id", QStringLiteral("u2")}, {"grinder_model", QStringLiteral("DF64")}, {"grinder_setting", QStringLiteral("2.6")}});
    Params p;
    p.myGrinder = QStringLiteral("DF64");
    const QVariantMap prof = summarize(in, p).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("shotCount").toInt(), 3);          // the unparseable row is still a shot
    QCOMPARE(prof.value("grinderMatched").toBool(), true); // and still a match
    QCOMPARE(prof.value("grindSampleCount").toInt(), 2);   // only two parseable settings
}

// 12. Unrated shots don't rank, but they DO feed the top family's display medians.
void TstCommunityAggregator::unratedShotsFeedTopFamilyMedians()
{
    QVariantList in;
    in << shot({{"bean_weight", 18.0}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("u1")}});
    in << shot({{"bean_weight", 18.0}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("u2")}});
    in << shot({{"bean_weight", 20.0}, {"espresso_enjoyment", 0.0}, {"user_id", QStringLiteral("u3")}});
    in << shot({{"bean_weight", 22.0}, {"espresso_enjoyment", 0.0}, {"user_id", QStringLiteral("u4")}});
    const QVariantMap prof = summarize(in, Params{}).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("shotCount").toInt(), 4);
    QCOMPARE(prof.value("ratedCount").toInt(), 2);
    QCOMPARE(prof.value("dose").toMap().value("median").toDouble(), 19.0); // (18+20)/2 over all 4
}

// 13. minEnjoyment floors the RANKING input, not the fallback shot list; a family
//     whose only ratings fall below the floor is unrankable.
void TstCommunityAggregator::minEnjoymentFloor_filtersRankingNotFallback()
{
    QVariantList in;
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 60.0}, {"user_id", QStringLiteral("u1")}});
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 60.0}, {"user_id", QStringLiteral("u2")}});
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 90.0}, {"user_id", QStringLiteral("u3")}});
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 90.0}, {"user_id", QStringLiteral("u4")}});
    in << shot({{"profile_title", QStringLiteral("Low")}, {"espresso_enjoyment", 60.0}, {"user_id", QStringLiteral("u5")}});
    in << shot({{"profile_title", QStringLiteral("Low")}, {"espresso_enjoyment", 60.0}, {"user_id", QStringLiteral("u6")}});
    Params p;
    p.minEnjoyment = 70;
    const QVariantMap out = summarize(in, p);
    const QVariantList ps = out.value("profiles").toList();
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Prof"));
    QCOMPARE(ps.first().toMap().value("ratedCount").toInt(), 2);     // only the two ≥70
    QCOMPARE(ps.first().toMap().value("rankScore").toDouble(), 90.0);
    QVERIFY(profileByTitle(out, QStringLiteral("Low")).isEmpty());   // below floor, unrankable
    QCOMPARE(out.value("shots").toList().size(), 6);                 // all still in the fallback list
}

// 14. A family with the most shots but zero ratings is excluded from ranking.
void TstCommunityAggregator::zeroRatedFamilyExcludedEvenIfMostShots()
{
    QVariantList in;
    for (int i = 0; i < 5; ++i)
        in << shot({{"profile_title", QStringLiteral("Big")}, {"espresso_enjoyment", 0.0}, {"user_id", QStringLiteral("b%1").arg(i)}});
    for (int i = 0; i < 2; ++i)
        in << shot({{"profile_title", QStringLiteral("Small")}, {"espresso_enjoyment", 80.0}, {"user_id", QStringLiteral("s%1").arg(i)}});
    const QVariantMap out = summarize(in, Params{});
    const QVariantList ps = out.value("profiles").toList();
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().toMap().value("title").toString(), QStringLiteral("Small"));
    QCOMPARE(out.value("shots").toList().size(), 7); // Big's shots remain in the fallback
}

// 15. The top profile exposes its family key and a representative shot id: the
//     best-rated shot at/above the floor, tie → latest start_time, then smallest id.
void TstCommunityAggregator::topShotId_bestRatedAboveFloorWithTiebreaks()
{
    QVariantList in;
    // A below-floor shot stays in the family for display but is never a candidate.
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 60.0},
                {"user_id", QStringLiteral("u0")}, {"id", QStringLiteral("low")}});
    // Two at 90 → the later start_time wins over the higher one? no — equal enjoyment,
    // later start_time wins.
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 90.0},
                {"user_id", QStringLiteral("u1")}, {"id", QStringLiteral("early")},
                {"start_time", QVariant(static_cast<qlonglong>(1000 * DAY))}});
    in << shot({{"profile_title", QStringLiteral("Prof")}, {"espresso_enjoyment", 90.0},
                {"user_id", QStringLiteral("u2")}, {"id", QStringLiteral("late")},
                {"start_time", QVariant(static_cast<qlonglong>(2000 * DAY))}});
    Params p;
    p.minEnjoyment = 70;
    const QVariantMap prof = summarize(in, p).value("profiles").toList().first().toMap();
    QCOMPARE(prof.value("family").toString(), QStringLiteral("prof"));
    QCOMPARE(prof.value("topShotId").toString(), QStringLiteral("late")); // 90 & latest

    // Final tiebreak: equal enjoyment AND equal start_time → smallest id ("aaa" < "bbb").
    QVariantList tie;
    tie << shot({{"profile_title", QStringLiteral("T")}, {"espresso_enjoyment", 88.0},
                 {"user_id", QStringLiteral("x")}, {"id", QStringLiteral("bbb")},
                 {"start_time", QVariant(static_cast<qlonglong>(500 * DAY))}});
    tie << shot({{"profile_title", QStringLiteral("T")}, {"espresso_enjoyment", 88.0},
                 {"user_id", QStringLiteral("y")}, {"id", QStringLiteral("aaa")},
                 {"start_time", QVariant(static_cast<qlonglong>(500 * DAY))}});
    const QVariantMap tp = summarize(tie, p).value("profiles").toList().first().toMap();
    QCOMPARE(tp.value("topShotId").toString(), QStringLiteral("aaa"));
}

void TstCommunityAggregator::micronScale_affineAndRoundTrip()
{
    // A populated scale: linear 1 -> 200 µm, linear 5 -> 600 µm (slope 100 µm/step).
    MicronScale s;
    s.linearA = 1.0; s.micronA = 200.0;
    s.linearB = 5.0; s.micronB = 600.0;
    s.prov = MicronScale::Provenance::Vendor;
    QVERIFY(s.populated());

    // Anchors, interpolation, and extrapolation are the one affine line.
    QCOMPARE(micronsFromLinear(s, 1.0).value(), 200.0);  // anchor A
    QCOMPARE(micronsFromLinear(s, 5.0).value(), 600.0);  // anchor B
    QCOMPARE(micronsFromLinear(s, 3.0).value(), 400.0);  // interpolated
    QCOMPARE(micronsFromLinear(s, 0.0).value(), 100.0);  // extrapolated below A
    QCOMPARE(micronsFromLinear(s, 7.0).value(), 800.0);  // extrapolated above B

    // linearFromMicrons is the exact inverse at and between the anchors.
    QCOMPARE(linearFromMicrons(s, 200.0).value(), 1.0);
    QCOMPARE(linearFromMicrons(s, 600.0).value(), 5.0);
    QCOMPARE(linearFromMicrons(s, 400.0).value(), 3.0);
    // Round-trip identity across the usable range.
    for (double lin : {0.5, 2.25, 4.0, 6.75}) {
        const double microns = micronsFromLinear(s, lin).value();
        QVERIFY(qFuzzyCompare(linearFromMicrons(s, microns).value(), lin));
    }
}

void TstCommunityAggregator::micronScale_unusableReturnsNullopt()
{
    // Default (prov None) is inert — the ship-state of every grinder today.
    MicronScale none;
    QVERIFY(!none.populated());
    QVERIFY(!micronsFromLinear(none, 3.0).has_value());
    QVERIFY(!linearFromMicrons(none, 400.0).has_value());

    // prov set but anchors not real: equal linear anchors -> infinite slope.
    MicronScale flatLinear;
    flatLinear.linearA = 2.0; flatLinear.micronA = 300.0;
    flatLinear.linearB = 2.0; flatLinear.micronB = 500.0;
    flatLinear.prov = MicronScale::Provenance::Measured;
    QVERIFY(!flatLinear.populated());
    QVERIFY(!micronsFromLinear(flatLinear, 2.0).has_value());

    // Non-positive micron reading is not a physical burr gap.
    MicronScale badMicron;
    badMicron.linearA = 1.0; badMicron.micronA = 0.0;
    badMicron.linearB = 5.0; badMicron.micronB = 600.0;
    badMicron.prov = MicronScale::Provenance::Vendor;
    QVERIFY(!badMicron.populated());

    // Populated for the forward direction, but equal micron anchors make the
    // inverse undefined — linearFromMicrons guards that division on its own.
    MicronScale flatMicron;
    flatMicron.linearA = 1.0; flatMicron.micronA = 400.0;
    flatMicron.linearB = 5.0; flatMicron.micronB = 400.0;
    flatMicron.prov = MicronScale::Provenance::Vendor;
    QVERIFY(flatMicron.populated());                           // forward is fine
    QCOMPARE(micronsFromLinear(flatMicron, 3.0).value(), 400.0);
    QVERIFY(!linearFromMicrons(flatMicron, 400.0).has_value()); // inverse refused
}

QTEST_APPLESS_MAIN(TstCommunityAggregator)

#include "tst_communityaggregator.moc"
