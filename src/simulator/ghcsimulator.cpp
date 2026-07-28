#include "ghcsimulator.h"

#ifdef DECENZA_SIMULATOR
#include "de1simulator.h"
#include "../ble/de1device.h"
#include <QDebug>

GHCSimulator::GHCSimulator(QObject* parent)
    : QObject(parent)
{
    // Initialize all LEDs to off (dark gray)
    setAllLeds(QColor(30, 30, 30));
}

void GHCSimulator::mainWindowActivated()
{
    // Prevent infinite loop: main activates -> raises GHC -> GHC activates -> raises main -> ...
    if (m_isRaisingWindows) return;
    m_isRaisingWindows = true;
    emit raiseGhcWindow();
    // Reset flag after a short delay to allow the raise to complete
    QMetaObject::invokeMethod(this, [this]() { m_isRaisingWindows = false; }, Qt::QueuedConnection);
}

void GHCSimulator::ghcWindowActivated()
{
    if (m_isRaisingWindows) return;
    m_isRaisingWindows = true;
    emit raiseMainWindow();
    QMetaObject::invokeMethod(this, [this]() { m_isRaisingWindows = false; }, Qt::QueuedConnection);
}

void GHCSimulator::setDE1Device(DE1Device* device)
{
    if (m_device) {
        disconnect(m_device, nullptr, this, nullptr);
    }

    m_device = device;

    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived, this,
                [this](const ShotSample& sample) {
            onShotSample(sample.groupPressure, sample.groupFlow);
        });
        connect(m_device, &DE1Device::stateChanged, this, &GHCSimulator::onStateChanged);
    }
}

void GHCSimulator::setDE1Simulator(DE1Simulator* simulator)
{
    if (m_simulator) {
        disconnect(m_simulator, nullptr, this, nullptr);
    }

    m_simulator = simulator;

    if (m_simulator) {
        connect(m_simulator, &DE1Simulator::shotSampleReceived, this,
                [this](const ShotSample& sample) {
            onShotSample(sample.groupPressure, sample.groupFlow);
        });
        connect(m_simulator, &DE1Simulator::stateChanged, this, [this]() {
            onSimulatorStateChanged();
        });
    }
}

void GHCSimulator::onSimulatorStateChanged()
{
    if (!m_simulator || m_stopPressed) {
        return;
    }

    DE1::State state = m_simulator->state();
    ActiveFunction newFunction = ActiveFunction::None;

    // Reset all LEDs to off
    setAllLeds(QColor(30, 30, 30));

    switch (state) {
    case DE1::State::Espresso:
        newFunction = ActiveFunction::Espresso;
        // Espresso uses special pressure/flow display - handled in onShotSample
        break;

    case DE1::State::Steam:
        newFunction = ActiveFunction::Steam;
        setLedRange(2, 3, QColor(100, 150, 255));
        emit ledColorsChanged();
        break;

    case DE1::State::HotWater:
        newFunction = ActiveFunction::HotWater;
        m_leds[11] = QColor(255, 200, 100);
        m_leds[0] = QColor(255, 200, 100);
        m_leds[1] = QColor(255, 200, 100);
        emit ledColorsChanged();
        break;

    case DE1::State::HotWaterRinse:
        newFunction = ActiveFunction::Flush;
        setLedRange(8, 3, QColor(100, 200, 255));
        emit ledColorsChanged();
        break;

    default:
        break;
    }

    if (newFunction != m_activeFunction) {
        m_activeFunction = newFunction;
        emit activeFunctionChanged();
    }
}

QVariantList GHCSimulator::ledColors() const
{
    QVariantList colors;
    for (int i = 0; i < LED_COUNT; ++i) {
        colors.append(m_leds[i]);
    }
    return colors;
}

void GHCSimulator::setAllLeds(const QColor& color)
{
    for (int i = 0; i < LED_COUNT; ++i) {
        m_leds[i] = color;
    }
    emit ledColorsChanged();
}

void GHCSimulator::setLedRange(int start, int count, const QColor& color)
{
    for (int i = 0; i < count; ++i) {
        int index = (start + i) % LED_COUNT;
        m_leds[index] = color;
    }
}

void GHCSimulator::pressEspresso()
{
    if (m_simulator) {
        m_simulator->startEspresso();
    } else if (m_device) {
        m_device->startEspresso();
    }
}

void GHCSimulator::pressSteam()
{
    if (m_simulator) {
        m_simulator->startSteam();
    } else if (m_device) {
        m_device->startSteam();
    }
}

void GHCSimulator::pressHotWater()
{
    if (m_simulator) {
        m_simulator->startHotWater();
    } else if (m_device) {
        m_device->startHotWater();
    }
}

void GHCSimulator::pressFlush()
{
    if (m_simulator) {
        m_simulator->startFlush();
    } else if (m_device) {
        m_device->startFlush();
    }
}

void GHCSimulator::pressStop()
{
    m_stopPressed = true;
    emit stopPressedChanged();

    // Show red LEDs when stop is pressed
    setAllLeds(QColor(255, 50, 50));

    if (m_simulator) {
        m_simulator->stop();
    } else if (m_device) {
        m_device->stopOperation();
    }
}

void GHCSimulator::releaseStop()
{
    m_stopPressed = false;
    emit stopPressedChanged();

    // Return to normal state
    if (m_simulator) {
        onSimulatorStateChanged();
    } else {
        onStateChanged();
    }
}

void GHCSimulator::onStateChanged()
{
    if (!m_device || m_stopPressed) {
        return;
    }

    DE1::State state = m_device->state();
    ActiveFunction newFunction = ActiveFunction::None;

    // Reset all LEDs to off
    setAllLeds(QColor(30, 30, 30));

    switch (state) {
    case DE1::State::Espresso:
        newFunction = ActiveFunction::Espresso;
        // Espresso uses special pressure/flow display - handled in onShotSample
        break;

    case DE1::State::Steam:
        newFunction = ActiveFunction::Steam;
        // Steam: light up LEDs 2, 3, 4 (right side, near steam button)
        setLedRange(2, 3, QColor(100, 150, 255));  // Light blue for steam
        emit ledColorsChanged();
        break;

    case DE1::State::HotWater:
        newFunction = ActiveFunction::HotWater;
        // Hot water: light up LEDs 11, 0, 1 (top, near hot water button)
        m_leds[11] = QColor(255, 200, 100);  // Warm orange
        m_leds[0] = QColor(255, 200, 100);
        m_leds[1] = QColor(255, 200, 100);
        emit ledColorsChanged();
        break;

    case DE1::State::HotWaterRinse:  // Flush
        newFunction = ActiveFunction::Flush;
        // Flush: light up LEDs 8, 9, 10 (left side, near flush button)
        setLedRange(8, 3, QColor(100, 200, 255));  // Cyan for flush
        emit ledColorsChanged();
        break;

    default:
        // Idle or other states - LEDs stay off
        break;
    }

    if (newFunction != m_activeFunction) {
        m_activeFunction = newFunction;
        emit activeFunctionChanged();
    }
}

void GHCSimulator::onShotSample(double pressure, double flow)
{
    if (m_stopPressed || m_activeFunction != ActiveFunction::Espresso) {
        return;
    }

    updateEspressoLeds(pressure, flow);
}

void GHCSimulator::updateEspressoLeds(double pressure, double flow)
{
    // Reset all LEDs to off
    for (int i = 0; i < LED_COUNT; ++i) {
        m_leds[i] = QColor(30, 30, 30);
    }

    // "Dot" style display like the real GHC - value shown as a position on the ring
    // If value falls between LEDs, brightness is distributed proportionally
    // e.g., value 4.5 shows LED 4 at 50% and LED 5 at 50%

    // Pressure position (0-12 bar maps to 0-12 LEDs)
    double pressurePos = qBound(0.0, (pressure / MAX_PRESSURE) * LED_COUNT, static_cast<double>(LED_COUNT - 1));
    int pressureLed1 = static_cast<int>(pressurePos);
    int pressureLed2 = (pressureLed1 + 1) % LED_COUNT;
    double pressureFrac = pressurePos - pressureLed1;

    // Flow position (0-6 ml/s maps to 0-12 LEDs)
    double flowPos = qBound(0.0, (flow / MAX_FLOW) * LED_COUNT, static_cast<double>(LED_COUNT - 1));
    int flowLed1 = static_cast<int>(flowPos);
    int flowLed2 = (flowLed1 + 1) % LED_COUNT;
    double flowFrac = flowPos - flowLed1;

    // Apply pressure (green) - distributed across adjacent LEDs
    if (pressure > 0.1) {
        int green1 = static_cast<int>(200 * (1.0 - pressureFrac));
        int green2 = static_cast<int>(200 * pressureFrac);
        m_leds[pressureLed1].setGreen(qMin(255, m_leds[pressureLed1].green() + green1));
        if (green2 > 10) {
            m_leds[pressureLed2].setGreen(qMin(255, m_leds[pressureLed2].green() + green2));
        }
    }

    // Apply flow (blue) - distributed across adjacent LEDs
    if (flow > 0.1) {
        int blue1 = static_cast<int>(200 * (1.0 - flowFrac));
        int blue2 = static_cast<int>(200 * flowFrac);
        m_leds[flowLed1].setBlue(qMin(255, m_leds[flowLed1].blue() + blue1));
        if (blue2 > 10) {
            m_leds[flowLed2].setBlue(qMin(255, m_leds[flowLed2].blue() + blue2));
        }
    }

    emit ledColorsChanged();
}

#endif  // DECENZA_SIMULATOR
