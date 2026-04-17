#include "de1simulator.h"
#include <QDebug>
#include <QDateTime>
#include <QtMath>
#include <QRandomGenerator>
#include <algorithm>
#include <numeric>

DE1Simulator::DE1Simulator(QObject* parent)
    : QObject(parent)
{
    m_tickTimer.setInterval(TICK_INTERVAL_MS);
    connect(&m_tickTimer, &QTimer::timeout, this, &DE1Simulator::onSimulationTimerTick);
    initNoisePermutation();
}

void DE1Simulator::initNoisePermutation()
{
    // Initialize Perlin noise permutation table with random seed per shot
    m_noiseSeed = QRandomGenerator::global()->generate();

    // Create permutation array 0-255
    std::array<int, 256> p;
    std::iota(p.begin(), p.end(), 0);

    // Shuffle using our seed
    QRandomGenerator rng(m_noiseSeed);
    for (int i = 255; i > 0; i--) {
        int j = rng.bounded(i + 1);
        std::swap(p[i], p[j]);
    }

    // Duplicate for overflow
    for (int i = 0; i < 256; i++) {
        m_perm[i] = p[i];
        m_perm[256 + i] = p[i];
    }
}

double DE1Simulator::perlinNoise1D(double x)
{
    // 1D Perlin noise - smooth interpolated random values
    int xi = static_cast<int>(std::floor(x)) & 255;
    double xf = x - std::floor(x);

    // Fade function: 6t^5 - 15t^4 + 10t^3 (Ken Perlin's improved version)
    double u = xf * xf * xf * (xf * (xf * 6.0 - 15.0) + 10.0);

    // Hash coordinates
    int a = m_perm[xi];
    int b = m_perm[xi + 1];

    // Gradient values from hash (-1 to 1 range)
    double gradA = (m_perm[a] / 128.0) - 1.0;
    double gradB = (m_perm[b] / 128.0) - 1.0;

    // Interpolate
    double valueA = gradA * xf;
    double valueB = gradB * (xf - 1.0);

    return valueA + u * (valueB - valueA);
}

double DE1Simulator::fractalNoise(double x, int octaves)
{
    // Fractal Brownian Motion - multiple octaves of Perlin noise
    double result = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        result += amplitude * perlinNoise1D(x * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;   // Each octave half the amplitude
        frequency *= 2.0;   // Each octave double the frequency
    }

    return result / maxValue;  // Normalize to -1..1
}

double DE1Simulator::channelNoise(double time)
{
    // Simulate micro-channeling events - sudden drops in resistance
    // that recover over time

    // Random channel events
    if (QRandomGenerator::global()->generateDouble() < CHANNEL_PROBABILITY) {
        if (m_channelIntensity < 0.1) {  // Only start new channel if previous recovered
            m_channelIntensity = 0.5 + QRandomGenerator::global()->generateDouble() * 0.5;
            m_lastChannelTime = time;
        }
    }

    // Decay channel intensity over time (exponential recovery)
    if (m_channelIntensity > 0.01) {
        double timeSinceChannel = time - m_lastChannelTime;
        double decay = qExp(-timeSinceChannel / (CHANNEL_DURATION * 0.5));
        m_channelIntensity *= decay;

        // Channel causes resistance drop
        return 1.0 - (m_channelIntensity * CHANNEL_RESISTANCE_DROP);
    }

    return 1.0;  // No channel effect
}

void DE1Simulator::setProfile(const Profile& profile)
{
    m_profile = profile;
    qDebug() << "DE1Simulator: Profile set:" << profile.title()
             << "with" << profile.steps().size() << "frames";
}

void DE1Simulator::setDose(double grams)
{
    m_dose = qBound(10.0, grams, 25.0);  // Reasonable dose range
    qDebug() << "DE1Simulator: Dose set to" << m_dose << "g";
}

void DE1Simulator::setGrindSetting(const QString& setting)
{
    // Parse grind setting - lower number = finer = more resistance
    bool ok;
    double grindValue = setting.toDouble(&ok);

    if (ok && grindValue > 0) {
        // grindFactor = reference / actual, so lower setting = higher factor
        // Clamp to reasonable range (0.5x to 3x resistance)
        m_grindFactor = qBound(0.5, REFERENCE_GRIND / grindValue, 3.0);
        qDebug() << "DE1Simulator: Grind setting" << setting
                 << "-> factor" << m_grindFactor;
    } else {
        // Can't parse or invalid - use neutral
        m_grindFactor = 1.0;
        qDebug() << "DE1Simulator: Grind setting not parseable, using factor 1.0";
    }
}

void DE1Simulator::setState(DE1::State state, DE1::SubState subState)
{
    bool stateChanged = (m_state != state);
    bool subStateChanged = (m_subState != subState);

    m_state = state;
    m_subState = subState;

    if (stateChanged) {
        qDebug() << "DE1Simulator: State ->" << DE1::stateToString(state);
        emit this->stateChanged();
    }
    if (subStateChanged) {
        qDebug() << "DE1Simulator: SubState ->" << DE1::subStateToString(subState);
        emit this->subStateChanged();
    }
}

void DE1Simulator::startEspresso()
{
    if (m_state == DE1::State::Sleep) {
        wakeUp();
    }

    if (m_state != DE1::State::Idle) {
        qDebug() << "DE1Simulator: Cannot start espresso, not idle";
        return;
    }

    qDebug() << "DE1Simulator: Starting espresso";

    // Reset shot state
    m_currentFrameIndex = 0;
    m_frameStartTime = 0.0;
    m_frameVolume = 0.0;
    m_totalVolume = 0.0;
    m_outputVolume = 0.0;
    m_scaleWeight = 0.0;
    m_waterInPuck = false;
    m_puckFilled = false;
    m_channelIntensity = 0.0;
    m_lastChannelTime = 0.0;

    // Reset dynamics
    m_pressure = 0.0;
    m_flow = 0.0;
    m_pressureVelocity = 0.0;
    m_flowVelocity = 0.0;
    m_targetPressure = 0.0;
    m_targetFlow = 0.0;

    // Reset valve and plumbing state
    m_valveOpen = false;
    m_plumbingVolume = 0.0;
    m_plumbingPressure = 0.0;

    // Reset puck state
    m_puckResistance = BASELINE_RESISTANCE;
    m_baseResistance = BASELINE_RESISTANCE;

    // Reinitialize noise for this shot (each shot is unique)
    initNoisePermutation();

    // Reset scale
    emit scaleWeightChanged(0.0);

    startOperation(DE1::State::Espresso);
    setState(DE1::State::Espresso, DE1::SubState::Heating);
}

void DE1Simulator::startSteam()
{
    if (m_state == DE1::State::Sleep) wakeUp();
    qDebug() << "DE1Simulator: Starting steam";
    startOperation(DE1::State::Steam);
    setState(DE1::State::Steam, DE1::SubState::Pouring);
}

void DE1Simulator::setTargetSteamTemp(double targetC)
{
    // Ambient when heater is off, otherwise snap to target. Real DE1 has a
    // thermal ramp; we skip it because the sim doesn't run a tick loop when
    // idle, and the UI only cares that "Heating steam..." clears instead of
    // climbing indefinitely.
    constexpr double kAmbientC = 25.0;
    double newSteamTemp = (targetC <= 0.0) ? kAmbientC : targetC;
    if (qFuzzyCompare(newSteamTemp, m_steamTemp)) return;
    m_steamTemp = newSteamTemp;
    qDebug() << "DE1Simulator: target steam temp=" << targetC << "C -> m_steamTemp=" << m_steamTemp;
    // Only emit when idle — during active steam, the tick loop is already
    // producing real shot samples at 5Hz and we'd inject a zero-pressure /
    // zero-flow sample into the steam graph. The +5s/-5s buttons route
    // through setShotSettings mid-steam; suppressing the emit here prevents
    // graph pollution. The in-session target change is moot anyway because
    // the Steam-state tick hardcodes m_steamTemp to 140°C + noise.
    if (m_state == DE1::State::Steam) return;
    emit idleSteamTempChanged(m_steamTemp);
}

void DE1Simulator::startHotWater()
{
    if (m_state == DE1::State::Sleep) wakeUp();
    qDebug() << "DE1Simulator: Starting hot water";
    startOperation(DE1::State::HotWater);
    setState(DE1::State::HotWater, DE1::SubState::Pouring);
}

void DE1Simulator::startFlush()
{
    if (m_state == DE1::State::Sleep) wakeUp();
    qDebug() << "DE1Simulator: Starting flush";
    startOperation(DE1::State::HotWaterRinse);
    setState(DE1::State::HotWaterRinse, DE1::SubState::Pouring);
}

void DE1Simulator::startDescale()
{
    if (m_state == DE1::State::Sleep) wakeUp();
    qDebug() << "DE1Simulator: Starting descale";
    m_descaleStep = 8;  // DescaleInit
    m_descaleStepStart = 0.0;
    startOperation(DE1::State::Descale);
    setState(DE1::State::Descale, static_cast<DE1::SubState>(m_descaleStep));
}

void DE1Simulator::startClean()
{
    if (m_state == DE1::State::Sleep) wakeUp();
    qDebug() << "DE1Simulator: Starting clean";
    m_descaleStep = 13;  // CleanInit (clean substates are 13-16)
    m_descaleStepStart = 0.0;
    startOperation(DE1::State::Clean);
    setState(DE1::State::Clean, static_cast<DE1::SubState>(m_descaleStep));
}

void DE1Simulator::stop()
{
    qDebug() << "DE1Simulator: Stop requested";
    stopOperation();
}

void DE1Simulator::goToSleep()
{
    stopOperation();
    setState(DE1::State::Sleep, DE1::SubState::Ready);
    m_groupTemp = 20.0;
    m_mixTemp = 20.0;
}

void DE1Simulator::wakeUp()
{
    if (m_state == DE1::State::Sleep) {
        qDebug() << "DE1Simulator: Waking up";
        // Machine heats up when waking - simulate already heated state
        // (real machine would go through Heating phase, but for UX we skip that)
        m_groupTemp = 93.0;
        m_mixTemp = 91.5;
        setState(DE1::State::Idle, DE1::SubState::Ready);
    }
}

void DE1Simulator::startOperation(DE1::State state)
{
    Q_UNUSED(state);
    m_running = true;
    m_shotTimer.start();
    m_operationTimer.start();
    m_tickTimer.start();
    emit runningChanged();
}

void DE1Simulator::stopOperation()
{
    m_tickTimer.stop();
    m_running = false;
    m_pressure = 0.0;
    m_flow = 0.0;
    m_pressureVelocity = 0.0;
    m_flowVelocity = 0.0;

    setState(DE1::State::Idle, DE1::SubState::Ready);
    emit runningChanged();
}

void DE1Simulator::onSimulationTimerTick()
{
    double elapsed = m_operationTimer.elapsed() / 1000.0;
    double dt = TICK_INTERVAL_MS / 1000.0;

    if (m_state == DE1::State::Espresso) {
        if (m_subState == DE1::SubState::Ending) {
            executeEnding(dt);
        } else {
            executeFrame();
        }
    } else if (m_state == DE1::State::Steam) {
        // Steam simulation with realistic pressure, flow, and temperature curves
        m_steamTemp = 140.0 + fractalNoise(elapsed * 0.5, 2) * 3.0;
        m_pressure = 1.5 + fractalNoise(elapsed * 2.0, 2) * 0.3;
        m_flow = 1.2 + fractalNoise(elapsed * 1.5, 2) * 0.2;

        // Emit shot samples at 5Hz so SteamDataModel receives data for the graph
        if (m_tickCount % 2 == 0) {
            ShotSample sample;
            sample.timestamp = QDateTime::currentMSecsSinceEpoch();
            sample.timer = m_shotTimer.elapsed() / 1000.0;
            sample.steamTemp = m_steamTemp;
            sample.groupPressure = m_pressure;
            sample.groupFlow = m_flow;
            emit shotSampleReceived(sample);
        }
    } else if (m_state == DE1::State::HotWater || m_state == DE1::State::HotWaterRinse) {
        // Simple hot water / flush simulation
        m_flow = 40.0 + fractalNoise(elapsed * 1.0, 2) * 5.0;
        m_pressure = 2.0 + fractalNoise(elapsed * 1.5, 2) * 0.3;

        // Accumulate volume and emit weight (water density ~1.0 g/mL)
        m_outputVolume += m_flow * dt;
        m_scaleWeight = m_outputVolume;  // 1:1 for water
        double scaleNoise = fractalNoise(elapsed * 2.0 + 200, 2) * SCALE_NOISE_AMP;
        emit scaleWeightChanged(qMax(0.0, m_scaleWeight + scaleNoise));
    } else if (m_state == DE1::State::Descale || m_state == DE1::State::Clean) {
        // Descale: substates 8→12 (5 steps), Clean: substates 13→16 (4 steps)
        // Each step lasts ~70 seconds on real hardware (~6 min total)
        // Simulator uses shorter steps for faster testing
        static constexpr double DESCALE_STEP_DURATION = 15.0;  // seconds per step

        int maxStep = (m_state == DE1::State::Descale) ? 12 : 16;
        double stepElapsed = elapsed - m_descaleStepStart;
        if (stepElapsed >= DESCALE_STEP_DURATION) {
            if (m_descaleStep < maxStep) {
                m_descaleStep++;
                m_descaleStepStart = elapsed;
                setState(m_state, static_cast<DE1::SubState>(m_descaleStep));
                qDebug() << "DE1Simulator: Descale/clean step" << m_descaleStep;
            } else {
                // All steps complete
                qDebug() << "DE1Simulator: Descale/clean complete";
                stopOperation();
                return;
            }
        }

        // Simulate water flow through system
        m_flow = 3.0 + fractalNoise(elapsed * 0.8, 2) * 1.0;
        m_pressure = 1.5 + fractalNoise(elapsed * 1.2, 2) * 0.5;
    }

    // Send shot samples at 5Hz (every other tick at 10Hz)
    m_tickCount++;
    if (m_tickCount % 2 == 0 && m_state == DE1::State::Espresso) {
        ShotSample sample;
        sample.timestamp = QDateTime::currentMSecsSinceEpoch();
        sample.timer = m_shotTimer.elapsed() / 1000.0;
        sample.groupPressure = m_pressure;
        sample.groupFlow = m_flow;
        sample.mixTemp = m_mixTemp;
        sample.headTemp = m_groupTemp;
        sample.steamTemp = m_steamTemp;
        sample.frameNumber = m_currentFrameIndex;

        // Get goals from current frame
        if (m_currentFrameIndex < m_profile.steps().size()) {
            const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];
            sample.setTempGoal = frame.temperature;
            if (frame.isFlowControl()) {
                sample.setFlowGoal = frame.flow;
                sample.setPressureGoal = 0;
            } else {
                sample.setPressureGoal = frame.pressure;
                sample.setFlowGoal = 0;
            }
        }

        emit shotSampleReceived(sample);
    }
}

void DE1Simulator::executeFrame()
{
    if (m_profile.steps().isEmpty()) {
        qDebug() << "DE1Simulator: No profile frames!";
        stopOperation();
        return;
    }

    double shotTime = m_shotTimer.elapsed() / 1000.0;
    double dt = TICK_INTERVAL_MS / 1000.0;

    // ========== PREHEAT PHASE (valve closed, building pressure in plumbing) ==========
    if (m_subState == DE1::SubState::Heating || m_subState == DE1::SubState::Stabilising) {
        double targetTemp = m_profile.steps().first().temperature;

        // Heat up with realistic thermal response
        double tempDiff = targetTemp - m_groupTemp;
        if (tempDiff > 0) {
            double heatRate = TEMP_RISE_RATE * (1.0 + 0.3 * fractalNoise(shotTime * 0.3, 2));
            m_groupTemp += qMin(tempDiff, heatRate * dt);
        }
        m_mixTemp = m_groupTemp - 1.0 - fractalNoise(shotTime * 0.5, 2) * 0.5;

        // VALVE IS CLOSED - pump pushes water into plumbing, building pressure
        // Target pressure from first profile frame (or default 4 bar for preinfusion)
        double targetPreheatPressure = 4.0;
        if (!m_profile.steps().isEmpty()) {
            const auto& firstFrame = m_profile.steps().first();
            targetPreheatPressure = firstFrame.isFlowControl() ? 4.0 : firstFrame.pressure;
        }
        targetPreheatPressure = qBound(2.0, targetPreheatPressure, 9.0);

        // Pump spin-up: takes ~1.5 seconds to get up to speed (starts at 0)
        // Use smooth ease-in curve: t^2 for natural acceleration
        double pumpSpinUpTime = 1.5;
        double spinUpProgress = qBound(0.0, shotTime / pumpSpinUpTime, 1.0);
        double pumpSpeedFactor = spinUpProgress * spinUpProgress;  // Ease-in (slow start)

        // Flow increases as pump spins up
        double targetFlow = PREHEAT_PUMP_FLOW * pumpSpeedFactor;

        // Once we reach target pressure, pump backs off
        if (m_plumbingPressure >= targetPreheatPressure) {
            targetFlow = 0.0;  // Target reached, hold pressure
        }

        m_flow = targetFlow * (1.0 + fractalNoise(shotTime * 2.0, 2) * 0.05);
        m_plumbingVolume += m_flow * dt;

        // Pressure builds based on accumulated volume and plumbing compliance
        // P = V / compliance (like a spring: more water = more pressure)
        m_plumbingPressure = m_plumbingVolume / PLUMBING_COMPLIANCE;

        // Pump can only push so hard - pressure is limited
        if (m_plumbingPressure > MAX_PRESSURE * 0.8) {
            m_plumbingPressure = MAX_PRESSURE * 0.8;
            m_flow = 0.0;
        }

        // Report the plumbing pressure as measured pressure
        m_pressure = m_plumbingPressure + fractalNoise(shotTime * 3.0, 2) * 0.15;
        m_pressure = qBound(0.0, m_pressure, MAX_PRESSURE);

        if (m_groupTemp >= targetTemp - 1.5) {
            setState(DE1::State::Espresso, DE1::SubState::Stabilising);
        }

        // After preheat duration, open the valve
        if (shotTime >= PREHEAT_DURATION && m_groupTemp >= targetTemp - 2.0) {
            m_valveOpen = true;
            m_frameStartTime = shotTime;
            m_waterInPuck = true;
            qDebug() << "DE1Simulator: Valve opening, pressure=" << m_plumbingPressure
                     << "bar, releasing into puck";

            if (m_profile.preinfuseFrameCount() > 0) {
                setState(DE1::State::Espresso, DE1::SubState::Preinfusion);
            } else {
                setState(DE1::State::Espresso, DE1::SubState::Pouring);
            }
        }
        return;
    }

    // ========== EXTRACTION PHASE ==========
    if (m_currentFrameIndex >= m_profile.steps().size()) {
        qDebug() << "DE1Simulator: Shot complete (all frames done)";
        m_endingStartTime = m_shotTimer.elapsed() / 1000.0;
        setState(DE1::State::Espresso, DE1::SubState::Ending);
        return;  // executeEnding() will handle pressure decay
    }

    const ProfileFrame& frame = m_profile.steps()[m_currentFrameIndex];
    double frameTime = shotTime - m_frameStartTime;
    double extractionTime = shotTime - PREHEAT_DURATION;

    // ========== PUCK RESISTANCE ==========
    m_baseResistance = simulatePuckResistance(extractionTime, m_totalVolume);

    // Apply channeling effects
    double channelFactor = channelNoise(shotTime);

    // Apply coherent noise for natural variation
    double resistanceNoise = 1.0 + fractalNoise(shotTime * 0.8, 3) * NOISE_RESISTANCE_AMP;

    m_puckResistance = m_baseResistance * channelFactor * resistanceNoise;
    m_puckResistance = qBound(MIN_RESISTANCE * 0.8, m_puckResistance, PEAK_RESISTANCE * 1.2);

    // ========== TEMPERATURE ==========
    double targetTemp = frame.temperature;
    double tempDiff = targetTemp - m_groupTemp;

    // Temperature changes slowly due to thermal mass
    if (qAbs(tempDiff) > 0.1) {
        double rate = (tempDiff > 0) ? TEMP_RISE_RATE : -TEMP_FALL_RATE;
        double maxChange = rate * dt;
        double change = qBound(-qAbs(maxChange), tempDiff * TEMP_APPROACH_RATE, qAbs(maxChange));
        m_groupTemp += change;
    }
    // Add subtle noise
    double tempNoise = fractalNoise(shotTime * 0.4, 2) * 0.3;
    m_mixTemp = m_groupTemp - 1.5 + tempNoise;

    // ========== PRESSURE/FLOW CONTROL ==========
    // Physical limit: max flow possible through puck at pump's max pressure
    double maxPuckFlow = calculateFlow(MAX_PRESSURE, m_puckResistance);

    if (frame.isFlowControl()) {
        // Flow control mode: we set flow, pressure follows
        m_targetFlow = frame.flow;

        // Smooth transitions
        if (frame.transition == "smooth" && frameTime < frame.seconds && frame.seconds > 0) {
            double progress = frameTime / frame.seconds;
            double startFlow = (m_currentFrameIndex > 0) ?
                m_profile.steps()[m_currentFrameIndex - 1].flow : 0;
            m_targetFlow = startFlow + (frame.flow - startFlow) * progress;
        }

        // Limit target flow to what's physically possible through puck
        m_targetFlow = qMin(m_targetFlow, maxPuckFlow);

        // Flow approaches target with inertia (second-order response)
        double flowError = m_targetFlow - m_flow;
        double flowAccel = flowError / FLOW_INERTIA - m_flowVelocity * 2.0;  // Damped spring
        m_flowVelocity += flowAccel * dt;
        m_flow += m_flowVelocity * dt;

        // Calculate resulting pressure from flow and resistance
        m_pressure = calculatePressure(m_flow, m_puckResistance);

        // Apply limiter
        if (frame.maxFlowOrPressure > 0 && m_pressure > frame.maxFlowOrPressure) {
            m_pressure = frame.maxFlowOrPressure;
            m_flow = calculateFlow(m_pressure, m_puckResistance);
        }
    } else {
        // Pressure control mode: we set pressure, flow follows
        m_targetPressure = frame.pressure;

        // Smooth transitions
        if (frame.transition == "smooth" && frameTime < frame.seconds && frame.seconds > 0) {
            double progress = frameTime / frame.seconds;
            double startPressure = (m_currentFrameIndex > 0) ?
                m_profile.steps()[m_currentFrameIndex - 1].pressure : 0;
            m_targetPressure = startPressure + (frame.pressure - startPressure) * progress;
        }

        // Pressure approaches target with inertia (second-order response)
        double pressureError = m_targetPressure - m_pressure;
        double pressureAccel = pressureError / PRESSURE_INERTIA - m_pressureVelocity * 2.0;
        m_pressureVelocity += pressureAccel * dt;
        m_pressure += m_pressureVelocity * dt;

        // Flow is determined by puck resistance (Darcy's law)
        m_flow = calculateFlow(m_pressure, m_puckResistance);

        // Apply limiter
        if (frame.maxFlowOrPressure > 0 && m_flow > frame.maxFlowOrPressure) {
            m_flow = frame.maxFlowOrPressure;
            m_pressure = calculatePressure(m_flow, m_puckResistance);
        }
    }

    // ========== APPLY LIMITS AND NOISE ==========
    // Clamp to physical limits - flow limited by puck resistance
    m_pressure = qBound(0.0, m_pressure, MAX_PRESSURE);
    m_flow = qBound(0.0, m_flow, qMin(MAX_FLOW, maxPuckFlow));

    // Add measurement noise (coherent, not white noise)
    double pressureNoise = fractalNoise(shotTime * 5.0, 2) * NOISE_PRESSURE_AMP;
    double flowNoise = fractalNoise(shotTime * 4.0 + 100, 2) * NOISE_FLOW_AMP;

    m_pressure = qMax(0.0, m_pressure + pressureNoise);
    m_flow = qMax(0.0, m_flow + flowNoise);

    // ========== VOLUME TRACKING ==========
    m_frameVolume += m_flow * dt;
    m_totalVolume += m_flow * dt;

    // ========== YIELD (SCALE WEIGHT) ==========
    if (!m_puckFilled && m_totalVolume >= PUCK_FILL_VOLUME) {
        m_puckFilled = true;
        qDebug() << "DE1Simulator: Puck saturated, coffee starting to drip";
    }

    if (m_puckFilled) {
        // Yield efficiency follows S-curve
        double extractionProgress = qMin(1.0, m_outputVolume / EFFICIENCY_RAMP_ML);
        // Smoothstep function: 3x² - 2x³
        double sCurve = extractionProgress * extractionProgress * (3.0 - 2.0 * extractionProgress);
        double efficiency = DRIP_START_EFFICIENCY + (DRIP_MAX_EFFICIENCY - DRIP_START_EFFICIENCY) * sCurve;

        // Pressure affects drip rate
        double pressureFactor = 0.8 + 0.2 * qMin(1.0, m_pressure / 9.0);

        double outputFlow = m_flow * efficiency * pressureFactor;
        m_outputVolume += outputFlow * dt;

        // Convert to weight with slight density increase from dissolved solids
        m_scaleWeight = m_outputVolume * COFFEE_DENSITY;

        // Add scale noise
        double scaleNoise = fractalNoise(shotTime * 2.0 + 200, 2) * SCALE_NOISE_AMP;
        double reportedWeight = qMax(0.0, m_scaleWeight + scaleNoise);

        emit scaleWeightChanged(reportedWeight);
    }

    // ========== FRAME TRANSITIONS ==========
    if (checkExitCondition(frame)) {
        advanceToNextFrame();
    }

    if (frameTime >= frame.seconds && frame.seconds > 0) {
        qDebug() << "DE1Simulator: Frame" << m_currentFrameIndex << "timeout";
        advanceToNextFrame();
    }

    if (frame.volume > 0 && m_frameVolume >= frame.volume) {
        qDebug() << "DE1Simulator: Frame" << m_currentFrameIndex << "volume reached";
        advanceToNextFrame();
    }
}

void DE1Simulator::executeEnding(double dt)
{
    // Simulate pressure bleeding off through the puck after pump stops
    // The headspace water above the puck is under pressure and must drain through
    // This creates the characteristic slow drip of rich, oily coffee at the end

    double shotTime = m_shotTimer.elapsed() / 1000.0;

    // Flow is driven by remaining pressure through puck resistance (Darcy's law)
    // As pressure drops, flow slows - creating the slow drip effect
    m_flow = calculateFlow(m_pressure, m_puckResistance);

    // Pressure decays as water volume drains from headspace
    // dP/dt = -flow / headspace_volume * pressure_per_ml
    // Simplified: pressure drops proportionally to flow rate
    double pressureLossRate = m_flow / HEADSPACE_VOLUME * m_pressure;
    m_pressure -= pressureLossRate * dt;

    // Add subtle noise to make it look natural
    double pressureNoise = fractalNoise(shotTime * 3.0, 2) * 0.03;
    m_pressure = qMax(0.0, m_pressure + pressureNoise);

    // Clamp flow to realistic minimum
    m_flow = qMax(0.0, m_flow);

    // Continue tracking volume and yield - this is the rich, oily stuff
    if (m_flow > 0.01) {
        m_totalVolume += m_flow * dt;

        if (m_puckFilled) {
            // During ending, efficiency is high - the water has been in contact longer
            double efficiency = DRIP_MAX_EFFICIENCY * 0.95;  // Slightly lower as pressure drops
            double outputFlow = m_flow * efficiency;
            m_outputVolume += outputFlow * dt;

            m_scaleWeight = m_outputVolume * COFFEE_DENSITY;

            // Scale noise
            double scaleNoise = fractalNoise(shotTime * 2.0 + 200, 2) * SCALE_NOISE_AMP;
            emit scaleWeightChanged(qMax(0.0, m_scaleWeight + scaleNoise));
        }
    }

    // Shot samples are sent by onSimulationTimerTick() - no need to duplicate here

    // End simulation when pressure has fully bled off or timeout reached
    double endingTime = shotTime - m_endingStartTime;
    if (m_pressure < MIN_ENDING_PRESSURE) {
        qDebug() << "DE1Simulator: Pressure bled off, shot complete";
        stopOperation();
    } else if (endingTime > MAX_ENDING_TIME) {
        qDebug() << "DE1Simulator: Ending timeout, shot complete";
        stopOperation();
    }
}

bool DE1Simulator::checkExitCondition(const ProfileFrame& frame)
{
    if (!frame.exitIf) return false;

    if (frame.exitType == "pressure_over" && m_pressure > frame.exitPressureOver) {
        qDebug() << "DE1Simulator: Exit condition - pressure over" << frame.exitPressureOver;
        return true;
    }
    if (frame.exitType == "pressure_under" && m_pressure < frame.exitPressureUnder && m_pressure > 0.5) {
        qDebug() << "DE1Simulator: Exit condition - pressure under" << frame.exitPressureUnder;
        return true;
    }
    if (frame.exitType == "flow_over" && m_flow > frame.exitFlowOver) {
        qDebug() << "DE1Simulator: Exit condition - flow over" << frame.exitFlowOver;
        return true;
    }
    if (frame.exitType == "flow_under" && m_flow < frame.exitFlowUnder && m_flow > 0.1) {
        qDebug() << "DE1Simulator: Exit condition - flow under" << frame.exitFlowUnder;
        return true;
    }

    return false;
}

void DE1Simulator::advanceToNextFrame()
{
    int prevFrame = m_currentFrameIndex;
    double prevFrameDuration = (prevFrame >= 0 && prevFrame < m_profile.steps().size())
        ? m_profile.steps()[prevFrame].seconds : 0;
    double actualTime = m_shotTimer.elapsed() / 1000.0 - m_frameStartTime;

    m_currentFrameIndex++;
    m_frameStartTime = m_shotTimer.elapsed() / 1000.0;
    m_frameVolume = 0.0;

    qDebug() << "DE1Simulator: Frame" << prevFrame << "ended after" << actualTime
             << "sec (expected:" << prevFrameDuration << "sec)";

    if (m_currentFrameIndex >= m_profile.steps().size()) {
        qDebug() << "DE1Simulator: All" << m_profile.steps().size() << "frames complete, entering Ending";
        m_endingStartTime = m_shotTimer.elapsed() / 1000.0;
        setState(DE1::State::Espresso, DE1::SubState::Ending);
        return;  // executeEnding() will handle pressure decay
    }

    const auto& nextFrame = m_profile.steps()[m_currentFrameIndex];
    qDebug() << "DE1Simulator: Starting frame" << m_currentFrameIndex
             << "-" << nextFrame.name << "(duration:" << nextFrame.seconds << "sec)";

    if (m_currentFrameIndex < m_profile.preinfuseFrameCount()) {
        setState(DE1::State::Espresso, DE1::SubState::Preinfusion);
    } else {
        setState(DE1::State::Espresso, DE1::SubState::Pouring);
    }
}

double DE1Simulator::simulatePuckResistance(double timeInExtraction, double totalWater)
{
    // Scale resistance based on dose and grind
    // More coffee = more resistance, finer grind = more resistance
    double doseFactor = m_dose / REFERENCE_DOSE;
    double combinedFactor = doseFactor * m_grindFactor;

    if (timeInExtraction < 0) {
        return BASELINE_RESISTANCE * combinedFactor;
    }

    // Phase 1: Puck swelling as coffee absorbs water
    // Based on Coffee ad Astra research - resistance peaks then declines
    double swellingFactor = 1.0;
    if (timeInExtraction < SWELLING_TIME) {
        // Resistance rises as coffee particles swell
        double swellProgress = timeInExtraction / SWELLING_TIME;
        // Sine curve for smooth rise
        swellingFactor = 1.0 + (PEAK_RESISTANCE / BASELINE_RESISTANCE - 1.0) *
                         qSin(swellProgress * M_PI / 2);
    } else {
        // After peak, maintain elevated resistance briefly
        double timePastPeak = timeInExtraction - SWELLING_TIME;
        double decayStart = qExp(-timePastPeak * 0.3);  // Gradual transition
        swellingFactor = 1.0 + (PEAK_RESISTANCE / BASELINE_RESISTANCE - 1.0) * decayStart;
    }

    // Phase 2: Oil extraction causing resistance decline
    // Research shows 2-3.5x decline over full extraction
    double degradation = 1.0 - (totalWater * DEGRADATION_RATE);
    degradation = qMax(MIN_RESISTANCE / BASELINE_RESISTANCE, degradation);

    // Combine all factors
    double resistance = BASELINE_RESISTANCE * combinedFactor * swellingFactor * degradation;

    // Clamp to physical limits (scaled by dose/grind)
    // Very fine grind + high dose can choke the machine (resistance -> infinity, flow -> 0)
    return qBound(MIN_RESISTANCE * combinedFactor, resistance, PEAK_RESISTANCE * combinedFactor * 1.5);
}

double DE1Simulator::calculateFlow(double pressure, double resistance)
{
    // Darcy's law: Q = k * P / R
    if (resistance <= 0) return 0;
    return DARCY_K * pressure / resistance;
}

double DE1Simulator::calculatePressure(double flow, double resistance)
{
    // Inverse Darcy's law: P = Q * R / k
    return flow * resistance / DARCY_K;
}
