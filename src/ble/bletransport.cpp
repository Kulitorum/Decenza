#include "bletransport.h"
#include "blecapability.h"
#include "bledeviceid.h"
#include "blecontrollererror.h"
#include "bleserviceerror.h"
#include "de1logging.h"
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
        DE1_WARN_STDERR_TAGGED("Android", QStringLiteral("storeDE1AddressForShutdown: Android context is invalid"));
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
        DE1_WARN_STDERR_TAGGED("Android", QStringLiteral("clearDE1AddressForShutdown: Android context is invalid"));
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
        DE1_WARN_STDERR_TAGGED("Android", QStringLiteral("startBleConnectionService: Android context is invalid"));
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
        DE1_WARN_STDERR_TAGGED("Android", QStringLiteral("stopBleConnectionService: Android context is invalid"));
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
        // Proceeding without confirmation means this characteristic's
        // notifications may never start flowing this session, yet the app will
        // present as fully connected. Name the stream and the consequence so a
        // field AI reading the log can tie "telemetry frozen / stale data" back
        // to this line rather than having to infer it. The zombie-link detector
        // is the only in-session backstop (up to NOTIFICATION_STALE_MS later).
        warn(QString("Notification subscribe timed out (%1) — proceeding without "
                     "confirmation; that stream's notifications may not flow until "
                     "the next reconnect")
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
                // Count BEFORE the emit. de1LinkFault runs its consumers
                // synchronously, and one of them reaching disconnect() would
                // run noteWriteSucceeded() and zero the counter, so the
                // increment below would restart the episode from 1 on exactly
                // the links that fault hardest. Nothing here reads state the
                // emit could invalidate, so the safe order is free.
                noteWriteAbandoned();
                emit writeAbandoned(m_lastWriteUuidFull, m_lastWriteData);
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
    queueCommand(uuid, [this, uuid, data]() {
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
        m_commandQueue.prepend({uuid, [this, uuid, data]() {
            writeCharacteristic(uuid, data);
        }});
    } else {
        writeCharacteristic(uuid, data);
    }
}

void BleTransport::read(const QBluetoothUuid& uuid) {
    // Queue the read so it runs after any pending writes complete. Without
    // queueing, a read issued right after a write executes immediately and
    // returns the pre-write value, defeating any read-after-write verification.
    queueCommand(uuid, [this, uuid]() {
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
    forgetWriteFailureState();
    m_queueDepthReported = false;

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

qsizetype BleTransport::discardQueued(const QList<QBluetoothUuid>& uuids) {
    if (uuids.isEmpty() || m_commandQueue.isEmpty()) return 0;

    // Deliberately does NOT touch the in-flight write. clearQueue() counts it
    // because its callers are about to change machine state and need the MMR
    // dedup cache invalidated if an MMR write was mid-air; here the caller is
    // withdrawing work it queued itself, and a write already dispatched has
    // either landed or is being retried by the write-timeout path.
    const qsizetype before = m_commandQueue.size();
    QQueue<QueuedCommand> kept;
    for (const QueuedCommand& c : std::as_const(m_commandQueue)) {
        if (!uuids.contains(c.uuid))
            kept.enqueue(c);
    }
    m_commandQueue.swap(kept);

    const qsizetype dropped = before - m_commandQueue.size();
    if (dropped > 0)
        log(QString("Discarded %1 queued command(s) for %2 characteristic(s)")
                .arg(dropped).arg(uuids.size()));
    return dropped;
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
    const QString deviceId = getDeviceIdentifier(device);

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

    // DEBUG. I promoted this to INFO on the argument that a failed connect would
    // otherwise be invisible, then checked what actually precedes it: on the
    // scan path BLEManager has just logged "Found DE1: <name> (<id>)" with the
    // same identifier, and on the direct-wake path "Direct wake: connecting to
    // <name> at <addr>". A third telling of the same fact on BOTH THOSE PATHS.
    //
    // Not on all four, though — DE1Device::connectToDevice is also called by the
    // `devices_connect_de1` MCP tool and by the connections-page device list, and
    // neither is preceded by either line (the list's "Found DE1:" may be many
    // minutes old). DE1Device::connectToDevice carries the INFO for those, at the
    // one layer every caller passes through.
    log(QStringLiteral("Connecting to DE1 at %1").arg(deviceId));

    if (!setupController(device)) {
        m_pendingDevice = QBluetoothDeviceInfo();
        return;
    }

    m_controller->connectToDevice();
}

// -- Private slots --

void BleTransport::onControllerConnected() {
    // The one shared BLE lifecycle event, in the shared words: the same line
    // every scale driver prints, so a reader comparing a DE1 log to a scale log
    // is comparing identical text.
    info(DECENZA_BLE_MSG_TRANSPORT_CONNECTED);

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
    info(DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED);
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
    // Failures observed while a link was already dying must not be carried
    // into the next connection, where they would make a healthy link look
    // write-dead after one more abandoned write.
    forgetWriteFailureState();
    m_queueDepthReported = false;

    if (!m_disconnectedEmittedForAttempt) {
        m_disconnectedEmittedForAttempt = true;
        emit disconnected();
    }
}

void BleTransport::onControllerError(QLowEnergyController::Error error) {
    const QString errorName = bleControllerErrorName(error);
    const QString stateName = bleControllerStateName(
        m_controller ? m_controller->state() : QLowEnergyController::UnconnectedState);
    warn(QString("!!! CONTROLLER ERROR: %1 (state=%2) !!!").arg(errorName, stateName));

    // A controller error never raises a modal. The whole path used to, and the
    // dialog it produced carried no information the user could act on: a bare
    // "Connection error" box with a single OK button, restating what the status
    // bar's machineStatus widget already says in words — a red icon and
    // "Disconnected" (qml/components/layout/items/MachineStatusItem.qml, present
    // by default per settings_network.cpp and drawn on every page).
    //
    // In the #1658 reporter's log it fired on every app start — they switch the
    // DE1 off overnight, and the direct-wake connect to the saved address
    // (main.cpp's startup path) runs before the machine finishes powering up —
    // while the reconnect ladder recovered the link 26-118 s later in every one
    // of the 7 affected sessions. Those figures are from that one report, not a
    // measured system property. AuthorizationError had already been carved out
    // for the same reason (#1093), as had write-retry exhaustion (#1423); this
    // finishes the job for the rest of the enum.
    //
    // Where the actionable ones go instead, stated with their real scope rather
    // than as blanket coverage:
    //   - permissions: BLEManager::onScanError (all platforms — the DE1 connect
    //     path always runs a scan alongside, see tryDirectConnectToDE1) and
    //     requestBluetoothPermission (Android/iOS only);
    //   - an adapter that will not come back up: finishAdapterRecovery, Android
    //     only, since that is the only platform that power-cycles the radio;
    //   - a link that connects but yields no DE1 service: the retry-exhausted
    //     branch below ("try toggling Bluetooth off/on"), all platforms.
    // On desktop a controller-level MissingPermissionsError or
    // InvalidBluetoothAdapterError is therefore log-only. That is deliberate:
    // the scan agent raises the same condition with a better message, and the
    // warn line above carries it into the debug log the issue template collects.

    // The link-teardown family is the dual-HIGH BLE-contention signature (#1093
    // AuthorizationError, #1176 ConnectionError, #1238 RemoteHostClosedError).
    // Surface it to the connection-priority coordinator. Scale-agnostic: this
    // layer does not know a scale exists; the coordinator only acts on it after
    // a scale has requested HIGH priority.
    if (bleControllerErrorIsLinkTeardown(error)) {
        emit de1LinkFault(QStringLiteral("controller-error"));
    }

    // Both Linux diagnostics below are guarded out of test builds, for the same
    // reason as the BLEManager block further down but with a sharper edge: they
    // are the only two things in this function that behave differently by
    // platform, and both are invisible on a macOS dev machine.
    //
    //   - logLinuxBtDiagnosticsOnce() is genuinely a no-op off Linux, but ON
    //     Linux it spawns a detached QThread running up to three 2-second
    //     QProcess calls, cleaned up via a queued deleteLater. A QTEST_MAIN
    //     process returns straight after qExec, so that deleteLater never gets
    //     an event-loop turn and the thread leaks — which only the nightly
    //     Linux ASan job can see, since LSan does not exist on macOS.
    //   - the setcap hint calls linuxMissing(), whose first call ALSO warns
    //     about CAP_NET_ADMIN. Unprivileged CI runners have no CAP_NET_ADMIN,
    //     so on Linux an UnknownRemoteDeviceError emits two extra qWarnings
    //     that a test's QTest::failOnWarning() would fail on.
    //
    // Neither is behaviour — both are diagnostics for a human reading the debug
    // log — so a test build losing them costs nothing.
#ifndef DECENZA_TESTING
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
#endif

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
                            warn(QString("CharacteristicWriteError FAILED after %1 retries (uuid=%2, %3 bytes)")
                                .arg(MAX_WRITE_RETRIES).arg(m_lastWriteUuid).arg(m_lastWriteData.size()));
                            // No user-facing errorOccurred here — same rationale as the
                            // write-timeout exhaustion path above (#1423).
                            // Counted before the emit — see the write-timeout
                            // path for why the order matters.
                            noteWriteAbandoned();
                            emit writeAbandoned(m_lastWriteUuidFull, m_lastWriteData);
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
            warn(QStringLiteral("DE1 service not found after all retries"));
            emit errorOccurred("DE1 service not found after " + QString::number(MAX_RETRIES) + " retries. Try toggling Bluetooth off/on.");
            m_pendingDevice = QBluetoothDeviceInfo();
            disconnect();
        }
    } else {
        info(QStringLiteral("Service discovery complete — DE1 service found"));
        // Success - clear pending device
        m_pendingDevice = QBluetoothDeviceInfo();
        m_retryCount = 0;
    }
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        setupService();
        m_characteristicsReady = true;
        // INFO: the machine is addressable from here on, which is the moment a
        // user is waiting for. Its absence after "service found" is the
        // fingerprint of a half-open link.
        info(QString("Characteristics ready: %1 registered").arg(m_characteristics.size()));
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
    noteWriteSucceeded();
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
    DE1_LOG_TAGGED("BLE", message);
}

void BleTransport::info(const QString& message) {
    DE1_INFO_TAGGED("BLE", message);
}

void BleTransport::warn(const QString& message) {
    DE1_WARN_TAGGED("BLE", message);
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
    m_lastWriteUuidFull = uuid;
    m_lastWriteData = data;
    m_writeTimeoutTimer.start();
    m_service->writeCharacteristic(m_characteristics[uuid], data);
}

void BleTransport::noteWriteAbandoned() {
    ++m_consecutiveWriteFailures;
    if (m_consecutiveWriteFailures < WRITE_DEAD_LINK_THRESHOLD)
        return;

    if (m_writeDeadLinkReported) {
        // Already reported; restate the run periodically so the log carries how
        // bad it got, not just that it started. See WRITE_DEAD_LINK_RESTATE.
        if ((m_consecutiveWriteFailures - WRITE_DEAD_LINK_THRESHOLD) % WRITE_DEAD_LINK_RESTATE == 0) {
            warn(QString("DE1 link still not accepting writes: %1 consecutive "
                         "writes abandoned so far.")
                     .arg(m_consecutiveWriteFailures));
        }
        return;
    }

    m_writeDeadLinkReported = true;

    // Deliberately not corroborated against m_controller->state(). decaid
    // confirms its equivalent finding with an OS connection-state query and
    // treats an inconclusive answer as changing nothing
    // (universal_ble_transport.dart:466-483) — a good rule, but Qt's
    // QLowEnergyController::state() is not that query. It reports what Qt
    // believes, and Qt believing the link is up is precisely the condition
    // being reported here, so reading it could only ever confirm what we
    // already know. Adding it would look like corroboration while supplying
    // none.
    //
    // WARN, and written to stand alone: these logs are read by users and by
    // their AI assistants, who have no knowledge of this subsystem, so a bare
    // failure count would be uninterpretable.
    warn(QString("DE1 link has stopped accepting writes: %1 consecutive writes "
                 "abandoned after exhausting their retries, while the link "
                 "still reports itself connected. Commands sent to the machine "
                 "are being discarded. Reconnecting the DE1 is what clears "
                 "this — from the Connections page, or over MCP with "
                 "devices_connect_de1.")
             .arg(m_consecutiveWriteFailures));
}

void BleTransport::noteWriteSucceeded() {
    if (m_writeDeadLinkReported) {
        // INFO, not DEBUG: this is the other half of a WARN a user has already
        // seen, and it carries the peak — the number the threshold was derived
        // from and the one a later reader needs to judge severity.
        info(QString("DE1 link is accepting writes again. The run reached %1 "
                     "consecutive abandoned writes before recovering.")
                 .arg(m_consecutiveWriteFailures));
    }
    m_consecutiveWriteFailures = 0;
    m_writeDeadLinkReported = false;
}

void BleTransport::forgetWriteFailureState() {
    if (m_writeDeadLinkReported) {
        warn(QString("Link dropped while it had stopped accepting writes. The "
                     "run reached %1 consecutive abandoned writes.")
                 .arg(m_consecutiveWriteFailures));
    }
    m_consecutiveWriteFailures = 0;
    m_writeDeadLinkReported = false;
}

void BleTransport::queueCommand(const QBluetoothUuid& uuid, std::function<void()> command) {
    m_commandQueue.enqueue({uuid, std::move(command)});

    // Report a backlog once per episode. A queue this deep means the link is
    // not keeping up, and today that is only ever visible after the fact, as
    // the write failures it goes on to produce. Nothing is shed — see the
    // header.
    if (m_commandQueue.size() >= QUEUE_DEPTH_WARN) {
        if (!m_queueDepthReported) {
            m_queueDepthReported = true;
            warn(QString("BLE write queue is %1 deep — the link is not keeping "
                         "up with the commands being issued")
                     .arg(m_commandQueue.size()));
        }
    } else if (m_commandQueue.size() <= QUEUE_DEPTH_WARN / 2) {
        // Re-arm only once well clear of the threshold, so a queue hovering at
        // the boundary does not log on every other enqueue.
        m_queueDepthReported = false;
    }
    if (!m_writePending && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}

void BleTransport::processCommandQueue() {
    if (m_writePending || m_commandQueue.isEmpty()) return;

    auto command = m_commandQueue.dequeue();
    m_lastCommand = command.run;  // Store for potential retry
    command.run();

    // Reads don't set m_writePending and don't re-enter via
    // onCharacteristicWritten, so the queue would otherwise stall after a
    // dispatched read until some other queueCommand() call. Re-arm the timer
    // here so subsequent queued items continue draining.
    if (!m_writePending && !m_commandQueue.isEmpty() && !m_commandTimer.isActive()) {
        m_commandTimer.start();
    }
}
