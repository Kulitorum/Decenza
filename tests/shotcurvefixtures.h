#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

#include "history/shothistorystorage.h"

// Shared synthetic shot-curve builders, extracted from tst_shotanalysis.cpp
// so other test binaries (e.g. tst_dialing_blocks.cpp) can drive
// ShotAnalysis::analyzeShot with the same known-good fixtures instead of
// hand-rolling a second, potentially-drifting copy.
namespace ShotCurveFixtures {

inline HistoryPhaseMarker phase(double time, const QString& label, int frameNumber,
                                 bool isFlowMode = false,
                                 const QString& transitionReason = QString())
{
    HistoryPhaseMarker marker;
    marker.time = time;
    marker.label = label;
    marker.frameNumber = frameNumber;
    marker.isFlowMode = isFlowMode;
    marker.transitionReason = transitionReason;
    return marker;
}

// Build a flat-value (time, value) series sampled at `rate` Hz across [t0, t1].
inline QVector<QPointF> flatSeries(double t0, double t1, double value, double rate = 10.0)
{
    QVector<QPointF> pts;
    const double dt = 1.0 / rate;
    for (double t = t0; t <= t1 + 1e-9; t += dt) pts.append(QPointF(t, value));
    return pts;
}

// Build a ramp (t0,v0) → (t1,v1) series at `rate` Hz.
inline QVector<QPointF> rampSeries(double t0, double t1, double v0, double v1,
                                    double rate = 10.0)
{
    QVector<QPointF> pts;
    const double dt = 1.0 / rate;
    const double span = t1 - t0;
    for (double t = t0; t <= t1 + 1e-9; t += dt) {
        const double alpha = span > 0 ? (t - t0) / span : 0.0;
        pts.append(QPointF(t, v0 + alpha * (v1 - v0)));
    }
    return pts;
}

// Concatenate contiguous series (assumes second starts after first ends).
inline QVector<QPointF> concat(QVector<QPointF> a, const QVector<QPointF>& b)
{
    a.reserve(a.size() + b.size());
    a.append(b);
    return a;
}

// Clean espresso shot with a configurable pressure peak; flow tracks goal
// (no grind fault), no channeling, on-target weight — isolates the
// expert-band check. peakBar sets the ramp/plateau height.
inline void bandFixture(double peakBar,
                         QList<HistoryPhaseMarker>& phases,
                         QVector<QPointF>& pressure, QVector<QPointF>& flow,
                         QVector<QPointF>& weight, QVector<QPointF>& dCdt,
                         QVector<QPointF>& pressureGoal,
                         QVector<QPointF>& flowGoal)
{
    phases = {
        phase(0.0, "preinfusion start", 0, /*isFlowMode=*/true),
        phase(8.0, "pour",              1, /*isFlowMode=*/false),
    };
    pressure = concat(rampSeries(0.0, 8.0, 1.0, peakBar),
                      flatSeries(8.1, 30.0, peakBar));
    flow = flatSeries(0.0, 30.0, 1.8);
    flowGoal = flatSeries(0.0, 30.0, 1.8);
    pressureGoal = pressure;
    dCdt = flatSeries(0.0, 30.0, 0.0);
    weight = rampSeries(0.0, 30.0, 0.0, 36.0);
}

} // namespace ShotCurveFixtures
