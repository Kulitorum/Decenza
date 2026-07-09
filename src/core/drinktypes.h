#pragma once

#include <QString>
#include <QStringList>

#include <cmath>

// Drink-type helpers for the recipe wizard (add-recipe-wizard-tea).
//
// Tea profile recommendation has two data-driven signals, both defined here so
// the ranking logic and its unit tests share one source:
//
//  1. Type match: the stock Decent tea profiles encode the tea type in their
//     TITLE ("Tea portafilter/black tea", ".../Sencha", ".../Oolong 1st
//     extraction", ".../tisane"). A bag whose extracted teaType matches ranks
//     that profile at the top of the similar tier with a reason chip. This is
//     a ranking signal, not a gate — a miss degrades to temp proximity, and
//     profile search is always available.
//
//  2. Temperature proximity: for profiles with no recognizable type in the
//     title (user-imported), order by |profile temp − the bag's stated
//     brewTempC|. The profile temps themselves encode the style (Sencha 60°,
//     Japanese green 74°, white 80°, oolong 88–94°, black ~98°).
//
// Tea default temperatures (design D8): used by the wizard when the vendor
// stated nothing — sensible per-type steeping classes, never guessed per-bag.

namespace DrinkTypes {

// Does this profile title name the given tea type? Keyword table over the 15
// stock tea_portafilter titles; matching is case-insensitive substring on the
// title. "sensha" covers the stock Blue Willow Tsuyuhikari Sensha spelling.
inline bool teaProfileMatchesType(const QString& profileTitle, const QString& teaType)
{
    const QString title = profileTitle.toLower();
    const QString type = teaType.trimmed().toLower();
    if (title.isEmpty() || type.isEmpty())
        return false;

    struct Keyword { const char* type; const char* titleWord; };
    static const Keyword kKeywords[] = {
        { "black",  "black tea" },
        { "green",  "green" },      // Japanese/Chinese/Yunnan green
        { "green",  "sencha" },
        { "green",  "sensha" },
        { "oolong", "oolong" },
        { "white",  "white tea" },
        { "herbal", "tisane" },
        { "pu-erh", "pu-erh" },
        { "pu-erh", "puerh" },
        { "pu-erh", "pu erh" },
    };
    for (const Keyword& k : kKeywords) {
        if (type == QLatin1String(k.type) && title.contains(QLatin1String(k.titleWord)))
            return true;
    }
    return false;
}

// Tier-③ tea ordering: distance between the profile's temperature and the
// bag's stated brew temperature. Callers sort ascending and fall back to
// alphabetical when the bag states no temp (brewTempC <= 0 → every profile
// keys 0, preserving the caller's alphabetical pre-sort).
inline double teaTempProximity(double profileTempC, double brewTempC)
{
    if (brewTempC <= 0 || profileTempC <= 0)
        return 0;
    return std::abs(profileTempC - brewTempC);
}

// Per-type default brew temperature (Celsius) for tea bags whose vendor
// stated nothing. Steeping classes, not per-bag guesses.
inline double defaultTeaTempC(const QString& teaType)
{
    const QString type = teaType.trimmed().toLower();
    if (type == QLatin1String("green") || type == QLatin1String("white"))
        return 80;
    if (type == QLatin1String("oolong"))
        return 90;
    if (type == QLatin1String("pu-erh"))
        return 95;
    if (type == QLatin1String("herbal"))
        return 100;
    if (type == QLatin1String("black"))
        return 98;
    return 90;  // unknown type: middle of the road
}

} // namespace DrinkTypes
