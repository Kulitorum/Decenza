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
    qDebug() << "[REFACTOR] ShotTimingController created, device =" << (device ? "valid" : "NULL");

    // Display timer - updates UI at 20Hz for smooth timer display
    m_displayTimer.setInterval(50);
    connect(&m_displayTimer, &QTimer::timeout, this, &ShotTimingController::updateDisplayTimer);
}

double ShotTimingController::shotTime() const
{
    if (m_shotActive && m_displayTimeBase > 0) {
        // Calculate time from wall clock for smooth UI updates between BLE samples
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_displayTimeBase;
        return elapsed / 1000.0;
    }
    return m_currentTime;
}

void ShotTimingController::setScale(ScaleDevice* scale)
{
    qDebug() << "[REFACTOR] ShotTimingController::setScale() scale =" << (scale ? scale->type() : "NULL");
    m_scale = scale;
}

void ShotTimingController::setSettings(Settings* settings)
{
    qDebug() << "[REFACTOR] ShotTimingController::setSettings()";
    m_settings = settings;
}

void ShotTimingController::setMachineState(MachineState* machineState)
{
    qDebug() << "[REFACTOR] ShotTimingController::setMachineState()";
    m_machineState = machineState;
}

void ShotTimingController::setTargetWeight(double weight)
{
    qDebug() << "[REFACTOR] ShotTimingController::setTargetWeight(" << weight << ")";
    m_targetWeight = weight;
}

void ShotTimingController::setCurrentProfile(const Profile* profile)
{
    qDebug() << "[REFACTOR] ShotTimingController::setCurrentProfile()"
             << "profile =" << (profile ? profile->title() : "NULL");
    m_currentProfile = profile;

    // Log frame exit weights for debugging
    if (profile) {
        const auto& steps = profile->steps();
        qDebug() << "[REFACTOR] Profile has" << steps.size() << "frames:";
        for (int i = 0; i < steps.size(); ++i) {
            const auto& frame = steps[i];
            qDebug() << "[REFACTOR]   Frame" << i << ":" << frame.name
                     << "exitWeight:" << frame.exitWeight
                     << "exitIf:" << frame.exitIf
                     << "exitType:" << frame.exitType;
        }
    }
}

void ShotTimingController::startShot()
{
    qDebug() << "[REFACTOR] ========== ShotTimingController::startShot() ==========";
    qDebug() << "[REFACTOR] m_device =" << (m_device ? "valid" : "NULL");
    qDebug() << "[REFACTOR] m_scale =" << (m_scale ? m_scale->type() : "NULL");
    qDebug() << "[REFACTOR] m_currentProfile =" << (m_currentProfile ? m_currentProfile->title() : "NULL");
    qDebug() << "[REFACTOR] m_targetWeight =" << m_targetWeight;

    // Reset all timing state
    m_bleTimeBase = 0;
    m_currentTime = 0;
    m_shotActive = true;

    // Reset weight state
    m_weight = 0;
    m_flowRate = 0;
    m_stopAtWeightTriggered = false;
    m_frameWeightSkipSent = -1;
    m_currentFrameNumber = -1;
    m_extractionStarted = false;

    // Reset tare state (will be set to Complete when tare() is called)
    m_tareState = TareState::Idle;

    // Start display timer for smooth UI updates
    m_displayTimeBase = QDateTime::currentMSecsSinceEpoch();
    m_displayTimer.start();

    qDebug() << "[REFACTOR] Shot started - all state reset, display timer started";

    emit shotTimeChanged();
    emit tareCompleteChanged();
    emit weightChanged();
}

void ShotTimingController::endShot()
{
    qDebug() << "[REFACTOR] ========== ShotTimingController::endShot() ==========";
    qDebug() << "[REFACTOR] Final time:" << m_currentTime << "s, weight:" << m_weight << "g";

    m_shotActive = false;
    m_displayTimer.stop();

    emit shotTimeChanged();
}

void ShotTimingController::onShotSample(const ShotSample& sample, double pressureGoal,
                                         double flowGoal, double tempGoal,
                                         int frameNumber, bool isFlowMode)
{
    if (!m_shotActive) {
        static int inactiveCount = 0;
        if (++inactiveCount % 100 == 1) {
            qDebug() << "[REFACTOR] onShotSample IGNORED - shot not active";
        }
        return;
    }

    // First sample of this shot - set the base time
    if (m_bleTimeBase == 0) {
        m_bleTimeBase = sample.timer;
        qDebug() << "[REFACTOR] FIRST BLE SAMPLE - base time =" << m_bleTimeBase;
    }

    // Calculate relative time from DE1's BLE timer (single source of truth)
    double time = sample.timer - m_bleTimeBase;
    m_currentTime = time;

    // Track frame number change
    if (frameNumber != m_currentFrameNumber) {
        qDebug() << "[REFACTOR] FRAME CHANGE:" << m_currentFrameNumber << "->" << frameNumber
                 << "at time" << time << "s";
        if (m_currentProfile && frameNumber >= 0 && frameNumber < m_currentProfile->steps().size()) {
            const auto& frame = m_currentProfile->steps()[frameNumber];
            qDebug() << "[REFACTOR] New frame:" << frame.name
                     << "exitWeight:" << frame.exitWeight
                     << "pressure:" << frame.pressure
                     << "flow:" << frame.flow;
        }
        m_currentFrameNumber = frameNumber;

        // Extraction starts when frame 0 is reached (preheating shows higher frame numbers)
        if (frameNumber == 0 && !m_extractionStarted) {
            m_extractionStarted = true;
            qDebug() << "[REFACTOR] EXTRACTION STARTED - frame 0 reached, weight tracking enabled";
        }
    }

    // Sync display timer base to match BLE time
    m_displayTimeBase = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(time * 1000);

    // Log sample data periodically
    static int sampleCount = 0;
    if (++sampleCount % 20 == 1) {
        qDebug() << "[REFACTOR] SAMPLE: time=" << QString::number(time, 'f', 2)
                 << "frame=" << frameNumber
                 << "P=" << QString::number(sample.groupPressure, 'f', 2)
                 << "F=" << QString::number(sample.groupFlow, 'f', 2)
                 << "T=" << QString::number(sample.headTemp, 'f', 1)
                 << "weight=" << QString::number(m_weight, 'f', 1);
    }

    emit shotTimeChanged();

    // Emit unified sample with consistent timestamp
    emit sampleReady(time, sample.groupPressure, sample.groupFlow, sample.headTemp,
                     pressureGoal, flowGoal, tempGoal, frameNumber, isFlowMode);
}

void ShotTimingController::onWeightSample(double weight, double flowRate)
{
    // Log EVERY weight sample received
    static int allWeightCount = 0;
    if (++allWeightCount % 5 == 1) {
        qDebug() << "[REFACTOR] onWeightSample RECEIVED: weight=" << QString::number(weight, 'f', 2)
                 << "shotActive=" << m_shotActive
                 << "extractionStarted=" << m_extractionStarted
                 << "frame=" << m_currentFrameNumber
                 << "currentTime=" << QString::number(m_currentTime, 'f', 2);
    }

    if (!m_shotActive) {
        static int inactiveCount = 0;
        if (++inactiveCount % 20 == 1) {
            qDebug() << "[REFACTOR] WEIGHT DROPPED: shot not active, weight=" << weight;
        }
        return;
    }

    // Ignore weight samples until extraction starts (frame 0 reached)
    // During preheating DE1 reports high frame numbers (e.g., 2), not -1
    // This gives the scale time to process the tare command
    if (!m_extractionStarted) {
        static int preheatCount = 0;
        if (++preheatCount % 10 == 1) {
            qDebug() << "[REFACTOR] WEIGHT IGNORED: extraction not started (frame=" << m_currentFrameNumber << "), weight=" << weight;
        }
        return;
    }

    double oldWeight = m_weight;
    m_weight = weight;
    m_flowRate = flowRate;

    emit weightChanged();

    // Use the last BLE sample time for weight timestamp
    // This ensures weight curve aligns with pressure/flow curves
    double time = m_currentTime;

    // Log weight being sent to graph
    static int graphCount = 0;
    if (++graphCount % 10 == 1 || qAbs(weight - oldWeight) > 0.5) {
        qDebug() << "[REFACTOR] WEIGHT->GRAPH: time=" << QString::number(time, 'f', 2)
                 << "weight=" << QString::number(weight, 'f', 2)
                 << "frame=" << m_currentFrameNumber;
    }

    // Emit weight sample for graph
    emit weightSampleReady(time, weight);

    // Check stop conditions
    checkStopAtWeight();

    // Check per-frame weight exit (uses m_currentFrameNumber from onShotSample)
    checkPerFrameWeight(m_currentFrameNumber);
}

void ShotTimingController::tare()
{
    qDebug() << "[REFACTOR] ShotTimingController::tare() called";
    qDebug() << "[REFACTOR] m_scale =" << (m_scale ? m_scale->type() : "NULL")
             << "connected =" << (m_scale ? m_scale->isConnected() : false);

    if (m_scale && m_scale->isConnected()) {
        m_scale->tare();
        m_scale->resetFlowCalculation();  // Avoid flow rate spikes after tare
        qDebug() << "[REFACTOR] TARE SENT (fire-and-forget)";
    }

    // Fire-and-forget: assume tare worked, set weight to 0 immediately
    // Weight samples are ignored until frame 0 starts anyway (4-5s preheating)
    m_weight = 0;
    m_tareState = TareState::Complete;
    emit tareCompleteChanged();
    emit weightChanged();
    qDebug() << "[REFACTOR] Tare assumed complete - weight reset to 0, waiting for frame 0";
}

void ShotTimingController::onTareTimeout()
{
    // No longer used - tare is fire-and-forget now
    // Weight samples are ignored until frame 0 starts (4-5s preheating)
}

void ShotTimingController::updateDisplayTimer()
{
    // Just emit the signal - shotTime() calculates from wall clock
    emit shotTimeChanged();
}

void ShotTimingController::checkStopAtWeight()
{
    if (m_stopAtWeightTriggered) return;

    if (m_tareState != TareState::Complete) {
        static int logCount = 0;
        if (++logCount % 50 == 1) {
            qDebug() << "[REFACTOR] STOP-AT-WEIGHT SKIPPED: tare not complete, state =" << static_cast<int>(m_tareState);
        }
        return;
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

    // Account for flow rate and lag
    double flowRate = m_flowRate;
    // Cap flow rate to reasonable range
    double maxFlowRate = (state == DE1::State::HotWater) ? 20.0 : 10.0;
    if (flowRate > maxFlowRate) flowRate = maxFlowRate;
    if (flowRate < 0) flowRate = 0;

    // Lag compensation: time from stop command to actual flow stop
    // Includes: BLE transmission, valve response, residual pressure, drip
    // Empirically measured: espresso ~1.5s, hot water ~1.2s (faster valve, less pressure)
    double lagSeconds = (state == DE1::State::HotWater) ? 1.2 : 1.5;
    double lagCompensation = flowRate * lagSeconds;

    if (m_weight >= (target - lagCompensation)) {
        m_stopAtWeightTriggered = true;
        qDebug() << "[REFACTOR] STOP-AT-WEIGHT TRIGGERED: weight =" << m_weight
                 << "target =" << target << "lagComp =" << lagCompensation;
        emit stopAtWeightReached();
    } else {
        static int progressCount = 0;
        if (++progressCount % 100 == 1) {
            qDebug() << "[REFACTOR] STOP-AT-WEIGHT: progress" << m_weight << "/" << target
                     << "(lagComp:" << lagCompensation << ")";
        }
    }
}

void ShotTimingController::checkPerFrameWeight(int frameNumber)
{
    if (!m_currentProfile) {
        static int noProfileCount = 0;
        if (++noProfileCount % 100 == 1) {
            qDebug() << "[REFACTOR] FRAME-WEIGHT: No profile set!";
        }
        return;
    }
    if (!m_device) {
        static int noDeviceCount = 0;
        if (++noDeviceCount % 100 == 1) {
            qDebug() << "[REFACTOR] FRAME-WEIGHT: No device set!";
        }
        return;
    }
    if (frameNumber < 0) {
        return;
    }
    if (frameNumber == m_frameWeightSkipSent) {
        return;
    }
    if (m_tareState != TareState::Complete) {
        static int tareCount = 0;
        if (++tareCount % 50 == 1) {
            qDebug() << "[REFACTOR] FRAME-WEIGHT: Tare not complete, state =" << static_cast<int>(m_tareState);
        }
        return;
    }

    const auto& steps = m_currentProfile->steps();
    if (frameNumber >= steps.size()) {
        qDebug() << "[REFACTOR] FRAME-WEIGHT: Frame" << frameNumber << "out of bounds (size:" << steps.size() << ")";
        return;
    }

    const ProfileFrame& frame = steps[frameNumber];

    // Log frame weight check periodically
    static int checkCount = 0;
    if (++checkCount % 20 == 1) {
        qDebug() << "[REFACTOR] FRAME-WEIGHT CHECK: frame=" << frameNumber << "(" << frame.name << ")"
                 << "exitWeight=" << frame.exitWeight << "current=" << m_weight;
    }

    if (frame.exitWeight > 0 && m_weight >= frame.exitWeight) {
        qDebug() << "[REFACTOR] *** FRAME-WEIGHT EXIT TRIGGERED ***"
                 << "weight" << m_weight << ">=" << frame.exitWeight
                 << "on frame" << frameNumber << "(" << frame.name << ")";
        m_frameWeightSkipSent = frameNumber;
        emit perFrameWeightReached(frameNumber);
    }
}
