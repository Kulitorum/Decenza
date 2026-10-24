#pragma once

#include <QString>

#include <optional>

// Deterministic stable-weight capture engine. The caller supplies a monotonic
// millisecond clock and schedules evaluation at Result::nextWakeMs.
class StableWeightCapture final
{
public:
    struct Config {
        double minNet = 5.0;
        double maxNet = 45.0;
        double tolerance = 0.5;
        int stableMs = 2500;
        int baselineMs = 1000;
        double loadThreshold = 3.0;
        double rearmDelta = 6.0;
    };

    struct Result {
        bool valid = true;
        QString error;
        double virtualZero = 0.0;
        bool seeded = false;
        bool loadPresent = false;
        double netWeight = 0.0;
        bool captured = false;
        std::optional<double> capturedGrams;
        std::optional<double> rejectedGrams;
        std::optional<qint64> nextWakeMs;
    };

    StableWeightCapture() = default;

    bool setConfig(const Config& config, QString* error = nullptr);
    Config config() const { return m_config; }

    // Call on any input change and again at nextWakeMs. Invalid samples or a
    // backwards clock value are rejected without changing the current state.
    Result update(double rawWeight, double cupWeight, bool active, qint64 nowMs);

    // Mirrors a tare/reset: force baseline re-adoption while retaining the last
    // virtualZero value for diagnostics, and immediately evaluate current inputs.
    Result reset(qint64 nowMs);

private:
    struct Evaluation {
        std::optional<double> capturedGrams;
        std::optional<double> rejectedGrams;
        std::optional<qint64> nextWakeMs;
    };

    Result makeResult(bool valid = true, const QString& error = {}) const;
    Evaluation evaluate(qint64 nowMs);
    bool settled(qint64 nowMs, int dwellMs, Evaluation* evaluation);
    void clearTracking();
    bool acceptTime(qint64 nowMs, QString* error);

    Config m_config;
    double m_rawWeight = 0.0;
    double m_cupWeight = 0.0;
    bool m_active = true;
    double m_virtualZero = 0.0;
    bool m_seeded = false;
    bool m_captured = false;
    double m_capturedNet = 0.0;
    bool m_hasCandidate = false;
    double m_candidate = 0.0;
    bool m_candidateRejected = false;
    qint64 m_candidateSinceMs = 0;
    bool m_hasLastNow = false;
    qint64 m_lastNowMs = 0;
};
