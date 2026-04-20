#include "conductance.h"

#include <algorithm>

namespace Conductance {

QVector<QPointF> fromPressureFlow(const QVector<QPointF>& pressure,
                                   const QVector<QPointF>& flow)
{
    const qsizetype n = std::min(pressure.size(), flow.size());
    QVector<QPointF> out;
    out.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        const double t = pressure[i].x();
        out.append(QPointF(t, sample(pressure[i].y(), flow[i].y())));
    }
    return out;
}

QVector<QPointF> derivative(const QVector<QPointF>& conductance)
{
    QVector<QPointF> out;
    const qsizetype n = conductance.size();
    if (n < 3) return out;

    // Step 1: centered difference, scaled ×10 (matches Visualizer.coffee).
    QVector<double> raw(n, 0.0);
    for (qsizetype i = 1; i < n - 1; ++i) {
        const double dt = conductance[i + 1].x() - conductance[i - 1].x();
        if (dt > 0.001) {
            const double dc = conductance[i + 1].y() - conductance[i - 1].y();
            raw[i] = (dc / dt) * 10.0;
        }
    }
    // Edge values: forward/backward difference.
    {
        double dt = conductance[1].x() - conductance[0].x();
        if (dt > 0.001)
            raw[0] = ((conductance[1].y() - conductance[0].y()) / dt) * 10.0;
        dt = conductance[n - 1].x() - conductance[n - 2].x();
        if (dt > 0.001)
            raw[n - 1] = ((conductance[n - 1].y() - conductance[n - 2].y()) / dt) * 10.0;
    }

    // Step 2: 9-point Gaussian kernel (Visualizer.coffee).
    static constexpr double GAUSSIAN[] = {
        0.048297, 0.08393, 0.124548, 0.157829, 0.170793,
        0.157829, 0.124548, 0.08393, 0.048297
    };
    static constexpr qsizetype KERNEL_HALF = 4;

    out.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        double smoothed = 0.0;
        double weightSum = 0.0;
        for (qsizetype k = -KERNEL_HALF; k <= KERNEL_HALF; ++k) {
            const qsizetype idx = i + k;
            if (idx >= 0 && idx < n) {
                const double w = GAUSSIAN[k + KERNEL_HALF];
                smoothed += raw[idx] * w;
                weightSum += w;
            }
        }
        if (weightSum > 0.0) smoothed /= weightSum;
        // Clamp to [-5, 19] per Visualizer convention.
        if (smoothed < -5.0) smoothed = -5.0;
        else if (smoothed > 19.0) smoothed = 19.0;
        out.append(QPointF(conductance[i].x(), smoothed));
    }

    return out;
}

} // namespace Conductance
