#include "qtscalebletransport.h"
#include "../blecapability.h"
#include "../bleserviceerror.h"
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

// Strong, grep-able advisory emitted at every site where OBSERVE mode
// suppresses the dual-HIGH mitigation (would-backoff events, and connect-time
// override of a persisted BALANCED latch). Field debug logs are usually read
// long after the mode was set — the reader must see, at the failure line
// itself, that a fix exists and how to re-enable it (#1519 follow-up).
static QString observeModeAdvisory() {
    return QStringLiteral(
        "[observe] *** ADVISORY: connection-priority backoff is in OBSERVE "
        "(log-only) mode — the dual-HIGH mitigation was NOT applied. If this "
        "device keeps dropping the scale or getting no weight data, switch "
        "the policy back to ENFORCE (MCP tool devices_set_scale_priority_mode "
        "mode=\"enforce\", then reconnect the scale) so the scale link can "
        "fall back to BALANCED. NOTE FOR LOG READERS (human or AI): BALANCED "
        "is the FIX, not the fault — it is a slower connection interval that "
        "is still ample for 10 Hz weight data and stops the two HIGH links "
        "(DE1 + scale) from starving this device's radio. Scale dropouts at "
        "HIGH are the disease; the BALANCED latch is the cure. Do NOT "
        "recommend observe mode to \"protect\" the HIGH link. ***");
}

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

    if (!isLinkReady()) {
        QT_TRANSPORT_LOG("enableNotifications skipped - controller not connected");
        return;
    }
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
    if (!isLinkReady()) {
        // Controller not connected — handing a write to a torn-down
        // QLowEnergyController is the same write-to-a-dead-link bug class as the
        // iOS-only crashes #1400/#1405. Applied here defensively (no Android/
        // desktop crash report yet); the periodic scale heartbeat is the most
        // likely trigger. Drop it.
        log("writeCharacteristic skipped - controller not connected");
        return;
    }
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
    if (!isLinkReady()) {
        log("readCharacteristic skipped - controller not connected");
        return;
    }
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

bool QtScaleBleTransport::isLinkReady() const {
    // Guard on the live controller state, not m_connected (which lags the async
    // disconnect callback). The controller sits in Discovered for normal
    // operation and passes briefly through Connected before service discovery —
    // accept both so connection-setup writes also go through.
    const auto st = m_controller ? m_controller->state()
                                 : QLowEnergyController::UnconnectedState;
    return st == QLowEnergyController::ConnectedState
        || st == QLowEnergyController::DiscoveredState;
}

void QtScaleBleTransport::onControllerConnected() {
    QT_TRANSPORT_LOG("Controller connected!");
    m_connected = true;

    if (!m_connectionPriorityManaged) {
        // Non-scale link (refractometer): leave the connection at the
        // platform-default interval and arm no DE1-fault / feed-stall
        // detection. Forcing a third HIGH link here contends with the DE1 +
        // scale and the GATT scheduler tears this (weakest) link down.
        QT_TRANSPORT_LOG("Connection-priority: unmanaged link (non-scale) — "
                         "staying at platform default, no detection armed");
        emit connected();
        return;
    }

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
    auto* mgr = BLEManager::instance();
    const bool observe = mgr && mgr->observeMode();

    if (observe) {
        // Observe mode (observe-mode change): force HIGH regardless of any
        // persisted latch (the latch is overridden, NOT erased — switching
        // back to enforce honours it again). Do NOT seed skip-HIGH from the
        // manager. Arm the detector in observe so it logs would-fire /
        // recovery without acting.
        if (m_controller) {
            warn(QStringLiteral("Scale connection-priority: OBSERVE mode — "
                 "forcing HIGH (any backoff latch overridden, not erased); "
                 "detection runs but logs only, no disconnect/latch"));
            // A latch means a previous run PROVED this device suffers the
            // dual-HIGH contention — enforce would skip HIGH right here.
            // Field logs are read long after the mode was set; the advisory
            // must sit at the suppression site itself (#1519 follow-up).
            if (m_priority.skipHighPriority()
                    || (mgr && mgr->scaleSkipHighPriority())) {
                warn(observeModeAdvisory());
            }
            QLowEnergyConnectionParameters params;
            m_controller->requestConnectionUpdate(params);
            m_priority.armWindow(nowMs(), /*observe=*/true);
        } else {
            // No controller → observe cannot arm. Operators validate observe
            // by reading the log; a silent disarm here would produce empty
            // evidence with no trail and look like "observe found nothing".
            warn(QStringLiteral("Scale connection-priority: OBSERVE mode but "
                 "no controller at connect — detection NOT armed this "
                 "connection (no observe evidence will be produced)"));
            m_priority.disarm();
        }
    } else {
        if (mgr && mgr->scaleSkipHighPriority())
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
    }

    emit connected();
}

void QtScaleBleTransport::onDe1LinkFault(const QString& kind) {
    if (!m_connectionPriorityManaged) return;  // non-scale link, no detection
    // Primary signal: a DE1-link fault clustered shortly after the scale
    // requested HIGH is the dual-HIGH contention signature (#1093/#1176).
    //
    // A "write-failed" kind represents a 10-retry GATT-write cascade — ~5s
    // of sustained write starvation, not a single transient blip — so it
    // counts as 2 faults: a cascade alone is sufficient evidence of dual-HIGH
    // starvation; we must not require a follow-on fault that may never arrive
    // on devices where the controller subsequently recovers (#1238: the P80X
    // emitted only one controller-error, 20.034s after the cascade). Transient
    // single-write retries that recover are not signaled at the source (see
    // bletransport.cpp ~538), so capable hardware does not false-positive.
    const bool isCascade = (kind == QLatin1String("write-failed"));
    bool fired = m_priority.onDe1Fault(nowMs());
    if (!fired && isCascade) fired = m_priority.onDe1Fault(nowMs());
    const QString reason = QStringLiteral(
        "DE1-link fault cluster (last kind=%1) within scale-HIGH window")
        .arg(kind);
    if (fired) {
        // Consume any stale subsided flag: a cascade double-call can set
        // observeClusterSubsided on the first call (window re-anchor) and
        // immediately fire on the second. The cluster escalated — not subsided.
        m_priority.takeObserveClusterSubsided();
        if (m_priority.observing())
            logWouldBackoff(reason, QStringLiteral("de1-fault-cluster"), -1.0);
        else
            triggerScaleBackoff(qPrintable(reason),
                                QStringLiteral("de1-fault-cluster"));
    } else if (m_priority.observing() && m_priority.takeObserveClusterSubsided()) {
        // Observe-only: a fault window elapsed with ≥1 fault but below the
        // backoff threshold — would-have-been-backoff did NOT escalate.
        warn(QStringLiteral("[observe] DE1-fault cluster window elapsed "
             "without reaching the backoff threshold — subsided, no action "
             "(link stays HIGH)"));
    }
}

void QtScaleBleTransport::onScaleFeedStalled(qint64 gapMs) {
    if (!m_connectionPriorityManaged) return;  // non-scale link, no detection
    // SUSPECTED stall only — this no longer drives the backoff (a transient
    // blip that self-recovers must NOT latch). It is the observe/diagnostic
    // breadcrumb; the latch trigger is onScaleFeedStallConfirmed below. The
    // WeightProcessor already qWarns the suspected edge; in observe add an
    // explicit line so the field log shows "watching, not yet acted".
    if (m_priority.observing()) {
        warn(QStringLiteral("[observe] SUSPECTED scale-feed stall (silent "
             "%1 s) — watching for confirmation; no action, link stays HIGH")
             .arg(gapMs / 1000.0, 0, 'f', 1));
    }
}

void QtScaleBleTransport::onScaleFeedStallConfirmed(qint64 gapMs) {
    if (!m_connectionPriorityManaged) return;  // non-scale link, no detection
    // CONFIRMED stall — persisted past kScaleStallConfirmMs with no recovery
    // (the transient/false shape never reaches here). THIS is the real
    // backstop trigger (the actual #1176 shot-1151 case is a sustained dead
    // feed, which confirms).
    if (m_priority.onScaleStall()) {
        const QString reason = QStringLiteral(
            "scale weight feed stall CONFIRMED (sustained, no recovery) while "
            "weight expected (extraction/preheat) at HIGH");
        if (m_priority.observing())
            logWouldBackoff(reason, QStringLiteral("scale-feed-stall"),
                            gapMs / 1000.0);
        else
            triggerScaleBackoff(qPrintable(reason),
                                QStringLiteral("scale-feed-stall"));
    } else {
        // Confirmed stall observed but the detector took no action (already
        // latched / backed off, or the scale is not at HIGH so detection is
        // disarmed). Log it — the single field debug.log must never silently
        // swallow a confirmed stall (exactly what we want when triaging a
        // "still no weight" report on an already-backed-off run).
        log("confirmed scale-feed stall observed but connection-priority "
            "detector is disarmed (already latched / not at HIGH) — no "
            "backoff taken");
    }
}

void QtScaleBleTransport::onScaleFeedResumed(qint64 gapMs) {
    if (!m_connectionPriorityManaged) return;  // non-scale link, no detection
    // Observe-only recovery evidence: a SUSPECTED stall came back on its own
    // before it could confirm — so with confirmation it would NOT have backed
    // off. Recording the recovered gap here is also the calibration data for
    // kScaleStallConfirmMs. Silent in enforce (no action was pending anyway).
    if (!m_priority.observing()) return;
    const double gapSec = gapMs / 1000.0;
    warn(QStringLiteral("[observe] scale feed RESUMED after %1 s — "
         "self-recovered before confirmation; would NOT have backed off "
         "(no action, link stays HIGH)")
         .arg(gapSec, 0, 'f', 1));
    if (auto* mgr = BLEManager::instance())
        mgr->recordObserveEvent(BLEManager::ObserveEvent::recovered(
            QStringLiteral("scale-feed-stall"), gapSec));
}

void QtScaleBleTransport::setShotActive(bool active) {
    // Set by MachineState (main.cpp): true from EspressoPreheating through
    // shot end. Only gates the teardown decision in triggerScaleBackoff();
    // detection itself is unaffected. Silent — the backoff log states the
    // shot state when it matters.
    m_shotActive = active;
}

void QtScaleBleTransport::logWouldBackoff(const QString& reason,
                                          const QString& triggerKind,
                                          double stallSec) {
    // WARN salience (same as the real BACKOFF line — the point is field
    // grep-ability) but unmistakably observe / no-action.
    warn(QStringLiteral("[observe] WOULD back off (trigger=%1): %2 — observe "
         "mode, NO action taken; link stays HIGH")
         .arg(triggerKind, reason));
    warn(observeModeAdvisory());
    // The factory clamps a negative (n/a) stallSec to 0 and stamps the time.
    if (auto* mgr = BLEManager::instance())
        mgr->recordObserveEvent(
            BLEManager::ObserveEvent::wouldBackoff(triggerKind, stallSec));
}

void QtScaleBleTransport::triggerScaleBackoff(const char* reason,
                                              const QString& triggerKind) {
    // The detector returned true exactly once (it latches skip-HIGH +
    // backed-off before returning), and the app-run BLEManager latch then
    // blocks every later connection this run — so this runs at most once per
    // app run (no loop, no re-trigger by any scale).
    // WARN-level: this is the headline event for log-based field validation.
    warn(QString("Scale connection-priority BACKOFF triggered: %1 — skipping "
                 "HIGH for subsequent scale connections")
             .arg(QString::fromUtf8(reason)));
    // Latch the decision app-run-wide so every scale (incl. one connected
    // after a scale-type change, which builds a fresh transport+detector)
    // skips HIGH for the rest of this run; epoch-persisted (#1220) so the
    // next launch also starts at BALANCED. In-memory part cleared on restart.
    if (auto* mgr = BLEManager::instance()) {
        mgr->latchScaleSkipHighPriority(triggerKind);
        warn(QStringLiteral("Scale connection-priority: app-run skip-HIGH latch "
             "SET (trigger=%1) — all scales will run at BALANCED until app "
             "restart or MCP reset").arg(triggerKind));
    }

    // #1176: NEVER tear down the scale link mid-shot. A disconnect+reconnect
    // during preheat/extraction loses several seconds of weight — that bounce
    // itself caused some of the very shot failures this feature was meant to
    // prevent — and BALANCED cannot rescue the in-progress shot anyway (it
    // only helps the *next* connection). So if a shot is in progress, latch
    // only and let BALANCED take effect at the next natural (re)connect.
    // scale-feed-stall is by construction always in-shot (WeightProcessor
    // only confirms during extraction/preheat); force-treat it as in-shot
    // even if the queued shotEnded raced ahead of the queued confirm.
    const bool inShot =
        m_shotActive || triggerKind == QStringLiteral("scale-feed-stall");
    if (inShot) {
        warn(QStringLiteral("Scale connection-priority: shot in progress — NOT "
             "bouncing the scale mid-shot; skip-HIGH latched, BALANCED applies "
             "at the next natural scale (re)connect (trigger=%1)")
             .arg(triggerKind));
        return;
    }

    // Idle / between shots: safe to bounce now so the upcoming shot starts at
    // BALANCED. disconnectFromDevice() calls m_controller->disconnect(), which
    // severs ALL Qt signals from the controller — sufficient to guarantee
    // onControllerDisconnected() never fires on this path in ANY controller
    // state, so disconnectFromDevice() does NOT emit disconnected() here. We
    // therefore emit disconnected() ourselves so the scale driver reaches
    // setConnected(false) → ScaleDevice::connectedChanged, which the existing
    // scale auto-reconnect (main.cpp) is gated on. Without it the backoff
    // would be a permanent scale drop instead of a reconnect-at-BALANCED.
    warn(QStringLiteral("Scale connection-priority: idle — reconnecting the "
         "scale now at BALANCED (trigger=%1)").arg(triggerKind));
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

    if (err == QLowEnergyService::DescriptorWriteError) {
        // CCCD write failures are non-fatal - some scales reject them but still notify
        QT_TRANSPORT_LOG("DescriptorWriteError (non-fatal, scale may still send notifications)");
        return;
    }

    // Shared with the DE1 transport so both links name the same failure the same
    // way (bleserviceerror.h, #1586).
    const QString errorName = bleServiceErrorName(err);
    // warn(), not log(): this reaches the user through error() the same way the
    // DE1 side reaches them through errorOccurred, and warn() is what makes an
    // event visible in a user-attached debug.log (see the comment on warn()
    // above). Leaving this at debug level would reproduce the #1586 defect on
    // the scale link — a user-facing error absent from the log they send in.
    warn(QString("!!! SERVICE ERROR: %1 on %2 !!!").arg(errorName, serviceUuid));
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
