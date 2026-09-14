#pragma once

#include "yieldspec.h"

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

// The brew baseline Brew Settings measures overrides against: the active recipe,
// else the active bag, else the profile. One definition for MainController (and
// through it Brew Settings) and the MCP tools. Header-only so the MCP tool tests
// can use it without linking MainController.
namespace BrewBaseline {

struct Yield {
    double value = 0.0;
    QString mode = YieldSpec::modeNone();
    QString source;  // "recipe" | "bag" | "profile"
};

// Value and mode come from ONE rung: pairing a recipe's "ratio" with a bag's
// 40 g would target 40 x the dose.
inline Yield resolveYield(const QVariantMap& activeRecipe, const QString& bagMode,
                          double bagValue, double profileTargetG)
{
    if (!activeRecipe.isEmpty()) {
        const QString mode = YieldSpec::normalizedMode(
            activeRecipe.value(QStringLiteral("yieldMode")).toString());
        const double value = activeRecipe.value(QStringLiteral("yieldValue")).toDouble();
        if (YieldSpec::isSet(mode) && value > 0.0)
            return {value, mode, QStringLiteral("recipe")};
    }
    if (YieldSpec::isSet(bagMode) && bagValue > 0.0)
        return {bagValue, bagMode, QStringLiteral("bag")};
    return {profileTargetG, YieldSpec::modeAbsolute(), QStringLiteral("profile")};
}

// The recipe stores a signed offset against its profile's temperature.
inline double temperatureC(const QVariantMap& activeRecipe, double profileTempC)
{
    const double offset = activeRecipe.value(QStringLiteral("tempOffsetC")).toDouble();
    if (!activeRecipe.isEmpty() && qAbs(offset) > 0.05 && profileTempC > 0)
        return profileTempC + offset;
    return profileTempC;
}

// Where Update Recipe / Update Bag writes a yield: the store being shown. A
// recipe that designs no yield has fallen through to its bag; a bean-less one
// has nothing beneath it. Empty = no store (the override still applies).
inline QString persistTarget(const QVariantMap& activeRecipe, bool recipeActive, bool bagActive)
{
    if (recipeActive) {
        if (YieldSpec::isSet(YieldSpec::normalizedMode(
                activeRecipe.value(QStringLiteral("yieldMode")).toString())))
            return QStringLiteral("recipe");
        return bagActive ? QStringLiteral("bag") : QStringLiteral("recipe");
    }
    return bagActive ? QStringLiteral("bag") : QString();
}

}  // namespace BrewBaseline
