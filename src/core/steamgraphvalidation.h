#pragma once

#include <QPointF>
#include <QString>
#include <QtGlobal>
#include <QVector>

#include <cmath>

// Shared bounds and numeric rules for SteamDataModel replacement and its
// bounded JSON projection. Keep the parser and renderer contract aligned.
namespace SteamGraphValidation {
constexpr qsizetype MaximumSamples = 6000;
constexpr qsizetype MaximumGoalPoints = 16;
constexpr qint64 MaximumExactJsonInteger = 9007199254740991LL;

inline bool curve(const QVector<QPointF>& points, qsizetype maximum,
                  const QString& label, QString* error = nullptr)
{
    if (points.size() > maximum) {
        if (error)
            *error = label + QStringLiteral(" exceeds its point limit");
        return false;
    }
    double previousX = -1.0;
    for (const QPointF& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())
            || point.x() < 0.0 || point.x() < previousX) {
            if (error)
                *error = label + QStringLiteral(" contains an invalid or unordered point");
            return false;
        }
        previousX = point.x();
    }
    return true;
}

inline bool samples(double rawTime, const QVector<QPointF>& pressure,
                    const QVector<QPointF>& flow, const QVector<QPointF>& temperature,
                    QString* error = nullptr)
{
    if (!std::isfinite(rawTime) || rawTime < 0.0) {
        if (error) *error = QStringLiteral("Steam graph raw time is invalid");
        return false;
    }
    if (pressure.size() != flow.size() || pressure.size() != temperature.size()) {
        if (error) *error = QStringLiteral("Steam graph sample curves have different lengths");
        return false;
    }
    for (qsizetype i = 0; i < pressure.size(); ++i) {
        if (pressure.at(i).x() != flow.at(i).x()
            || pressure.at(i).x() != temperature.at(i).x()) {
            if (error) *error = QStringLiteral("Steam graph sample timestamps are not aligned");
            return false;
        }
    }
    if (!pressure.isEmpty() && rawTime < pressure.constLast().x()) {
        if (error) *error = QStringLiteral("Steam graph raw time precedes its last sample");
        return false;
    }
    return true;
}
} // namespace SteamGraphValidation
