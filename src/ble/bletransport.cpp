#include "bletransport.h"
#include "blecapability.h"
#include "bleserviceerror.h"
#ifndef DECENZA_TESTING
#include "blemanager.h"
#endif
#include "protocol/de1characteristics.h"

#include <QBluetoothAddress>
#include <QLowEnergyConnectionParameters>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>

// Store DE1 address in Android SharedPreferences for shutdown service
static void storeDE1AddressForShutdown(const QString& address) {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "[BLE DE1] storeDE1AddressForShutdown: Android context is invalid";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/DeviceShutdownService",
        "setDe1Address",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object(),
        QJniObject::fromString(address).object<jstring>());
}

static void clearDE1AddressForShutdown() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "[BLE DE1] clearDE1AddressForShutdown: Android context is invalid";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/DeviceShutdownService",
        "clearDe1Address",
        "(Landroid/content/Context;)V",
        context.object());
}

static void startBleConnectionService() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "[BLE DE1] startBleConnectionService: Android context is invalid";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/BleConnectionService",
        "start",
        "(Landroid/content/Context;)V",
        context.object());
}

static void stopBleConnectionService() {
    QJniObject context = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "getContext",
        "()Landroid/content/Context;");

    if (!context.isValid()) {
        qWarning() << "[BLE DE1] stopBleConnectionService: Android context is invalid";
        return;
    }

    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/BleConnectionService",
        "stop",
        "(Landroid/content/Context;)V",
        context.object());
}
#endif

BleTransport::BleTransport(QObject* parent)
    : DE1Transport(parent)
{
    m_commandTimer.setInterval(50);  // Process queue every 50ms
    m_commandTimer.setSingleShot(true);
    connect(&m_commandTimer, &QTimer::timeout, this, &BleTransport::processCommandQueue);

    // Notification-subscribe timeout: bounds each CCCD descriptor write during
    // subscribeAll() so one stuck subscription can't block the connection
    // forever. No retry — subscribeNext() just moves on and logs the failure
    // (see subscribeNext()/onDescriptorWritten()).
    m_subscribeTimeoutTimer.setSingleShot(true);
    m_subscribeTimeoutTimer.setInterval(SUBSCRIBE_TIMEOUT_MS);
    connect(&m_subscribeTimeoutTimer, &QTimer::timeout, this, [this]() {
        warn(QString("Notification subscribe timed out (%1) — proceeding without confirmation")
                 .arg(m_currentSubscribeUuid.toString().mid(1, 8)));
        m_writePending = false;
        subscribeNext();
    });

    // Write timeout timer - detect hung BLE writes (like de1app)
    m_writeTimeoutTimer.setSingleShot(true);
    m_writeTimeoutTimer.setInterval(WRITE_TIMEOUT_MS);
    connect(&m_writeTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_writePending) {
            m_writePending = false;
            if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                m_writeRetryCount++;
                log(QString("Write timeout, retrying %1/%2 (uuid=%3)")
                    .arg(m_writeRetryCount).arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                QTimer::singleShot(WRITE_RETRY_DELAY_MS, this, [this]() {
                    if (m_lastCommand) {
                        m_lastCommand();
                    }
                });
            } else {
                warn(QString("Write FAILED after %1 retries (uuid=%2, %3 bytes)")
                    .arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid).arg(m_lastWriteData.size()));
                // Deliberately no errorOccurred (user-facing): retry exhaustion means
                // the link is dead and the reconnect ladder takes over — typically
                // self-healing in seconds. Surfacing it queued stale "Connection
                // Error" modals behind the screensaver (#1423). Persistent failures
                // still reach the user via the reconnect path's own errors.
                emit de1LinkFault(QStringLiteral("write-failed"));
                m_lastCommand = nullptr;
                m_writeRetryCount = 0;
                processCommandQueue();  // Move on to next command
                if (!m_writePending && m_commandQueue.isEmpty())
                    emit queueDrained();
            }
        }
    });

    // Retry timer for failed service discovery
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(RETRY_DELAY_MS);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingDevice.isValid()) {
            log(QString("Service discovery retry %1/%2").arg(m_retryCount).arg(MAX_RETRIES));
            // Clean up before retry
            if (m_controller) {
                m_controller->disconnectFromDevice();
                delete m_controller;
                m_controller = nullptr;
            }
            if (!setupController(m_pendingDevice)) {
                warn("Retry abandoned - failed to create BLE controller");
                m_pendingDevice = QBluetoothDeviceInfo();
                return;
            }
            // Re-arm the disconnected-synthesis flag for the fresh attempt.
            // The previous attempt's onControllerDisconnected (or the
            // stateChanged synthesizer) already set it to true; without this
            // reset, if the retry also fails Connecting->Unconnected the
            // synthesizer would be skipped and DE1Device::m_connecting would
            // stick at true — exactly the bug the outer connectToDevice()
            // reset protects against.
            m_disconnectedEmittedForAttempt = false;
            m_controller->connectToDevice();
        }
    });

    // Connect watchdog: fires only if a connect attempt is still wedged in
    // Connecting at the deadline (see header). Aborts the hung controller and
    // synthesizes disconnected() so the normal retry path recreates it (#1303).
    m_connectWatchdogTimer.setSingleShot(true);
    m_connectWatchdogTimer.setInterval(CONNECT_WATCHDOG_MS);
    connect(&m_connectWatchdogTimer, &QTimer::timeout, this, [this]() {
        m_connectWatchdogTimer.stop();  // belt-and-suspenders: don't let a re-entrant Connecting re-arm us
        if (!m_controller || m_controller->state() != QLowEnergyController::ConnectingState) {
            return;  // resolved between the timeout firing and now — nothing to do
        }
        warn(QString("Connect watchdog: stuck in Connecting for %1s — aborting hung "
                     "attempt and synthesizing disconnected()").arg(CONNECT_WATCHDOG_MS / 1000));
        // Abort the wedged GATT connect. The next connectToDevice() (driven by
        // the reconnect loop after the synthesized disconnect) does its own
        // "Cleaning up previous controller" teardown and recreate.
        m_controller->disconnectFromDevice();
        if (!m_disconnectedEmittedForAttempt) {
            m_disconnectedEmittedForAttempt = true;
            emit disconnected();
        }
    });
}

BleTransport::~BleTransport() {
    disconnect();
}

// -- DE1Transport interface implementation --

void BleTransport::write(const QBluetoothUuid& uuid, const QByteArray& data) {
    queueCommand([this, uuid, data]() {
        writeCharacteristic(uuid, data);
    });
}

void BleTransport::writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) {
    // Bypass the 50ms command queue for immediate write. Does NOT clear the queue —
    // callers that need to clear (SAW, sleep) do so explicitly before calling this.
    // This allows ensureChargerOn (app suspend) to write urgently without dropping
    // any pending extraction frames.
    //
    // If a write is already in-flight, prepend to the queue instead of calling
    // writeCharacteristic directly — writeCharacteristic is not re-entrant and would
    // corrupt m_writePending/m_lastWriteUuid/m_writeTimeoutTimer state.
    if (m_writePending) {
        m_commandQueue.prepend([this, uuid, data]() {
            writeCharacteristic(uuid, data);
        });
    } else {
        writeCharacteristic(uuid, data);
    }
}

void BleTransport::read(const QBluetoothUuid& uuid) {
    // Queue the read so it runs after any pending writes complete. Without
    // queueing, a read issued right after a write executes immediately and
    // returns the pre-write value, defeating any read-after-write verification.
    queueCommand([this, uuid]() {
        if (!m_service || !m_characteristics.contains(uuid)) {
            log(QString("read(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
            return;
        }
        m_service->readCharacteristic(m_characteristics[uuid]);
    });
}

void BleTransport::subscribe(const QBluetoothUuid& uuid) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("subscribe(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        return;
    }
    QLowEnergyCharacteristic c = m_characteristics[uuid];
    QLowEnergyDescriptor notification = c.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (notification.isValid()) {
        m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
    } else {
        warn(QString("subscribe(%1) FAILED - CCCD descriptor not found")
            .arg(uuid.toString().mid(1, 8)));
    }
}

void BleTransport::subscribeAll() {
    if (!m_service) return;

    // Sequenced, not fire-and-forget: each CCCD "enable notifications" write is
    // confirmed (or individually timed out) before the next is attempted, and
    // connected() is not emitted until the whole queue drains. Without this, a
    // one-shot MMR read (issued once connected() fires) can have its response
    // notification sent by the DE1 before the client has actually finished
    // enabling notifications for READ_FROM_MMR — the response is then silently
    // dropped with no recovery, since it's not a repeating notification like
    // STATE_INFO/SHOT_SAMPLE that self-heals on the next push.
    m_pendingSubscribeQueue = {
        DE1::Characteristic::STATE_INFO,
        DE1::Characteristic::SHOT_SAMPLE,
        DE1::Characteristic::WATER_LEVELS,
        DE1::Characteristic::READ_FROM_MMR,
        DE1::Characteristic::TEMPERATURES,
    };
    // SHOT_SETTINGS is intentionally NOT subscribed: the DE1 firmware does
    // not push notifications on writes (confirmed in de1app's de1_comms.tcl).
    // Verification happens via explicit read() after each write in
    // DE1Device::setShotSettings().

    subscribeNext();
}

void BleTransport::subscribeNext() {
    if (m_pendingSubscribeQueue.isEmpty()) {
        // All subscriptions are confirmed (or individually timed out past the
        // point where waiting further is worthwhile). Read initial values —
        // these are plain GATT reads (an immediate request/response over the
        // same ATT transaction), not notification-based, so they don't share
        // the CCCD-vs-notification race and don't need to wait on it.
        read(DE1::Characteristic::VERSION);
        read(DE1::Characteristic::STATE_INFO);
        read(DE1::Characteristic::WATER_LEVELS);
        read(DE1::Characteristic::SHOT_SETTINGS);
        // Baseline the liveness clock now so a link that connects but never
        // pushes a single notification also ages out and is caught as a zombie
        // on the next reconnect attempt — not just one that goes quiet later.
        m_notificationLiveness.start();
        emit connected();
        return;
    }

    const QBluetoothUuid uuid = m_pendingSubscribeQueue.takeFirst();
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("subscribe(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        subscribeNext();
        return;
    }
    QLowEnergyCharacteristic c = m_characteristics[uuid];
    QLowEnergyDescriptor notification = c.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!notification.isValid()) {
        warn(QString("subscribe(%1) FAILED - CCCD descriptor not found")
            .arg(uuid.toString().mid(1, 8)));
        subscribeNext();
        return;
    }

    m_currentSubscribeUuid = uuid;
    // Shared with the command queue's single-outstanding-GATT-operation gate —
    // holding it here keeps a queued characteristic write/read from being
    // dispatched while a CCCD descriptor write is still in flight, the same
    // protection m_writePending already gives characteristic writes.
    m_writePending = true;
    m_subscribeTimeoutTimer.start();
    m_service->writeDescriptor(notification, QByteArray::fromHex("0100"));
}

void BleTransport::disconnect() {
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_lastCommand = nullptr;
    m_writeRetryCount = 0;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();

    // Reset the notification-subscribe sequence so a stale in-flight
    // subscribeAll() from a torn-down connection can't bleed into the next
    // attempt's subscribeNext() chain.
    m_pendingSubscribeQueue.clear();
    m_currentSubscribeUuid = QBluetoothUuid();
    m_subscribeTimeoutTimer.stop();
    // Invalidate liveness so a torn-down connection's stale timestamp can't
    // make the very next fresh connect look like a zombie before its first
    // notification arrives (subscribeNext() re-baselines it on connected()).
    m_notificationLiveness.invalidate();

    // Stop any pending retries
    m_retryTimer.stop();
    m_connectWatchdogTimer.stop();
    m_pendingDevice = QBluetoothDeviceInfo();
    m_retryCount = 0;

    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }
    m_characteristics.clear();
    m_characteristicsReady = false;
    setServiceDiscoveryActive(false);

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;
    }

#ifdef Q_OS_ANDROID
    clearDE1AddressForShutdown();
    stopBleConnectionService();
#endif

    emit disconnected();
    // Reset the synthesis flag AFTER the emit so any listener that re-enters
    // connectToDevice() on this signal (expected: DE1Device's reconnect path)
    // starts the next attempt with a clean slate. Without this, a manual
    // disconnect() following the stateChanged synthesizer would leave the
    // flag stuck at true; the next attempt's connectToDevice() does its own
    // reset so this is defence in depth, not strictly required.
    m_disconnectedEmittedForAttempt = false;
}

qsizetype BleTransport::clearQueue() {
    // processCommandQueue dequeues a command before dispatching it, so the
    // currently-in-flight write is no longer in m_commandQueue but is still
    // live (m_writePending=true). Count it too — otherwise an aborted MMR
    // write would leave m_lastMMRValues claiming the DE1 has the value.
    qsizetype cleared = m_commandQueue.size() + (m_writePending ? 1 : 0);
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_lastCommand = nullptr;
    m_writeRetryCount = 0;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();
    return cleared;
}

bool BleTransport::isConnected() const {
    return m_controller &&
           (m_controller->state() == QLowEnergyController::ConnectedState ||
            m_controller->state() == QLowEnergyController::DiscoveredState) &&
           m_service != nullptr &&
           m_characteristicsReady;
}

// -- BLE-specific public API --

void BleTransport::connectToDevice(const QBluetoothDeviceInfo& device) {
    QString deviceId = device.address().isNull()
        ? device.deviceUuid().toString()
        : device.address().toString();

    bool zombieReconnect = false;
    if (isConnected()) {
        // Normally a redundant connect is a no-op. But a zombie link reports
        // connected and ACKs writes while silently delivering no notifications
        // — the reconnect ladder would then skip here forever and the DE1 UI
        // would freeze on stale data. If notifications have gone stale past the
        // threshold, treat this as a real (not redundant) reconnect: fall
        // through to teardown + fresh connect instead of returning.
        const bool notificationsStale =
            m_notificationLiveness.isValid()
            && m_notificationLiveness.elapsed() > NOTIFICATION_STALE_MS;
        if (!notificationsStale) {
            log(QString("connectToDevice(%1) skipped - already connected").arg(deviceId));
            return;
        }
        warn(QString("connectToDevice(%1) - link reports connected but notifications "
                     "stale for %2ms; tearing down suspected zombie link and reconnecting")
                 .arg(deviceId)
                 .arg(m_notificationLiveness.elapsed()));
        zombieReconnect = true;
    }

    if (m_controller) {
        log("Cleaning up previous controller before new connection");
        disconnect();
    }

    // Report the zombie fault only AFTER teardown. disconnect() emits
    // disconnected(), which flips BLEManager's m_de1Connected to false — so the
    // wedge detector's `!m_de1Connected` gate now passes and this fault can
    // actually contribute (emitting before teardown left it inert). It also
    // avoids running the fault handlers re-entrantly in the middle of teardown.
    // A single zombie fault only records the timestamp; sustained-wedge
    // recovery still needs its confirm window, so this cannot power-cycle the
    // adapter out from under the fresh connect started just below.
    if (zombieReconnect) {
        emit de1LinkFault(QStringLiteral("zombie-link"));
    }

    // Store device for potential retries and reset counter
    m_pendingDevice = device;
    m_retryCount = 0;
    m_retryTimer.stop();
    m_disconnectedEmittedForAttempt = false;

    log(QString("Connecting to DE1 at %1").arg(deviceId));

    if (!setupController(device)) {
        m_pendingDevice = QBluetoothDeviceInfo();
        return;
    }

    m_controller->connectToDevice();
}

// -- Private slots --

void BleTransport::onControllerConnected() {
    log("Controller connected, starting service discovery");

    // Connection-priority for the DE1 link (#342, #1093/#1176, design D8).
    // A default-constructed QLowEnergyConnectionParameters has minimumInterval
    // 7.5 ms, which Qt maps to BluetoothGatt.CONNECTION_PRIORITY_HIGH on
    // Android (interval < 30 ms ⇒ HIGH) — reducing the BLE connection interval
    // from the default ~30-50 ms to ~11-15 ms so Android GC pauses delay
    // notification delivery less.
    //
    // EXCEPT when the dual-HIGH-incapable latch is set: a proven-weak radio
    // cannot sustain TWO HIGH GATT links (scale + DE1) — the scale-only
    // backoff (#1185) is insufficient because a lone HIGH DE1 still starves
    // even a BALANCED scale (field log, #1176 shot-2). So the latched device
    // skips HIGH here too and runs the DE1 at the platform-default BALANCED
    // interval — both links BALANCED, the known-good config (matches de1app,
    // which requests no priority at all). The latch is the SAME app-run /
    // persisted BLEManager latch the scale transport consults (it is a
    // device-level property, not per-link). Eventually-consistent: a latch
    // set mid-run takes effect on the DE1's next connect — we do NOT
    // renegotiate a live link (consistent with the scale path / #1185).
    // Capable hardware never latches ⇒ DE1 keeps HIGH ⇒ no regression.
    // Logged in BOTH branches: this is the only DE1-side connection-priority
    // log line — it closes the long-standing DE1-priority observability gap.
    // (Android never confirms the negotiated interval: the Qt
    // connectionUpdated() signal exists but Android's BLE stack does not
    // reliably fire the underlying onConnectionUpdated callback, so Qt never
    // emits it in practice — no negotiated-interval feedback is available.)
#ifndef DECENZA_TESTING
    if (auto* mgr = BLEManager::instance(); mgr && mgr->scaleSkipHighPriority()) {
        log(QString("DE1 connection-priority: skipping HIGH "
                    "(dual-HIGH-incapable latch set, trigger=%1) — DE1 link "
                    "stays at BALANCED")
                .arg(mgr->scaleSkipHighTriggerKind()));
    } else {
        log("DE1 connection-priority: requesting HIGH");
        QLowEnergyConnectionParameters params;
        m_controller->requestConnectionUpdate(params);
    }
#else
    // Test build: blemanager.h is intentionally not included (see the
    // #ifndef DECENZA_TESTING include guard at the top of this file) and
    // blemanager.cpp is not linked, so BLEManager::instance() is unavailable
    // here — keep the original unconditional HIGH request. Production builds
    // (DECENZA_TESTING never defined) always take the latch-aware branch above.
    QLowEnergyConnectionParameters params;
    m_controller->requestConnectionUpdate(params);
#endif

    m_controller->discoverServices();
}

void BleTransport::onControllerDisconnected() {
    log("Controller disconnected");
#ifdef Q_OS_ANDROID
    clearDE1AddressForShutdown();
    stopBleConnectionService();
#endif

    // Clear pending BLE operations to prevent writes against a dead connection,
    // which causes DeadObjectException crashes on Android (issue #189)
    m_commandQueue.clear();
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_commandTimer.stop();
    m_characteristicsReady = false;
    setServiceDiscoveryActive(false);
    m_pendingSubscribeQueue.clear();
    m_currentSubscribeUuid = QBluetoothUuid();
    m_subscribeTimeoutTimer.stop();
    m_notificationLiveness.invalidate();

    if (!m_disconnectedEmittedForAttempt) {
        m_disconnectedEmittedForAttempt = true;
        emit disconnected();
    }
}

void BleTransport::onControllerError(QLowEnergyController::Error error) {
    QString errorName;
    QString userMessage;
    switch (error) {
        case QLowEnergyController::UnknownError:
            errorName = "UnknownError"; userMessage = "Unknown error"; break;
        case QLowEnergyController::UnknownRemoteDeviceError:
            errorName = "UnknownRemoteDeviceError"; userMessage = "Remote device not found"; break;
        case QLowEnergyController::NetworkError:
            errorName = "NetworkError"; userMessage = "Network error"; break;
        case QLowEnergyController::InvalidBluetoothAdapterError:
            errorName = "InvalidBluetoothAdapterError"; userMessage = "Invalid Bluetooth adapter"; break;
        case QLowEnergyController::ConnectionError:
            errorName = "ConnectionError"; userMessage = "Connection error"; break;
        case QLowEnergyController::AdvertisingError:
            errorName = "AdvertisingError"; userMessage = "Advertising error"; break;
        case QLowEnergyController::RemoteHostClosedError:
            errorName = "RemoteHostClosedError"; userMessage = "Remote device closed connection"; break;
        case QLowEnergyController::AuthorizationError:
            errorName = "AuthorizationError"; userMessage = "Authorization error"; break;
        case QLowEnergyController::MissingPermissionsError:
            errorName = "MissingPermissionsError"; userMessage = "Missing Bluetooth permissions"; break;
        default:
            errorName = QString::number(static_cast<int>(error)); userMessage = "Connection error"; break;
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
    warn(QString("!!! CONTROLLER ERROR: %1 (state=%2) !!!").arg(errorName, stateName));
    emit errorOccurred(userMessage);

    // Connection-teardown family is the dual-HIGH BLE-contention signature
    // (#1093 AuthorizationError, #1176 ConnectionError). Surface it to the
    // connection-priority coordinator. Scale-agnostic: this layer does not
    // know a scale exists; the coordinator only acts on it after a scale
    // has requested HIGH priority.
    if (error == QLowEnergyController::ConnectionError ||
        error == QLowEnergyController::RemoteHostClosedError ||
        error == QLowEnergyController::AuthorizationError) {
        emit de1LinkFault(QStringLiteral("controller-error"));
    }

    // Dump a one-shot Linux BT diagnostics block into the debug log the
    // first time any transport error fires. The issue template attaches
    // the debug log to every bug report, so this flows to maintainers
    // automatically. No-op on non-Linux.
    BleCapability::logLinuxBtDiagnosticsOnce();

    // On Linux, UnknownRemoteDeviceError usually means the process lacks
    // CAP_NET_ADMIN and BlueZ guessed the address type wrong. Only log the
    // setcap hint when we've actually detected the capability is missing —
    // otherwise we'd mislead users whose error has a different cause.
    if (error == QLowEnergyController::UnknownRemoteDeviceError
        && BleCapability::linuxMissing()) {
        warn(QStringLiteral("Linux hint: run `%1` and restart the app "
                            "(capability is often cleared by OS updates).")
                 .arg(BleCapability::linuxSetcapCommand()));
    }

    // Caps are fine but the DE1 still couldn't be resolved — almost always
    // a stale BlueZ cache after an OS upgrade. Ask BLEManager to surface
    // the recovery dialog (it de-dupes; only the first call per session
    // fires the signal). Linux-only: macOS/iOS/Android surface
    // UnknownRemoteDeviceError for unrelated reasons (Core Bluetooth cache,
    // Android scan-restart races) where the bluetoothctl/systemctl recovery
    // steps are irrelevant and confusing.
#if !defined(DECENZA_TESTING) && defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Guarded out of test builds: test targets link bletransport.cpp
    // without blemanager.cpp, and pulling blemanager.cpp in would drag in
    // most of the BLE stack (scales, refractometers, permissions).
    if (error == QLowEnergyController::UnknownRemoteDeviceError
        && !BleCapability::linuxMissing()) {
        if (auto* m = BLEManager::instance()) m->requestBluezCacheHint();
    }
#endif

    // A controller error during discovery would otherwise leave
    // m_serviceDiscoveryActive stuck at true (BlueZ does not always fire a
    // stateChanged→Unconnected after UnknownRemoteDeviceError). Reset here so
    // peer scales aren't held in pause forever after a connect-time failure.
    setServiceDiscoveryActive(false);

    // Synthesize disconnected() so the upper layer treats the attempt as
    // terminated. Without this, DE1Device::m_connecting stays true forever
    // after a connect-time error like UnknownRemoteDeviceError, and the
    // !isConnecting() guard in the de1Discovered handler (main.cpp) silently
    // drops every subsequent scan-triggered retry. Qt's stateChanged →
    // UnconnectedState synthesis at setupController() handles the common
    // case, but BlueZ does not reliably transition to Unconnected after
    // UnknownRemoteDeviceError when the device isn't in the adapter cache.
    // The m_disconnectedEmittedForAttempt guard de-dupes if Qt fires later.
    if (!m_disconnectedEmittedForAttempt) {
        m_disconnectedEmittedForAttempt = true;
        log("Controller error — synthesizing disconnected() so retry path can fire");
        emit disconnected();
    }
}

void BleTransport::onServiceDiscovered(const QBluetoothUuid& uuid) {
    log(QString("Service discovered: %1%2").arg(uuid.toString(), uuid == DE1::SERVICE_UUID ? " (DE1)" : ""));
    if (uuid == DE1::SERVICE_UUID) {
        m_service = m_controller->createServiceObject(uuid, this);
        if (m_service) {
            // Use Qt::QueuedConnection for all service signals - fixes iOS CoreBluetooth
            // threading issues where callbacks arrive on CoreBluetooth thread
            auto qc = Qt::QueuedConnection;
            connect(m_service, &QLowEnergyService::stateChanged,
                    this, &BleTransport::onServiceStateChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicChanged,
                    this, &BleTransport::onCharacteristicChanged, qc);
            connect(m_service, &QLowEnergyService::characteristicRead,
                    this, &BleTransport::onCharacteristicChanged, qc);  // Use same handler for reads
            connect(m_service, &QLowEnergyService::characteristicWritten,
                    this, &BleTransport::onCharacteristicWritten, qc);
            connect(m_service, &QLowEnergyService::descriptorWritten,
                    this, &BleTransport::onDescriptorWritten, qc);
            connect(m_service, &QLowEnergyService::errorOccurred,
                    this, [this](QLowEnergyService::ServiceError error) {
                // Log but don't fail on descriptor errors - common on Windows
                if (error != QLowEnergyService::DescriptorReadError &&
                    error != QLowEnergyService::DescriptorWriteError) {
                    // Handle write errors with retry (like de1app)
                    if (error == QLowEnergyService::CharacteristicWriteError && m_writePending) {
                        m_writePending = false;
                        m_writeTimeoutTimer.stop();
                        if (m_lastCommand && m_writeRetryCount < MAX_WRITE_RETRIES) {
                            m_writeRetryCount++;
                            log(QString("CharacteristicWriteError, retrying %1/%2 (uuid=%3)")
                                .arg(m_writeRetryCount).arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                            // Intentionally NOT a de1LinkFault: a single transient
                            // retry that then succeeds is normal even on capable
                            // hardware. Only write-failed (retries exhausted) and
                            // connection-teardown errors count toward the dual-HIGH
                            // signature (matches design D1) — counting every retry
                            // produced false positives on healthy devices.
                            QTimer::singleShot(WRITE_RETRY_DELAY_MS, this, [this]() {
                                if (m_lastCommand) {
                                    m_lastCommand();
                                }
                            });
                        } else {
                            warn(QString("CharacteristicWriteError FAILED after %1 retries (uuid=%2)")
                                .arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid));
                            // No user-facing errorOccurred here — same rationale as the
                            // write-timeout exhaustion path above (#1423).
                            emit de1LinkFault(QStringLiteral("write-failed"));
                            m_lastCommand = nullptr;
                            m_writeRetryCount = 0;
                            processCommandQueue();
                            if (!m_writePending && m_commandQueue.isEmpty())
                                emit queueDrained();
                        }
                    } else {
                        // Log BEFORE emitting. This branch used to emit straight to
                        // the UI with no log call at all, so a user could report
                        // "Service error: 5" and the debug log they attached would
                        // not contain it anywhere — the one error they named was
                        // the one thing undiagnosable from the capture (#1586).
                        const QString name = bleServiceErrorName(error);
                        warn(QString("SERVICE ERROR: %1").arg(name));
                        emit errorOccurred(QString("Service error: %1").arg(name));
                    }
                } else {
                    log(QString("Descriptor error (suppressed): %1").arg(bleServiceErrorName(error)));
                }
            }, qc);
            log("Starting characteristic discovery for DE1 service");
            setServiceDiscoveryActive(true);
            m_service->discoverDetails();
        } else {
            warn("ERROR: createServiceObject() returned null for DE1 service UUID");
            emit errorOccurred("Failed to initialize DE1 service - try reconnecting");
        }
    }
}

void BleTransport::onServiceDiscoveryFinished() {
    if (!m_service) {
        // Retry logic - Android sometimes returns wrong/cached services
        m_retryCount++;
        if (m_retryCount <= MAX_RETRIES && m_pendingDevice.isValid()) {
            log(QString("DE1 service not found after discovery, scheduling retry %1/%2").arg(m_retryCount).arg(MAX_RETRIES));
            if (m_controller) {
                m_controller->disconnectFromDevice();
            }
            m_retryTimer.start();
        } else {
            log("DE1 service not found after all retries");
            emit errorOccurred("DE1 service not found after " + QString::number(MAX_RETRIES) + " retries. Try toggling Bluetooth off/on.");
            m_pendingDevice = QBluetoothDeviceInfo();
            disconnect();
        }
    } else {
        log("Service discovery complete - DE1 service found");
        // Success - clear pending device
        m_pendingDevice = QBluetoothDeviceInfo();
        m_retryCount = 0;
    }
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        setupService();
        m_characteristicsReady = true;
        log(QString("Characteristics ready: %1 registered").arg(m_characteristics.size()));
        // Discovery window closed — peer scales can resume normal write traffic.
        setServiceDiscoveryActive(false);

#ifdef Q_OS_ANDROID
        // Store address for shutdown service (handles swipe-to-kill)
        if (m_controller) {
            storeDE1AddressForShutdown(m_controller->remoteAddress().toString());
        }
        // Start foreground service to prevent Samsung/OEM app killing
        startBleConnectionService();
#endif

        // connected() is emitted from subscribeNext() once every notification
        // subscription is confirmed (or individually timed out) — see
        // subscribeAll()/subscribeNext() for why this can no longer fire here
        // immediately.
        subscribeAll();
    }
}

void BleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // Any inbound notification/read response is proof the link is delivering
    // data — restart the liveness clock the zombie-link check in
    // connectToDevice() consults. (This slot is wired to both
    // characteristicChanged and characteristicRead; in steady state the DE1's
    // periodic pushes dominate, which is exactly the signal we want.)
    m_notificationLiveness.restart();
    emit dataReceived(c.uuid(), value);
}

void BleTransport::onDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value) {
    Q_UNUSED(value);
    // Only meaningful mid-subscribeAll() sequence. An ad-hoc subscribe(uuid)
    // call outside that sequence (e.g. firmware update's FW_MAP_REQUEST
    // subscribe) doesn't arm this timer and isn't part of this bookkeeping.
    if (!m_subscribeTimeoutTimer.isActive()) return;

    // Only advance when the CCCD that just completed is the one for the
    // subscription currently in flight. Without this check, a late ACK from a
    // characteristic that already TIMED OUT (its write is still pending in the
    // stack after subscribeNext() moved on) would be misattributed to the
    // current step and advance the sequence prematurely — skipping a real
    // confirmation and re-opening the dropped-notification race this exists to
    // close. Exactly the congested-radio timing this change targets.
    if (!m_characteristics.contains(m_currentSubscribeUuid)) return;
    const QLowEnergyDescriptor expected = m_characteristics[m_currentSubscribeUuid].descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (descriptor != expected) return;  // stray/late ACK for a different characteristic

    m_subscribeTimeoutTimer.stop();
    m_writePending = false;
    subscribeNext();
}

void BleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    m_writePending = false;
    m_writeTimeoutTimer.stop();
    m_writeRetryCount = 0;
    m_lastCommand = nullptr;
    m_lastWriteUuid.clear();
    m_lastWriteData.clear();

    emit writeComplete(c.uuid(), value);
    processCommandQueue();

    if (!m_writePending && m_commandQueue.isEmpty())
        emit queueDrained();
}

// -- Private helpers --

void BleTransport::setServiceDiscoveryActive(bool active) {
    if (m_serviceDiscoveryActive == active) return;
    m_serviceDiscoveryActive = active;
    emit serviceDiscoveryActiveChanged(active);
}

void BleTransport::log(const QString& message) {
    QString msg = QString("[BLE DE1] ") + message;
    qDebug().noquote() << msg;
    emit logMessage(msg);
}

void BleTransport::warn(const QString& message) {
    QString msg = QString("[BLE DE1] ") + message;
    qWarning().noquote() << msg;
    emit logMessage(msg);
}

bool BleTransport::setupController(const QBluetoothDeviceInfo& device) {
    m_controller = QLowEnergyController::createCentral(device, this);
    if (!m_controller) {
        warn("ERROR: Failed to create BLE controller!");
        emit errorOccurred("Failed to create BLE controller");
        return false;
    }

    // Use Qt::QueuedConnection for all BLE signals - fixes iOS CoreBluetooth threading
    // issues where callbacks arrive on CoreBluetooth thread and cause re-entrancy/crash
    auto qc = Qt::QueuedConnection;
    connect(m_controller, &QLowEnergyController::connected,
            this, &BleTransport::onControllerConnected, qc);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleTransport::onControllerDisconnected, qc);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleTransport::onControllerError, qc);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleTransport::onServiceDiscovered, qc);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleTransport::onServiceDiscoveryFinished, qc);
    // Log all controller state changes for debugging, and synthesize a
    // disconnected() signal for failed connect attempts.
    //
    // Qt's QLowEnergyController::disconnected() signal only fires on a
    // Connected→Disconnected transition — NOT when a connection attempt fails
    // (Connecting→Unconnected without ever reaching Connected). Without a
    // synthesized emission, DE1Device::m_connecting would stick at true
    // forever after a failed retry, and the reconnect loop (plus the
    // de1Discovered handler) would bail out every subsequent attempt with
    // "already connected/connecting". This was the root cause of the
    // "DE1 reboot → app never reconnects until restarted" bug.
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
        this->log(QString("Controller state: %1").arg(stateName));

        // Connect watchdog: arm while Connecting, disarm on any resolution.
        if (state == QLowEnergyController::ConnectingState) {
            m_connectWatchdogTimer.start();
        } else {
            m_connectWatchdogTimer.stop();
        }

        if (state == QLowEnergyController::UnconnectedState
            && !m_disconnectedEmittedForAttempt) {
            // Terminal failure of a connect attempt — Qt won't fire
            // disconnected() for us, so synthesize it. The flag prevents
            // double-emission if Qt's native disconnected() also fires.
            m_disconnectedEmittedForAttempt = true;
            this->log("Connection attempt failed — synthesizing disconnected()");
            emit disconnected();
        }
    }, qc);

    return true;
}

void BleTransport::setupService() {
    if (!m_service) return;

    const QList<QLowEnergyCharacteristic> chars = m_service->characteristics();
    for (const auto& c : chars) {
        m_characteristics[c.uuid()] = c;
        log(QString("  Char %1 props=0x%2")
            .arg(c.uuid().toString().mid(1, 8))
            .arg(static_cast<int>(c.properties()), 2, 16, QChar('0')));
    }
}

void BleTransport::writeCharacteristic(const QBluetoothUuid& uuid, const QByteArray& data) {
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("writeCharacteristic(%1) skipped - %2").arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        return;
    }
    // Don't hand a write to Qt once the controller has left the connected/
    // discovered state. Writing through a torn-down QLowEnergyController crashes
    // inside DarwinBTCentralManager's write queue on the LE dispatch queue
    // (iOS #1400 — symbolicated to the GATT write path; the periodic MMR
    // keepalive is the likely trigger writing to a dead link). We guard on
    // controller state (not isConnected(), which also requires
    // m_characteristicsReady) so connection-setup writes still go through.
    const auto controllerState = m_controller ? m_controller->state()
                                               : QLowEnergyController::UnconnectedState;
    if (controllerState != QLowEnergyController::ConnectedState
        && controllerState != QLowEnergyController::DiscoveredState) {
        log(QString("writeCharacteristic(%1) skipped - controller not connected (state %2)")
                .arg(uuid.toString().mid(1, 8)).arg(static_cast<int>(controllerState)));
        return;
    }
    m_writePending = true;
    QString uuidShort = uuid.toString().mid(1, 8);
    m_lastWriteUuid = uuidShort;
    m_lastWriteData = data;
    m_writeTimeoutTimer.start();
    m_service->writeCharacteristic(m_characteristics[uuid], data);
}

void BleTransport::queueCommand(std::function<void()> command) {
    m_commandQueue.enqueue(command);
    if (!m_writePending && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}

void BleTransport::processCommandQueue() {
    if (m_writePending || m_commandQueue.isEmpty()) return;

    auto command = m_commandQueue.dequeue();
    m_lastCommand = command;  // Store for potential retry
    command();

    // Reads don't set m_writePending and don't re-enter via
    // onCharacteristicWritten, so the queue would otherwise stall after a
    // dispatched read until some other queueCommand() call. Re-arm the timer
    // here so subsequent queued items continue draining.
    if (!m_writePending && !m_commandQueue.isEmpty() && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}
