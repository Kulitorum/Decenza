#pragma once

// Whole file compiles to nothing without DECENZA_SIMULATOR — the same guard, for the same
// reason, as de1simulator.h: this class drives DE1Simulator's signals and cannot exist without
// it. That condition is every desktop configuration plus Debug on Android and iOS, so a tablet
// production build has neither. (moc then reports "No relevant classes found" for this header on
// those builds; that note is expected, not a fault.)
#ifdef DECENZA_SIMULATOR

#include <QObject>
#include <QColor>
#include <QVariantList>

class DE1Device;
class DE1Simulator;

// GHC Simulator - Virtual Group Head Controller for debugging
// Shows 5 buttons and 12 RGB LEDs in a ring like the real GHC
class GHCSimulator : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList ledColors READ ledColors NOTIFY ledColorsChanged)
    Q_PROPERTY(bool stopPressed READ stopPressed NOTIFY stopPressedChanged)
    Q_PROPERTY(ActiveFunction activeFunction READ activeFunction NOTIFY activeFunctionChanged)

public:
    enum class ActiveFunction {
        None,
        Espresso,
        Steam,
        HotWater,
        Flush
    };
    Q_ENUM(ActiveFunction)

    explicit GHCSimulator(QObject* parent = nullptr);

    void setDE1Device(DE1Device* device);
    void setDE1Simulator(DE1Simulator* simulator);

    QVariantList ledColors() const;
    bool stopPressed() const { return m_stopPressed; }
    ActiveFunction activeFunction() const { return m_activeFunction; }

public slots:
    // Button press handlers (called from QML)
    void pressEspresso();
    void pressSteam();
    void pressHotWater();
    void pressFlush();
    void pressStop();
    void releaseStop();

    // Window management - called when a window becomes active
    // Uses a flag to prevent infinite loop when raising triggers activation
    void mainWindowActivated();
    void ghcWindowActivated();

    // Called when shot data arrives
    void onShotSample(double pressure, double flow);

    // Called when machine state changes
    void onStateChanged();
    void onSimulatorStateChanged();

signals:
    void ledColorsChanged();
    void stopPressedChanged();
    void activeFunctionChanged();
    void raiseMainWindow();
    void raiseGhcWindow();

private:
    void setAllLeds(const QColor& color);
    void setLedRange(int start, int count, const QColor& color);
    void updateFunctionLeds();
    void updateEspressoLeds(double pressure, double flow);

    // LED positions (clockwise from 12:00):
    // 0=12:00, 1=1:00, 2=2:00, 3=3:00, 4=4:00, 5=5:00
    // 6=6:00, 7=7:00, 8=8:00, 9=9:00, 10=10:00, 11=11:00
    //
    // Button LED groups (3 LEDs closest to each button):
    // HotWater (top): 11, 0, 1
    // Steam (right): 2, 3, 4
    // Espresso (bottom): 5, 6, 7
    // Flush (left): 8, 9, 10
    static constexpr int LED_COUNT = 12;

    QColor m_leds[LED_COUNT];
    DE1Device* m_device = nullptr;
    DE1Simulator* m_simulator = nullptr;
    bool m_stopPressed = false;
    ActiveFunction m_activeFunction = ActiveFunction::None;
    bool m_isRaisingWindows = false;  // Guard to prevent raise loop

    // Pressure/flow scaling
    static constexpr double MAX_PRESSURE = 12.0;  // bar
    static constexpr double MAX_FLOW = 6.0;       // ml/s
};

#endif  // DECENZA_SIMULATOR
