#pragma once

#include <QObject>
#include <QTimer>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QMap>

class SettingsAutoWake;

/**
 * @brief Manages automatic wake-up scheduling for the DE1 machine.
 *
 * Wake scheduling uses a single-shot QTimer armed to the exact next
 * enabled wake time:
 * - scheduleNextWake() scans up to 8 days ahead for the nearest enabled
 *   day and arms the timer to that precise delta
 * - On fire, emits wakeRequested(), records the day in
 *   m_lastTriggeredDates to avoid duplicate triggers, then reschedules
 * - Reschedules automatically when the schedule setting changes
 *
 * Separately, isWithinStayAwakeWindow() answers "is the machine inside a
 * scheduled stay-awake window right now?" as a pure function of the
 * schedule + current wall clock (no internal timer/state), so callers can
 * keep the machine awake across app restarts and missed wake ticks
 * (Kulitorum/Decenza#1203).
 */
class AutoWakeManager : public QObject {
    Q_OBJECT

public:
    explicit AutoWakeManager(SettingsAutoWake* settings, QObject* parent = nullptr);

    /// Start the wake schedule checker (call after app initialization)
    void start();

    /// Stop the wake schedule checker
    void stop();

    /// True if the current local time falls inside an enabled day's
    /// scheduled stay-awake window [wakeTime, wakeTime + stayAwakeMinutes).
    /// Evaluated continuously from the schedule + wall clock (not armed by
    /// the wake event), so it stays correct across app restarts, manual
    /// wakes, and process suspension. Also checks the previous day so a
    /// window that began before midnight and spills into the next day is
    /// still honored. Returns false when stay-awake is disabled, the
    /// duration is non-positive, or no scheduled day is active right now.
    Q_INVOKABLE bool isWithinStayAwakeWindow() const;

signals:
    /// Emitted when the machine should be woken up
    void wakeRequested();

private slots:
    void onTimerFired();

private:
    void scheduleNextWake();

    // Pure date-math core of isWithinStayAwakeWindow(), with the clock
    // injected so the midnight-spanning logic is unit-testable.
    bool isWithinStayAwakeWindowAt(const QDateTime& now) const;

    SettingsAutoWake* m_settings;
    QTimer* m_checkTimer;

    // Track which days we've already triggered (0=Monday, 6=Sunday)
    // Key: dayOfWeek (0-6), Value: date when last triggered
    QMap<int, QDate> m_lastTriggeredDates;

#ifdef DECENZA_TESTING
    friend class tst_AutoWakeWindow;
#endif
};
