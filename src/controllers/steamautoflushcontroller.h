#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class DE1Device;
class MachineState;
class SettingsBrew;

// Owns the native steam auto-flush countdown. It is tied to the machine's
// actual Steam/Puffing state, so a page changing or disappearing cannot leave
// a timer behind that stops a later operation.
class SteamAutoFlushController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged FINAL)

public:
    explicit SteamAutoFlushController(SettingsBrew* brew, DE1Device* device,
                                      MachineState* machineState,
                                      QObject* parent = nullptr);

    double remainingSeconds() const { return m_remainingSeconds; }
    bool isPuffing() const;

#ifdef DECENZA_TESTING
    void setClockForTesting(std::function<qint64()> clock);
    void evaluateForTesting();
#endif

signals:
    void remainingSecondsChanged();
    void expired();

private:
    void refreshMachineState();
    void updateCountdown();
    void arm(int seconds);
    void cancel();
    qint64 nowMs() const;

    QPointer<SettingsBrew> m_brew;
    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;
    QElapsedTimer m_clock;
    QTimer m_timer;
    std::function<qint64()> m_testClock;
    qint64 m_deadlineMs = 0;
    double m_remainingSeconds = 0.0;
    bool m_wasPuffing = false;
    bool m_armed = false;
};
