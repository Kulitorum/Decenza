#include "communityaggregator.h"

#include "grinderaliases.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <optional>

// Bean Search Phase 2 — see communityaggregator.h for the seam and output contract.
//
// Statistics/parity constants. These are NOT user choices (the one knob that is —
// the ranking enjoyment floor — rides in Params::minEnjoyment, a Settings prop);
// they are the algorithm's defining parameters, so they live here as named
// constexpr with their rationale rather than as magic numbers at the call site.
namespace {

// --- Cleaning bounds. Parity with Visualizer's trending_profiles_job
//     (app/jobs/trending_profiles_job.rb): leading-number parse of free-text
//     dose/yield/grind, and these validity gates. Kept identical on purpose so a
//     shot we rank matches what visualizer.coffee's own trending surface would —
//     do not "fix" the allongé/ristretto exclusions, they are the parity.
constexpr double kRatioMin = 1.0;   // yield/dose, inclusive
constexpr double kRatioMax = 6.0;   // inclusive
constexpr double kTimeMinSec = 10.0;  // inclusive
constexpr double kTimeMaxSec = 120.0; // inclusive
constexpr double kEnjoymentMax = 100.0; // >this → corrupt row, dropped; ≤0 → unrated

// --- Ranking. Normal-approximation lower credible bound on a profile family's
//     mean enjoyment: score = mean − z·sd/√n (sample sd, Bessel). This ranks by
//     "a recipe many people reliably liked", not by a high point estimate: a lone
//     stranger's 100 has no confidence and must not outrank ten people's 80. A
//     1-shot family has no sd, so n≥2 is required to rank at all (a 1-shot family
//     is structurally unrankable rather than numerically patched); it lands in the
//     shots-only fallback. Reference: Evan Miller, "Ranking Items With Star
//     Ratings". z=1.64 ≈ one-sided 95% lower bound (standard, not tuned to a test).
constexpr double kConfidenceZ = 1.64;
constexpr int kMinRatedToRank = 2;

// --- Gates / windows.
constexpr int kMinDistinctUsers = 3;              // bean-level, before any suggestion shows
constexpr int kMinGrindSamples = 3;               // parseable matched-grinder rows before grind shows
constexpr qint64 kRecentWindowSec = 365LL * 86400LL; // prefer the last 12 months (roast batches change)
constexpr int kMaxShotsPassthrough = 50;          // cap on the normalised row list crossing into QML

// A trailing EXPLICIT version marker only. Deliberately conservative: a bare
// trailing integer is NOT stripped — "Blooming 88" vs "Blooming 93" are different
// profiles (temperature-in-name is common). Over-splitting merely dilutes a
// family; over-merging silently averages two different recipes — asymmetric, so
// split. Applied to the trimmed, whitespace-collapsed, case-folded title. The \b
// before v/rev stops a mid-word match ("Negev 3" must keep its v, not strip "v 3").
const QRegularExpression& versionSuffixRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(\s*(?:\b(?:v(?:er(?:sion)?)?|rev)\.?\s*\d+(?:\.\d+)*|\(\s*\d+\s*\)|#\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Leading-number parse of a free-text / numeric field. A value may already BE a
// number (JSON number → QVariant double); only strings need parsing, and EU users
// write "18,5" — normalise comma→dot rather than trust a locale-sensitive convert.
std::optional<double> cleanNumber(const QVariant& v)
{
    if (!v.isValid() || v.isNull())
        return std::nullopt;

    switch (v.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        const double d = v.toDouble();
        return std::isfinite(d) ? std::optional<double>(d) : std::nullopt; // NaN/Inf never a valid measurement
    }
    default:
        break;
    }

    QString s = v.toString().trimmed();
    if (s.isEmpty())
        return std::nullopt;
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    // Anchored: a LEADING number only, matching Visualizer's trending_profiles_job
    // `.to_f` ("approx 18" → no number → dropped, not 18).
    static const QRegularExpression numRe(QStringLiteral(R"(^-?\d+(?:\.\d+)?)"));
    const auto m = numRe.match(s);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double d = m.captured(0).toDouble(&ok);
    return ok ? std::optional<double>(d) : std::nullopt;
}

// start_time is pinned to epoch SECONDS on the Phase-3 wire. Reject-with-drop
// (→ nullopt, treated as out-of-window) on anything else rather than guessing a
// timezone in the pure core.
std::optional<qint64> parseEpochSeconds(const QVariant& v)
{
    if (!v.isValid() || v.isNull())
        return std::nullopt;
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return static_cast<qint64>(v.toDouble());
    default:
        break;
    }
    const QString s = v.toString().trimmed();
    if (s.isEmpty())
        return std::nullopt;
    bool ok = false;
    const qlonglong e = s.toLongLong(&ok);
    return ok ? std::optional<qint64>(e) : std::nullopt;
}

QString familyKey(const QString& rawTitle)
{
    QString s = rawTitle.simplified().toLower(); // trim + collapse internal whitespace, then case-fold
    s.remove(versionSuffixRe());
    return s.trimmed();
}

// Interpolated median (mean of the two central values on an even count) — for the
// display stats, where a value between two dialed recipes is a fair summary.
double interpolatedMedian(QVector<double> xs)
{
    if (xs.isEmpty())
        return 0.0;
    std::sort(xs.begin(), xs.end());
    const qsizetype n = xs.size();
    if (n % 2)
        return xs[n / 2];
    return (xs[n / 2 - 1] + xs[n / 2]) / 2.0;
}

// Observed median (lower-middle on an even count) — for grind, so we quote a
// setting a human actually dialed rather than an interpolated half-click.
double observedMedian(QVector<double> xs)
{
    if (xs.isEmpty())
        return 0.0;
    std::sort(xs.begin(), xs.end());
    return xs[(xs.size() - 1) / 2];
}

double sampleStdDev(const QVector<double>& xs, double mean)
{
    const qsizetype n = xs.size();
    if (n < 2)
        return 0.0;
    double ss = 0.0;
    for (double x : xs)
        ss += (x - mean) * (x - mean);
    return std::sqrt(ss / static_cast<double>(n - 1));
}

QVariantMap rangeMap(const QVector<double>& xs)
{
    QVariantMap m;
    if (xs.isEmpty()) {
        m[QStringLiteral("median")] = 0.0;
        m[QStringLiteral("min")] = 0.0;
        m[QStringLiteral("max")] = 0.0;
        return m;
    }
    const auto mm = std::minmax_element(xs.cbegin(), xs.cend());
    m[QStringLiteral("median")] = interpolatedMedian(xs);
    m[QStringLiteral("min")] = *mm.first;
    m[QStringLiteral("max")] = *mm.second;
    return m;
}

// One cleaned shot. Enjoyment: has a real rating only when espresso_enjoyment
// parsed in (0, 100]; ≤0 is "unrated" (feeds display stats, never ranking).
struct CleanShot {
    QString id;
    QString userId;
    QString rawTitle;
    QString family;
    double dose = 0.0;
    double yield = 0.0;
    double ratio = 0.0;
    double timeSec = 0.0;
    bool rated = false;
    double enjoyment = 0.0;
    QString grinderModel;
    bool grinderMatched = false;
    std::optional<double> grindLinear; // parseable matched-grinder dial, linear scalar
    std::optional<qint64> startTime;
};

} // namespace

namespace CommunityAggregator {

QVariantMap summarize(const QVariantList& shotsIn, const Params& params)
{
    const int minEnjoyment = std::clamp(params.minEnjoyment, 0, 100);

    // Resolve the user's grinder once: a match requires BOTH strings to resolve to
    // the same canonical brand+model (two not-found strings must never string-equal
    // into a false match on garbage).
    const GrinderAliases::LookupResult mine = GrinderAliases::lookup(params.myGrinder);
    const GrinderAliases::GrinderEntry* myEntry =
        mine.found ? GrinderAliases::findEntry(mine.brand, mine.model) : nullptr;
    const QString grinderDisplayName =
        mine.found ? (mine.brand + QLatin1Char(' ') + mine.model) : QString();

    // --- Clean pass (applied once; the window is applied afterward). ---
    QVector<CleanShot> clean;
    clean.reserve(shotsIn.size());
    for (const QVariant& rowV : shotsIn) {
        const QVariantMap row = rowV.toMap();

        const auto dose = cleanNumber(row.value(QStringLiteral("bean_weight")));
        const auto yield = cleanNumber(row.value(QStringLiteral("drink_weight")));
        const auto timeSec = cleanNumber(row.value(QStringLiteral("duration")));
        if (!dose || !yield || *dose <= 0.0 || *yield <= 0.0)
            continue; // zero or negative weights are not a shot
        const double ratio = *yield / *dose;
        if (ratio < kRatioMin || ratio > kRatioMax)
            continue;
        if (!timeSec || *timeSec < kTimeMinSec || *timeSec > kTimeMaxSec)
            continue;

        CleanShot c;
        c.dose = *dose;
        c.yield = *yield;
        c.ratio = ratio;
        c.timeSec = *timeSec;

        if (const auto enj = cleanNumber(row.value(QStringLiteral("espresso_enjoyment")))) {
            if (*enj > kEnjoymentMax)
                continue; // corrupt
            if (*enj > 0.0) {
                c.rated = true;
                c.enjoyment = *enj;
            }
        }

        c.id = row.value(QStringLiteral("id")).toString();
        c.userId = row.value(QStringLiteral("user_id")).toString();
        c.rawTitle = row.value(QStringLiteral("profile_title")).toString();
        c.family = familyKey(c.rawTitle);
        c.grinderModel = row.value(QStringLiteral("grinder_model")).toString();
        c.startTime = parseEpochSeconds(row.value(QStringLiteral("start_time")));

        if (mine.found) {
            const GrinderAliases::LookupResult rl = GrinderAliases::lookup(c.grinderModel);
            c.grinderMatched = rl.found && rl.brand == mine.brand && rl.model == mine.model;
            if (c.grinderMatched && myEntry)
                c.grindLinear = GrinderAliases::parseGrinderSetting(
                    *myEntry, row.value(QStringLiteral("grinder_setting")).toString());
        }

        clean.push_back(std::move(c));
    }

    // --- Window: hard-filter to the last 12 months; if that fails the distinct-user
    //     gate, fall back to all-time and flag it. Honored here and nowhere else.
    //     A shot with no parseable start_time is out-of-window (dropped in the
    //     windowed pass, kept in the all-time fallback). nowEpochSec==0 (unset)
    //     disables the window entirely → all-time.
    auto distinctUsers = [](const QVector<CleanShot>& v) {
        QSet<QString> u;
        for (const auto& c : v)
            if (!c.userId.isEmpty())
                u.insert(c.userId);
        return u.size();
    };

    QVector<CleanShot> kept;
    bool isAllTime = true;
    if (params.nowEpochSec > 0) {
        const qint64 windowStart = params.nowEpochSec - kRecentWindowSec;
        QVector<CleanShot> windowed;
        for (const auto& c : clean)
            if (c.startTime && *c.startTime >= windowStart)
                windowed.push_back(c);
        if (distinctUsers(windowed) >= kMinDistinctUsers) {
            kept = std::move(windowed);
            isAllTime = false;
        } else {
            kept = clean; // all-time fallback
        }
    } else {
        kept = clean;
    }

    // --- Group by family. ---
    QHash<QString, QVector<CleanShot>> byFamily;
    for (const auto& c : kept)
        byFamily[c.family].push_back(c);

    // --- Rank each family by the lower credible bound over its rated-and-above-floor
    //     shots; families with < kMinRatedToRank such shots are not rankable. Display
    //     stats cover ALL kept shots in the family (unrated included). ---
    QVariantList profiles;
    for (auto it = byFamily.cbegin(); it != byFamily.cend(); ++it) {
        const QVector<CleanShot>& fam = it.value();
        if (it.key().isEmpty())
            continue; // untitled shots stay in the fallback list, never a nameless suggestion

        // The ranking population (rated and above the floor) also chooses the
        // family's representative shot for Phase-5 Apply (temperature + profile
        // import): highest enjoyment, tie → latest, final tie → smallest id.
        QVector<double> rankRatings;
        const CleanShot* top = nullptr;
        for (const auto& c : fam) {
            if (!(c.rated && c.enjoyment >= minEnjoyment))
                continue;
            rankRatings.push_back(c.enjoyment);
            const bool better = !top
                || c.enjoyment > top->enjoyment
                || (c.enjoyment == top->enjoyment && c.startTime.value_or(0) > top->startTime.value_or(0))
                || (c.enjoyment == top->enjoyment && c.startTime.value_or(0) == top->startTime.value_or(0) && c.id < top->id);
            if (better)
                top = &c;
        }
        if (rankRatings.size() < kMinRatedToRank)
            continue; // unrankable → its shots remain in the fallback list only

        double sum = 0.0;
        for (double r : rankRatings)
            sum += r;
        const double mean = sum / static_cast<double>(rankRatings.size());
        const double sd = sampleStdDev(rankRatings, mean);
        const double score = mean - kConfidenceZ * sd / std::sqrt(static_cast<double>(rankRatings.size()));

        QVector<double> doses, yields, ratios, times, grinds;
        QSet<QString> famUsers;
        QHash<QString, int> titleCounts;
        int matchedCount = 0; // your grinder represented, parseable or not
        for (const auto& c : fam) {
            doses.push_back(c.dose);
            yields.push_back(c.yield);
            ratios.push_back(c.ratio);
            times.push_back(c.timeSec);
            if (!c.userId.isEmpty())
                famUsers.insert(c.userId);
            titleCounts[c.rawTitle]++;
            if (c.grinderMatched) {
                ++matchedCount;
                if (c.grindLinear)
                    grinds.push_back(*c.grindLinear); // parseable settings only feed the grind median
            }
        }

        // Display title = modal raw title (ties → lexicographically smallest, for determinism).
        QString displayTitle;
        int bestCount = -1;
        for (auto t = titleCounts.cbegin(); t != titleCounts.cend(); ++t) {
            if (t.value() > bestCount || (t.value() == bestCount && t.key() < displayTitle)) {
                bestCount = t.value();
                displayTitle = t.key();
            }
        }

        QVariantMap p;
        p[QStringLiteral("title")] = displayTitle;
        p[QStringLiteral("family")] = it.key();
        p[QStringLiteral("topShotId")] = top ? top->id : QString(); // "" if the best shot carries no id
        p[QStringLiteral("shotCount")] = static_cast<int>(fam.size());
        p[QStringLiteral("userCount")] = static_cast<int>(famUsers.size());
        p[QStringLiteral("rankScore")] = score;
        p[QStringLiteral("ratingMean")] = mean;
        p[QStringLiteral("ratingSd")] = sd;
        p[QStringLiteral("ratedCount")] = static_cast<int>(rankRatings.size());
        p[QStringLiteral("dose")] = rangeMap(doses);
        p[QStringLiteral("yield")] = rangeMap(yields);
        p[QStringLiteral("ratio")] = rangeMap(ratios);
        p[QStringLiteral("timeSec")] = rangeMap(times);

        const bool grindShown = grinds.size() >= kMinGrindSamples;
        p[QStringLiteral("grinderMatched")] = matchedCount > 0; // represented, even if too few to quote a grind
        p[QStringLiteral("grindSampleCount")] = static_cast<int>(grinds.size());
        p[QStringLiteral("grinderDisplayName")] = grinderDisplayName;
        p[QStringLiteral("grindRepresentative")] =
            (grindShown && myEntry)
                ? GrinderAliases::formatGrinderSetting(*myEntry, observedMedian(grinds))
                : QString();

        profiles.push_back(p);
    }

    // Best-first: rankScore desc, then ratedCount desc, then title asc (deterministic).
    std::sort(profiles.begin(), profiles.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        const double sa = ma.value(QStringLiteral("rankScore")).toDouble();
        const double sb = mb.value(QStringLiteral("rankScore")).toDouble();
        if (sa != sb)
            return sa > sb;
        const int ca = ma.value(QStringLiteral("ratedCount")).toInt();
        const int cb = mb.value(QStringLiteral("ratedCount")).toInt();
        if (ca != cb)
            return ca > cb;
        return ma.value(QStringLiteral("title")).toString() < mb.value(QStringLiteral("title")).toString();
    });

    // --- Normalised passthrough rows (parsed doubles + family key), enjoyment desc
    //     then recency; capped so the map stays bounded crossing into QML. ---
    QVector<CleanShot> ordered = kept;
    std::sort(ordered.begin(), ordered.end(), [](const CleanShot& a, const CleanShot& b) {
        const double ea = a.rated ? a.enjoyment : -1.0;
        const double eb = b.rated ? b.enjoyment : -1.0;
        if (ea != eb)
            return ea > eb;
        return a.startTime.value_or(0) > b.startTime.value_or(0);
    });
    QVariantList shotsOut;
    for (const auto& c : ordered) {
        if (shotsOut.size() >= kMaxShotsPassthrough)
            break;
        QVariantMap s;
        s[QStringLiteral("id")] = c.id;
        s[QStringLiteral("userId")] = c.userId;
        s[QStringLiteral("profileTitle")] = c.rawTitle;
        s[QStringLiteral("family")] = c.family;
        s[QStringLiteral("doseG")] = c.dose;
        s[QStringLiteral("yieldG")] = c.yield;
        s[QStringLiteral("ratio")] = c.ratio;
        s[QStringLiteral("timeSec")] = c.timeSec;
        s[QStringLiteral("rated")] = c.rated;
        s[QStringLiteral("enjoyment")] = c.rated ? c.enjoyment : -1.0;
        s[QStringLiteral("grinderModel")] = c.grinderModel;
        s[QStringLiteral("grinderMatched")] = c.grinderMatched;
        s[QStringLiteral("startTime")] = c.startTime ? QVariant(static_cast<qlonglong>(*c.startTime)) : QVariant();
        shotsOut.push_back(s);
    }

    int ratedCount = 0;
    for (const auto& c : kept)
        if (c.rated)
            ++ratedCount;
    const int userCount = static_cast<int>(distinctUsers(kept));

    QVariantMap out;
    out[QStringLiteral("shotCount")] = static_cast<int>(kept.size());
    out[QStringLiteral("userCount")] = userCount;
    out[QStringLiteral("ratedShotCount")] = ratedCount;
    out[QStringLiteral("hasSuggestion")] = (userCount >= kMinDistinctUsers) && !profiles.isEmpty();
    out[QStringLiteral("isAllTime")] = isAllTime;
    out[QStringLiteral("profiles")] = profiles;
    out[QStringLiteral("shots")] = shotsOut;
    return out;
}

} // namespace CommunityAggregator
