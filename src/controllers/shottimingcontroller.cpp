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
    m_settlingTimer.setSingleShot(true);
    m_settlingTimer.setInterval(7000);  // 7 seconds for drips to stop
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
    if (m_settlingTimer.isActive()) {
        qDebug() << "[SAW] Cancelling settling timer - new shot started";
        m_settlingTimer.stop();
    }

    // Reset all timing state
    m_currentTime = 0;
    m_shotActive = true;

    // Reset weight state
    m_weight = 0;
    m_flowRate = 0;
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

void ShotTimingController::onWeightSample(double weight, double flowRate)
{
    // Keep updating weight while settling timer is running (for SAW learning)
    if (m_settlingTimer.isActive()) {
        // Detect cup removal during settling: dramatic weight drop (>20g decrease)
        // Freeze weight at last valid reading to preserve shot data
        if (m_weight > 20.0 && weight < m_weight - 20.0) {
            qWarning() << "[SAW] Cup removed during settling (weight dropped from"
                       << m_weight << "to" << weight << ") - freezing weight at last valid reading";
            // Stop settling immediately and use the frozen weight
            m_settlingTimer.stop();
            onSettlingComplete();
            return;
        }

        m_weight = weight;
        m_flowRate = flowRate;
        emit weightChanged();

        // Also emit to graph so drip is visible (use live calculated time)
        // LSLR produces clean flow rates even during settling — emit the real value
        double time = shotTime();
        emit weightSampleReady(time, weight, flowRate);

        // Check for weight stabilization (time-based since scale only sends on change)
        double delta = qAbs(weight - m_lastStableWeight);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 stableMs = now - m_lastWeightChangeTime;

        qDebug() << "[SAW] Settling:" << QString::number(weight, 'f', 1) << "g"
                 << "delta:" << QString::number(delta, 'f', 2)
                 << "stable:" << stableMs << "ms";

        if (delta >= 0.1) {
            // Significant weight change - reset stability timer
            m_lastStableWeight = weight;
            m_lastWeightChangeTime = now;
        } else if (stableMs >= 1000) {
            // Weight stable for 1 second
            qDebug() << "[SAW] Weight stabilized at" << weight << "g (stable for" << stableMs << "ms)";
            m_settlingTimer.stop();
            onSettlingComplete();
        }

        // Don't process stop conditions - just track weight
        return;
    }

    if (!m_shotActive || !m_extractionStarted) {
        return;
    }

    m_weight = weight;
    m_flowRate = flowRate;

    emit weightChanged();

    // Weight is cached here, emitted to graph in onShotSample for perfect timestamp sync
    // Check stop conditions
    checkStopAtWeight();

    // Check per-frame weight exit (uses m_currentFrameNumber from onShotSample)
    checkPerFrameWeight(m_currentFrameNumber);
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
        qint64 stableMs = QDateTime::currentMSecsSinceEpoch() - m_lastWeightChangeTime;
        if (stableMs >= 1000) {
            qDebug() << "[SAW] Weight stabilized at" << m_weight << "g (stable for" << stableMs << "ms, detected by timer)";
            m_settlingTimer.stop();
            onSettlingComplete();
        }
    }
}

void ShotTimingController::checkStopAtWeight()
{
    if (m_stopAtWeightTriggered) return;
    if (m_tareState != TareState::Complete) return;

    // Sanity check: if we're very early in extraction and weight is unreasonably high,
    // assume tare hasn't completed yet (race condition when preheating is skipped).
    // Real coffee can't drip 50g in 3 seconds.
    if (m_extractionStarted && m_displayTimeBase > 0) {
        double extractionTime = (QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0;
        if (extractionTime < 3.0 && m_weight > 50.0) {
            qDebug() << "[SAW] Sanity check: weight" << m_weight << "g at" << extractionTime
                     << "s - likely untared cup, skipping SAW check";
            return;
        }
    }

    // Determine target based on current state
    double target = 0;
    DE1::State state = m_device ? m_device->state() : DE1::State::Sleep;

    if (state == DE1::State::HotWater && m_settings) {
        target = m_settings->waterVolume();  // ml ≈ g for water
    } else {
        target = m_targetWeight;  // Espresso target
    }

    if (target <= 0) return;

    double stopThreshold;
    if (state == DE1::State::HotWater) {
        // Hot water: use fixed 5g offset (predictable, avoids scale-dependent issues)
        stopThreshold = target - 5.0;
    } else {
        // Espresso: predict drip based on current flow and learning history
        double flowRate = m_flowRate;
        if (flowRate > 12.0) flowRate = 12.0;  // Cap at reasonable max
        if (flowRate < 0.5) flowRate = 0.5;    // Minimum to avoid division issues
        double expectedDrip = m_settings ? m_settings->getExpectedDrip(flowRate) : (flowRate * 1.5);
        stopThreshold = target - expectedDrip;

        // Debug: log the expected drip (once per shot when it changes significantly)
        static double lastLoggedDrip = -1;
        if (qAbs(expectedDrip - lastLoggedDrip) > 0.5) {
            qDebug() << "[SAW] Expected drip:" << expectedDrip << "g at flow" << flowRate << "ml/s";
            lastLoggedDrip = expectedDrip;
        }
    }

    if (m_weight >= stopThreshold) {
        m_stopAtWeightTriggered = true;

        // Capture state for SAW learning (espresso only)
        if (state != DE1::State::HotWater) {
            m_sawTriggeredThisShot = true;
            m_flowRateAtStop = m_flowRate;
            m_weightAtStop = m_weight;
            m_targetWeightAtStop = target;
            double expectedDrip = m_settings ? m_settings->getExpectedDrip(m_flowRate) : 0;
            qDebug() << "[SAW] Stop triggered: weight=" << m_weightAtStop
                     << "threshold=" << stopThreshold
                     << "expectedDrip=" << expectedDrip
                     << "flow=" << m_flowRateAtStop
                     << "target=" << m_targetWeightAtStop;
        }

        emit stopAtWeightReached();
    }
}

void ShotTimingController::checkPerFrameWeight(int frameNumber)
{
    if (!m_currentProfile || !m_device) return;
    if (frameNumber < 0 || frameNumber == m_frameWeightSkipSent) return;
    if (m_tareState != TareState::Complete) return;

    // Same sanity check as SAW - skip if weight is unreasonably high early in extraction
    if (m_extractionStarted && m_displayTimeBase > 0) {
        double extractionTime = (QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase) / 1000.0;
        if (extractionTime < 3.0 && m_weight > 50.0) {
            return;  // Likely untared cup
        }
    }

    const auto& steps = m_currentProfile->steps();
    if (frameNumber >= steps.size()) return;

    const ProfileFrame& frame = steps[frameNumber];

    if (frame.exitWeight > 0 && m_weight >= frame.exitWeight) {
        qDebug() << "FRAME-WEIGHT EXIT: weight" << m_weight << ">=" << frame.exitWeight
                 << "on frame" << frameNumber << "(" << frame.name << ")";
        m_frameWeightSkipSent = frameNumber;
        m_weightExitFrames.insert(frameNumber);
        emit perFrameWeightReached(frameNumber);
    }
}

void ShotTimingController::startSettlingTimer()
{
    qDebug() << "[SAW] Starting settling (max 15s, or stable for 1s) - current weight:" << m_weight;
    m_lastStableWeight = m_weight;
    m_lastWeightChangeTime = QDateTime::currentMSecsSinceEpoch();
    m_settlingTimer.setInterval(15000);  // 15 second max timeout
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
        qDebug() << "[SAW] Scale disconnected, skipping learning";
        return;
    }

    // Validate flow rate at stop (low flow makes division unstable)
    if (m_flowRateAtStop < 0.5) {
        qDebug() << "[SAW] Flow at stop too low (" << m_flowRateAtStop << "), skipping learning";
        return;
    }

    // Calculate how much weight came after we sent the stop command
    double drip = m_weight - m_weightAtStop;
    if (drip < 0) drip = 0;  // Weight can't decrease

    double overshoot = m_weight - m_targetWeightAtStop;

    // Validate drip is in reasonable range (0 to 15 grams)
    if (drip > 15.0) {
        qDebug() << "[SAW] Drip out of range (" << drip << "g), skipping learning";
        return;
    }

    qDebug() << "[SAW] Learning: final=" << m_weight << "g target=" << m_targetWeightAtStop
             << "drip=" << drip << "g flow=" << m_flowRateAtStop << "ml/s overshoot=" << overshoot << "g";

    // Emit signal for main.cpp to handle persistence (drip and flow, not lag)
    emit sawLearningComplete(drip, m_flowRateAtStop, overshoot);
}
