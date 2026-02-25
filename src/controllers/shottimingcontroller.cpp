#include "shottimingcontroller.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"
#include "../core/settings.h"
#include "../machine/machinestate.h"
#include <QDebug>

ShotTimingController::ShotTimingController(DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_device(device)
{
    // Display timer - updates UI at 20Hz for smooth timer display
    m_displayTimer.setInterval(50);
    connect(&m_displayTimer, &QTimer::timeout, this, &ShotTimingController::updateDisplayTimer);

    // SAW learning settling timer - waits for weight to stabilize after shot ends
    // Interval set by startSettlingTimer() when settling begins (currently 10s max)
    m_settlingTimer.setSingleShot(true);
    connect(&m_settlingTimer, &QTimer::timeout, this, &ShotTimingController::onSettlingComplete);
}

double ShotTimingController::shotTime() const
{
    // Show 0 during preheating, start counting when first extraction frame arrives
    if (!m_extractionStarted) {
        return 0.0;
    }
    // Calculate time from wall clock during shot OR during settling (for drip phase)
    if ((m_shotActive || m_settlingTimer.isActive()) && m_displayTimeBase > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase;
        return elapsed / 1000.0;
    }
    return m_currentTime;
}

void ShotTimingController::setScale(ScaleDevice* scale)
{
    // Signal connections to scale are managed externally in main.cpp
    m_scale = scale;
}

void ShotTimingController::setSettings(Settings* settings)
{
    m_settings = settings;
}

void ShotTimingController::setMachineState(MachineState* machineState)
{
    m_machineState = machineState;
}

void ShotTimingController::setTargetWeight(double weight)
{
    m_targetWeight = weight;
}

void ShotTimingController::setCurrentProfile(const Profile* profile)
{
    m_currentProfile = profile;
}

void ShotTimingController::startShot()
{
    // Cancel settling timer if running (user started new shot before settling completed)
    // Emit shotProcessingReady so the previous shot is saved before we reset state
    if (m_settlingTimer.isActive()) {
        qWarning() << "[SAW] Cancelling settling timer - new shot started, saving previous shot";
        m_sawTriggeredThisShot = false;
        m_settlingTimer.stop();
        m_displayTimer.stop();
        emit sawSettlingChanged();
        emit shotProcessingReady();
    }

    // Reset all timing state
    m_currentTime = 0;
    m_shotActive = true;

    // Reset weight state
    m_weight = 0;
    m_flowRate = 0;
    m_smoothedFlowRate = 0.0;
    m_flowRateInitialized = false;
    m_flowRateShort = 0;
    m_stopAtWeightTriggered = false;
    m_frameWeightSkipSent = -1;
    m_weightExitFrames.clear();
    m_currentFrameNumber = -1;
    m_extractionStarted = false;

    // Reset SAW learning state
    m_sawTriggeredThisShot = false;
    m_flowRateAtStop = 0.0;
    m_weightAtStop = 0.0;
    m_targetWeightAtStop = 0.0;
    m_lastStableWeight = 0.0;
    m_lastWeightChangeTime = 0;
    m_settlingPeakWeight = 0.0;

    // Reset tare state (will be set to Complete when tare() is called)
    m_tareState = TareState::Idle;

    // Start display timer for smooth UI updates
    m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
    m_displayTimer.start();

    emit shotTimeChanged();
    emit tareCompleteChanged();
    emit weightChanged();
}

void ShotTimingController::endShot()
{
    m_shotActive = false;

    // Start settling timer if SAW triggered this shot (for learning)
    // Keep display timer running during settling so graph continues to update
    if (m_sawTriggeredThisShot) {
        startSettlingTimer();
        // Don't stop display timer - keep time incrementing for graph
        // shotProcessingReady will be emitted after settling completes
        qDebug() << "[SAW] SAW triggered - waiting for weight to settle before processing shot";
    } else {
        m_displayTimer.stop();
        // No SAW - shot can be processed immediately
        qDebug() << "[SAW] No SAW - emitting shotProcessingReady immediately";
        emit shotProcessingReady();
    }

    emit shotTimeChanged();
}

void ShotTimingController::onShotSample(const ShotSample& sample, double pressureGoal,
                                         double flowGoal, double tempGoal,
                                         int frameNumber, bool isFlowMode)
{
    // Keep capturing samples during settling (shows pressure/flow declining after stop)
    bool isSettling = m_settlingTimer.isActive();
    if (!m_shotActive && !isSettling) {
        return;
    }

    // Track frame number change and detect extraction start (skip during settling)
    if (!isSettling && frameNumber != m_currentFrameNumber) {
        if (m_currentProfile && frameNumber >= 0 && frameNumber < m_currentProfile->steps().size()) {
            const auto& frame = m_currentProfile->steps()[frameNumber];
            qDebug() << "FRAME CHANGE:" << m_currentFrameNumber << "->" << frameNumber
                     << "name:" << frame.name << "exitWeight:" << frame.exitWeight;
        }
        m_currentFrameNumber = frameNumber;

        // Extraction starts on the first frame we see. The DE1 may skip preheating
        // frames (0-1) if the group is already hot, jumping straight to frame 2+.
        if (!m_extractionStarted) {
            m_extractionStarted = true;
            m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "EXTRACTION STARTED at frame" << frameNumber;
        }
    }

    // Calculate time from wall clock (simple and reliable)
    double time = (QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0;
    m_currentTime = time;

    // shotTimeChanged deferred to ShotDataModel's 33ms flush timer (avoid blocking BLE handler)

    // Emit unified sample with consistent timestamp
    emit sampleReady(time, sample.groupPressure, sample.groupFlow, sample.headTemp,
                     pressureGoal, flowGoal, tempGoal, frameNumber, isFlowMode);

    // Emit weight sample with same timestamp as other curves (perfect sync)
    // Weight value is cached from onWeightSample, emitted here for graph alignment
    // The LSLR smoother produces clean flow rates even during the Ending phase,
    // so we always emit the real value — it naturally decays to zero as dripping stops
    if (m_extractionStarted && m_weight >= 0.1) {
        emit weightSampleReady(time, m_weight, m_flowRate);
    }
}

void ShotTimingController::onWeightSample(double weight, double flowRate, double flowRateShort)
{
    // Keep updating weight while settling timer is running (for SAW learning)
    if (m_settlingTimer.isActive()) {
        // Track peak weight during settling for cup removal detection
        if (weight > m_settlingPeakWeight) {
            m_settlingPeakWeight = weight;
        }

        // Detect cup removal during settling:
        // 1. Single-step dramatic drop (>20g decrease from current)
        // 2. Cumulative drop >20g below peak weight (catches multi-step removal
        //    where no single step exceeds 20g)
        bool cupRemoved = (m_weight > 20.0 && weight < m_weight - 20.0) ||
                          (m_settlingPeakWeight > 20.0 && weight < m_settlingPeakWeight - 20.0);
        if (cupRemoved) {
            qWarning() << "[SAW] Cup removed during settling (weight:" << weight
                       << "peak:" << m_settlingPeakWeight << ") - skipping learning";
            // Cup removal corrupts weight data — bypass learning entirely
            // but still emit signals so the shot is saved.
            // NOTE: m_weight is intentionally NOT updated here. It retains the last
            // valid pre-removal reading so the saved shot preserves the correct
            // final weight. The corrupted `weight` parameter is discarded.
            m_sawTriggeredThisShot = false;  // Prevent stale SAW state on next operation
            m_settlingTimer.stop();
            m_displayTimer.stop();
            emit sawSettlingChanged();
            emit shotProcessingReady();
            return;
        }

        m_weight = weight;
        m_flowRate = flowRate;
        emit weightChanged();

        // Also emit to graph so drip is visible (use live calculated time)
        // LSLR produces clean flow rates even during settling — emit the real value
        double time = shotTime();
        emit weightSampleReady(time, weight, flowRate);

        // Rolling average stability detection
        // Add sample to circular buffer
        m_settlingWindow[m_settlingWindowIndex] = weight;
        m_settlingWindowIndex = (m_settlingWindowIndex + 1) % SETTLING_WINDOW_SIZE;
        if (m_settlingWindowCount < SETTLING_WINDOW_SIZE)
            m_settlingWindowCount++;

        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Also track per-sample changes for the old-style fast path
        double delta = qAbs(weight - m_lastStableWeight);
        qint64 stableMs = now - m_lastWeightChangeTime;
        if (delta >= 0.1) {
            m_lastStableWeight = weight;
            m_lastWeightChangeTime = now;
        }

        // Calculate rolling average
        double avg = 0;
        for (int i = 0; i < m_settlingWindowCount; i++)
            avg += m_settlingWindow[i];
        avg /= m_settlingWindowCount;

        double avgDrift = qAbs(avg - m_lastSettlingAvg);

        qDebug() << "[SAW] Settling:" << QString::number(weight, 'f', 1) << "g"
                 << "delta:" << QString::number(delta, 'f', 2)
                 << "avg:" << QString::number(avg, 'f', 1)
                 << "drift:" << QString::number(avgDrift, 'f', 2)
                 << "stable:" << stableMs << "ms";

        // Fast path: absolute stillness for 1 second (original behavior)
        if (stableMs >= 1000) {
            qDebug() << "[SAW] Weight stabilized at" << weight << "g (stable for" << stableMs << "ms)";
            m_settlingTimer.stop();
            onSettlingComplete();
        }
        // Rolling average path: tolerates oscillations
        else if (m_settlingWindowCount >= SETTLING_WINDOW_SIZE) {
            // Sanity guard: drip only adds weight, so the settled average must be
            // at least the weight when SAW triggered.  If it's below, the scale is
            // still recovering from pump-vibration artifacts — don't declare stable.
            bool avgBelowStop = (m_weightAtStop > 0 && avg < m_weightAtStop - 0.5);

            if (avgDrift < SETTLING_AVG_THRESHOLD && !avgBelowStop) {
                // Average is stable - check how long
                if (m_settlingAvgStableSince == 0)
                    m_settlingAvgStableSince = now;

                qint64 avgStableMs = now - m_settlingAvgStableSince;
                if (avgStableMs >= SETTLING_STABLE_MS) {
                    qDebug() << "[SAW] Weight settled by avg at" << QString::number(avg, 'f', 1)
                             << "g (avg stable for" << avgStableMs << "ms, current:" << weight << "g)";
                    m_weight = avg;  // Use the average as final weight
                    m_settlingTimer.stop();
                    onSettlingComplete();
                }
            } else {
                // Average still drifting or below stop weight - reset
                if (avgBelowStop && m_settlingAvgStableSince > 0)
                    qDebug() << "[SAW] Avg" << QString::number(avg, 'f', 1)
                             << "g below stop weight" << QString::number(m_weightAtStop, 'f', 1)
                             << "g - not settling yet";
                m_settlingAvgStableSince = 0;
            }
            m_lastSettlingAvg = avg;
        } else {
            m_lastSettlingAvg = avg;
        }

        // Don't process stop conditions - just track weight
        return;
    }

    if (!m_shotActive || !m_extractionStarted) {
        return;
    }

    m_weight = weight;
    m_flowRateShort = flowRateShort;

    // Apply EMA smoothing to flow rate for display/recording (alpha=0.3, ~0.6s at 5Hz)
    // Combined with 1s LSLR window gives ~1.6s effective smoothing
    constexpr double alpha = 0.3;
    if (!m_flowRateInitialized) {
        m_smoothedFlowRate = flowRate;  // Bootstrap first sample
        m_flowRateInitialized = true;
    } else {
        m_smoothedFlowRate = alpha * flowRate + (1.0 - alpha) * m_smoothedFlowRate;
    }
    m_flowRate = m_smoothedFlowRate;

    emit weightChanged();

    // Weight is cached here, emitted to graph in onShotSample for perfect timestamp sync.
    // SOW and per-frame weight checks are now handled by WeightProcessor on a dedicated
    // worker thread, eliminating main-thread congestion from the critical stop path.
}

void ShotTimingController::tare()
{
    if (m_scale && m_scale->isConnected()) {
        m_scale->tare();
        m_scale->resetFlowCalculation();  // Avoid flow rate spikes after tare
    }

    // Fire-and-forget: assume tare worked, set weight to 0 immediately
    // Weight samples are ignored until extraction starts anyway (preheating phase)
    m_weight = 0;
    m_tareState = TareState::Complete;
    emit tareCompleteChanged();
    emit weightChanged();
}

void ShotTimingController::onTareTimeout()
{
    // No longer used - tare is fire-and-forget now
    // Weight samples are ignored until extraction starts (preheating phase)
}

void ShotTimingController::updateDisplayTimer()
{
    // shotTimeChanged deferred to ShotDataModel's 33ms flush timer

    // Check settling stability here (in case scale stops sending samples)
    if (m_settlingTimer.isActive() && m_lastWeightChangeTime > 0) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Fast path: no weight samples at all for 1 second
        qint64 stableMs = now - m_lastWeightChangeTime;
        if (stableMs >= 1000) {
            qDebug() << "[SAW] Weight stabilized at" << m_weight << "g (stable for" << stableMs << "ms, detected by timer)";
            m_settlingTimer.stop();
            onSettlingComplete();
        }
        // Rolling average path: check if avg has been stable long enough
        else if (m_settlingAvgStableSince > 0) {
            qint64 avgStableMs = now - m_settlingAvgStableSince;
            if (avgStableMs >= SETTLING_STABLE_MS) {
                double avg = 0;
                for (int i = 0; i < m_settlingWindowCount; i++)
                    avg += m_settlingWindow[i];
                avg /= m_settlingWindowCount;
                qDebug() << "[SAW] Weight settled by avg at" << QString::number(avg, 'f', 1) << "g (detected by timer)";
                m_weight = avg;
                m_settlingTimer.stop();
                onSettlingComplete();
            }
        }
    }
}

void ShotTimingController::onSawTriggered(double weightAtStop, double flowRateAtStop, double targetWeight)
{
    // Called by WeightProcessor (via QueuedConnection) when SAW triggers on worker thread.
    // Captures state for SAW learning — settling will run after the shot ends.
    m_stopAtWeightTriggered = true;
    m_sawTriggeredThisShot = true;
    m_flowRateAtStop = flowRateAtStop;
    m_weightAtStop = weightAtStop;
    m_targetWeightAtStop = targetWeight;
    qDebug() << "[SAW] Worker thread triggered stop: weight=" << weightAtStop
             << "flow=" << flowRateAtStop << "target=" << targetWeight;
}

void ShotTimingController::recordWeightExit(int frameNumber)
{
    // Called by WeightProcessor (via QueuedConnection) when per-frame weight exit fires.
    // Tracks which frames exited by weight for transition reason inference.
    m_weightExitFrames.insert(frameNumber);
}

void ShotTimingController::startSettlingTimer()
{
    qDebug() << "[SAW] Starting settling (max 10s, or avg stable for" << SETTLING_STABLE_MS << "ms) - current weight:" << m_weight;
    m_lastStableWeight = m_weight;
    m_settlingPeakWeight = m_weight;
    m_lastWeightChangeTime = QDateTime::currentMSecsSinceEpoch();

    // Initialize rolling average window
    m_settlingWindowCount = 0;
    m_settlingWindowIndex = 0;
    m_lastSettlingAvg = m_weight;
    m_settlingAvgStableSince = 0;

    m_settlingTimer.setInterval(10000);  // 10 second max timeout
    m_settlingTimer.start();
    emit sawSettlingChanged();
}

void ShotTimingController::onSettlingComplete()
{
    // Reset flag FIRST to prevent re-triggering if another operation ends (e.g., steaming)
    m_sawTriggeredThisShot = false;

    // Settling is done - stop display timer and notify UI
    m_displayTimer.stop();
    emit sawSettlingChanged();
    emit shotProcessingReady();

    // Check scale is still connected
    if (!m_scale || !m_scale->isConnected()) {
        qWarning() << "[SAW] Scale disconnected, skipping learning";
        return;
    }

    // Validate flow rate at stop (low flow makes division unstable)
    if (m_flowRateAtStop < 0.5) {
        qWarning() << "[SAW] Flow at stop too low (" << m_flowRateAtStop << "), skipping learning";
        return;
    }

    // Calculate how much weight came after we sent the stop command
    double drip = m_weight - m_weightAtStop;
    if (drip < 0) {
        qWarning() << "[SAW] Negative drip (" << drip << "g), clamping to 0";
        drip = 0;  // Weight can't decrease
    }

    double overshoot = m_weight - m_targetWeightAtStop;

    // Validate settled weight is reasonable. Scale readings can go haywire after
    // the shot (drip tray interference, cup removal, scale oscillation). A 20g miss
    // is clearly a scale glitch; 10-15g can happen with a badly miscalibrated prediction
    // and the system needs to learn from those to recover.
    if (m_weight < 0 || qAbs(overshoot) > 20.0) {
        qWarning() << "[SAW] Settled weight unreasonable (weight=" << m_weight
                   << "overshoot=" << overshoot << "g), skipping learning";
        return;
    }

    // Extra cup-removal guard at completion time. Handles slow/multi-step cup
    // removal paths that may not trigger single-sample bypass checks.
    if (m_settlingPeakWeight > 20.0 && m_weight < m_settlingPeakWeight - 20.0) {
        qWarning() << "[SAW] Possible cup removal detected at settling complete"
                   << "(weight=" << m_weight << "peak=" << m_settlingPeakWeight
                   << "), skipping learning";
        return;
    }

    // Validate drip is in reasonable range (0 to 20 grams)
    // Widened from 15g to allow learning from badly miscalibrated predictions
    if (drip > 20.0) {
        qWarning() << "[SAW] Drip out of range (" << drip << "g), skipping learning";
        return;
    }

    qDebug() << "[SAW] Learning: final=" << m_weight << "g target=" << m_targetWeightAtStop
             << "drip=" << drip << "g flow=" << m_flowRateAtStop << "ml/s overshoot=" << overshoot << "g";

    // Emit signal for main.cpp to handle persistence (drip and flow, not lag)
    emit sawLearningComplete(drip, m_flowRateAtStop, overshoot);
}
