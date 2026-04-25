#pragma once

#include <QObject>
#include <QTimer>
#include <QDate>
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

signals:
    /// Emitted when the machine should be woken up
    void wakeRequested();

private slots:
    void onTimerFired();

private:
    void scheduleNextWake();
    SettingsAutoWake* m_settings;
    QTimer* m_checkTimer;

    // Track which days we've already triggered (0=Monday, 6=Sunday)
    // Key: dayOfWeek (0-6), Value: date when last triggered
    QMap<int, QDate> m_lastTriggeredDates;
};
