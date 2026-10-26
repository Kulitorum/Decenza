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

    // Off: no trustworthy scale reading, so everything learned is dropped.
    // Tracking: the empty reading is still learned but nothing is captured, so a load
    // that is already on the scale when capture is armed is measured from it rather
    // than adopted as the zero (#2058). Armed: capture.
    enum class Arming { Off, Tracking, Armed };

    StableWeightCapture() = default;

    bool setConfig(const Config& config, QString* error = nullptr);
    Config config() const { return m_config; }

    // Call on any input change and again at nextWakeMs. Invalid samples or a
    // backwards clock value are rejected without changing the current state.
    Result update(double rawWeight, double cupWeight, Arming arming, qint64 nowMs);

    // A tare moves the scale's zero, not what is on it. The first later sample that
    // reads ~0 confirms it and shifts the empty reading by the last pre-tare reading,
    // so a pitcher tared on the scale keeps its measured load (#2058). Samples still
    // carrying the pre-tare reading are waited out; any other reading leaves the tare
    // unconfirmed, and the empty reading is re-learned. Call it before the scale's
    // zero arrives.
    void tare();
    bool tarePending() const { return m_tarePending; }

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
    void resolveTare();
    bool acceptTime(qint64 nowMs, QString* error);

    Config m_config;
    double m_rawWeight = 0.0;
    double m_cupWeight = 0.0;
    Arming m_arming = Arming::Armed;
    double m_virtualZero = 0.0;
    bool m_seeded = false;
    bool m_captured = false;
    double m_capturedNet = 0.0;
    bool m_hasCandidate = false;
    double m_candidate = 0.0;
    bool m_candidateRejected = false;
    qint64 m_candidateSinceMs = 0;
    bool m_tarePending = false;
    double m_preTareRaw = 0.0;
    bool m_hasLastNow = false;
    qint64 m_lastNowMs = 0;
};
