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

inline QString sourceRecipe()  { return QStringLiteral("recipe"); }
inline QString sourceBag()     { return QStringLiteral("bag"); }
inline QString sourceProfile() { return QStringLiteral("profile"); }

struct Yield {
    double value = 0.0;
    QString mode = YieldSpec::modeNone();
    QString source;  // sourceRecipe() | sourceBag() | sourceProfile()

    // A recipe or bag designs this yield (the profile rung is not an anchor).
    bool isStoreAnchor() const { return source == sourceRecipe() || source == sourceBag(); }
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
            return {value, mode, sourceRecipe()};
    }
    if (YieldSpec::isSet(bagMode) && bagValue > 0.0)
        return {bagValue, bagMode, sourceBag()};
    return {profileTargetG, YieldSpec::modeAbsolute(), sourceProfile()};
}

// The recipe stores a signed offset against its profile's temperature.
inline double temperatureC(const QVariantMap& activeRecipe, double profileTempC)
{
    const double offset = activeRecipe.value(QStringLiteral("tempOffsetC")).toDouble();
    if (!activeRecipe.isEmpty() && qAbs(offset) > 0.05 && profileTempC > 0)
        return profileTempC + offset;
    return profileTempC;
}

// Where Update Recipe / Update Bag writes: the store `shown` came from. When the
// profile answers, the bag if one is active, else an active (bean-less) recipe.
inline QString persistTarget(const Yield& shown, bool recipeActive, bool bagActive)
{
    if (shown.isStoreAnchor())
        return shown.source;
    if (bagActive)
        return sourceBag();
    return recipeActive ? sourceRecipe() : QString();
}

// The anchor to arm after a profile load left none: whatever the recipe or bag
// designs, except a recipe's gram yield equal to the profile's own target,
// which is not an override (the activation rule).
inline Yield anchorToRestore(const Yield& baseline, double profileTargetG)
{
    if (!baseline.isStoreAnchor())
        return {};
    if (baseline.source == sourceRecipe() && baseline.mode == YieldSpec::modeAbsolute()
        && qAbs(baseline.value - profileTargetG) <= 0.1)
        return {};
    return baseline;
}

}  // namespace BrewBaseline
