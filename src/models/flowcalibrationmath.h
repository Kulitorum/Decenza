#pragma once

#include <QPointF>
#include <QVector>
#include <QtGlobal>

#include <cmath>

// Shared replay transform for FlowCalibrationModel and the per-client Flow
// Calibration QML preview. Zero is the legacy/imported unknown-multiplier
// sentinel, not a zero flow multiplier.
namespace FlowCalibrationMath {
inline constexpr double kRecordedMultiplierEpsilon = 0.001;
inline constexpr double kMinMultiplier = 0.35;
inline constexpr double kMaxMultiplier = 3.0;

inline bool validMultiplier(double multiplier)
{
    return std::isfinite(multiplier) && multiplier >= kMinMultiplier
        && multiplier <= kMaxMultiplier;
}

inline bool validSaveRequest(double multiplier, qint64 expectedRevision, qint64 currentRevision)
{
    return expectedRevision == currentRevision && validMultiplier(multiplier);
}

inline QVector<QPointF> replay(const QVector<QPointF>& original,
                               double recordedMultiplier,
                               double proposedMultiplier)
{
    const double recorded = recordedMultiplier > kRecordedMultiplierEpsilon
        ? recordedMultiplier : 1.0;
    const double proposed = qBound(kMinMultiplier, proposedMultiplier, kMaxMultiplier);
    QVector<QPointF> result;
    result.reserve(original.size());
    for (const QPointF& point : original)
        result.append(QPointF(point.x(), proposed * point.y() / recorded));
    return result;
}
} // namespace FlowCalibrationMath
