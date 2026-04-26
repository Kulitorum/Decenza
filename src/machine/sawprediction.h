#pragma once

#include <QtCore/QtMath>
#include <QtCore/QVector>
#include <QtCore/qnumeric.h>

// Shared math for SAW expected-drip prediction.
//
// Three sites compute the same Gaussian-weighted average over recent
// (drip, flow) entries: the live SAW threshold in WeightProcessor and the
// post-shot prediction used for learning-pool feedback in Settings (global
// pool + per-(profile, scale) pool). All three MUST stay in lockstep — drift
// in σ between the live threshold and the learning feedback would silently
// desync SAW behaviour. PR #870 narrowed σ from 1.5 → 0.25 ml/s; issue #873
// flagged that the WeightProcessor copy carried no σ-specific test.
//
// Centralising the constant + math here lets all three sites share one σ
// value and lets the math be unit-tested directly.

namespace SawPrediction {

// Gaussian flow-similarity σ (ml/s) used when weighting past entries by how
// close their training flow was to the current flow rate.
constexpr double kFlowSimilaritySigma = 0.25;

// Pre-computed 2σ² used in the exp(-x²/(2σ²)) kernel.
constexpr double kFlowSimilaritySigmaSq2 =
    2.0 * kFlowSimilaritySigma * kFlowSimilaritySigma;

// Floor below which the weighted sum is considered untrustworthy (all entries
// far from currentFlowRate). Caller falls back to a sensor-lag default.
constexpr double kMinTotalWeight = 0.01;

// Output clamp range (grams). Predictions outside this band are not credible
// and are pinned to the nearest edge.
constexpr double kMinDripPrediction = 0.5;
constexpr double kMaxDripPrediction = 20.0;

// Predict expected drip from past (drip, flow) pairs using a Gaussian
// flow-similarity kernel and a linear recency weight.
//
// Inputs:
//   drips, flows      Parallel vectors, recency-ordered (index 0 = newest).
//                     Sizes must match.
//   currentFlowRate   Current flow at the moment of prediction (ml/s).
//   recencyMax        Recency weight for the newest entry.
//   recencyMin        Recency weight for the oldest entry.
//
// Returns the clamped weighted-average drip in grams, or qQNaN() when the
// total weight falls below kMinTotalWeight (every entry's flow is far from
// currentFlowRate). Callers handle the NaN case by falling back to their
// own sensor-lag default — fallback chains differ between sites.
inline double weightedDripPrediction(const QVector<double>& drips,
                                     const QVector<double>& flows,
                                     double currentFlowRate,
                                     double recencyMax,
                                     double recencyMin)
{
    Q_ASSERT(drips.size() == flows.size());
    const qsizetype count = drips.size();
    if (count == 0) {
        return qQNaN();
    }

    const qsizetype denom = qMax(qsizetype{1}, count - 1);
    double weightedDripSum = 0.0;
    double totalWeight = 0.0;

    for (qsizetype i = 0; i < count; ++i) {
        const double recencyWeight =
            recencyMax - i * (recencyMax - recencyMin) / denom;
        const double flowDiff = qAbs(flows[i] - currentFlowRate);
        const double flowWeight =
            qExp(-(flowDiff * flowDiff) / kFlowSimilaritySigmaSq2);
        const double w = recencyWeight * flowWeight;
        weightedDripSum += drips[i] * w;
        totalWeight += w;
    }

    if (totalWeight < kMinTotalWeight) {
        return qQNaN();
    }
    return qBound(kMinDripPrediction,
                  weightedDripSum / totalWeight,
                  kMaxDripPrediction);
}

}  // namespace SawPrediction
