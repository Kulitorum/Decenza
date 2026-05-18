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
 * Uses a "time passed" approach to ensure wake times are never missed:
 * - Checks every 30 seconds
 * - If current time >= target time AND we haven't triggered today, wake up
 * - Tracks last triggered date per day to avoid duplicate wake-ups
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
