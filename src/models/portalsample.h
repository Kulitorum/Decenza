#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <cmath>

struct PortalSample {
    double time;
    double ecRaw;
    double temperatureC;
    bool breakBefore;
};

namespace PortalSamples {
// About 10 Hz observed; one conservative silence policy for readout and chart gaps.
inline constexpr qint64 StaleAfterMs = 5000;
inline constexpr double StaleAfterSeconds = StaleAfterMs / 1000.0;
inline bool valid(double time, double ecRaw, double temperatureC, double previousTime = -1)
{
    return std::isfinite(time) && std::isfinite(ecRaw) && std::isfinite(temperatureC)
        && time >= 0 && time >= previousTime;
}

struct EcBounds {
    double minimum = 0;
    double maximum = 0.1;
    void add(double value) { minimum = qMin(minimum, value); maximum = qMax(maximum, value); }
};

inline QVariantList toVariant(const QVector<PortalSample>& samples)
{
    QVariantList result;
    result.reserve(samples.size());
    for (const auto& sample : samples)
        result.append(QVariantMap{{"time", sample.time}, {"ecRaw", sample.ecRaw},
            {"temperatureC", sample.temperatureC}, {"breakBefore", sample.breakBefore}});
    return result;
}

inline QVector<PortalSample> fromVariant(const QVariantList& values)
{
    QVector<PortalSample> result;
    result.reserve(values.size());
    bool gap = true;
    for (const auto& value : values) {
        const auto map = value.toMap();
        bool timeOk = false, ecOk = false, tempOk = false;
        PortalSample sample{map.value("time").toDouble(&timeOk),
            map.value("ecRaw").toDouble(&ecOk), map.value("temperatureC").toDouble(&tempOk),
            map.value("breakBefore").toBool()};
        if (!timeOk || !ecOk || !tempOk || !valid(sample.time, sample.ecRaw, sample.temperatureC,
                result.isEmpty() ? -1 : result.last().time)) {
            gap = true;
            continue;
        }
        sample.breakBefore = sample.breakBefore || gap;
        result.append(sample);
        gap = false;
    }
    return result;
}
}
