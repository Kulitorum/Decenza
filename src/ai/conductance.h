#pragma once

#include <QPointF>
#include <QVector>

// Pure-math helpers for puck-integrity signals. Extracted so ShotDataModel
// (production, per-sample) and offline tools (tools/shot_eval, batch) can
// share identical code paths — changes to the formulas automatically flow
// to both.
//
// Conductance and its derivative follow Visualizer.coffee conventions (see
// app/models/shot_chart/additional_charts.rb) so Decenza's curves line up
// with visualizer.coffee uploads. resistance() and darcyResistanceSample()
// are Decenza/de1app-native formulas (DSx2 plain resistance and the Darcy
// P/F² form respectively) with no Visualizer.coffee counterpart to match.
namespace Conductance {

// Darcy conductance C = F² / P, clamped to 19 to match the Visualizer
// convention. Returns 0 when either P or F is essentially zero.
inline double sample(double pressureBar, double flowMlS)
{
    if (flowMlS <= 0.05 || pressureBar <= 0.05) return 0.0;
    const double c = (flowMlS * flowMlS) / pressureBar;
    return c < 19.0 ? c : 19.0;
}

// Puck resistance R = P / F (DSx2 formula), clamped to 15. Returns 0 when
// flow is essentially zero — pressure alone is not gated here, matching the
// live per-sample path this mirrors.
inline double resistance(double pressureBar, double flowMlS)
{
    if (flowMlS <= 0.05) return 0.0;
    const double r = pressureBar / flowMlS;
    return r < 15.0 ? r : 15.0;
}

// Darcy resistance R = P / F² (inverse of conductance), clamped to 19.
// Returns 0 when either P or F is essentially zero.
inline double darcyResistanceSample(double pressureBar, double flowMlS)
{
    if (flowMlS <= 0.05 || pressureBar <= 0.05) return 0.0;
    const double r = pressureBar / (flowMlS * flowMlS);
    return r < 19.0 ? r : 19.0;
}

// Build a time-aligned conductance series from pressure and flow series.
// Samples are paired by index — callers must ensure the two series share the
// same time axis (true for both ShotDataModel and visualizer payloads).
// When the series differ in length, output length = min(pressure.size(),
// flow.size()).
QVector<QPointF> fromPressureFlow(const QVector<QPointF>& pressure,
                                   const QVector<QPointF>& flow);

// Compute dC/dt on a conductance series: centered-difference derivative
// scaled × 10, then 9-point Gaussian smoothing, then clamped to [-5, 19]
// (Visualizer convention). Returns empty if the input is too short.
//
// This is the single most diagnostic puck-integrity signal — it exposes
// transient channeling events invisible in pressure/flow/resistance alone.
QVector<QPointF> derivative(const QVector<QPointF>& conductance);

} // namespace Conductance
