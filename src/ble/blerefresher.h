#ifndef BLEREFRESHER_H
#define BLEREFRESHER_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

class DE1Device;
class BLEManager;
class MachineState;
class Settings;

// Periodically cycles BLE connections (disconnect + reconnect) to prevent
// Android Bluetooth stack degradation over long uptimes.
//
// Triggers:
// 1. Wake from sleep: When DE1 wakes from Sleep (detected internally)
// 2. Periodic fallback: Every N hours if the machine never sleeps
//
// Safety: Never refreshes during flowing/operating phases.
class BleRefresher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
public:
    explicit BleRefresher(DE1Device* de1, BLEManager* bleManager,
                          MachineState* machineState, Settings* settings,
                          QObject* parent = nullptr);

    bool refreshing() const { return m_refreshInProgress; }

    // Start periodic refresh timer (for machines that never sleep)
    void startPeriodicRefresh(int intervalHours = 5);

signals:
    void refreshingChanged();

private:
    void scheduleRefresh(bool isWakeTrigger = false);
    void executeRefresh();
    void onRefreshComplete();

    DE1Device* m_de1;
    BLEManager* m_bleManager;
    MachineState* m_machineState;
    Settings* m_settings;

    QTimer m_periodicTimer;
    bool m_sleeping = false;
    bool m_refreshPending = false;
    bool m_refreshInProgress = false;
    bool m_de1WasConnected = false;
    bool m_scaleWasConnected = false;
    QElapsedTimer m_lastRefresh;
    QElapsedTimer m_de1ConnectedSince;  // Tracks current connection uptime
    bool m_de1ConnectedSinceValid = false;  // Whether m_de1ConnectedSince is running
    int m_periodicIntervalMs = 5 * 3600 * 1000;  // 5 hours default
    int m_scanRetryCount = 0;

    // Temporary connections for event-driven sequencing
    QMetaObject::Connection m_phaseConn;
    QMetaObject::Connection m_de1ConnConn;
    QMetaObject::Connection m_scanConn;
    QMetaObject::Connection m_de1UptimeConn;

    static constexpr int MIN_REFRESH_INTERVAL_MS = 60 * 60 * 1000;  // 60 minute debounce
    static constexpr int WAKE_MIN_UPTIME_MS = 2 * 3600 * 1000;  // 2 hours
    static constexpr int MAX_SCAN_RETRIES = 2;  // 3 total scans (initial + 2 retries)
};

#endif // BLEREFRESHER_H
