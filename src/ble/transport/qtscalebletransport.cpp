#include "qtscalebletransport.h"
#include "../blecapability.h"
#include "../blemanager.h"
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QLowEnergyConnectionParameters>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Helper macro for consistent logging
#define QT_TRANSPORT_LOG(msg) log(msg)

QtScaleBleTransport::QtScaleBleTransport(QObject* parent)
    : ScaleBleTransport(parent)
{
    m_clock.start();  // monotonic source for the connection-priority window
}

int64_t QtScaleBleTransport::nowMs() {
    return m_clock.isValid() ? m_clock.elapsed() : 0;
}

void QtScaleBleTransport::log(const QString& message) {
    QString msg = QString("[BLE QtTransport] ") + message;
    qDebug().noquote() << msg;
    emit logMessage(msg);
}

void QtScaleBleTransport::warn(const QString& message) {
    // Significant connection-priority events: WARN so they stand out in the
    // user-attached debug.log (this feature is validated by reading those),
    // and still flow to the scale log view via logMessage.
    QString msg = QString("[BLE QtTransport] ") + message;
    qWarning().noquote() << msg;
    emit logMessage(msg);
}

QtScaleBleTransport::~QtScaleBleTransport() {
    disconnectFromDevice();
}

void QtScaleBleTransport::connectToDevice(const QString& address, const QString& name) {
    // Create device info from address - works on Android/desktop, not on iOS
    QT_TRANSPORT_LOG(QString("connectToDevice by address: %1 (%2)").arg(name, address));
    QBluetoothDeviceInfo deviceInfo(QBluetoothAddress(address), name, 0);
    connectToDevice(deviceInfo);
}

void QtScaleBleTransport::connectToDevice(const QBluetoothDeviceInfo& device) {
    // Get device identifier (UUID on iOS, address on other platforms)
    QString deviceId = device.address().isNull()
        ? device.deviceUuid().toString()
        : device.address().toString();

    // Diagnostic logging - detect duplicate connect calls
    QT_TRANSPORT_LOG(QString("connectToDevice() called for %1 (%2). controller=%3 state=%4")
        .arg(device.name(), deviceId)
        .arg(m_controller ? "yes" : "no")
        .arg(m_controller ? static_cast<int>(m_controller->state()) : -1));

    // Debounce: ignore duplicate connect attempts to the same device while busy
    if (m_controller) {
        const auto st = m_controller->state();
        const bool busy = (st == QLowEnergyController::ConnectingState ||
                           st == QLowEnergyController::ConnectedState ||
                           st == QLowEnergyController::DiscoveringState ||
                           st == QLowEnergyController::DiscoveredState);

        if (busy && deviceId == m_deviceId) {
            QT_TRANSPORT_LOG("Ignoring duplicate connect request to same device while busy");
            return;
        }

        QT_TRANSPORT_LOG("Cleaning up previous controller");
        disconnectFromDevice();
    }

    m_deviceAddress = device.address().toString();
    m_deviceName = device.name();
    m_deviceId = deviceId;

    QT_TRANSPORT_LOG(QString("Connecting to %1 (%2)").arg(m_deviceName, deviceId));

    // Use the full device info - this is required for iOS where address is not available
    m_controller = QLowEnergyController::createCentral(device, this);

    if (!m_controller) {
        QT_TRANSPORT_LOG("ERROR: Failed to create BLE controller!");
        emit error("Failed to create BLE controller");
        return;
    }

    // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading issues
    // where callbacks arrive on CoreBluetooth thread and cause re-entrancy problems
    auto qc = Qt::QueuedConnection;

    connect(m_controller, &QLowEnergyController::connected,
            this, &QtScaleBleTransport::onControllerConnected, qc);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &QtScaleBleTransport::onControllerDisconnected, qc);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &QtScaleBleTransport::onControllerError, qc);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &QtScaleBleTransport::onServiceDiscovered, qc);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &QtScaleBleTransport::onServiceDiscoveryFinished, qc);
    // Log all state changes for debugging - also use QueuedConnection
    connect(m_controller, &QLowEnergyController::stateChanged, this, [this](QLowEnergyController::ControllerState state) {
        QString stateName;
        switch (state) {
            case QLowEnergyController::UnconnectedState: stateName = "Unconnected"; break;
            case QLowEnergyController::ConnectingState: stateName = "Connecting"; break;
            case QLowEnergyController::ConnectedState: stateName = "Connected"; break;
            case QLowEnergyController::DiscoveringState: stateName = "Discovering"; break;
            case QLowEnergyController::DiscoveredState: stateName = "Discovered"; break;
            case QLowEnergyController::ClosingState: stateName = "Closing"; break;
            default: stateName = QString::number(static_cast<int>(state)); break;
        }
        this->log(QString(">>> Controller state changed: %1").arg(stateName));
    }, qc);

    QT_TRANSPORT_LOG("Calling connectToDevice on controller...");
    m_controller->connectToDevice();
}

void QtScaleBleTransport::disconnectFromDevice() {
    // Clean up services
    for (auto* service : m_services) {
        service->disconnect();
        service->deleteLater();
    }
    m_services.clear();

    if (m_controller) {
        m_controller->disconnect();
        if (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveringState) {
            m_controller->disconnectFromDevice();
        }
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    m_connected = false;
}

void QtScaleBleTransport::discoverServices() {
    if (m_controller &&
        (m_controller->state() == QLowEnergyController::ConnectedState ||
         m_controller->state() == QLowEnergyController::DiscoveredState)) {
        QT_TRANSPORT_LOG("Starting service discovery");
        m_controller->discoverServices();
    } else {
        QT_TRANSPORT_LOG(QString("Cannot discover services - state: %1").arg(static_cast<int>(m_controller ? m_controller->state() : -1)));
    }
}

void QtScaleBleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
    QT_TRANSPORT_LOG(QString("Discovering characteristics for service %1").arg(serviceUuid.toString()));
    QLowEnergyService* service = getOrCreateService(serviceUuid);
    if (service) {
        QT_TRANSPORT_LOG(QString("Service object created, state: %1").arg(static_cast<int>(service->state())));
#ifdef Q_OS_IOS
        // iOS: Use FullDiscovery to get CCCD descriptors (SkipValueDiscovery doesn't discover them)
        QT_TRANSPORT_LOG(QString("Calling discoverDetails(FullDiscovery) for %1 [iOS]").arg(serviceUuid.toString()));
        service->discoverDetails(QLowEnergyService::FullDiscovery);
#else
        // Other platforms: SkipValueDiscovery works fine and is faster
        QT_TRANSPORT_LOG(QString("Calling discoverDetails(SkipValueDiscovery) for %1").arg(serviceUuid.toString()));
        service->discoverDetails(QLowEnergyService::SkipValueDiscovery);
#endif
    } else {
        QT_TRANSPORT_LOG("ERROR: Failed to create service object!");
        emit error("Failed to create service object");
    }
}

void QtScaleBleTransport::enableNotifications(const QBluetoothUuid& serviceUuid,
                                              const QBluetoothUuid& characteristicUuid) {
    QT_TRANSPORT_LOG(QString("Enabling notifications for %1").arg(characteristicUuid.toString()));

    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        QT_TRANSPORT_LOG("ERROR: Service not found for enabling notifications");
        emit error("Service not found for enabling notifications");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        QT_TRANSPORT_LOG("ERROR: Characteristic not found for enabling notifications");
        emit error("Characteristic not found for enabling notifications");
        return;
    }

    QLowEnergyDescriptor cccd = characteristic.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);

    if (cccd.isValid()) {
        // Expected to fire once per characteristic per session. If it appears more often,
        // something is re-enabling notifications and we want to see it in the log.
        QT_TRANSPORT_LOG(QString("write CCCD enable %1").arg(characteristicUuid.toString().mid(1, 8)));
        service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    } else {
        QT_TRANSPORT_LOG("CCCD descriptor not found - scale may still send notifications");
    }

    // Emit immediately (fire-and-forget) - don't wait for CCCD write response.
    // Some scales (e.g. Bookoo) reject CCCD writes but still send notifications.
    // Nordic BLE library had the same behavior: report success regardless of CCCD outcome.
    emit notificationsEnabled(characteristicUuid);
}

void QtScaleBleTransport::writeCharacteristic(const QBluetoothUuid& serviceUuid,
                                              const QBluetoothUuid& characteristicUuid,
                                              const QByteArray& data,
                                              WriteType writeType) {
    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        emit error("Service not found for write");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        emit error("Characteristic not found for write");
        return;
    }

    // Map our WriteType to Qt's WriteMode
    QLowEnergyService::WriteMode mode = (writeType == WriteType::WithoutResponse)
        ? QLowEnergyService::WriteWithoutResponse
        : QLowEnergyService::WriteWithResponse;

    service->writeCharacteristic(characteristic, data, mode);
}

void QtScaleBleTransport::readCharacteristic(const QBluetoothUuid& serviceUuid,
                                             const QBluetoothUuid& characteristicUuid) {
    QLowEnergyService* service = m_services.value(serviceUuid);
    if (!service) {
        emit error("Service not found for read");
        return;
    }

    QLowEnergyCharacteristic characteristic = service->characteristic(characteristicUuid);
    if (!characteristic.isValid()) {
        emit error("Characteristic not found for read");
        return;
    }

    service->readCharacteristic(characteristic);
}

bool QtScaleBleTransport::isConnected() const {
    return m_connected;
}

void QtScaleBleTransport::onControllerConnected() {
    QT_TRANSPORT_LOG("Controller connected!");
    m_connected = true;

    // Connection-priority for the scale link (dual-HIGH BLE contention,
    // #1093/#1176). The DE1 always requests HIGH; the scale also requests
    // HIGH UNLESS a backoff was triggered earlier this app run, in which case
    // the link stays at the platform-default BALANCED — exactly the proven
    // #1097 behaviour, decided by observed runtime behaviour instead of the
    // (retired) Android SDK<30 gate.
    //
    // The backoff decision is app-run-scoped and shared across ALL scales via
    // the BLEManager singleton: the contention is a property of this device's
    // radio + the DE1 link, not of any one scale, so a fresh transport built
    // for a different scale (after a scale-type change) must inherit it rather
    // than re-pay the detection window.
    if (auto* mgr = BLEManager::instance(); mgr && mgr->scaleSkipHighPriority())
        m_priority.setSkipHighPriority(true);

    if (m_priority.skipHighPriority()) {
        QT_TRANSPORT_LOG("Scale connection-priority: skipping HIGH "
                         "(app-run backoff latch set) — link stays at BALANCED");
        m_priority.disarm();
    } else if (m_controller) {
        QT_TRANSPORT_LOG("Requesting CONNECTION_PRIORITY_HIGH on scale");
        QLowEnergyConnectionParameters params;
        m_controller->requestConnectionUpdate(params);
        m_priority.armWindow(nowMs());  // arm DE1-fault / scale-stall detection
    } else {
        m_priority.disarm();
    }

    emit connected();
}

void QtScaleBleTransport::onDe1LinkFault(const QString& kind) {
    // Primary signal: a DE1-link fault clustered shortly after the scale
    // requested HIGH is the dual-HIGH contention signature (#1093/#1176).
    if (m_priority.onDe1Fault(nowMs())) {
        triggerScaleBackoff(qPrintable(QStringLiteral(
            "DE1-link fault cluster (last kind=%1) within scale-HIGH window")
            .arg(kind)));
    }
}

void QtScaleBleTransport::onScaleFeedStalled() {
    // Backstop: covers sessions where the DE1 cluster never appears early and
    // the scale silently stalls mid-shot (the actual #1176 shot-1151 case).
    if (m_priority.onScaleStall()) {
        triggerScaleBackoff("scale weight feed stalled mid-extraction while at HIGH");
    }
}

void QtScaleBleTransport::triggerScaleBackoff(const char* reason) {
    // The detector returned true exactly once (it latches skip-HIGH +
    // backed-off before returning), and the app-run BLEManager latch then
    // blocks every later connection this run — so this runs at most once per
    // app run (no loop, no re-trigger by any scale).
    // WARN-level: this is the headline event for log-based field validation.
    warn(QString("Scale connection-priority BACKOFF triggered: %1 — skipping "
                 "HIGH and reconnecting the scale at BALANCED")
             .arg(QString::fromUtf8(reason)));
    // Latch the decision app-run-wide so every scale (incl. one connected
    // after a scale-type change, which builds a fresh transport+detector)
    // skips HIGH for the rest of this run. In-memory only; cleared on restart.
    if (auto* mgr = BLEManager::instance()) {
        mgr->setScaleSkipHighPriority(true);
        warn("Scale connection-priority: app-run skip-HIGH latch SET — all "
             "scales will run at BALANCED until app restart");
    }
    // Tear down the link, then emit disconnected() explicitly.
    // disconnectFromDevice() calls m_controller->disconnect(), which severs
    // ALL Qt signals from the controller — that alone is sufficient to
    // guarantee onControllerDisconnected() never fires on this path, in ANY
    // controller state, so disconnectFromDevice() does NOT emit disconnected()
    // here. (Separately, for a fully-discovered scale in DiscoveredState the
    // inner m_controller->disconnectFromDevice() is also skipped, so the BLE
    // peer isn't explicitly notified — a distinct fact, NOT the reason the
    // signal is missing.) We must therefore emit disconnected() ourselves —
    // unconditionally, regardless of state — honouring the ScaleBleTransport
    // contract, so the scale driver reaches setConnected(false) →
    // ScaleDevice::connectedChanged, which the existing scale auto-reconnect
    // (main.cpp) is gated on. Without it the backoff would be a permanent
    // scale drop instead of a reconnect-at-BALANCED.
    disconnectFromDevice();
    emit disconnected();
}

void QtScaleBleTransport::onControllerDisconnected() {
    QT_TRANSPORT_LOG("Controller disconnected");
    m_connected = false;
    emit disconnected();
}

void QtScaleBleTransport::onControllerError(QLowEnergyController::Error err) {
    QString errorName;
    switch (err) {
        case QLowEnergyController::NoError: errorName = "NoError"; break;
        case QLowEnergyController::UnknownError: errorName = "UnknownError"; break;
        case QLowEnergyController::UnknownRemoteDeviceError: errorName = "UnknownRemoteDeviceError"; break;
        case QLowEnergyController::NetworkError: errorName = "NetworkError"; break;
        case QLowEnergyController::InvalidBluetoothAdapterError: errorName = "InvalidBluetoothAdapterError"; break;
        case QLowEnergyController::ConnectionError: errorName = "ConnectionError"; break;
        case QLowEnergyController::AdvertisingError: errorName = "AdvertisingError"; break;
        case QLowEnergyController::RemoteHostClosedError: errorName = "RemoteHostClosedError"; break;
        case QLowEnergyController::AuthorizationError: errorName = "AuthorizationError"; break;
        case QLowEnergyController::MissingPermissionsError: errorName = "MissingPermissionsError"; break;
        default: errorName = QString::number(static_cast<int>(err)); break;
    }
    QString stateName;
    switch (m_controller ? m_controller->state() : QLowEnergyController::UnconnectedState) {
        case QLowEnergyController::UnconnectedState: stateName = "Unconnected"; break;
        case QLowEnergyController::ConnectingState:  stateName = "Connecting"; break;
        case QLowEnergyController::ConnectedState:   stateName = "Connected"; break;
        case QLowEnergyController::DiscoveringState: stateName = "Discovering"; break;
        case QLowEnergyController::DiscoveredState:  stateName = "Discovered"; break;
        case QLowEnergyController::ClosingState:     stateName = "Closing"; break;
        default: stateName = QString::number(static_cast<int>(m_controller ? m_controller->state() : -1)); break;
    }
    QString msg = QString("!!! CONTROLLER ERROR: %1 (state=%2) !!!").arg(errorName, stateName);
    QT_TRANSPORT_LOG(msg);
    emit error(msg);

    // Same one-shot Linux BT diagnostics dump as the DE1 transport —
    // whichever transport hits an error first triggers it.
    BleCapability::logLinuxBtDiagnosticsOnce();

    // Only log the setcap hint when we've actually detected the missing
    // capability — the check is a no-op / always false on non-Linux.
    if (err == QLowEnergyController::UnknownRemoteDeviceError
        && BleCapability::linuxMissing()) {
        QT_TRANSPORT_LOG(QStringLiteral("Linux hint: run `%1` and restart the app "
                                        "(capability is often cleared by OS updates).")
                             .arg(BleCapability::linuxSetcapCommand()));
    }

    // Caps OK but still UnknownRemoteDeviceError — surface the BlueZ cache
    // recovery dialog via BLEManager. Linux-only: the recovery commands
    // (bluetoothctl/systemctl) do not apply on macOS/iOS/Android, where
    // UnknownRemoteDeviceError surfaces for unrelated reasons.
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (err == QLowEnergyController::UnknownRemoteDeviceError
        && !BleCapability::linuxMissing()) {
        if (auto* m = BLEManager::instance()) m->requestBluezCacheHint();
    }
#endif
}

void QtScaleBleTransport::onServiceDiscovered(const QBluetoothUuid& uuid) {
    QT_TRANSPORT_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    emit serviceDiscovered(uuid);
}

void QtScaleBleTransport::onServiceDiscoveryFinished() {
    QT_TRANSPORT_LOG("Service discovery finished");
    emit servicesDiscoveryFinished();
}

void QtScaleBleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    QLowEnergyService* service = qobject_cast<QLowEnergyService*>(sender());
    if (!service) return;

    QString stateName;
    switch (state) {
        case QLowEnergyService::InvalidService: stateName = "InvalidService"; break;
        case QLowEnergyService::RemoteService: stateName = "RemoteService"; break;
        case QLowEnergyService::RemoteServiceDiscovering: stateName = "RemoteServiceDiscovering"; break;
        case QLowEnergyService::RemoteServiceDiscovered: stateName = "RemoteServiceDiscovered"; break;
        default: stateName = QString::number(static_cast<int>(state)); break;
    }
    QT_TRANSPORT_LOG(QString("Service %1 state changed: %2")
        .arg(service->serviceUuid().toString(), stateName));

    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        QBluetoothUuid serviceUuid = service->serviceUuid();

        // Emit discovered characteristics with descriptor info
        const QList<QLowEnergyCharacteristic> chars = service->characteristics();
        QT_TRANSPORT_LOG(QString("Found %1 characteristics").arg(chars.size()));
        for (const QLowEnergyCharacteristic& c : chars) {
            auto props = c.properties();
            auto descs = c.descriptors();
            QT_TRANSPORT_LOG(QString("  - Char %1 props=0x%2 descCount=%3")
                .arg(c.uuid().toString())
                .arg(static_cast<int>(props), 2, 16, QChar('0'))
                .arg(descs.size()));
            // Log each descriptor
            for (const QLowEnergyDescriptor& d : descs) {
                QT_TRANSPORT_LOG(QString("      desc %1").arg(d.uuid().toString()));
            }
            emit characteristicDiscovered(serviceUuid, c.uuid(),
                                         static_cast<int>(props));
        }

        // Each scale calls enableNotifications() explicitly for the characteristic(s) it needs;
        // the transport no longer auto-enables CCCDs for every notify-capable characteristic.
        // The auto-enable was a redundant second CCCD write per characteristic that piled onto
        // the Android GATT pipeline at scale-connect time. On Android 9 + concurrent DE1 GATT
        // writes, those extra descriptor writes appear to starve DE1 writes
        // (CharacteristicWriteError on 0xa00f / 0xa010). The CoreBluetooth transport already
        // skipped auto-enable (its rationale was Bookoo-scale double-enable confusion);
        // de1app writes CCCDs once, never automatically.
        emit characteristicsDiscoveryFinished(serviceUuid);
    }
}

void QtScaleBleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                                   const QByteArray& value) {
    emit characteristicChanged(c.uuid(), value);
}

void QtScaleBleTransport::onCharacteristicRead(const QLowEnergyCharacteristic& c,
                                                const QByteArray& value) {
    // Log raw read data for debugging
    QT_TRANSPORT_LOG(QString("Read %1: %2 bytes: %3")
        .arg(c.uuid().toString())
        .arg(value.size())
        .arg(QString(value.toHex())));
    emit characteristicRead(c.uuid(), value);
}

void QtScaleBleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c) {
    emit characteristicWritten(c.uuid());
}

void QtScaleBleTransport::onDescriptorWritten(const QLowEnergyDescriptor& d, const QByteArray& value) {
    Q_UNUSED(value);
    // CCCD confirmations are not logged — some scales send them frequently
    Q_UNUSED(d);
}

void QtScaleBleTransport::onServiceError(QLowEnergyService::ServiceError err) {
    QLowEnergyService* service = qobject_cast<QLowEnergyService*>(sender());
    QString serviceUuid = service ? service->serviceUuid().toString() : "unknown";

    QString errorName;
    switch (err) {
        case QLowEnergyService::NoError: errorName = "NoError"; break;
        case QLowEnergyService::OperationError: errorName = "OperationError"; break;
        case QLowEnergyService::CharacteristicWriteError: errorName = "CharacteristicWriteError"; break;
        case QLowEnergyService::DescriptorWriteError:
            // CCCD write failures are non-fatal - some scales reject them but still notify
            QT_TRANSPORT_LOG("DescriptorWriteError (non-fatal, scale may still send notifications)");
            return;
        case QLowEnergyService::UnknownError: errorName = "UnknownError"; break;
        case QLowEnergyService::CharacteristicReadError: errorName = "CharacteristicReadError"; break;
        case QLowEnergyService::DescriptorReadError: errorName = "DescriptorReadError"; break;
        default: errorName = QString::number(static_cast<int>(err)); break;
    }
    QT_TRANSPORT_LOG(QString("!!! SERVICE ERROR: %1 on %2 !!!").arg(errorName, serviceUuid));
    emit error(QString("Service error: %1").arg(errorName));
}

QLowEnergyService* QtScaleBleTransport::getOrCreateService(const QBluetoothUuid& serviceUuid) {
    if (m_services.contains(serviceUuid)) {
        return m_services.value(serviceUuid);
    }

    if (!m_controller) {
        return nullptr;
    }

    QLowEnergyService* service = m_controller->createServiceObject(serviceUuid, this);
    if (service) {
        connectServiceSignals(service);
        m_services.insert(serviceUuid, service);
    }

    return service;
}

void QtScaleBleTransport::connectServiceSignals(QLowEnergyService* service) {
    // Use Qt::QueuedConnection for all service signals - fixes iOS CoreBluetooth threading issues
    auto qc = Qt::QueuedConnection;

    connect(service, &QLowEnergyService::stateChanged,
            this, &QtScaleBleTransport::onServiceStateChanged, qc);
    connect(service, &QLowEnergyService::characteristicChanged,
            this, &QtScaleBleTransport::onCharacteristicChanged, qc);
    connect(service, &QLowEnergyService::characteristicRead,
            this, &QtScaleBleTransport::onCharacteristicRead, qc);
    connect(service, &QLowEnergyService::characteristicWritten,
            this, &QtScaleBleTransport::onCharacteristicWritten, qc);
    connect(service, &QLowEnergyService::descriptorWritten,
            this, &QtScaleBleTransport::onDescriptorWritten, qc);
    connect(service, &QLowEnergyService::errorOccurred,
            this, &QtScaleBleTransport::onServiceError, qc);
}
