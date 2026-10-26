#include "stableweightcapture.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
void setError(QString* error, const QString& value)
{
    if (error)
        *error = value;
}

// What counts as a tared reading: MachineState's own tare-landed test
// (machinestate.cpp:1078) uses the same 1 g.
constexpr double kTaredReadingG = 1.0;

} // namespace

bool StableWeightCapture::setConfig(const Config& config, QString* error)
{
    if (!std::isfinite(config.minNet) || !std::isfinite(config.maxNet) || !std::isfinite(config.tolerance)
        || !std::isfinite(config.loadThreshold) || !std::isfinite(config.rearmDelta)
        || config.minNet < 0.0 || config.maxNet < config.minNet || config.tolerance < 0.0
        || config.stableMs <= 0 || config.baselineMs <= 0 || config.loadThreshold < 0.0
        || config.rearmDelta < 0.0) {
        setError(error, QStringLiteral("Stable weight capture configuration is invalid"));
        return false;
    }
    m_config = config;
    if (error)
        error->clear();
    return true;
}

StableWeightCapture::Result StableWeightCapture::update(double rawWeight, double cupWeight,
                                                         Arming arming, qint64 nowMs)
{
    if (!std::isfinite(rawWeight) || !std::isfinite(cupWeight))
        return makeResult(false, QStringLiteral("Scale and cup weights must be finite"));

    QString timeError;
    if (!acceptTime(nowMs, &timeError))
        return makeResult(false, timeError);

    m_rawWeight = rawWeight;
    m_cupWeight = cupWeight;
    m_arming = arming;
    if (m_tarePending)
        resolveTare();
    if (m_arming == Arming::Off) {
        clearTracking();
        return makeResult();
    }

    const Evaluation evaluation = evaluate(nowMs);
    Result result = makeResult();
    result.capturedGrams = evaluation.capturedGrams;
    result.rejectedGrams = evaluation.rejectedGrams;
    result.nextWakeMs = evaluation.nextWakeMs;
    return result;
}

void StableWeightCapture::tare()
{
    if (!m_seeded) {
        clearTracking();
        return;
    }
    // Already reading ~0: the tare moves nothing.
    if (std::abs(m_rawWeight) <= kTaredReadingG)
        return;
    m_tarePending = true;
    m_preTareRaw = m_rawWeight;
}

void StableWeightCapture::resolveTare()
{
    if (std::abs(m_rawWeight - m_preTareRaw) <= kTaredReadingG)
        return;
    if (std::abs(m_rawWeight) > kTaredReadingG) {
        // Unconfirmed: re-learn as before rather than risk a shifted wrong weight.
        clearTracking();
        return;
    }
    m_tarePending = false;
    // The load is unchanged, so the settling candidate keeps its dwell too.
    m_virtualZero -= m_preTareRaw;
    m_candidate -= m_preTareRaw;
}

StableWeightCapture::Result StableWeightCapture::makeResult(bool valid, const QString& error) const
{
    Result result;
    result.valid = valid;
    result.error = error;
    result.virtualZero = m_virtualZero;
    result.seeded = m_seeded;
    result.loadPresent = m_seeded && (m_rawWeight - m_virtualZero) >= m_config.loadThreshold;
    result.netWeight = result.loadPresent
        ? m_rawWeight - m_virtualZero - m_cupWeight : 0.0;
    result.captured = m_captured;
    return result;
}

StableWeightCapture::Evaluation StableWeightCapture::evaluate(qint64 nowMs)
{
    Evaluation result;
    if (m_arming == Arming::Off)
        return result;

    if (!m_seeded) {
        if (settled(nowMs, m_config.baselineMs, &result)) {
            m_virtualZero = m_rawWeight;
            m_seeded = true;
        }
        return result;
    }

    const bool loaded = (m_rawWeight - m_virtualZero) >= m_config.loadThreshold;
    if (m_arming == Arming::Tracking) {
        // A load seen while disarmed starts its capture dwell only once armed.
        m_captured = false;
        if (loaded) {
            m_hasCandidate = false;
            return result;
        }
    }

    const double net = m_rawWeight - m_virtualZero - m_cupWeight;
    if (m_captured) {
        if (loaded && std::abs(net - m_capturedNet) <= m_config.rearmDelta) {
            return result;
        }
        m_captured = false;
        m_hasCandidate = false;
    }

    if (!loaded) {
        if (settled(nowMs, m_config.baselineMs, &result))
            m_virtualZero = m_rawWeight;
        return result;
    }

    if (settled(nowMs, m_config.stableMs, &result)) {
        if (m_cupWeight > 0.0 && net >= m_config.minNet && net <= m_config.maxNet) {
            m_captured = true;
            m_capturedNet = net;
            result.capturedGrams = net;
        } else if (m_cupWeight > 0.0 && !m_candidateRejected) {
            m_candidateRejected = true;
            result.rejectedGrams = net;
        }
    }
    return result;
}

bool StableWeightCapture::settled(qint64 nowMs, int dwellMs, Evaluation* evaluation)
{
    if (!m_hasCandidate || std::abs(m_rawWeight - m_candidate) > m_config.tolerance) {
        m_candidate = m_rawWeight;
        m_candidateSinceMs = nowMs;
        m_candidateRejected = false;
        m_hasCandidate = true;
    }

    const qint64 elapsed = nowMs - m_candidateSinceMs;
    if (elapsed >= dwellMs)
        return true;

    const qint64 maxTime = std::numeric_limits<qint64>::max();
    const qint64 deadline = m_candidateSinceMs > maxTime - dwellMs
        ? maxTime : m_candidateSinceMs + dwellMs;
    if (evaluation)
        evaluation->nextWakeMs = deadline;
    return false;
}

void StableWeightCapture::clearTracking()
{
    m_seeded = false;
    m_captured = false;
    m_capturedNet = 0.0;
    m_hasCandidate = false;
    m_candidateRejected = false;
    m_tarePending = false;
}

bool StableWeightCapture::acceptTime(qint64 nowMs, QString* error)
{
    if (nowMs < 0 || (m_hasLastNow && nowMs < m_lastNowMs)) {
        setError(error, QStringLiteral("Clock must be nonnegative and monotonic"));
        return false;
    }
    m_lastNowMs = nowMs;
    m_hasLastNow = true;
    return true;
}
