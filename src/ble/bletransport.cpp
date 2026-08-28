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

BleTransport::BleTransport(QObject* parent, BleGattQueue* queue)
    : DE1Transport(parent)
    , m_gattQueue(queue ? queue : &BleGattQueue::instance())
{
    // The outer bound on an operation the platform never answers. Retry and
    // abandonment are the queue's; this only says "no answer at all is also an
    // answer". See the member's declaration for why there is exactly one.
    m_operationTimeoutTimer.setSingleShot(true);
    connect(&m_operationTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_gattQueue->inFlightRequester() != this) return;
        log(QString("No answer for %1 within %2 ms")
                .arg(m_gattQueue->inFlightKey().toString().mid(1, 8))
                .arg(m_operationTimeoutTimer.interval()));
        m_gattQueue->noteFailed(this);
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
    submitWrite(uuid, data, /*toFront=*/false);
}

void BleTransport::writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) {
    // Urgency is queue POSITION, never a bypass: this jumps ahead of everything
    // waiting but still waits for whatever is outstanding, on this device or any
    // other. That is the whole guarantee the queue provides.
    //
    // It does NOT clear — callers that need to clear (SAW, sleep) do so
    // explicitly first — so ensureChargerOn() on app suspend cannot drop pending
    // extraction frames.
    //
    // The write it carries used to be issued synchronously when nothing was in
    // flight, and now waits one event-loop turn for the queue's posted dispatch.
    // That costs nothing real: QLowEnergyService::writeCharacteristic is itself
    // asynchronous, so the synchronous call only ever reached Qt's own per-
    // controller queue, which needs the same event loop to drain.
    submitWrite(uuid, data, /*toFront=*/true);
}

void BleTransport::read(const QBluetoothUuid& uuid) {
    submitRead(uuid);
}

void BleTransport::subscribe(const QBluetoothUuid& uuid) {
    // Ad-hoc subscribe outside the connect sequence — the firmware updater's
    // FW_MAP_REQUEST. Not a required stream: nothing about the machine's
    // usability depends on it.
    submitSubscribe(uuid, /*required=*/false);
}

void BleTransport::subscribeAll() {
    if (!m_service) return;

    m_streamsNotEnabled.clear();

    // One queue entry per stream, then the marker that reports the link ready,
    // then the initial reads. FIFO ordering is what makes this a plain list
    // instead of the hand-rolled recursion it replaces — the sequencing, the
    // one-at-a-time confirmation and the late-ACK matching are all the queue's
    // now.
    //
    // Enabling notifications one confirmed step at a time still matters for the
    // reason it always did: a one-shot MMR read issued once connected() fires
    // can have its response notification sent by the DE1 before the client has
    // finished enabling notifications for READ_FROM_MMR, and that response is
    // then silently dropped with no recovery — unlike a repeating stream such as
    // STATE_INFO, which self-heals on the next push.
    //
    // The two required streams are checked for a SUBMISSION-time failure and
    // stop the sequence. failRequiredStream()'s forget() expresses "do not
    // report connected" by dropping the ready marker, and at this point the
    // marker has not been submitted yet — so on this path, unlike on the
    // abandonment path, returning is what stops it being queued at all.
    if (!submitSubscribe(DE1::Characteristic::STATE_INFO,  /*required=*/true)) return;
    if (!submitSubscribe(DE1::Characteristic::SHOT_SAMPLE, /*required=*/true)) return;
    submitSubscribe(DE1::Characteristic::WATER_LEVELS,  /*required=*/false);
    submitSubscribe(DE1::Characteristic::READ_FROM_MMR, /*required=*/false);
    submitSubscribe(DE1::Characteristic::TEMPERATURES,  /*required=*/false);
    // Sensor calibration replies. Not required: a machine or transport that
    // cannot notify here must still connect normally — the calibration wizards
    // then show their values as unavailable and refuse to write, rather than the
    // whole connection failing over a screen almost nobody opens.
    submitSubscribe(DE1::Characteristic::CALIBRATION,   /*required=*/false);
    // SHOT_SETTINGS is intentionally NOT subscribed: the DE1 firmware does
    // not push notifications on writes (confirmed in de1app's de1_comms.tcl).
    // Verification happens via explicit read() after each write in
    // DE1Device::setShotSettings().

    submitReadyMarker();

    // Queued after the marker, exactly as before: these are plain GATT reads
    // (request/response over the same ATT transaction), so they don't share the
    // CCCD-vs-notification race and connected() need not wait on them.
    read(DE1::Characteristic::VERSION);
    read(DE1::Characteristic::STATE_INFO);
    read(DE1::Characteristic::WATER_LEVELS);
    read(DE1::Characteristic::SHOT_SETTINGS);
}

void BleTransport::disconnect() {
    // Releases the slot if we hold it and drops everything of ours still
    // waiting, including any half-finished subscribeAll() sequence — a
    // torn-down connection's queued work must not bleed into the next attempt,
    // and a dead link must not hold the radio for every other device.
    m_operationTimeoutTimer.stop();
    m_gattQueue->forget(this);
    forgetWriteFailureState();

    // Invalidate liveness so a torn-down connection's stale timestamp can't
    // make the very next fresh connect look like a zombie before its first
    // notification arrives (the ready marker re-baselines it on connected()).
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
    // forget() counts the in-flight operation as well as the queued ones, which
    // is what this caller needs: it is about to change machine state and must
    // invalidate the MMR dedup cache if an MMR write was mid-air. Under-report
    // there and m_lastMMRValues claims the DE1 holds a value it never received.
    m_operationTimeoutTimer.stop();
    return m_gattQueue->forget(this);
}

qsizetype BleTransport::discardQueued(const QList<QBluetoothUuid>& uuids) {
    // Scoped to this transport's own entries, and deliberately does NOT touch
    // the in-flight operation — the asymmetry with clearQueue() above. Here the
    // caller is withdrawing work it queued itself, and an operation already
    // dispatched has either landed or is being retried. Counting it would report
    // a cancellation that did not happen.
    return m_gattQueue->discard(this, uuids);
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
    m_operationTimeoutTimer.stop();
    m_gattQueue->forget(this);
    m_characteristicsReady = false;
    m_notificationLiveness.invalidate();
    // Failures observed while a link was already dying must not be carried
    // into the next connection, where they would make a healthy link look
    // write-dead after one more abandoned write.
    forgetWriteFailureState();

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
                    this, &BleTransport::onCharacteristicRead, qc);
            connect(m_service, &QLowEnergyService::characteristicWritten,
                    this, &BleTransport::onCharacteristicWritten, qc);
            connect(m_service, &QLowEnergyService::descriptorWritten,
                    this, &BleTransport::onDescriptorWritten, qc);
            connect(m_service, &QLowEnergyService::errorOccurred,
                    this, [this](QLowEnergyService::ServiceError error) {
                const QString name = bleServiceErrorName(error);
                switch (error) {
                // OperationError is the one Qt emits most: every synchronous
                // rejection sets it, NOT the per-operation errors below.
                // qlowenergyservice.cpp does it in discoverDetails (:581),
                // readCharacteristic (:650), writeCharacteristic (:724) and
                // writeDescriptor — a null controller, a service that is not
                // RemoteServiceDiscovered, or a characteristic the service does
                // not contain. Left in the default arm it raised a user-facing
                // error and never released the slot, so one rejection cost every
                // device on the radio 5 s, or 20 s if it landed during discovery.
                case QLowEnergyService::OperationError:
                case QLowEnergyService::CharacteristicWriteError:
                case QLowEnergyService::CharacteristicReadError:
                case QLowEnergyService::DescriptorWriteError: {
                    // The operation in flight has failed, definitively and now.
                    // Retry (if its budget allows) and abandonment are the
                    // queue's; this only reports the outcome and releases the
                    // clock.
                    //
                    // DescriptorWriteError used to be swallowed at DEBUG as
                    // "common on Windows". It is the #1819 signal: three CCCD
                    // writes were rejected at +45 ms while a scale ran discovery
                    // on the same stack, each was then waited out for a full
                    // 3 s, and the machine was reported CONNECTED with no
                    // telemetry enabled. INFO, because the audience is the user
                    // reading the connections view — which filters to INFO — and
                    // not the protocol detail DEBUG is for.
                    if (m_gattQueue->inFlightRequester() != this) {
                        log(QString("%1 with no operation of ours in flight — ignored").arg(name));
                        break;
                    }
                    info(QString("%1 on %2 — the operation failed and will be retried "
                                 "if its budget allows")
                             .arg(name, m_gattQueue->inFlightKey().toString().mid(1, 8)));
                    // Intentionally NOT a de1LinkFault: a single transient
                    // failure that then succeeds is normal even on capable
                    // hardware. Only exhaustion and connection-teardown errors
                    // count toward the dual-HIGH signature (design D1) —
                    // counting every retry produced false positives on healthy
                    // devices.
                    m_operationTimeoutTimer.stop();
                    m_gattQueue->noteFailed(this);
                    break;
                }
                case QLowEnergyService::DescriptorReadError:
                    // Nothing here ever reads a descriptor, so this can only be
                    // a stack quirk about one we wrote. Genuinely noise.
                    log(QString("Descriptor read error (suppressed): %1").arg(name));
                    break;
                default:
                    // Log BEFORE emitting. This branch used to emit straight to
                    // the UI with no log call at all, so a user could report
                    // "Service error: 5" and the debug log they attached would
                    // not contain it anywhere — the one error they named was
                    // the one thing undiagnosable from the capture (#1586).
                    warn(QString("SERVICE ERROR: %1").arg(name));
                    emit errorOccurred(QString("Service error: %1").arg(name));
                    break;
                }
            }, qc);
            submitDiscovery();
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
    if (state == QLowEnergyService::InvalidService) {
        // Definitive: discovery is not going to complete. Left unhandled it
        // burned the full DISCOVERY_TIMEOUT_MS with no output at all, and the
        // whole radio with it.
        warn(QStringLiteral("DE1 service became invalid — characteristic discovery "
                            "cannot complete; the connection attempt will be retried"));
        m_operationTimeoutTimer.stop();
        m_gattQueue->noteFailed(this);
        return;
    }
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        completeOperation(DE1::SERVICE_UUID);
        emitQueueDrainedIfIdle();
        setupService();
        m_characteristicsReady = true;
        // INFO: the machine is addressable from here on, which is the moment a
        // user is waiting for. Its absence after "service found" is the
        // fingerprint of a half-open link.
        info(QString("Characteristics ready: %1 registered").arg(m_characteristics.size()));
        // Discovery window closed — peer scales can resume normal write traffic.
    
#ifdef Q_OS_ANDROID
        // Store address for shutdown service (handles swipe-to-kill)
        if (m_controller) {
            storeDE1AddressForShutdown(m_controller->remoteAddress().toString());
        }
        // Start foreground service to prevent Samsung/OEM app killing
        startBleConnectionService();
#endif

        // connected() is emitted by the ready marker subscribeAll() queues
        // behind the notification subscriptions, once every one of them has
        // been confirmed — see subscribeAll() for why it cannot fire here.
        subscribeAll();
    }
}

void BleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // An unsolicited push. Proof the link is delivering data, so it restarts the
    // liveness clock the zombie-link check in connectToDevice() consults — but
    // it ends no operation, which is why onCharacteristicRead below is a
    // separate slot rather than this one wired to both signals.
    m_notificationLiveness.restart();
    emit dataReceived(c.uuid(), value);
}

void BleTransport::onCharacteristicRead(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // Delivered exactly like a push, and additionally ends the read holding the
    // slot. Delivery first so a consumer sees the value before anything else is
    // dispatched; the queue posts its next dispatch either way.
    onCharacteristicChanged(c, value);
    completeOperation(c.uuid());
    emitQueueDrainedIfIdle();
}

void BleTransport::onDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value) {
    Q_UNUSED(value);
    // Ends the CCCD write holding the slot, and only that one. A late ACK for a
    // subscription already abandoned would otherwise release whatever holds the
    // slot now — including another device's operation. This is the whole of what
    // the old ACK matcher did, once the sequencing it also carried became the
    // queue's job.
    if (descriptor != cccdFor(m_gattQueue->inFlightKey())) {
        // Every other misattribution guard in this file logs its drop. A CCCD ACK
        // that fails to match is what a stalled subscribe looks like from outside.
        log(QStringLiteral("Descriptor ACK did not match the subscribe in flight — ignored"));
        return;
    }
    completeOperation(m_gattQueue->inFlightKey());
    // Every other terminal-success path emits this; a subscribe that happened to
    // be our last outstanding work reported nothing, and main.cpp waits on it at
    // exit to know the sleep and charger writes went out. Last, for the same
    // reason as in onCharacteristicWritten: a consumer reached from a completion
    // may enqueue, and "drained" must not be claimed ahead of that.
    emitQueueDrainedIfIdle();
}

void BleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c, const QByteArray& value) {
    // Guarded, like completeOperation() below it. An ACK for a write already
    // abandoned at the clock would otherwise zero the consecutive-failure run and
    // print "accepting writes again" about a link that is not — and a late ACK is
    // precisely the pattern of a link that is sick, so the detector would be
    // defeated by its own evidence.
    if (ownsInFlight(c.uuid())) noteWriteSucceeded();
    completeOperation(c.uuid());

    emit writeComplete(c.uuid(), value);
    emitQueueDrainedIfIdle();
}

// -- Private helpers --

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
    // Called only as a queued operation's issue step, and every path out of here
    // must reach the platform or release the slot. A path that does neither
    // wedges every device until something else forces a teardown — the failure
    // mode these early returns were free of only because nothing was waiting on
    // them.
    if (!linkAcceptsGattOperations(uuid, QStringLiteral("write"))) {
        m_gattQueue->noteFailed(this);
        return;
    }
    m_service->writeCharacteristic(m_characteristics[uuid], data);
}

bool BleTransport::linkAcceptsGattOperations(const QBluetoothUuid& uuid,
                                             const QString& verb) {
    // One predicate for all three issue callbacks. It used to be open-coded in
    // writeCharacteristic() only, which is how read and subscribe came to issue
    // against a link that had gone: the check existed, it just was not where the
    // other two could reach it.
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("%1(%2) skipped - %3")
                .arg(verb, uuid.toString().mid(1, 8),
                     !m_service ? "no service" : "unknown characteristic"));
        return false;
    }
    // Controller state, not isConnected(), which also requires
    // m_characteristicsReady — connection-setup operations must still go through.
    // Handing anything to a torn-down QLowEnergyController crashes inside
    // DarwinBTCentralManager's write queue on the LE dispatch queue (iOS #1400,
    // symbolicated to the GATT write path).
    const auto controllerState = m_controller ? m_controller->state()
                                              : QLowEnergyController::UnconnectedState;
    if (controllerState != QLowEnergyController::ConnectedState
        && controllerState != QLowEnergyController::DiscoveredState) {
        log(QString("%1(%2) skipped - controller not connected (state %3)")
                .arg(verb, uuid.toString().mid(1, 8))
                .arg(static_cast<int>(controllerState)));
        return false;
    }
    return true;
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

// -- The DE1's half of the shared GATT queue -----------------------------

QLowEnergyDescriptor BleTransport::cccdFor(const QBluetoothUuid& uuid) const {
    if (!m_service || !m_characteristics.contains(uuid)) return {};
    return m_characteristics[uuid].descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
}

bool BleTransport::ownsInFlight(const QBluetoothUuid& uuid) const {
    return m_gattQueue->inFlightRequester() == this
           && m_gattQueue->inFlightKey() == uuid;
}

void BleTransport::completeOperation(const QBluetoothUuid& uuid) {
    if (!ownsInFlight(uuid)) return;
    m_operationTimeoutTimer.stop();
    m_gattQueue->noteSucceeded(this);
}

void BleTransport::emitQueueDrainedIfIdle() {
    if (m_gattQueue->inFlightRequester() == this) return;
    if (m_gattQueue->pendingCount(this) > 0) return;
    emit queueDrained();
}

BleGattQueue::Operation BleTransport::operationFor(const QBluetoothUuid& key,
                                                   const QString& verb,
                                                   std::function<void()> issue,
                                                   int timeoutMs) {
    BleGattQueue::Operation op;
    op.requester = this;
    op.key = key;
    op.label = QStringLiteral("%1 %2").arg(verb, key.toString().mid(1, 8));
    // Named, not positional: both fields are ints, so a swapped pair would
    // compile clean and turn a 5-retry/500 ms policy into 500 retries.
    op.policy.maxRetries = MAX_WRITE_RETRIES;
    op.policy.retryDelayMs = WRITE_RETRY_DELAY_MS;
    op.issue = [this, timeoutMs, issue = std::move(issue)]() {
        // Armed here rather than by the callers so it covers retries too: the
        // queue calls issue() again for each one, and a retry that also goes
        // unanswered must be bounded exactly like the first attempt.
        m_operationTimeoutTimer.start(timeoutMs);
        issue();
    };
    return op;
}

void BleTransport::submitWrite(const QBluetoothUuid& uuid, const QByteArray& data, bool toFront) {
    auto op = operationFor(uuid, QStringLiteral("write"), [this, uuid, data]() {
        writeCharacteristic(uuid, data);
    });
    op.onAbandoned = [this, uuid, data]() {
        m_operationTimeoutTimer.stop();
        warn(QString("Write FAILED after %1 retries (uuid=%2, %3 bytes)")
                 .arg(MAX_WRITE_RETRIES).arg(uuid.toString().mid(1, 8)).arg(data.size()));
        // Deliberately no errorOccurred (user-facing): retry exhaustion means
        // the link is dead and the reconnect ladder takes over — typically
        // self-healing in seconds. Surfacing it queued stale "Connection Error"
        // modals behind the screensaver (#1423). Persistent failures still reach
        // the user via the reconnect path's own errors.
        //
        // Count BEFORE the emit. de1LinkFault runs its consumers synchronously,
        // and one of them reaching disconnect() would run
        // forgetWriteFailureState() and zero the counter, so the increment would
        // restart the episode from 1 on exactly the links that fault hardest.
        noteWriteAbandoned();
        emit writeAbandoned(uuid, data);
        emit de1LinkFault(QStringLiteral("write-failed"));
        emitQueueDrainedIfIdle();
    };
    if (toFront)
        m_gattQueue->submitFront(std::move(op));
    else
        m_gattQueue->submit(std::move(op));
}

void BleTransport::submitRead(const QBluetoothUuid& uuid) {
    // Queued rather than issued, so a read runs after the writes ahead of it.
    // Issued straight away, a read placed right after a write returns the
    // pre-write value and defeats read-after-write verification.
    if (!m_service || !m_characteristics.contains(uuid)) {
        log(QString("read(%1) skipped - %2")
                .arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        return;
    }
    auto op = operationFor(uuid, QStringLiteral("read"), [this, uuid]() {
        // Re-checked at DISPATCH, not just at submission. Every controller and
        // service signal here is a Qt::QueuedConnection, so the link can go down
        // between the two and forget() will not have run yet. Issuing then is the
        // write-to-a-dead-link path that crashes inside DarwinBTCentralManager
        // (#1400/#1405), and it would hold the shared slot to the clock.
        if (!linkAcceptsGattOperations(uuid, QStringLiteral("read"))) {
            m_gattQueue->noteFailed(this);
            return;
        }
        m_service->readCharacteristic(m_characteristics[uuid]);
    });
    op.onAbandoned = [this, uuid]() {
        m_operationTimeoutTimer.stop();
        // No de1LinkFault and no writeAbandoned: a lost read leaves a value
        // unknown, where a lost write leaves the machine holding something other
        // than what it was told. Different severity, so a different close-out.
        warn(QString("Read FAILED after %1 retries (uuid=%2)")
                 .arg(MAX_WRITE_RETRIES).arg(uuid.toString().mid(1, 8)));
        emitQueueDrainedIfIdle();
    };
    m_gattQueue->submit(std::move(op));
}

bool BleTransport::submitSubscribe(const QBluetoothUuid& uuid, bool required) {
    // Checked at submission, not at issue: the characteristic map is fully
    // populated before subscribeAll() runs, so either of these is a permanent
    // fact about this connection and retrying it five times would only delay
    // saying so.
    //
    // RETURNS FALSE when the stream is permanently unavailable, and
    // subscribeAll() must stop on a required one. This is not decoration: a
    // failure found HERE reaches failRequiredStream() before the ready marker
    // has been submitted, so the forget() it performs drops nothing and the
    // marker is queued immediately afterwards — the machine reports CONNECTED
    // with STATE_INFO never enabled, which is #1819 exactly. The abandonment
    // path below has the opposite ordering and forget() is sufficient there.
    if (!m_service || !m_characteristics.contains(uuid)) {
        // Expected during teardown, and a caller subscribing to something this
        // DE1 does not expose is not a fault of the link. DEBUG, like the
        // matching guard in submitRead().
        log(QString("subscribe(%1) skipped - %2")
                .arg(uuid.toString().mid(1, 8), !m_service ? "no service" : "unknown characteristic"));
        // Recorded here as well as on abandonment. The ready marker makes a
        // POSITIVE statement about which telemetry is live, so a stream that
        // was skipped rather than abandoned must still appear in the exception
        // list or that statement is false.
        m_streamsNotEnabled.append(uuid.toString().mid(1, 8));
        if (required) failRequiredStream(uuid);
        return false;
    }
    if (!cccdFor(uuid).isValid()) {
        warn(QString("subscribe(%1) FAILED - CCCD descriptor not found")
                 .arg(uuid.toString().mid(1, 8)));
        m_streamsNotEnabled.append(uuid.toString().mid(1, 8));
        if (required) failRequiredStream(uuid);
        return false;
    }

    auto op = operationFor(uuid, QStringLiteral("subscribe"), [this, uuid]() {
        // Same dispatch-time re-check as read and write. A CCCD enable IS a
        // write, so the dead-link crash path applies to it in full.
        if (!linkAcceptsGattOperations(uuid, QStringLiteral("subscribe"))) {
            m_gattQueue->noteFailed(this);
            return;
        }
        m_service->writeDescriptor(cccdFor(uuid), QByteArray::fromHex("0100"));
    });
    op.onAbandoned = [this, uuid, required]() {
        m_operationTimeoutTimer.stop();
        warn(QString("Could not enable notifications for %1 after %2 retries")
                 .arg(uuid.toString().mid(1, 8)).arg(MAX_WRITE_RETRIES));
        m_streamsNotEnabled.append(uuid.toString().mid(1, 8));
        if (required) failRequiredStream(uuid);
        emitQueueDrainedIfIdle();
    };
    m_gattQueue->submit(std::move(op));
    return true;
}

void BleTransport::failRequiredStream(const QBluetoothUuid& uuid) {
    // Written to stand alone: these logs are read by users and by their AI
    // assistants, who have no knowledge of this subsystem. The failure this
    // describes is the one #1819 reported — a machine that says CONNECTED and
    // then does nothing a user can see.
    warn(QString("DE1 connection failed: %1 is a stream the machine cannot be "
                 "used without, and notifications for it could not be enabled. "
                 "Without it there is no live chart, no shot detection and no "
                 "stop-at-weight, so the connection is being failed rather than "
                 "reported as working. Reconnecting is what clears this — from "
                 "the Connections page, or over MCP with devices_connect_de1.")
             .arg(uuid.toString().mid(1, 8)));

    // Reaches evaluateBleWedge() via BLEManager, which needs a fault on record
    // before it will act — a DE1 stuck mid-subscribe with a scale connected is
    // otherwise caught by nothing.
    emit de1LinkFault(QStringLiteral("subscribe-failed"));

    // Drops everything of ours still queued, which includes the ready marker.
    // That is how "do not report connected" is expressed: no flag to set, no
    // flag to forget to clear.
    m_gattQueue->forget(this);
}

void BleTransport::submitDiscovery() {
    // Characteristic discovery is not a read or a write, but it occupies the
    // same radio, and in the #1819 capture it was a peripheral in discovery that
    // the DE1's rejected CCCD writes ran against. Queueing it on both sides is
    // what closes that; a queue only one participant submits to orders nothing.
    //
    // Keyed by the service, which is what its completion — stateChanged ->
    // RemoteServiceDiscovered — reports.
    auto op = operationFor(DE1::SERVICE_UUID, QStringLiteral("discover"), [this]() {
        if (!m_service) {
            log(QStringLiteral("Characteristic discovery skipped - no service"));
            m_gattQueue->noteFailed(this);
            return;
        }
        log("Starting characteristic discovery for DE1 service");
        m_service->discoverDetails();
    }, BleGatt::DISCOVERY_TIMEOUT_MS);
    op.onAbandoned = [this]() {
        m_operationTimeoutTimer.stop();
        // Not a de1LinkFault: nothing was written and nothing was lost. The
        // service-discovery retry path (onServiceDiscoveryFinished) owns what
        // happens next, and it already tears the attempt down after MAX_RETRIES.
        warn(QStringLiteral("Characteristic discovery did not complete — the "
                            "connection attempt will be retried"));
        emitQueueDrainedIfIdle();
    };
    m_gattQueue->submit(std::move(op));
}

void BleTransport::submitReadyMarker() {
    BleGattQueue::Operation op;
    op.requester = this;
    op.label = QStringLiteral("de1 ready");
    op.issue = [this]() {
        // One positive statement of what the machine will actually send. INFO,
        // because this is the connect narrative a user reads.
        if (m_streamsNotEnabled.isEmpty()) {
            info(QStringLiteral("DE1 telemetry live: state, shot samples, water level, "
                                "MMR responses, temperatures, calibration replies"));
        } else {
            info(QString("DE1 telemetry live except %1 — that stream will not update "
                         "until the next reconnect")
                     .arg(m_streamsNotEnabled.join(QStringLiteral(", "))));
        }
        // Baseline the liveness clock here so a link that connects but never
        // pushes a single notification also ages out and is caught as a zombie
        // on the next reconnect attempt — not just one that goes quiet later.
        m_notificationLiveness.start();
        emit connected();
        // Issues nothing to the platform, so nothing else will ever end it.
        m_gattQueue->noteSucceeded(this);
    };
    m_gattQueue->submit(std::move(op));
}
