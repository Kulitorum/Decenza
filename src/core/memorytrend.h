#pragma once

#include <QVector>
#include <algorithm>
#include <optional>

struct MemorySample {
    qint64 timestampMs;
    quint64 rssBytes;
    int qobjectCount;
};

namespace MemoryTrend {
struct Growth {
    double firstMB;
    double middleMB;
    double lastMB;
    qint64 spanMs;
};

// Three rising block medians distinguish sustained growth from a single step or
// spike. Wider windows retain slow trends. This is diagnostic evidence, not a
// leak verdict. Sampling, peak accounting and the on-demand snapshot stay exact.
inline std::optional<Growth> sustainedGrowth(const QVector<MemorySample>& samples)
{
    for (const int block : {5, 30, 120}) {
        const qsizetype count = block * 3;
        if (samples.size() < count)
            continue;
        const qsizetype start = samples.size() - count;
        bool contiguous = true;
        for (qsizetype i = start; i < samples.size(); ++i) {
            if (!samples[i].rssBytes || (i > start
                && (samples[i].timestampMs <= samples[i - 1].timestampMs
                    || samples[i].timestampMs - samples[i - 1].timestampMs > 90'000))) {
                contiguous = false;
                break;
            }
        }
        if (!contiguous)
            continue;
        double medians[3];
        for (int part = 0; part < 3; ++part) {
            QVector<quint64> values;
            values.reserve(block);
            for (int j = 0; j < block; ++j)
                values.append(samples[start + part * block + j].rssBytes);
            std::sort(values.begin(), values.end());
            // Use an observed median, not an interpolated value: averaging
            // across a single step can invent a third level that never existed.
            medians[part] = values[block / 2] / (1024.0 * 1024.0);
        }
        const double growth = medians[2] - medians[0];
        // Both intervals must contribute materially; a step followed by tiny
        // jitter is not sustained growth even if the overall change is large.
        if (growth >= 5.0 && medians[1] - medians[0] >= growth / 4
                          && medians[2] - medians[1] >= growth / 4) {
            return Growth{medians[0], medians[1], medians[2],
                          samples.last().timestampMs - samples[start].timestampMs};
        }
    }
    return std::nullopt;
}
} // namespace MemoryTrend
