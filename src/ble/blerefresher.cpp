#include "blerefresher.h"
#include "de1device.h"
#include "blemanager.h"
#include "scaledevice.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"

BleRefresher::BleRefresher(DE1Device* de1, BLEManager* bleManager,
                           MachineState* machineState, Settings* settings,
                           QObject* parent)
    : QObject(parent)
    , m_de1(de1)
    , m_bleManager(bleManager)
    , m_machineState(machineState)
    , m_settings(settings)
{
    m_periodicTimer.setSingleShot(true);
    connect(&m_periodicTimer, &QTimer::timeout, this, [this]() { scheduleRefresh(); });

    connect(m_settings, &Settings::bleHealthRefreshEnabledChanged, this, [this]() {
        if (!m_settings->bleHealthRefreshEnabled()) {
            m_periodicTimer.stop();
            m_refreshPending = false;
            qDebug() << "[BleRefresher] BLE health refresh disabled";
        } else if (m_periodicIntervalMs > 0 && !m_periodicTimer.isActive()) {
            m_periodicTimer.start(m_periodicIntervalMs);
            qDebug() << "[BleRefresher] BLE health refresh enabled";
        }
    });

    // Track DE1 connection uptime for wake trigger gating
    m_de1UptimeConn = connect(m_de1, &DE1Device::connectedChanged, this, [this]() {
        if (m_de1->isConnected()) {
            m_de1ConnectedSince.start();
            m_de1ConnectedSinceValid = true;
        } else {
            m_de1ConnectedSinceValid = false;
        }
    });

    // Detect wake from sleep: when phase leaves Sleep, schedule a refresh.
    // The 60-minute debounce prevents treating the initial Disconnected→Sleep→Idle
    // transition (normal first connect) as a wake-from-sleep event.
    connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
        auto phase = m_machineState->phase();
        if (phase == MachineState::Phase::Sleep) {
            m_sleeping = true;
        } else if (m_sleeping) {
            m_sleeping = false;
            if (m_settings->bleHealthRefreshEnabled()) {
                qDebug() << "[BleRefresher] Wake from sleep detected, scheduling BLE refresh";
                scheduleRefresh(/*isWakeTrigger=*/true);
            } else {
                qDebug() << "[BleRefresher] Wake from sleep detected, BLE health refresh disabled by setting";
            }
        }
    });

    m_lastRefresh.start();
}

void BleRefresher::startPeriodicRefresh(int intervalHours) {
    m_periodicIntervalMs = intervalHours * 3600 * 1000;
    if (!m_settings->bleHealthRefreshEnabled()) {
        qDebug() << "[BleRefresher] Periodic refresh disabled by setting";
        return;
    }
    m_periodicTimer.start(m_periodicIntervalMs);
    qDebug() << "[BleRefresher] Periodic refresh started, interval:" << intervalHours << "hours";
}

void BleRefresher::scheduleRefresh(bool isWakeTrigger) {
    if (!m_settings->bleHealthRefreshEnabled()) {
        qDebug() << "[BleRefresher] BLE health refresh disabled by setting";
        return;
    }

    auto rearmPeriodicCheck = [this]() {
        if (m_periodicIntervalMs > 0 && !m_periodicTimer.isActive()) {
            m_periodicTimer.start(m_periodicIntervalMs);
        }
    };

    if (m_refreshInProgress) {
        qDebug() << "[BleRefresher] Refresh already in progress, skipping";
        rearmPeriodicCheck();
        return;
    }

    // Wake triggers only make sense after long uptimes where BLE stack degrades.
    // Short connections (< 2 hours) don't need refreshing — skip to avoid disrupting
    // users who just connected after waking the tablet.
    if (isWakeTrigger) {
        if (!m_de1ConnectedSinceValid) {
            qDebug() << "[BleRefresher] Wake trigger skipped: DE1 not connected";
            rearmPeriodicCheck();
            return;
        }
        qint64 uptimeMs = m_de1ConnectedSince.elapsed();
        if (uptimeMs < WAKE_MIN_UPTIME_MS) {
            qDebug() << "[BleRefresher] Wake trigger skipped: connection uptime"
                     << uptimeMs / 1000 << "s < minimum" << WAKE_MIN_UPTIME_MS / 1000 << "s";
            rearmPeriodicCheck();
            return;
        }
        qDebug() << "[BleRefresher] Wake trigger accepted: connection uptime"
                 << uptimeMs / 1000 << "s";
    }

    // Periodic refresh should never run while machine is sleeping. Reconnecting BLE
    // during sleep can wake some DE1 setups unintentionally.
    if (m_machineState->phase() == MachineState::Phase::Sleep) {
        qDebug() << "[BleRefresher] Machine sleeping, skipping periodic BLE refresh";
        rearmPeriodicCheck();
        return;
    }

    // Debounce: skip if last refresh was very recent (rapid sleep/wake cycles)
    if (m_lastRefresh.elapsed() < MIN_REFRESH_INTERVAL_MS) {
        qDebug() << "[BleRefresher] Last refresh was" << m_lastRefresh.elapsed() / 1000
                 << "s ago, skipping (debounce)";
        rearmPeriodicCheck();
        return;
    }

    // Safety: defer if a shot/steam/flush is running
    if (m_machineState->isFlowing()) {
        qDebug() << "[BleRefresher] Operation in progress, deferring refresh";
        m_refreshPending = true;

        // Disconnect any previous deferred connection
        QObject::disconnect(m_phaseConn);

        // Wait for operation to end
        m_phaseConn = connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
            if (m_refreshPending && !m_machineState->isFlowing()) {
                QObject::disconnect(m_phaseConn);
                m_refreshPending = false;
                scheduleRefresh();
            }
        });
        return;
    }

    executeRefresh();
}

void BleRefresher::executeRefresh() {
    // No real BLE stack to refresh in simulation mode — isConnected() always
    // returns true so the disconnect/reconnect sequence can never complete.
    if (m_de1->simulationMode()) {
        qDebug() << "[BleRefresher] Skipping refresh in simulation mode (no real BLE stack)";
        // Restart periodic timer so it keeps scheduling (even though each attempt is a no-op)
        if (m_periodicIntervalMs > 0) {
            m_periodicTimer.start(m_periodicIntervalMs);
        }
        return;
    }

    m_refreshInProgress = true;
    emit refreshingChanged();
    m_de1WasConnected = m_de1->isConnected();
    m_scaleWasConnected = m_bleManager->scaleDevice() && m_bleManager->scaleDevice()->isConnected();
    m_scanRetryCount = 0;

    qDebug() << "[BleRefresher] Cycling BLE connections to reset Android BLE stack"
             << "(DE1:" << (m_de1WasConnected ? "connected" : "disconnected")
             << ", Scale:" << (m_scaleWasConnected ? "connected" : "disconnected") << ")";

    if (!m_de1WasConnected) {
        if (m_scaleWasConnected) {
            qDebug() << "[BleRefresher] Only scale connected, reconnecting scale via saved address";
            m_bleManager->tryDirectConnectToScale();
        }
        onRefreshComplete();
        return;
    }

    // Disconnect any previous sequencing connection
    QObject::disconnect(m_de1ConnConn);

    // Listen for DE1 connection state changes to sequence the reconnection.
    // Use QueuedConnection because DE1Device::disconnect() emits connectedChanged synchronously
    // — we need to let disconnect() fully return before starting the scan.
    // This listener stays active through scan retries so reconnection during any scan is caught.
    int step = 0;  // 0 = waiting for disconnect, 1 = waiting for reconnect
    m_de1ConnConn = connect(m_de1, &DE1Device::connectedChanged, this, [this, step]() mutable {
        if (step == 0 && !m_de1->isConnected()) {
            step = 1;
            qDebug() << "[BleRefresher] DE1 disconnected, starting scan to reconnect";
            m_bleManager->startScan();
        } else if (step == 1 && m_de1->isConnected()) {
            QObject::disconnect(m_de1ConnConn);
            QObject::disconnect(m_scanConn);
            qDebug() << "[BleRefresher] DE1 reconnected";

            if (m_scaleWasConnected) {
                qDebug() << "[BleRefresher] Reconnecting scale via saved address";
                m_bleManager->tryDirectConnectToScale();
            }

            onRefreshComplete();
        }
    }, Qt::QueuedConnection);

    // Detect scan completion without reconnection — retry up to MAX_SCAN_RETRIES times
    // before giving up. Each scan is ~15s, giving slow BLE chipsets up to ~45s total.
    QObject::disconnect(m_scanConn);
    m_scanConn = connect(m_bleManager, &BLEManager::scanningChanged, this, [this]() {
        if (!m_bleManager->isScanning() && m_refreshInProgress && !m_de1->isConnected()) {
            if (m_scanRetryCount < MAX_SCAN_RETRIES) {
                m_scanRetryCount++;
                qWarning() << "[BleRefresher] Scan finished without DE1 reconnecting, retrying"
                           << "(" << m_scanRetryCount << "/" << MAX_SCAN_RETRIES << ")";
                m_bleManager->startScan();
                return;  // Keep m_de1ConnConn and m_scanConn active for the retry
            }

            qWarning() << "[BleRefresher] All" << (MAX_SCAN_RETRIES + 1)
                       << "scans exhausted without DE1 reconnecting";
            QObject::disconnect(m_scanConn);
            QObject::disconnect(m_de1ConnConn);

            if (m_scaleWasConnected) {
                m_bleManager->tryDirectConnectToScale();
            }

            // Start one final background scan for main.cpp's auto-connect lambda
            m_bleManager->startScan();
            onRefreshComplete();
        }
    });

    qDebug() << "[BleRefresher] Disconnecting DE1...";
    m_de1->disconnect();
}

void BleRefresher::onRefreshComplete() {
    if (!m_refreshInProgress) {
        return;  // Already completed (timeout + queued reconnect race)
    }

    m_refreshInProgress = false;
    emit refreshingChanged();
    m_refreshPending = false;
    m_lastRefresh.restart();

    // Cleanup any lingering connections
    QObject::disconnect(m_phaseConn);
    QObject::disconnect(m_de1ConnConn);
    QObject::disconnect(m_scanConn);

    // Reset periodic timer
    if (m_periodicIntervalMs > 0) {
        m_periodicTimer.start(m_periodicIntervalMs);
    }

    qDebug() << "[BleRefresher] Refresh complete";
}
