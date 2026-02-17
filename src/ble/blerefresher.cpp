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
    connect(&m_periodicTimer, &QTimer::timeout, this, &BleRefresher::scheduleRefresh);

    // Detect wake from sleep: when phase leaves Sleep, schedule a refresh.
    // The 60-minute debounce prevents treating the initial Disconnected→Sleep→Idle
    // transition (normal first connect) as a wake-from-sleep event.
    connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
        auto phase = m_machineState->phase();
        if (phase == MachineState::Phase::Sleep) {
            m_sleeping = true;
        } else if (m_sleeping) {
            m_sleeping = false;
            if (m_settings->resetBleOnWake()) {
                qDebug() << "[BleRefresher] Wake from sleep detected, scheduling BLE refresh";
                scheduleRefresh();
            } else {
                qDebug() << "[BleRefresher] Wake from sleep detected, BLE refresh disabled by setting";
            }
        }
    });

    m_lastRefresh.start();
}

void BleRefresher::startPeriodicRefresh(int intervalHours) {
    m_periodicIntervalMs = intervalHours * 3600 * 1000;
    m_periodicTimer.start(m_periodicIntervalMs);
    qDebug() << "[BleRefresher] Periodic refresh started, interval:" << intervalHours << "hours";
}

void BleRefresher::scheduleRefresh() {
    if (m_refreshInProgress) {
        qDebug() << "[BleRefresher] Refresh already in progress, skipping";
        return;
    }

    // Debounce: skip if last refresh was very recent (rapid sleep/wake cycles)
    if (m_lastRefresh.elapsed() < MIN_REFRESH_INTERVAL_MS) {
        qDebug() << "[BleRefresher] Last refresh was" << m_lastRefresh.elapsed() / 1000
                 << "s ago, skipping (debounce)";
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
    m_refreshInProgress = true;
    emit refreshingChanged();
    m_de1WasConnected = m_de1->isConnected();
    m_scaleWasConnected = m_bleManager->scaleDevice() && m_bleManager->scaleDevice()->isConnected();

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
    int step = 0;  // 0 = waiting for disconnect, 1 = waiting for reconnect
    m_de1ConnConn = connect(m_de1, &DE1Device::connectedChanged, this, [this, step]() mutable {
        if (step == 0 && !m_de1->isConnected()) {
            step = 1;
            qDebug() << "[BleRefresher] DE1 disconnected, starting scan to reconnect";
            m_bleManager->startScan();
        } else if (step == 1 && m_de1->isConnected()) {
            QObject::disconnect(m_de1ConnConn);
            qDebug() << "[BleRefresher] DE1 reconnected";

            if (m_scaleWasConnected) {
                qDebug() << "[BleRefresher] Reconnecting scale via saved address";
                m_bleManager->tryDirectConnectToScale();
            }

            onRefreshComplete();
        }
    }, Qt::QueuedConnection);

    qDebug() << "[BleRefresher] Disconnecting DE1...";
    m_de1->disconnect();
}

void BleRefresher::onRefreshComplete() {
    m_refreshInProgress = false;
    emit refreshingChanged();
    m_refreshPending = false;
    m_lastRefresh.restart();

    // Cleanup any lingering connections
    QObject::disconnect(m_phaseConn);
    QObject::disconnect(m_de1ConnConn);

    // Reset periodic timer
    if (m_periodicIntervalMs > 0) {
        m_periodicTimer.start(m_periodicIntervalMs);
    }

    qDebug() << "[BleRefresher] Refresh complete";
}
