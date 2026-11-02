#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <cmath>

namespace WaterAmount {

inline bool isRatio(const QJsonObject& water)
{
    return water.value(QStringLiteral("amountMode")).toString() == QLatin1String("ratio");
}

inline QString definitionError(const QJsonObject& water, bool hasProfile, bool requireVessel = true)
{
    if (!water.value(QStringLiteral("hasWater")).toBool())
        return {};
    const QString amountMode = water.value(QStringLiteral("amountMode")).toString();
    if (!amountMode.isEmpty() && amountMode != QLatin1String("fixed") && amountMode != QLatin1String("ratio"))
        return QStringLiteral("Unknown water amount mode.");
    if (!isRatio(water))
        return water.contains(QStringLiteral("waterToEspressoRatio"))
            ? QStringLiteral("Choose either a fixed water amount or a water ratio.") : QString();
    if (!hasProfile)
        return QStringLiteral("Ratio water requires an espresso target yield.");
    const auto ratio = water.value(QStringLiteral("waterToEspressoRatio"));
    if (!ratio.isDouble() || !std::isfinite(ratio.toDouble()) || ratio.toDouble() <= 0)
        return QStringLiteral("Water ratio must be a positive number.");
    if (water.contains(QStringLiteral("volume")))
        return QStringLiteral("Choose either a fixed water amount or a water ratio.");
    if (water.value(QStringLiteral("mode")).toString(QStringLiteral("weight")) != QLatin1String("weight"))
        return QStringLiteral("Ratio water uses weight in grams.");
    if (requireVessel && water.value(QStringLiteral("vesselName")).toString().trimmed().isEmpty())
        return QStringLiteral("Choose a vessel for ratio water.");
    return {};
}

struct Result {
    int amount = 0;
    QString error;
    bool valid() const { return error.isEmpty() && amount > 0; }
};

inline Result resolve(const QJsonObject& water, double espressoTargetG)
{
    const QString error = definitionError(water, true, false);
    if (!error.isEmpty())
        return {0, error};
    if (!isRatio(water))
        return {water.value(QStringLiteral("volume")).toInt(), {}};
    if (!std::isfinite(espressoTargetG) || espressoTargetG <= 0)
        return {0, QStringLiteral("Set a positive espresso target yield to use ratio water.")};
    const double grams = water.value(QStringLiteral("waterToEspressoRatio")).toDouble() * espressoTargetG;
    // Bound before converting, including overflow from a finite multiplier.
    if (!std::isfinite(grams) || grams < 9.5 || grams >= 500.5)
        return {0, QStringLiteral("Added water must resolve to 10–500 g. Adjust the ratio or espresso target.")};
    return {qRound(grams), {}};
}

} // namespace WaterAmount
