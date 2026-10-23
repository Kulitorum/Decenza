#pragma once

#include <QList>
#include <QtGlobal>

#include <algorithm>

// Scale flow from timestamped weights. One definition for WeightProcessor (the live SAW
// stop and post-stop settling) and tools/shot_eval's settle replay. The arithmetic
// is shared; a replay from stored graph samples remains approximate because those
// samples are not the worker's raw arrivals and de-jittered timestamps.
namespace ScaleFlow {

struct Sample {
    qint64 timestamp;  // ms
    double weight;     // g
};

// The rolling buffer WeightProcessor keeps: the longest window it computes a slope over.
inline constexpr qint64 kBufferMs = 1000;

inline void append(QList<Sample>& buffer, Sample sample)
{
    buffer.append(sample);
    while (sample.timestamp - buffer.first().timestamp > kBufferMs)
        buffer.removeFirst();
}

// slopeGps() returns 0 until the samples span this fraction of the window.
// Lowered once already, 0.80 -> 0.65 (#514); fix sample timestamps, not this.
inline constexpr double kMinWindowFill = 0.65;

// Least-squares slope over the trailing `windowMs`, in g/s, floored at 0. Uses every
// sample in the window, averaging out scale quantization and BLE timing jitter.
inline double slopeGps(const QList<Sample>& samples, int windowMs)
{
    if (samples.size() < 2) return 0.0;

    const qint64 cutoff = samples.last().timestamp - windowMs;
    qsizetype startIdx = samples.size() - 1;
    while (startIdx > 0 && samples[startIdx - 1].timestamp >= cutoff)
        --startIdx;

    const qsizetype n = samples.size() - startIdx;
    if (n < 2) return 0.0;

    const double dt = (samples.last().timestamp - samples[startIdx].timestamp) / 1000.0;
    if (dt < (windowMs * kMinWindowFill / 1000.0)) return 0.0;

    const qint64 t0 = samples[startIdx].timestamp;
    double sumT = 0, sumW = 0, sumTW = 0, sumTT = 0;
    for (qsizetype i = startIdx; i < samples.size(); ++i) {
        const double t = (samples[i].timestamp - t0) / 1000.0;
        sumT += t;
        sumW += samples[i].weight;
        sumTW += t * samples[i].weight;
        sumTT += t * t;
    }
    const double denom = n * sumTT - sumT * sumT;
    const double slope = (denom > 1e-12) ? (n * sumTW - sumT * sumW) / denom : 0.0;
    return std::max(0.0, slope);
}

// A short window that covers at least three samples whatever the scale's rate: 500 ms
// at 5-10 Hz, up to 1000 ms at 2 Hz (Bookoo). Without it, sparse scales flicker the
// short flow near 0 and SAW is gated out.
inline int shortWindowMs(const QList<Sample>& samples)
{
    if (samples.size() < 3) return 500;
    const qint64 spanOf3 = samples.last().timestamp - samples[samples.size() - 3].timestamp;
    return std::clamp(static_cast<int>(spanOf3) + 50, 500, 1000);
}

}  // namespace ScaleFlow
