#include "shotcomparisonreadout.h"

#include <QPointF>
#include <cmath>

namespace {
double nearestValue(const QVariantList& values, double time, bool* found = nullptr)
{
    if (found) *found = false;
    if (!std::isfinite(time) || values.isEmpty()) return -1.0;
    double closest = 0.0;
    double minDistance = 0.0;
    bool havePoint = false;
    for (const QVariant& value : values) {
        if (!value.canConvert<QPointF>()) continue;
        const QPointF point = value.toPointF();
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
        const double distance = std::abs(point.x() - time);
        if (!havePoint || distance < minDistance) {
            havePoint = true;
            closest = point.y();
            minDistance = distance;
        } else if (distance > minDistance) {
            break; // shot series are sorted by time
        }
    }
    if (havePoint && minDistance < 1.0) {
        if (found) *found = true;
        return closest;
    }
    return -1.0;
}
}

QVariantMap ShotComparisonReadout::valuesAtTime(const QVariantList& series, double time)
{
    const auto get = [&series](qsizetype index) {
        return index >= 0 && index < series.size() ? series.at(index).toList() : QVariantList{};
    };
    bool hasDcdt = false;
    const double pressure = nearestValue(get(0), time);
    const double flow = nearestValue(get(1), time);
    const double temperature = nearestValue(get(2), time);
    const double weight = nearestValue(get(3), time);
    const double weightFlow = nearestValue(get(4), time);
    const double resistance = nearestValue(get(5), time);
    const double conductance = nearestValue(get(6), time);
    const double dcdt = nearestValue(get(7), time, &hasDcdt);
    const double darcy = nearestValue(get(8), time);
    const double mixTemp = nearestValue(get(9), time);
    const double mixTempGoal = nearestValue(get(10), time);

    return {
        {QStringLiteral("hasPressure"), pressure >= 0.0},
        {QStringLiteral("hasFlow"), flow >= 0.0},
        {QStringLiteral("hasTemperature"), temperature >= 0.0},
        {QStringLiteral("hasWeight"), weight >= 0.0},
        {QStringLiteral("hasWeightFlow"), weightFlow >= 0.0},
        {QStringLiteral("hasResistance"), resistance >= 0.0},
        {QStringLiteral("hasConductance"), conductance >= 0.0},
        {QStringLiteral("hasDarcyResistance"), darcy >= 0.0},
        {QStringLiteral("hasTemperatureMix"), mixTemp >= 0.0},
        {QStringLiteral("hasTemperatureMixGoal"), mixTempGoal >= 0.0},
        {QStringLiteral("hasConductanceDerivative"), hasDcdt},
        {QStringLiteral("pressure"), pressure},
        {QStringLiteral("flow"), flow},
        {QStringLiteral("temperature"), temperature},
        {QStringLiteral("weight"), weight},
        {QStringLiteral("weightFlow"), weightFlow},
        {QStringLiteral("resistance"), resistance},
        {QStringLiteral("conductance"), conductance},
        {QStringLiteral("darcyResistance"), darcy},
        {QStringLiteral("temperatureMix"), mixTemp},
        {QStringLiteral("temperatureMixGoal"), mixTempGoal},
        {QStringLiteral("conductanceDerivative"), dcdt}
    };
}
