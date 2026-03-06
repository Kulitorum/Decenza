#include "directcontroller.h"
#include "../ble/protocol/binarycodec.h"
#include <QDebug>

DirectController::DirectController(DE1Device* device, ScaleDevice* scale, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_scale(scale)
{
    // Update timer for progress tracking
    m_updateTimer.setInterval(100);  // 10 Hz
    connect(&m_updateTimer, &QTimer::timeout, this, &DirectController::onUpdateTimer);

    // Connect to device signals
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &DirectController::onShotSampleReceived);
    }

    // Connect to scale signals
    if (m_scale) {
        connect(m_scale, &ScaleDevice::weightChanged,
                this, &DirectController::onWeightChanged);
    }
}

DirectController::~DirectController() {
    stopShot();
}

void DirectController::setProfile(const Profile& profile) {
    m_profile = profile;
    qDebug() << "DirectController: Set profile" << profile.title()
             << "with" << profile.steps().size() << "steps";
}

void DirectController::startShot() {
    if (m_profile.steps().isEmpty()) {
        emit errorOccurred("Cannot start shot: profile has no steps");
        return;
    }

    if (!m_device || !m_device->isConnected()) {
        emit errorOccurred("Cannot start shot: DE1 not connected");
        return;
    }

    qDebug() << "DirectController: Starting shot in direct control mode";

    // Reset state
    m_active = true;
    m_currentFrameIndex = 0;
    m_shotStartTime = 0;
    m_frameStartTime = 0;
    m_frameElapsedTime = 0;
    m_lastWeight = 0;
    m_liveOverrideActive = false;

    emit activeChanged();

    // Create the direct control profile on the machine
    // This uploads a minimal profile that we'll override with live frames
    createDirectControlProfile();

    // Start the shot
    m_device->requestState(DE1::State::Espresso);

    // Send the first frame
    sendCurrentFrame();

    // Start update timer
    m_updateTimer.start();

    emit frameChanged(m_currentFrameIndex, currentFrameName(), QString());
}

void DirectController::stopShot() {
    if (!m_active) return;

    qDebug() << "DirectController: Stopping shot";

    m_active = false;
    m_updateTimer.stop();

    if (m_device && m_device->isConnected()) {
        m_device->stopOperation();
    }

    emit activeChanged();
    emit shotCompleted();
}

void DirectController::skipToNextFrame() {
    if (!m_active) return;

    if (m_currentFrameIndex < m_profile.steps().size() - 1) {
        advanceToNextFrame();
    } else {
        // Last frame - stop the shot
        stopShot();
    }
}

void DirectController::goToPreviousFrame() {
    if (!m_active || m_currentFrameIndex <= 0) return;

    m_currentFrameIndex--;
    m_frameStartTime = m_shotStartTime + m_frameElapsedTime;  // Approximate
    m_frameElapsedTime = 0;

    sendCurrentFrame();
    emit frameChanged(m_currentFrameIndex, currentFrameName(), QString());
}

void DirectController::setLivePressure(double pressure) {
    m_livePressure = pressure;
    m_liveOverrideActive = true;

    // Create a modified frame with the live pressure
    if (m_active && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        ProfileFrame frame = m_profile.steps()[m_currentFrameIndex];
        frame.pressure = pressure;
        frame.pump = "pressure";

        QByteArray frameData = frameToBytes(frame, 0);
        m_device->writeFrame(frameData);

        qDebug() << "DirectController: Live pressure override:" << pressure;
    }
}

void DirectController::setLiveFlow(double flow) {
    m_liveFlow = flow;
    m_liveOverrideActive = true;

    if (m_active && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        ProfileFrame frame = m_profile.steps()[m_currentFrameIndex];
        frame.flow = flow;
        frame.pump = "flow";

        QByteArray frameData = frameToBytes(frame, 0);
        m_device->writeFrame(frameData);

        qDebug() << "DirectController: Live flow override:" << flow;
    }
}

void DirectController::setLiveTemperature(double temperature) {
    m_liveTemperature = temperature;
    m_liveOverrideActive = true;

    if (m_active && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        ProfileFrame frame = m_profile.steps()[m_currentFrameIndex];
        frame.temperature = temperature;

        QByteArray frameData = frameToBytes(frame, 0);
        m_device->writeFrame(frameData);

        qDebug() << "DirectController: Live temperature override:" << temperature;
    }
}

QString DirectController::currentFrameName() const {
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        return m_profile.steps()[m_currentFrameIndex].name;
    }
    return QString();
}

double DirectController::frameProgress() const {
    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= m_profile.steps().size()) {
        return 0;
    }

    const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];
    if (frame.seconds <= 0) return 0;

    return qMin(1.0, m_frameElapsedTime / frame.seconds);
}

void DirectController::onShotSampleReceived(const ShotSample& sample) {
    if (!m_active) return;

    // Initialize shot start time from first sample
    if (m_shotStartTime == 0) {
        m_shotStartTime = sample.timer;
        m_frameStartTime = sample.timer;
    }

    // Update timing
    double totalElapsed = sample.timer - m_shotStartTime;
    m_frameElapsedTime = sample.timer - m_frameStartTime;

    // Update last values
    m_lastPressure = sample.groupPressure;
    m_lastFlow = sample.groupFlow;

    // Check exit conditions
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];

        // Check time-based exit
        if (m_frameElapsedTime >= frame.seconds) {
            advanceToNextFrame();
        }
        // Check sensor-based exit conditions
        else if (checkExitCondition(frame, sample)) {
            advanceToNextFrame();
        }
    }

    emit progressUpdated();
}

void DirectController::onWeightChanged(double weight) {
    if (!m_active) return;

    m_lastWeight = weight;

    // Check per-frame weight exit condition
    // NOTE: Weight exit is INDEPENDENT of exitIf - the "weight" property triggers
    // app-side frame skip regardless of the machine-side exit_if flag
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < m_profile.steps().size()) {
        const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];
        if (frame.exitWeight > 0) {
            if (weight >= frame.exitWeight) {
                qDebug() << "DirectController: Frame weight exit reached:" << weight << "/" << frame.exitWeight;
                advanceToNextFrame();
                return;
            }
        }
    }

    // Check global weight target (stop entire shot)
    double targetWeight = m_profile.targetWeight();
    if (targetWeight > 0 && weight >= targetWeight) {
        qDebug() << "DirectController: Weight target reached:" << weight << "/" << targetWeight;
        stopShot();
    }
}

void DirectController::onUpdateTimer() {
    if (!m_active) return;

    // Emit progress update for UI
    emit progressUpdated();
}

void DirectController::sendCurrentFrame() {
    if (!m_device || !m_active) return;

    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= m_profile.steps().size()) {
        return;
    }

    const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];
    QByteArray frameData = frameToBytes(frame, 0);

    m_device->writeFrame(frameData);

    qDebug() << "DirectController: Sent frame" << m_currentFrameIndex
             << "(" << frame.name << ")"
             << (frame.pump == "flow" ? "flow" : "pressure") << "="
             << (frame.pump == "flow" ? frame.flow : frame.pressure)
             << "temp=" << frame.temperature;
}

void DirectController::advanceToNextFrame() {
    if (m_currentFrameIndex >= m_profile.steps().size() - 1) {
        // Last frame completed
        qDebug() << "DirectController: All frames completed";
        stopShot();
        return;
    }

    m_currentFrameIndex++;
    m_frameStartTime = m_shotStartTime + m_frameElapsedTime;
    m_frameElapsedTime = 0;
    m_liveOverrideActive = false;

    sendCurrentFrame();

    qDebug() << "DirectController: Advanced to frame" << m_currentFrameIndex;
    emit frameChanged(m_currentFrameIndex, currentFrameName(), QString());
}

bool DirectController::checkExitCondition(const ProfileFrame& frame, const ShotSample& sample) {
    if (!frame.exitIf) return false;

    if (frame.exitType == "pressure_over") {
        return sample.groupPressure > frame.exitPressureOver;
    } else if (frame.exitType == "pressure_under") {
        return sample.groupPressure < frame.exitPressureUnder;
    } else if (frame.exitType == "flow_over") {
        return sample.groupFlow > frame.exitFlowOver;
    } else if (frame.exitType == "flow_under") {
        return sample.groupFlow < frame.exitFlowUnder;
    }

    return false;
}


void DirectController::createDirectControlProfile() {
    // Upload a minimal profile header
    // In direct control mode, we only need 1-2 frames that we'll keep updating

    QByteArray header(5, 0);
    header[0] = 1;  // HeaderV
    header[1] = 2;  // NumberOfFrames (minimal)
    header[2] = 1;  // NumberOfPreinfuseFrames
    header[3] = BinaryCodec::encodeU8P4(0.0);   // MinimumPressure
    header[4] = BinaryCodec::encodeU8P4(8.0);   // MaximumFlow

    m_device->writeHeader(header);

    // Write initial frames
    if (!m_profile.steps().isEmpty()) {
        QByteArray frame0 = frameToBytes(m_profile.steps()[0], 0);
        m_device->writeFrame(frame0);

        if (m_profile.steps().size() > 1) {
            QByteArray frame1 = frameToBytes(m_profile.steps()[1], 1);
            m_device->writeFrame(frame1);
        }
    }

    // Tail frame
    QByteArray tail(8, 0);
    tail[0] = 2;  // FrameToWrite = number of frames
    m_device->writeFrame(tail);
}

QByteArray DirectController::frameToBytes(const ProfileFrame& frame, int frameIndex) {
    QByteArray data(8, 0);

    data[0] = static_cast<char>(frameIndex);  // FrameToWrite
    data[1] = static_cast<char>(frame.computeFlags());  // Flag
    data[2] = BinaryCodec::encodeU8P4(frame.getSetVal());  // SetVal
    data[3] = BinaryCodec::encodeU8P1(frame.temperature);  // Temp
    data[4] = BinaryCodec::encodeF8_1_7(frame.seconds);  // FrameLen
    data[5] = BinaryCodec::encodeU8P4(frame.getTriggerVal());  // TriggerVal

    uint16_t maxVol = BinaryCodec::encodeU10P0(frame.volume);
    data[6] = static_cast<char>((maxVol >> 8) & 0xFF);
    data[7] = static_cast<char>(maxVol & 0xFF);

    return data;
}
