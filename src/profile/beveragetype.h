#pragma once

#include <QString>

// Beverage-type mappings, free of Profile so lightweight tools (shot_eval) share them.
// Profile's static isMaintenanceBeverageType/beverageGroup/beverageBucket forward here.
namespace BeverageType {

inline QString normalized(const QString& beverageType) {
    QString type = beverageType.trimmed();
    // Legacy de1app JSON exports retain Tcl braces around single-value strings.
    if (type.size() >= 2 && type.startsWith(QLatin1Char('{')) && type.endsWith(QLatin1Char('}')))
        type = type.mid(1, type.size() - 2).trimmed();
    return type.toLower();
}

// The no-coffee maintenance tier: cleaning/descale/calibrate. Normalizes
// internally (legacy braces, trim + lowercase) so imported values match.
// Single source for the QML Shot Plan warning (via ProfileManager), the
// shot-history exclusion (maincontroller), the Visualizer upload gate, and the
// MCP upload gate. Add any new maintenance beverage_type HERE — plus
// bucket() below and the beverage-type list in
// PostShotReviewPage.qml if it should be user-visible.
inline bool isMaintenance(const QString& beverageType) {
    const QString t = normalized(beverageType);
    return t == QLatin1String("cleaning") || t == QLatin1String("descale")
        || t == QLatin1String("calibrate");
}

// A dialed ratio carries only between profiles of one group: an espresso 1:2
// cut a tea steep short (#1941) and means nothing for filter. Unknown and empty
// types are espresso, the profile JSON default. Post-stop settling also uses it:
// only an espresso puck's drip is bounded in amount.
inline QString group(const QString& beverageType) {
    const QString t = normalized(beverageType);
    if (t == QLatin1String("tea") || t == QLatin1String("tea_portafilter"))
        return QStringLiteral("tea");
    if (t == QLatin1String("filter") || t == QLatin1String("pourover"))
        return QStringLiteral("filter");
    if (isMaintenance(t))
        return QStringLiteral("maintenance");
    return QStringLiteral("espresso");
}

// The picker's Beverage chip bucket (profile-picker spec): espresso (incl.
// empty/unknown), filter, tea, maintenance. NOT group() above — that
// one leaves "manual" as espresso for the ratio rule, while the picker's
// Maintenance chip must positively include cleaning/descale/calibrate/manual.
// One mapping, used by ProfileManager's C++ predicate/facet-counter and by
// tests; QML must not carry a second copy.
inline QString bucket(const QString& beverageType) {
    const QString t = normalized(beverageType);
    if (t == QLatin1String("filter") || t == QLatin1String("pourover"))
        return QStringLiteral("filter");
    if (t == QLatin1String("tea") || t == QLatin1String("tea_portafilter"))
        return QStringLiteral("tea");
    if (t == QLatin1String("cleaning") || t == QLatin1String("descale")
        || t == QLatin1String("calibrate") || t == QLatin1String("manual"))
        return QStringLiteral("maintenance");
    return QStringLiteral("espresso");
}

}  // namespace BeverageType
