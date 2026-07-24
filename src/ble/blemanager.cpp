#include "blemanager.h"
#include "blecapability.h"
#include "scaledevice.h"
#include "transport/scalebletransport.h"
#include "protocol/de1characteristics.h"
#include "scales/decentscale.h"
#include "scales/scalefactory.h"
#include "refractometers/difluidr1.h"
#include "refractometers/difluidr2.h"
#include "refractometers/refractometerdevice.h"
#include "../core/settings_hardware.h"
#include "../core/translationmanager.h"
#include "../network/wifiscalediscovery.h"
#include "bleepochgate.h"
#include "version.h"
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QCoreApplication>
#include <QDebug>
#include <QBluetoothPermission>
#include <QLocationPermission>
#include <QStandardPaths>
#include <QDir>
#include <QTextStream>
#include <algorithm>
#include <QDateTime>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

#ifdef Q_OS_IOS
#include <UIKit/UIKit.h>
#endif

#ifdef Q_OS_MACOS
#include <QDesktopServices>
#include <QUrl>
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include "applebtstate.h"
#endif

BLEManager* BLEManager::s_instance = nullptr;

BLEManager::BLEManager(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    m_appStartTime = QDateTime::currentDateTime();

    // Discovery agent is created lazily in ensureDiscoveryAgent() to avoid
    // initializing CoreBluetooth (and triggering TCC privacy checks) when
    // BLE is disabled (e.g. simulation mode on Mac debug builds).

    // Track Bluetooth adapter power state.
    // QBluetoothLocalDevice is not available on iOS — CoreBluetooth manages state there.
#ifndef Q_OS_IOS
    m_localDevice = new QBluetoothLocalDevice(this);
    connect(m_localDevice, &QBluetoothLocalDevice::hostModeStateChanged,
            this, &BLEManager::onHostModeStateChanged);
#endif

    // Timer for scale connection timeout (20 seconds)
    m_scaleConnectionTimer = new QTimer(this);
    m_scaleConnectionTimer->setSingleShot(true);
    m_scaleConnectionTimer->setInterval(20000);
    connect(m_scaleConnectionTimer, &QTimer::timeout, this, &BLEManager::onScaleConnectionTimeout);

    // Bounds a foreground scale direct-connect: aborts the parked controller and
    // falls back to the scan if it hasn't connected in ~4s (issue #1303).
    m_scaleDirectAbortTimer = new QTimer(this);
    m_scaleDirectAbortTimer->setSingleShot(true);
    m_scaleDirectAbortTimer->setInterval(kScaleDirectConnectAbortMs);
    connect(m_scaleDirectAbortTimer, &QTimer::timeout, this, [this]() {
        abortScaleDirectConnectIfPending(QStringLiteral("~4s elapsed"));
    });

    // Backstop for serializing the scale's BLE connect behind the DE1's: if the
    // DE1 never finishes connecting (e.g. no DE1 present while debugging),
    // connect the scale anyway after 15 s rather than waiting forever.
    m_de1WaitTimer = new QTimer(this);
    m_de1WaitTimer->setSingleShot(true);
    m_de1WaitTimer->setInterval(15000);
    connect(m_de1WaitTimer, &QTimer::timeout, this, [this]() {
        // Cap reached: stop gating the scale behind the DE1 unconditionally, so
        // m_de1DirectConnectInFlight can't stick if the DE1 never resolves and no
        // scale was waiting. If one is waiting, connect it now.
        m_de1DirectConnectInFlight = false;
        if (m_scaleConnectDeferred) {
            m_scaleConnectDeferred = false;
            appendScaleLog("DE1 did not connect within 15 s — connecting scale anyway");
            tryDirectConnectToScale();
        }
    });

    // Fail-safe watchdog for the BLE-stack-wedge adapter power-cycle (#1309).
    // The cycle is normally event-driven (HostPoweredOff → powerOn() →
    // HostConnectable → done); this timer guards each leg in case the OS never
    // delivers the expected host-mode transition, so we never silently strand
    // the radio off. It re-checks the ACTUAL adapter state and finalises
    // accordingly — it does not blindly assume success. Single-shot, re-armed
    // for the power-on leg; not a logic guard, a last-resort verifier.
    m_adapterRecoverySafetyTimer = new QTimer(this);
    m_adapterRecoverySafetyTimer->setSingleShot(true);
    m_adapterRecoverySafetyTimer->setInterval(kAdapterRecoverySafetyMs);
    // The connect is guarded, not the lambda body.
    //
    // An earlier version kept one lambda with Q_UNUSED(this), justified by
    // "the timer is still constructed and armed on every platform". That was
    // simply wrong — both start() calls are themselves inside #ifndef
    // Q_OS_IOS, so on iOS the timer is constructed and never started. There is
    // no timer firing into nothing to protect against, and the workaround was
    // guarding a hazard that did not exist.
#ifndef Q_OS_IOS
    connect(m_adapterRecoverySafetyTimer, &QTimer::timeout, this, [this]() {
        if (!m_adapterRecoveryInFlight || !m_localDevice) return;
        const bool adapterOff = m_localDevice->hostMode() == QBluetoothLocalDevice::HostPoweredOff;
        if (adapterOff) {
            // Either powerOff() never reported HostPoweredOff, or the power-on
            // leg never brought it back. Make one more attempt to re-enable, then
            // let finishAdapterRecovery(false) surface it — never leave BT off.
            qWarning() << "BLEManager: adapter still powered off"
                       << (kAdapterRecoverySafetyMs / 1000) << "s into recovery — forcing power-on (#1309)";
            setAdapterPower(true);
            finishAdapterRecovery(false);
        } else {
            // Adapter is on but we missed the HostConnectable event (or powerOff
            // was a no-op and it was never actually off). Treat as recovered.
            qWarning() << "BLEManager: recovery watchdog — adapter is on without an explicit "
                          "HostConnectable event; treating as recovered (#1309)";
            finishAdapterRecovery(true);
        }
    });
#endif

    // Eagerly run the Linux capability check so any qWarning lands early in
    // startup logs; subsequent calls hit the cached result.
    (void) BleCapability::linuxMissing();
}

bool BLEManager::isBluetoothAvailable() const
{
    if (m_disabled) return true;  // simulator mode — always report available

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // On both Apple platforms QBluetoothLocalDevice is unreliable: it doesn't
    // exist on iOS, and on macOS hostMode() always returns HostConnectable
    // regardless of the real BT state (QTBUG-50838). CBCentralManager is the
    // native source of truth, wrapped in AppleBtState. Created lazily here so
    // simulator-mode launches don't pay the CoreBluetooth init / permission
    // prompt cost (m_disabled is set before QML's first binding evaluation).
    if (!m_appleBtState) {
        auto* self = const_cast<BLEManager*>(this);
        self->m_appleBtState = new AppleBtState(self);
        connect(self->m_appleBtState, &AppleBtState::stateChanged,
                self, &BLEManager::bluetoothAvailableChanged);
    }
    return !m_appleBtState->isUnavailable();
#else
#ifdef Q_OS_ANDROID
    // On Android, QBluetoothLocalDevice::hostMode() returns HostPoweredOff until the
    // BLUETOOTH_CONNECT runtime permission has been granted (Android 12+). On a fresh
    // install, permission is Undetermined — if we bail out here, the scan flow never
    // gets a chance to call requestBluetoothPermission(), and the user is stuck with
    // "Bluetooth is powered off" even though the adapter is fine. Report available
    // only while the status is Undetermined so the scan proceeds and prompts the
    // user. If the user has explicitly denied, fall through to the real hostMode()
    // check (which will report PoweredOff) so bluetoothAvailable accurately reflects
    // that BLE is unusable.
    QBluetoothPermission perm;
    perm.setCommunicationModes(QBluetoothPermission::Access);
    if (qApp->checkPermission(perm) == Qt::PermissionStatus::Undetermined) {
        return true;
    }
#endif
    return m_localDevice->hostMode() != QBluetoothLocalDevice::HostPoweredOff;
#endif
}

void BLEManager::onHostModeStateChanged(QBluetoothLocalDevice::HostMode mode)
{
    qDebug() << "BLEManager: Bluetooth host mode changed to" << mode;

#ifndef Q_OS_IOS
    // Drive the wedge-recovery power-cycle off observed adapter transitions
    // rather than a fixed delay (the adapter takes a variable time to power
    // down): once we see it actually off, turn it back on; once it's back on,
    // re-arm the reconnect paths. (#1309)
    if (m_adapterRecoveryInFlight && m_localDevice) {
        if (mode == QBluetoothLocalDevice::HostPoweredOff) {
            // Power-off leg done — bring it back up, and re-arm the watchdog so
            // the power-ON leg is itself covered (powerOn never landing must not
            // leave the radio off).
            m_recoverySawPoweredOff = true;
            qDebug() << "BLEManager: adapter powered off during recovery — powering back on (#1309)";
            setAdapterPower(true);
            m_adapterRecoverySafetyTimer->start();
        } else {
            // HostConnectable / HostDiscoverable — adapter is back up.
            finishAdapterRecovery(true);
        }
    }
#endif

    emit bluetoothAvailableChanged();
}

void BLEManager::setAdapterPower(bool on)
{
#if defined(Q_OS_ANDROID)
    // Qt's QBluetoothLocalDevice has no powerOff() on Android (and powerOn()
    // shows the system consent dialog), so drive the framework adapter directly.
    // BluetoothAdapter.disable()/enable() are silent on API ≤ 32 and no-ops on
    // 33+; BLUETOOTH_ADMIN/BLUETOOTH_CONNECT are declared in the manifest.
    // Qt still observes the resulting state change and emits hostModeStateChanged.
    QJniObject adapter = QJniObject::callStaticObjectMethod(
        "android/bluetooth/BluetoothAdapter", "getDefaultAdapter",
        "()Landroid/bluetooth/BluetoothAdapter;");
    if (!adapter.isValid()) {
        qWarning() << "BLEManager: no BluetoothAdapter — cannot" << (on ? "enable" : "disable") << "(#1309)";
        return;
    }
    adapter.callMethod<jboolean>(on ? "enable" : "disable");
#else
    // Wedge recovery is Android-only (maybeRecoverWedgedStack returns early
    // elsewhere), and QBluetoothLocalDevice has no portable powerOff() — it
    // exists on neither Android nor macOS in this Qt build. No-op everywhere else.
    Q_UNUSED(on);
#endif
}

void BLEManager::finishAdapterRecovery(bool adapterOn)
{
#ifndef Q_OS_IOS
    m_adapterRecoverySafetyTimer->stop();
    m_adapterRecoveryInFlight = false;
    m_recoverySawPoweredOff = false;
    m_wedgeSince = QDateTime();

    if (adapterOn) {
        m_recoveryLeftAdapterOff = false;
        m_lastDe1FaultTime = QDateTime();  // stale faults shouldn't re-trip immediately
        qDebug() << "BLEManager: adapter recovered — re-arming DE1 + scale reconnect (#1309)";
        emit bleStackRecovered();          // main.cpp resets the DE1 reconnect budget + retries
        if (!m_savedScaleAddress.isEmpty())
            tryDirectConnectToScale();     // scale side re-arm
    } else {
        // We could not bring the radio back up. Flag it (so the next attempt
        // powers it on instead of treating OFF as user intent) and tell the
        // user — do NOT emit bleStackRecovered(), the stack is not recovered.
        m_recoveryLeftAdapterOff = true;
        qWarning() << "BLEManager: automatic Bluetooth restart did not bring the adapter "
                      "back up — asking the user to toggle it manually (#1309)";
        appendScaleLog(QStringLiteral("Auto Bluetooth restart failed — adapter still off (#1309)"));
        emit errorOccurred(translateUiString(
            QStringLiteral("ble.error.bluetoothRestartFailed"),
            QStringLiteral("Decenza tried to restart Bluetooth but it's still off. "
                           "Please turn Bluetooth off and on in your device settings.")));
    }
#else
    Q_UNUSED(adapterOn);
#endif
}

void BLEManager::noteDe1Connected(bool connected)
{
    m_de1Connected = connected;
    if (connected) {
        // A working DE1 link means the stack is healthy — clear any wedge state.
        m_anyBleSuccessThisSession = true;
        m_wedgeSince = QDateTime();
        m_lastDe1FaultTime = QDateTime();
        m_lastDe1ErrorShown.clear();  // Healthy again — allow a future DE1 error to surface
    }
}

void BLEManager::onDe1Error(const QString& error)
{
    // Debounce: the reconnect ladder emits the same "Connection error" on every
    // failed attempt (~once/60s during a wedge), so show each distinct message
    // only once until the DE1 reconnects. Mirrors the onScanError debounce.
    if (error.isEmpty() || error == m_lastDe1ErrorShown) return;
    m_lastDe1ErrorShown = error;
    emit errorOccurred(error);
}

void BLEManager::onDe1LinkFault(const QString& kind)
{
    // de1LinkFault fires only on genuine trouble — controller errors,
    // write-retry exhaustion, or a detected zombie link (connected but no
    // notifications) — never on plain device-absence, so this timestamp is the
    // load-bearing "stack in trouble" signal the wedge detector keys off.
    m_lastDe1FaultTime = QDateTime::currentDateTime();
    evaluateBleWedge(QStringLiteral("de1-fault:%1").arg(kind));
}

void BLEManager::evaluateBleWedge(const QString& reason)
{
    // The wedge fingerprint (#1309): a saved DE1 that won't connect, throwing
    // controller faults, WHILE the physical scale also can't connect. A single
    // absent device never satisfies all three — only a stack that's failing
    // every link does. m_scaleDevice is always the physical scale (FlowScale is
    // never assigned here), so its disconnected state is real, not a fallback.
    const bool scaleConnected = m_scaleDevice && m_scaleDevice->isConnected();
    const bool de1FaultFresh = m_lastDe1FaultTime.isValid()
        && m_lastDe1FaultTime.msecsTo(QDateTime::currentDateTime()) <= kWedgeFaultFreshnessMs;

    const bool wedgeSuspected = hasSavedDE1() && !m_de1Connected
                                && de1FaultFresh && !scaleConnected;

    if (!wedgeSuspected) {
        m_wedgeSince = QDateTime();  // condition broken — reset the confirm window
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (!m_wedgeSince.isValid()) {
        m_wedgeSince = now;  // start of a sustained-wedge window
        return;
    }
    if (m_wedgeSince.msecsTo(now) >= kWedgeConfirmMs) {
        maybeRecoverWedgedStack(reason);
    }
}

void BLEManager::maybeRecoverWedgedStack(const QString& reason)
{
#ifndef Q_OS_ANDROID
    // Only Android exhibits the wedge, and it's the only platform where a
    // non-privileged app can cycle the adapter. Elsewhere this is a no-op —
    // the existing "Try toggling Bluetooth off/on" error guidance stands.
    Q_UNUSED(reason);
    return;
#else
    if (m_disabled || !m_localDevice) return;
    if (m_adapterRecoveryInFlight) return;
    if (m_localDevice->hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        // Adapter is off. If a PRIOR recovery cycle of ours left it off, keep
        // trying to power it back on (subject to backoff) rather than mistaking
        // our own failure for a deliberate user power-off. If the user turned it
        // off, m_recoveryLeftAdapterOff is false and we respect that — leave it.
        if (m_recoveryLeftAdapterOff) {
            const QDateTime t = QDateTime::currentDateTime();
            if (!m_lastAdapterRecovery.isValid()
                || m_lastAdapterRecovery.msecsTo(t) >= kAdapterRecoveryBackoffMs) {
                m_lastAdapterRecovery = t;
                qWarning() << "BLEManager: adapter still off from a prior failed recovery — "
                              "retrying power-on (#1309)";
                setAdapterPower(true);
            }
        }
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastAdapterRecovery.isValid()
        && m_lastAdapterRecovery.msecsTo(now) < kAdapterRecoveryBackoffMs) {
        qDebug() << "BLEManager: BLE stack still appears wedged (" << reason
                 << ") but within recovery backoff — not cycling adapter yet (#1309)";
        return;
    }

    m_adapterRecoveryInFlight = true;
    m_recoverySawPoweredOff = false;
    m_lastAdapterRecovery = now;
    m_adapterRecoveryCount++;
    m_wedgeSince = QDateTime();
    qWarning() << "BLEManager: BLE stack appears wedged (" << reason
               << ") — power-cycling Bluetooth adapter, recovery #" << m_adapterRecoveryCount
               << "this session (#1309)";
    appendScaleLog(QStringLiteral("BLE stack wedged — auto power-cycling Bluetooth adapter (#1309)"));
    emit bleStackRecoveryStarted();

    // powerOff() is async; the host-mode handler powers it back on once it sees
    // HostPoweredOff, and the safety timer re-enables it if that event never lands.
    m_adapterRecoverySafetyTimer->start();
    setAdapterPower(false);
#endif
}

void BLEManager::ensureDiscoveryAgent() {
    if (m_discoveryAgent) return;

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(15000);  // 15 seconds per scan cycle

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BLEManager::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BLEManager::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BLEManager::onScanError);
}

BLEManager::~BLEManager() {
    if (m_scanning) {
        stopScan();
    }
    cancelWifiProbe();  // tear down any in-flight WiFi-primary reachability probe
    if (s_instance == this) s_instance = nullptr;
}

void BLEManager::setSettings(SettingsHardware* settings)
{
    m_settings = settings;
    if (!m_settings) return;

    // Backoff policy mode (observe-mode change). Loaded UNCONDITIONALLY and
    // independent of the latch: it is deliberately NOT build-scoped, so it is
    // read even when the latch is absent/cleared and is never touched by the
    // build-change safety valve below. Absent/unrecognized ⇒ Enforce.
    m_backoffMode.store(backoffModeFromString(m_settings->cpMode()),
                        std::memory_order_relaxed);
    if (observeMode()) {
        qWarning().noquote()
            << "[BLE] Backoff policy mode = OBSERVE (persisted) — connection-"
               "priority detection runs but takes NO action and the scale "
               "link is forced HIGH (any latch is overridden, not erased). "
               "Set enforce via MCP to restore the dual-HIGH backoff.";
    }

    if (!m_settings->cpLatched()) {
#ifdef Q_OS_ANDROID
        // First-launch seed for the dual-HIGH-incapable cohort (#1238).
        //
        // The runtime detector (#1185) does eventually catch these devices,
        // but only after one full DE1-outage detection window (~minutes of
        // broken scale discovery on the P80X chipset, then a ~70s DE1 GATT
        // collapse). Seeding the latch up front on the population that the
        // retired #1097 SDK<30 gate used to cover (Android < 11) bypasses
        // that first-launch pain.
        //
        // This is a SEED, not a gate: the runtime detector continues to
        // handle SDK≥30 devices on weak chipsets (the #1176 Galaxy Tab A8 /
        // T618 case that motivated #1185), unchanged.
        //
        // Sticky by design: SDK_INT is a permanent OS characteristic, so the
        // seed re-evaluates on every launch where the latch is absent (e.g.,
        // after an MCP clear or epoch bump that wiped the record). While the
        // latch persists, subsequent launches rehydrate it without re-running
        // this block. There is no in-app way to permanently restore HIGH on
        // SDK<30 hardware — the only exit is an OS upgrade to SDK≥30.
        constexpr int kSeedSdkBelow = 30;  // #1097's predicate, now reused as a seed
        QJniEnvironment jniEnv;
        const jint sdkInt = QJniObject::getStaticField<jint>(
            "android/os/Build$VERSION", "SDK_INT");
        const bool jniFailed = jniEnv.checkAndClearExceptions();
        if (jniFailed || sdkInt <= 0) {
            qWarning().noquote()
                << QStringLiteral("[BLE] First-launch seed: failed to read "
                      "Android SDK_INT via JNI (sdkInt=%1, jni_exception=%2) "
                      "— seed skipped, runtime detector will arm normally.")
                       .arg(sdkInt).arg(jniFailed ? "yes" : "no");
        } else if (sdkInt < kSeedSdkBelow) {
            const QString kind = QStringLiteral("seed:sdk<%1").arg(kSeedSdkBelow);
            latchScaleSkipHighPriority(kind);
            qWarning().noquote()
                << QStringLiteral("[BLE] First-launch seed: Android SDK %1 < "
                      "%2 (dual-HIGH-incapable cohort, ex-#1097) — skip-HIGH "
                      "latch SET without running the detection window. "
                      "Persisted under epoch %3; both BLE links start at "
                      "BALANCED. Seed re-applies on every launch where the "
                      "latch is absent (SDK_INT is permanent — no in-app "
                      "escape hatch on this SDK cohort).")
                       .arg(sdkInt).arg(kSeedSdkBelow).arg(kBleDetectionEpoch);
        }
#endif
        return;
    }

    const int storedEpoch = m_settings->cpEpoch();   // -1 ⇒ legacy (no key)
    const int storedBuild = m_settings->cpBuildCode(); // diagnostic only now

    const BleEpochDecision decision =
        decideBleEpochGate(true, storedEpoch, kBleDetectionEpoch);

    if (decision == BleEpochDecision::Discard) {
        // Either a DELIBERATE epoch bump (a release that changed BLE handling
        // / re-classifies everyone) OR a corrupt persisted epoch. This is the
        // ONLY auto-wipe path — it fires only on an intentional epoch change
        // or genuine corruption, NOT on every build. Discard + re-detect.
        m_settings->clearConnectionPriorityLatch();
        if (storedEpoch >= 0) {
            qWarning().noquote()
                << QStringLiteral("[BLE] Persisted connection-priority "
                      "classification was set under detection epoch %1 but "
                      "this build is epoch %2 — discarding and re-detecting "
                      "from scratch (deliberate epoch reset)")
                       .arg(storedEpoch).arg(kBleDetectionEpoch);
        } else {
            // Negative but not the -1 "no key" sentinel ⇒ corrupt record.
            qWarning().noquote()
                << QStringLiteral("[BLE] Persisted connection-priority "
                      "detection epoch is corrupt/unrecognized (stored=%1, "
                      "expected %2 or absent) — discarding and re-detecting "
                      "from scratch rather than honoring a damaged record")
                       .arg(storedEpoch).arg(kBleDetectionEpoch);
        }
        return;
    }

    // Rehydrate (storedEpoch == kBleDetectionEpoch) OR MigrateForward (a
    // legacy pre-epoch record, epoch key absent → honor + stamp forward,
    // ZERO extra detection on the upgrade that introduces epoch scoping).
    const bool legacy = (decision == BleEpochDecision::MigrateForward);

    // rehydrate() preserves the ORIGINAL set-time (unlike set(), which would
    // re-stamp it) and sanitises possibly-corrupt persisted input so the
    // ScaleSkipHighLatch invariant ("kind non-empty AND time valid IFF
    // latched") holds even on a partial write / manual edit / ISO drift. It
    // returns false iff the stored timestamp was invalid and had to be
    // substituted — log that anomaly.
    const QString isoIn = m_settings->cpSetTimeIso();
    const QString kindIn = m_settings->cpTriggerKind();
    const bool timeOk = m_scaleSkipHigh.rehydrate(
        kindIn, QDateTime::fromString(isoIn, Qt::ISODate));
    m_scaleSkipHighBuildCode = storedBuild;  // diagnostic ("classified by N")
    if (kindIn.isEmpty()) {
        // Symmetric with the !timeOk anomaly below: a missing trigger kind is
        // also corruption (partial write / manual edit). rehydrate() salvaged
        // it to "unknown" — surface that so the MCP "unknown"-kind latch isn't
        // mistaken for a genuine unknown-cause classification with no trail.
        qWarning().noquote()
            << QStringLiteral("[BLE] Persisted connection-priority trigger "
                  "kind was missing/empty — salvaged to \"%1\"; classification "
                  "kept (it is the load-bearing fact)")
                   .arg(m_scaleSkipHigh.triggerKind);
    }
    qWarning().noquote()
        << QStringLiteral("[BLE] Loaded persisted dual-HIGH-incapable "
              "classification (epoch %1, build %2 [diagnostic], trigger=%3) — "
              "BOTH BLE links will start at BALANCED this run (no detection "
              "window)")
               .arg(legacy ? kBleDetectionEpoch : storedEpoch)
               .arg(storedBuild).arg(m_scaleSkipHigh.triggerKind);
    if (!timeOk) {
        qWarning().noquote()
            << QStringLiteral("[BLE] Persisted connection-priority set-time "
                  "was invalid/missing (stored=\"%1\") — substituted current "
                  "time; classification kept (it is the load-bearing fact)")
                   .arg(isoIn);
    }

    if (legacy) {
        // One-time forward migration: stamp the current epoch so subsequent
        // same-epoch builds rehydrate normally (and are never re-migrated).
        // Persist from the now-sanitised in-memory latch (keeps the
        // invariant); keep the original buildCode as the diagnostic.
        m_settings->setConnectionPriorityLatch(
            m_scaleSkipHigh.triggerKind,
            m_scaleSkipHigh.setTime.toString(Qt::ISODate),
            storedBuild, kBleDetectionEpoch);
        qWarning().noquote()
            << QStringLiteral("[BLE] Legacy (pre-epoch) connection-priority "
                  "record honored; stamping detection epoch %1. NO re-detection "
                  "is incurred regardless (the in-memory latch is already live "
                  "— BALANCED this run). If a 'Failed to PERSIST' warning "
                  "follows, the stamp did not stick and this line will repeat "
                  "next launch (still no re-detection — cosmetic only)")
                   .arg(kBleDetectionEpoch);
    }
}

void BLEManager::latchScaleSkipHighPriority(const QString& triggerKind)
{
    // The value type enforces "kind+time set iff latched".
    m_scaleSkipHigh.set(triggerKind);
    m_scaleSkipHighBuildCode = versionCode();  // diagnostic ("classified by N")
    // Write through so the classification survives restarts. Now EPOCH-scoped:
    // it persists across all builds sharing kBleDetectionEpoch; versionCode()
    // is stored only as a diagnostic ("last classified by build N"), no
    // longer the rehydrate gate.
    if (m_settings) {
        m_settings->setConnectionPriorityLatch(
            m_scaleSkipHigh.triggerKind,
            m_scaleSkipHigh.setTime.toString(Qt::ISODate),
            versionCode(), kBleDetectionEpoch);
    }
}

void BLEManager::clearScaleSkipHighPriority()
{
    const bool wasLatched = m_scaleSkipHigh.latched;
    m_scaleSkipHigh.clear();
    m_scaleSkipHighBuildCode = 0;  // diagnostic cleared with the latch
    // D9: clear the persisted record too (idempotent — safe even if the
    // in-memory latch was already clear).
    if (m_settings) m_settings->clearConnectionPriorityLatch();
    if (wasLatched) {
        qWarning().noquote()
            << "[BLE] Scale connection-priority skip-HIGH latch CLEARED via "
               "MCP reset — next (re)connect will request HIGH on both links "
               "and re-enter detection from scratch";
    }
}

void BLEManager::setBackoffMode(BackoffMode mode)
{
    const bool changed =
        m_backoffMode.exchange(mode, std::memory_order_relaxed) != mode;
    // Write through to the (non-build-scoped) persisted store so the choice
    // survives restarts and build upgrades. Deliberately does NOT touch the
    // latch: observe overrides the latch at the transport, but the latch
    // value is preserved so switching back to enforce honours it honestly.
    if (m_settings) m_settings->setCpMode(backoffModeToString(mode));
    if (changed) {
        qWarning().noquote()
            << "[BLE] Backoff policy mode set to" << backoffModeToString(mode).toUpper()
            << "— applies on the next scale (re)connect (eventually-consistent; "
               "the current connection is not torn down)";
    }
}
// recordObserveEvent / recentObserveEvents are header-inline (they delegate to
// the self-locking ObserveEventRing — see blemanager.h).

void BLEManager::requestBluezCacheHint()
{
    if (m_disabled) return;  // don't burn the one-shot token in simulator mode
    if (BleCapability::takeBluezCacheHintToken()) {
        emit linuxBlueZCacheHintNeeded();
    }
}

void BLEManager::setDisabled(bool disabled) {
    if (m_disabled != disabled) {
        m_disabled = disabled;
        if (m_disabled) {
            if (m_scanning) {
                stopScan();
            }
            // NOTE: no longer stops m_scaleConnectionTimer or drops the physical
            // scale. Those belong to the scale, and the DE1 simulator has no
            // business tearing down a real scale the user is weighing with —
            // that teardown is now setScaleSimulated's job.
        }
        qDebug() << "BLEManager: DE1 BLE operations" << (disabled ? "disabled (simulator mode)" : "enabled");
        emit disabledChanged();
    }
}

void BLEManager::setScaleSimulated(bool simulated) {
    if (m_scaleSimulated == simulated) return;
    m_scaleSimulated = simulated;
    if (m_scaleSimulated) {
        // A simulated scale is taking over the weight stream — stand the real
        // one down so the two don't both drive weight.
        m_scaleConnectionTimer->stop();
        emit disconnectScaleRequested();
    }
    qDebug() << "BLEManager: real-scale connects"
             << (simulated ? "blocked (simulated scale active)" : "allowed");
    // MUST be emitted on BOTH edges. The rising edge stops the connection timer
    // and drops the physical scale; nothing restarts either of those, so without
    // a falling-edge signal for main.cpp to hang a re-arm on, switching the
    // simulated scale back off leaves the real scale stranded until the app is
    // restarted or the user rescans — with no indication why. This mirrors
    // disabledChanged, which main.cpp already uses for exactly this purpose on
    // the R2 reconnect path.
    emit scaleSimulatedChanged();
}

bool BLEManager::isScanning() const {
    return m_scanning;
}

QVariantList BLEManager::discoveredDevices() const {
    QVariantList result;
    for (const auto& device : m_de1Devices) {
        QVariantMap map;
        map["name"] = device.name();
        map["address"] = getDeviceIdentifier(device);
        result.append(map);
    }
    return result;
}

QVariantList BLEManager::discoveredScales() const {
    QVariantList result;
    for (const auto& entry : m_scales) {
        QVariantMap map;
        map["name"] = entry.name;
        map["address"] = entry.address;
        map["type"] = entry.type;
        map["transport"] = entry.transport;
        result.append(map);
    }
    return result;
}

QBluetoothDeviceInfo BLEManager::getScaleDeviceInfo(const QString& address) const {
    for (const auto& entry : m_scales) {
        if (entry.address.compare(address, Qt::CaseInsensitive) == 0) {
            return entry.device;  // Default-constructed for WiFi entries.
        }
    }
    return QBluetoothDeviceInfo();
}

QString BLEManager::getScaleType(const QString& address) const {
    for (const auto& entry : m_scales) {
        if (entry.address.compare(address, Qt::CaseInsensitive) == 0) {
            return entry.type;
        }
    }
    return QString();
}

void BLEManager::connectToScale(const QString& address) {
    for (const auto& entry : m_scales) {
        if (entry.address.compare(address, Qt::CaseInsensitive) != 0) continue;

        appendScaleLog(QString("Connecting to %1...").arg(entry.name));
        // If already connected to a different scale, disconnect it first so
        // the scaleDiscovered/wifiScaleSelected handler can connect to the new one.
        if (m_scaleDevice && m_scaleDevice->isConnected()
                && address.compare(m_savedScaleAddress, Qt::CaseInsensitive) != 0) {
            emit disconnectScaleRequested();
        }

        if (entry.transport == QStringLiteral("usb")) {
            // USB connect is owned by UsbScaleManager: it creates + opens the
            // UsbDecentScale and emits its own scaleDiscovered. Don't emit
            // scaleDiscovered here (no QBluetoothDeviceInfo / factory path).
            emit usbConnectRequested();
        } else if (entry.transport == QStringLiteral("wifi")) {
            // Arm the connection timer, exactly as connectToWifiScale,
            // switchToWifiPrimary and tryDirectConnectToScale all do. This is
            // the ONLY backstop on the tap-a-discovered-scale path: the WiFi
            // driver treats an unreachable host as transient and returns
            // without retrying, deferring to the app-level ladder — and that
            // ladder is started by flowScaleFallback / scaleRetryNeeded, both
            // of which are emitted by onScaleConnectionTimeout. Without the
            // timer nothing emits either, so a scale that dropped into
            // power-save between the scan and the tap fails with no retry, no
            // dialog and no user-visible trace at all.
            m_scaleConnectionTimer->start();
            // Strip the "wifi:" prefix to get the bare hostname.
            m_pendingWifiHostname = address.mid(QStringLiteral("wifi:").size());
            // Hand along the IP the scan's mDNS discovery already resolved
            // (see ScaleEntry::resolvedIp) so main.cpp can seed DecentScaleWifi's
            // IP cache and skip Qt's own hostname resolver entirely.
            m_pendingWifiResolvedIp = entry.resolvedIp;
            emit scaleDiscovered(QBluetoothDeviceInfo{}, entry.type);
        } else {
            emit scaleDiscovered(entry.device, entry.type);
        }
        return;
    }
    qWarning() << "Scale not found in discovered list:" << address;
}

void BLEManager::setUsbScaleAvailable(bool available, const QString& name) {
    // Synthetic USB scale row, mirroring the WiFi entry. Stable address so
    // it can be the saved primary across plug/unplug and app restarts.
    const QString kUsbAddress = QStringLiteral("usb:decent");

    // Find any existing USB entry.
    qsizetype existing = -1;
    for (qsizetype i = 0; i < m_scales.size(); ++i) {
        if (m_scales[i].transport == QStringLiteral("usb")) {
            existing = i;
            break;
        }
    }

    if (available) {
        if (existing >= 0) {
            // Refresh the display name only.
            if (m_scales[existing].name != name) {
                m_scales[existing].name = name;
                emit scalesChanged();
            }
            return;
        }
        ScaleEntry entry;
        entry.type = QStringLiteral("decent-usb");
        entry.transport = QStringLiteral("usb");
        entry.name = name;
        entry.address = kUsbAddress;
        m_scales.append(entry);
        appendScaleLog(QString("Found %1 (%2)").arg(entry.name, entry.address));
        emit scalesChanged();
    } else {
        if (existing < 0) return;  // Nothing to remove.
        m_scales.removeAt(existing);
        appendScaleLog(QStringLiteral("USB scale unplugged"));
        emit scalesChanged();
    }
}

void BLEManager::probeMdnsForManualEntry() {
    // Dedicated mDNS probe for the "Add WiFi Scale" dialog. We keep this
    // separate from m_wifiDiscovery (used by the user-initiated scan / saved-
    // scale rehydration path), because m_wifiDiscovery's scaleFound handler
    // auto-connects when the discovered hostname matches the saved primary
    // (and no user-initiated scan is in progress) — we explicitly do NOT
    // want that side effect here. The manual flow's contract is "tell the
    // user we found a scale and let them choose"; connect happens only when
    // they tap Use.
    if (!m_manualEntryDiscovery) {
        m_manualEntryDiscovery = new WifiScaleDiscovery(this);
        // Forward the dedicated probe's diagnostics into the scale debug log too —
        // a user reporting "I clicked Add WiFi Scale but nothing showed up" will
        // have the mDNS-side reason (timeout vs no-responder vs lookup failure)
        // captured in the log they share.
        connect(m_manualEntryDiscovery, &WifiScaleDiscovery::logMessage, this,
                [this](const QString& msg) {
            appendScaleLog(QString("[WifiScaleDiscovery/manual] %1").arg(msg));
        });
        connect(m_manualEntryDiscovery, &WifiScaleDiscovery::scaleFound, this,
                [this](const QString& hostname, const QString& resolvedAddress) {
            // (Result line — the WifiScaleDiscovery logMessage above already
            // logged the "mDNS resolved …" detail; this is the higher-level
            // event for the user reading the log top-to-bottom.)
            appendScaleLog(QString("Manual-entry mDNS found %1 at %2").arg(hostname, resolvedAddress));
            m_manualEntryFoundThisProbe = true;
            emit manualWifiMdnsDiscovered(hostname, resolvedAddress);
        });
        connect(m_manualEntryDiscovery, &WifiScaleDiscovery::probeFinished, this,
                [this]() {
            if (!m_manualEntryFoundThisProbe) {
                appendScaleLog(QStringLiteral(
                    "Manual-entry mDNS: no HDS scale responded — user can still type an address"));
            }
            emit manualWifiMdnsProbeFinished();
        });
    }
    m_manualEntryFoundThisProbe = false;
    appendScaleLog(QStringLiteral("Probing mDNS for hds.local (manual entry)..."));
    m_manualEntryDiscovery->probe();
}

void BLEManager::connectToWifiScale(const QString& hostnameOrIp, const QString& resolvedIp) {
    QString host = hostnameOrIp.trimmed();
    if (host.isEmpty()) return;

    // A bare name with no dot is an mDNS hostname missing its suffix — append
    // ".local" so it resolves (matches the discovery default "hds.local"). IPs
    // and already-dotted/qualified names pass through unchanged.
    if (!host.contains(QLatin1Char('.')))
        host += QStringLiteral(".local");

    appendScaleLog(QString("Adding WiFi scale at %1...").arg(host));

    // Drop any currently-connected scale first: main.cpp's scaleDiscovered
    // handler early-returns while a scale is connected. Its disconnectScaleRequested
    // handler runs synchronously and clears m_scaleDevice before we emit below.
    if (m_scaleDevice && m_scaleDevice->isConnected())
        emit disconnectScaleRequested();

    // Arm the connection timer so a wrong/unreachable host is caught by
    // onScaleConnectionTimeout. WiFi socket errors are otherwise log-only (#1253),
    // so without this a bad address fails with NO user feedback. m_manualWifiConnect
    // makes that timeout report "Not found" directly instead of starting a WiFi→BLE
    // fallback scan — the user asked for a specific WiFi address, so we don't
    // silently switch transports. Then take the same emit path as a discovered WiFi
    // scale (connectToScale's wifi branch), minus the discovered-list lookup: set
    // the pending hostname and emit scaleDiscovered with the WiFi type. main.cpp
    // creates the DecentScaleWifi driver, wires the IP-cache callbacks, dials
    // connectToHost(host), and records it in Known Devices + as primary when the
    // connect starts (as with any scale connect — not gated on connect success).
    m_manualWifiConnect = true;
    m_wifiFallbackToBleActive = false;
    m_scaleConnectionTimer->start();
    m_pendingWifiHostname = host;
    // Callers with a fresh mDNS resolution in hand (the "Add WiFi Scale"
    // dialog's suggested-scale "Use" button) pass it along so main.cpp can
    // seed the IP cache and skip re-resolving the hostname. A genuinely typed
    // address (the dialog's text-field submit) passes nothing — resolvedIp
    // defaults to empty and there's nothing to seed.
    m_pendingWifiResolvedIp = resolvedIp;
    emit scaleDiscovered(QBluetoothDeviceInfo{}, QStringLiteral("decent-wifi"));
}

void BLEManager::connectToSavedScale() {
    // Switch the live connection to the current saved primary (the caller sets it
    // via setSavedScaleAddress immediately before). The Known Devices picker uses
    // this so selecting a scale actually CONNECTS to it rather than only relabeling
    // the saved primary. Goes through tryDirectConnectToScale() — the direct-wake
    // path — so it works even when the chosen scale isn't in the discovered list.
    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) return;

    // USB saved-scale switch: USB needs neither Bluetooth nor a direct-wake. Hand
    // off to UsbScaleManager (usbConnectRequested) and skip the BLE guards below.
    if (m_savedScaleAddress.startsWith(QStringLiteral("usb:"), Qt::CaseInsensitive)) {
        // Bail BEFORE dropping the live scale if the USB scale isn't currently
        // plugged in: UsbScaleManager::connectToScale() no-ops when nothing is
        // available, which would otherwise strand the user on FlowScale with no
        // feedback. The USB entry is present in m_scales only while probe-confirmed.
        const bool usbPresent = std::any_of(m_scales.cbegin(), m_scales.cend(),
            [](const ScaleEntry& e) { return e.transport == QStringLiteral("usb"); });
        if (!usbPresent) {
            appendScaleLog(QStringLiteral("USB scale switch ignored — scale not plugged in"));
            return;
        }
        if (m_scaleDevice && m_scaleDevice->isConnected())
            emit disconnectScaleRequested();
        emit usbConnectRequested();
        return;
    }

    // Bail BEFORE dropping the working scale if the new connect can't proceed —
    // otherwise we'd disconnect the current scale and tryDirectConnectToScale()
    // would then silently early-return, leaving the user on FlowScale with no
    // feedback. (These mirror tryDirectConnectToScale()'s own guards.)
    // Gated on the SCALE simulator, not the DE1 one. Running the DE1 simulator
    // means "no machine attached"; it says nothing about whether the user has a
    // real scale they want to weigh with — and for a WiFi scale there isn't
    // even a radio in common with the DE1.
    if (m_scaleSimulated) {
        appendScaleLog("Scale switch ignored — simulated scale is active");
        return;
    }
    // The simulator's synthetic entry appears in the Known Devices picker like
    // any other saved scale, so the user can select it — but it is not a
    // dialable address and tryDirectConnectToScale below refuses it. Without
    // this bail we would emit disconnectScaleRequested first and drop a working
    // real scale to connect nothing, which is exactly the failure the comment
    // above says these guards exist to prevent.
    if (savedScaleIsSimulated()) {
        appendScaleLog("Scale switch ignored — that entry is the simulator's "
                       "synthetic scale, not a real device");
        return;
    }
    if (!isBluetoothAvailable()) {
        appendScaleLog("Cannot switch scale — Bluetooth is powered off");
        emit errorOccurred(translateUiString("ble.error.bluetoothPoweredOff",
                                             "Bluetooth is powered off"));
        return;
    }

    // Fresh user-initiated attempt: clear stale failure state so the status line
    // doesn't flash "Not found" during the new connect (mirrors scanForDevices()).
    m_scaleConnectionFailed = false;
    m_flowScaleFallbackEmitted = false;
    emit scaleConnectionFailedChanged();

    // Drop the currently-connected scale (if any) so the new primary can take over.
    // main.cpp's disconnectScaleRequested handler runs synchronously (same-thread
    // direct connection) and clears m_scaleDevice, so tryDirectConnectToScale()'s
    // "already connected" guard won't block the new dial.
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        appendScaleLog(QString("Switching scale to %1")
                           .arg(m_savedScaleName.isEmpty() ? m_savedScaleAddress : m_savedScaleName));
        emit disconnectScaleRequested();
    }
    m_wifiFallbackToBleActive = false;  // user explicitly chose this scale
    resetScaleConnectionState();
    tryDirectConnectToScale();
}

void BLEManager::startScan() {
    if (m_disabled && !m_scanningForScales) {
        // In simulator mode, suppress DE1 scanning but allow scale/refractometer scans
        // (m_scanningForScales is set by scanForDevices() before calling here).
        qDebug() << "BLEManager: DE1 scan request ignored (simulator mode)";
        return;
    }

    if (m_scanning) {
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: Scan request ignored (Bluetooth is powered off)";
        // Callers (tryDirectConnectToRefractometer, scanForDevices) set the
        // scan flags before calling here on the assumption a scan will run.
        // A scan that never starts must not leave them latched — they are
        // only cleared by scan finished/error/stop, none of which will fire.
        clearScanRequestFlags();
        return;
    }

    // Check and request Bluetooth permission on Android
    requestBluetoothPermission();
}

void BLEManager::clearScanRequestFlags() {
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
}

void BLEManager::requestBluetoothPermission() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    emit de1LogMessage("Checking permissions...");

#ifdef Q_OS_ANDROID
    // First check/request location permission (required for BLE scanning on Android)
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Precise);

    if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Undetermined) {
        emit de1LogMessage("Requesting location permission...");
        qApp->requestPermission(locationPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                emit de1LogMessage("Location permission granted");
                requestBluetoothPermission();  // Continue with Bluetooth permission
            } else {
                emit de1LogMessage("Location permission denied");
                clearScanRequestFlags();  // No scan will start; don't latch the request flags
                emit errorOccurred(translateUiString("ble.error.locationPermissionDeniedForBluetooth",
                    "Location permission denied - required for Bluetooth scanning"));
            }
        });
        return;
    } else if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Denied) {
        emit de1LogMessage("Location permission denied");
        clearScanRequestFlags();  // No scan will start; don't latch the request flags
        emit errorOccurred(translateUiString("ble.error.locationPermissionRequired",
            "Location permission required. Please enable in Settings."));
        return;
    }
#endif

    // Check Bluetooth permission (required on both Android and iOS)
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        emit de1LogMessage("Requesting Bluetooth permission...");
        qApp->requestPermission(bluetoothPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                emit de1LogMessage("Bluetooth permission granted");
                // isBluetoothAvailable() now switches from the Undetermined bypass
                // to the real hostMode() check. Notify QML bindings so any "Bluetooth
                // unavailable" UI re-evaluates immediately.
                emit bluetoothAvailableChanged();
                doStartScan();
            } else {
                emit de1LogMessage("Bluetooth permission denied");
                // Transition Undetermined → Denied also flips isBluetoothAvailable()
                // from true to false (via the hostMode() fall-through).
                emit bluetoothAvailableChanged();
                clearScanRequestFlags();  // No scan will start; don't latch the request flags
                emit errorOccurred(translateUiString("ble.error.bluetoothPermissionDenied",
                    "Bluetooth permission denied"));
            }
        });
        return;
    case Qt::PermissionStatus::Denied:
        emit de1LogMessage("Bluetooth permission denied");
        clearScanRequestFlags();  // No scan will start; don't latch the request flags
        emit errorOccurred(translateUiString("ble.error.bluetoothPermissionRequired",
            "Bluetooth permission required. Please enable in Settings."));
        return;
    case Qt::PermissionStatus::Granted:
        emit de1LogMessage("Permissions OK");
        break;
    }
#endif
    doStartScan();
}

void BLEManager::doStartScan() {
    // Always clear the DE1 device list for a fresh scan
    m_de1Devices.clear();
    emit devicesChanged();
    // Only clear scales/refractometers when the user explicitly asked to scan for them;
    // a DE1-only scan must not wipe the discovered scale list.
    if (m_scanningForScales) {
        // Clear only BLE-discovered scale entries. WiFi entries come from a
        // separate mDNS path and shouldn't be wiped by an unrelated scan
        // cycle — notably the periodic refractometer auto-reconnect tick,
        // which calls startScan() every ~30 s and would otherwise erase a
        // freshly-discovered WiFi-scale row before the user can tap it.
        for (qsizetype i = m_scales.size() - 1; i >= 0; --i) {
            if (m_scales[i].transport == QStringLiteral("ble")) {
                m_scales.removeAt(i);
            }
        }
        m_refractometerDevices.clear();
        emit scalesChanged();
        emit refractometersChanged();
    }
    m_scanning = true;
    emit scanningChanged();
    emit scanStarted();  // Notify that scan has actually started
    emit de1LogMessage("Scanning for devices...");

    // Scan for BLE devices only
    ensureDiscoveryAgent();
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BLEManager::stopScan() {
    if (!m_scanning) return;

    emit de1LogMessage("Scan stopped");
    if (m_discoveryAgent)
        m_discoveryAgent->stop();
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    emit scanningChanged();
}

void BLEManager::clearDevices() {
    m_de1Devices.clear();
    m_scales.clear();
    m_refractometerDevices.clear();
    emit devicesChanged();
    emit scalesChanged();
    emit refractometersChanged();
}

void BLEManager::onDeviceDiscovered(const QBluetoothDeviceInfo& device) {
    // BLE is actually delivering devices → permission is healthy.
    m_anyBleSuccessThisSession = true;

    // Check if it's a DE1
    if (isDE1Device(device)) {
        // Avoid duplicates
        for (const auto& existing : m_de1Devices) {
            if (getDeviceIdentifier(existing) == getDeviceIdentifier(device)) {
                return;
            }
        }
        m_de1Devices.append(device);
        emit devicesChanged();
        qDebug() << "[BLE] Found DE1:" << device.name() << "at" << getDeviceIdentifier(device);
        emit de1LogMessage(QString("Found DE1: %1 (%2)").arg(device.name()).arg(getDeviceIdentifier(device)));
        emit de1Discovered(device);
        return;
    }

    // Only look for scales/refractometers if user requested it or we're looking for saved device
    if (!m_scanningForScales) {
        return;
    }

    // Check if it's a refractometer BEFORE scale detection (prevents misclassification
    // — both R2 and R1 family names overlap with broad scale name heuristics).
    if (DiFluidR2::isR2Device(device.name()) || DiFluidR1::isR1Device(device.name())) {
        // Avoid duplicates
        for (const auto& existing : m_refractometerDevices) {
            if (getDeviceIdentifier(existing) == getDeviceIdentifier(device)) {
                // No log here: onDeviceDiscovered fires on every BLE
                // advertisement (~100-500 ms); the device is already logged
                // once on first discovery below. Logging here would flood the
                // diagnostic timeline this instrumentation exists to keep
                // readable.
                return;
            }
        }
        m_refractometerDevices.append(device);
        emit refractometersChanged();
        qDebug() << "[BLE] Found refractometer:" << device.name() << "at" << getDeviceIdentifier(device);
        appendScaleLog(QString("Found refractometer: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));

        const bool savedMatch = !m_savedRefractometerAddress.isEmpty()
            && deviceIdentifiersMatch(device, m_savedRefractometerAddress);
        qDebug().noquote() << QString("[R2-diag] R2 advert dev=%1 savedMatch=%2 userInitiatedScan=%3 -> %4")
            .arg(getDeviceIdentifier(device),
                 savedMatch ? QStringLiteral("true") : QStringLiteral("false"),
                 m_userInitiatedScaleScan ? QStringLiteral("true") : QStringLiteral("false"),
                 savedMatch ? QStringLiteral("emit refractometerDiscovered")
                            : (m_userInitiatedScaleScan ? QStringLiteral("listed only (no auto-connect)")
                                                         : QStringLiteral("listed, skip auto-connect (not saved device)")));
        // Auto-connect if this is our saved refractometer
        if (savedMatch) {
            emit refractometerDiscovered(device);
        } else if (m_userInitiatedScaleScan) {
            // User scan — show all devices (emit for UI listing only, not auto-connect)
        }
        return;
    }

    // Check if it's a scale
    QString scaleType = getScaleType(device);
    if (!scaleType.isEmpty()) {
        // Avoid duplicates
        const QString deviceId = getDeviceIdentifier(device);
        for (const auto& entry : m_scales) {
            if (entry.transport == QStringLiteral("ble") && entry.address == deviceId) {
                return;
            }
        }
        ScaleEntry entry;
        entry.device = device;
        entry.type = scaleType;
        entry.transport = QStringLiteral("ble");
        entry.name = device.name();
        entry.address = deviceId;
        m_scales.append(entry);
        emit scalesChanged();
        qDebug() << "[BLE] Found scale:" << device.name() << "type:" << scaleType << "at" << getDeviceIdentifier(device);
        appendScaleLog(QString("Found %1: %2 (%3)").arg(scaleType).arg(device.name()).arg(getDeviceIdentifier(device)));

        // If we're doing a direct wake and this is our saved scale found via scan,
        // log it and clear the direct connect state. The scan-discovered device has
        // proper BLE metadata which may help with connection.
        if (m_directConnectInProgress && deviceIdentifiersMatch(device, m_directConnectAddress)) {
            appendScaleLog("Direct wake: found saved scale in scan, using scanned device");
            m_directConnectInProgress = false;
            m_directConnectAddress.clear();
        }

        // WiFi-to-BLE fallback: when the saved scale is a WiFi address but
        // we've timed out reaching it, accept the first matching-family
        // (Decent) BLE scale found as a substitute. This is the only path
        // that intentionally violates the #440 "primary-only auto-reconnect"
        // guarantee — explicitly user-visible via the toast emitted from
        // beginWifiFallbackToBleScan().
        const bool isFallbackCandidate = m_wifiFallbackToBleActive
            && m_savedScaleType == QStringLiteral("decent-wifi")
            && scaleType == QStringLiteral("decent");

        // Auto-connect rules (BLE path — WiFi has its own handler in
        // the WifiScaleDiscovery::scaleFound lambda further down):
        //   • Saved BLE primary → always auto-connect (even on user-initiated
        //     scans). The user's "Scan" tap when their scale is offline is
        //     itself a "please reconnect" — the original "don't hijack the
        //     user's pick" concern doesn't apply when the found scale IS what
        //     they previously chose as primary. The companion "don't silently
        //     re-save a forgotten scale via addKnownScale()" concern is also
        //     inert here: the scale is already saved, so the re-save is a
        //     no-op against the same record. This restores the natural "scan
        //     got my scale back" flow that was previously only reachable via
        //     an awkward BT-then-WiFi switch dance.
        //
        //     Note: this rule only fires for BLE primaries. When the saved
        //     primary is `wifi:hostname`, deviceIdentifiersMatch can never
        //     return true here (BLE devices don't carry hostnames), so a
        //     WiFi-primary user scanning while looking at BLE devices falls
        //     through to the list-only branch. The WiFi handler below mirrors
        //     this rule for the WiFi-primary case.
        //   • WiFi→BLE fallback candidate → auto-connect (the #440-violating
        //     "accept a Decent BLE scale as a substitute" path; opt-in via the
        //     m_wifiFallbackToBleActive gate).
        //   • Anything else during a user-initiated scan → list only, don't
        //     hijack the user's pick. The user explicitly selects via the UI
        //     (connectToScale()).
        //   • Anything else in a background scan → ignore (no auto-connect to
        //     non-primaries — #440 — and don't silently re-save a scale the
        //     user just forgot via the scaleDiscovered handler's
        //     addKnownScale()/setPrimaryScale()).
        const bool matchesSaved = !m_savedScaleAddress.isEmpty()
            && deviceIdentifiersMatch(device, m_savedScaleAddress);

        if (!isFallbackCandidate && !matchesSaved) {
            if (m_userInitiatedScaleScan) {
                // User is choosing — leave the discovered scale in the list
                // for explicit selection. (Appended to m_scales above.)
                return;
            }
            if (m_savedScaleAddress.isEmpty()) {
                appendScaleLog(QString("No saved scale — listing %1 for manual selection (no auto-connect)")
                               .arg(device.name()));
                return;
            }
            appendScaleLog(QString("Ignoring non-primary scale: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));
            return;
        }

        if (isFallbackCandidate) {
            appendScaleLog(QString("WiFi fallback: connecting to BLE Decent scale %1 (%2)")
                           .arg(device.name(), getDeviceIdentifier(device)));
        }

        // Auto-reconnect to the saved primary, or the WiFi-fallback Decent BLE
        // candidate. (User-scan discoveries returned above; manual selection
        // emits scaleDiscovered via connectToScale().)
        emit scaleDiscovered(device, scaleType);
    }
}

void BLEManager::onScanFinished() {
    qDebug().noquote() << "[R2-diag] scan cycle finished — clearing scanning/scanningForScales/userInitiated flags";
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    emit de1LogMessage("Scan complete");
    appendScaleLog("Scan complete");
    emit scanningChanged();

    // Hunt mode (post-shot review page): restart the scan back-to-back so a
    // refractometer powered on at an arbitrary moment is seen within one scan
    // cycle instead of waiting out the background reconnect tick's dead window.
    // Scan-finished is the continuation event — no polling timer. Deliberately
    // not restarted from onScanError: the background tick recovers from errors.
    if (m_refractometerHunt && !m_savedRefractometerAddress.isEmpty()
        && !isRefractometerConnected() && !m_disabled && isBluetoothAvailable()) {
        qDebug().noquote() << "[R2-diag] hunt active — restarting scan";
        m_scanningForScales = true;
        startScan();
    }
}

void BLEManager::onScanError(QBluetoothDeviceDiscoveryAgent::Error error) {
    QString errorMsg;
    switch (error) {
        case QBluetoothDeviceDiscoveryAgent::NoError:
            return;  // No error, nothing to do
        case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
            errorMsg = translateUiString("ble.error.bluetoothPoweredOff", "Bluetooth is powered off");
            break;
        case QBluetoothDeviceDiscoveryAgent::InputOutputError:
            errorMsg = translateUiString("ble.error.bluetoothIoError", "Bluetooth I/O error");
            break;
        case QBluetoothDeviceDiscoveryAgent::InvalidBluetoothAdapterError:
            errorMsg = translateUiString("ble.error.invalidAdapter", "Invalid Bluetooth adapter");
            break;
        case QBluetoothDeviceDiscoveryAgent::UnsupportedPlatformError:
            errorMsg = translateUiString("ble.error.unsupportedPlatform", "Platform does not support Bluetooth LE");
            break;
        case QBluetoothDeviceDiscoveryAgent::UnsupportedDiscoveryMethod:
            errorMsg = translateUiString("ble.error.unsupportedDiscoveryMethod", "Unsupported discovery method");
            break;
        case QBluetoothDeviceDiscoveryAgent::LocationServiceTurnedOffError:
            errorMsg = translateUiString("ble.error.locationServicesOff", "Location services are turned off");
            break;
        case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
            // On macOS Tahoe + Qt 6.11, MissingPermissionsError fires
            // transiently after app-resume even when the user's Bluetooth
            // permission is still granted — CoreBluetooth's permission state
            // takes a moment to re-establish post-suspend. If BLE has been
            // working this session, treat the error as a transient hiccup:
            // log it, suppress the popup, let the next scan tick retry.
            if (m_anyBleSuccessThisSession) {
                qWarning() << "[BLE] Transient MissingPermissionsError "
                              "(permission previously OK this session — "
                              "likely CoreBluetooth post-resume hiccup); "
                              "not surfacing to user";
                appendScaleLog("Bluetooth scan transient error (ignored — permission OK)");
                m_scanning = false;
                m_scanningForScales = false;
                m_userInitiatedScaleScan = false;
                emit scanningChanged();
                return;  // Skip the user-visible errorOccurred path
            }
            // BLE has NEVER worked this session — could be a real permission
            // denial. Fall through to the normal popup.
            errorMsg = translateUiString("ble.error.bluetoothPermissionDeniedSettings",
                "Bluetooth permission denied. Please allow Bluetooth access in Settings.");
            break;
        default:
            errorMsg = translateUiString("ble.error.unknownCode",
                "Bluetooth error (code %1)").arg(static_cast<int>(error));
            break;
    }
    qWarning() << "BLEManager scan error:" << errorMsg << "code:" << static_cast<int>(error);
    emit de1LogMessage(QString("Error: %1").arg(errorMsg));
    appendScaleLog(QString("Error: %1").arg(errorMsg));
    // Debounce the user-visible popup: scan errors from the refractometer/
    // scale auto-reconnect cycle would otherwise re-fire the same error toast
    // every ~30 s (e.g. macOS Tahoe sometimes returns MissingPermissionsError
    // for QBluetoothDeviceDiscoveryAgent after sleep/wake — the user shouldn't
    // see the same dialog 20+ times in a row). Pop it once per distinct error
    // message; reset when something successfully connects (handled in
    // onScaleConnectedChanged + the DE1 connect path).
    if (errorMsg != m_lastScanErrorShown) {
        m_lastScanErrorShown = errorMsg;
        emit errorOccurred(errorMsg);
    }
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;
    // Clear any in-flight WiFi-to-BLE fallback — the scan that was supposed
    // to find a substitute BLE Decent scale just errored out, so the fallback
    // attempt is over. Leaving the flag armed would silently demote the
    // user's NEXT scale pick (BLE candidate would be classified as a
    // "temporary fallback connect" by main.cpp).
    m_wifiFallbackToBleActive = false;
    emit scanningChanged();
}

bool BLEManager::isDE1Device(const QBluetoothDeviceInfo& device) const {
    // Check by name - "DE1" is the standard prefix, "BENGLE" is used for developer/debug units
    QString name = device.name();
    if (name.startsWith("DE1", Qt::CaseInsensitive) ||
        name.startsWith("BENGLE", Qt::CaseInsensitive)) {
        return true;
    }

    // Check by service UUID
    QList<QBluetoothUuid> uuids = device.serviceUuids();
    for (const auto& uuid : uuids) {
        if (uuid == DE1::SERVICE_UUID) {
            return true;
        }
    }

    return false;
}

QString BLEManager::getScaleType(const QBluetoothDeviceInfo& device) const {
    ScaleType type = ScaleFactory::detectScaleType(device);
    if (type == ScaleType::Unknown) {
        return "";
    }
    // Return the canonical type-id (e.g. "decent"), NOT the display name. scaleType
    // is a persistence/lookup key (known scales, SAW per-(profile, scale) learning,
    // sensorLag) and must be rename-stable; the human label is carried separately in
    // the entry's `name` field.
    return ScaleFactory::scaleTypeId(type);
}

void BLEManager::setScaleDevice(ScaleDevice* scale) {
    if (m_scaleDevice) {
        disconnect(m_scaleDevice, nullptr, this, nullptr);
    }

    m_scaleDevice = scale;

    if (m_scaleDevice) {
        connect(m_scaleDevice, &ScaleDevice::connectedChanged,
                this, &BLEManager::onScaleConnectedChanged);
        // Connect scale's debug log to our logging system
        connect(m_scaleDevice, &ScaleDevice::logMessage,
                this, &BLEManager::appendScaleLog);
        // Push current DE1-discovery state immediately so a scale that connects
        // mid-discovery (e.g. after the gate timed out) starts in the right
        // pause state instead of waiting for the next edge.
        if (auto* ds = qobject_cast<DecentScale*>(m_scaleDevice)) {
            ds->setHeartbeatsPaused(m_de1ServiceDiscoveryActive);
        }
    }
}

void BLEManager::setDe1ServiceDiscoveryActive(bool active) {
    if (m_de1ServiceDiscoveryActive == active) return;
    m_de1ServiceDiscoveryActive = active;
    if (auto* ds = qobject_cast<DecentScale*>(m_scaleDevice)) {
        ds->setHeartbeatsPaused(active);
    }
}

void BLEManager::onScaleConnectedChanged() {
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Scale connected - stop timers, clear failure flag, clear direct connect state
        m_scaleConnectionTimer->stop();
        m_scaleDirectAbortTimer->stop();
        m_directConnectInProgress = false;
        m_directConnectAddress.clear();
        m_wifiFallbackToBleActive = false;  // Reset for the next saved-scale cycle
        m_manualWifiConnect = false;        // Manual WiFi add resolved (connected)
        m_lastScanErrorShown.clear();       // Healthy state — allow a future fresh scan error to pop again
        m_anyBleSuccessThisSession = true;  // Permission proven good (WiFi scales hit this too — see note below)
        m_flowScaleFallbackEmitted = false;  // Allow dialog again if scale disconnects and reconnect fails
        m_wedgeSince = QDateTime();          // A connecting scale proves the stack isn't wedged (#1309)
        if (m_scaleConnectionFailed) {
            m_scaleConnectionFailed = false;
            emit scaleConnectionFailedChanged();
        }
        qDebug() << "BLEManager: Scale connected";
        emit scaleConnected();  // UI auto-dismisses the scale-disconnect / no-scale notice on reconnect
    } else {
        // Scale disconnected - notify UI immediately
        qDebug() << "BLEManager: Scale disconnected";
        appendScaleLog("Scale disconnected");
        emit scaleDisconnected();
    }
}

void BLEManager::abortScaleDirectConnectIfPending(const QString& reason) {
    m_scaleDirectAbortTimer->stop();
    // Only a foreground direct-connect parks a controller; bail otherwise.
    if (!m_directConnectInProgress) return;
    if (m_scaleDevice && m_scaleDevice->isConnected()) return;  // connect raced in

    appendScaleLog(QString("Direct connect not established (%1) — aborting, scan continues").arg(reason));
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();

    // Tear down the parked connecting controller via the transport. The
    // controller for a BLE scale lives in its ScaleBleTransport, NOT in the
    // base ScaleDevice::m_controller (which is always null for transport-based
    // scales), so ScaleDevice::disconnectFromScale() would not reach it.
    // ScaleBleTransport::disconnectFromDevice() severs the controller's signals
    // before teardown, so this fires no spurious disconnected() cascade, and the
    // connecting→disconnected transition doesn't flip connectedChanged.
    if (auto* transport = m_scaleDevice ? m_scaleDevice->bleTransport() : nullptr) {
        transport->disconnectFromDevice();
    }

    // Keep hunting passively — a present scale auto-connects via
    // onDeviceDiscovered the instant it's seen advertising.
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
}

void BLEManager::onScaleConnectionTimeout() {
    m_scaleDirectAbortTimer->stop();

    // A transport still holding anything at the overall timeout is stuck and
    // must be torn down before the retry/fallback below. Two shapes:
    //  - a parked foreground direct-connect (controller held in Connecting,
    //    #1303) — the ~4s abort usually clears this, so this only runs when
    //    that abort hasn't;
    //  - a connect (foreground or background) whose link came up but whose
    //    setup stalled (e.g. a discovery error before the scale became ready —
    //    the driver's watchdog isn't armed pre-ready, so nothing else recovers
    //    it, and a connected peripheral doesn't advertise, so the retry scans
    //    below can never find it while the link is held) (#1519).
    // Clear the direct-connect flag FIRST so any signal emitted during
    // teardown can't re-enter abortScaleDirectConnectIfPending.
    const bool notConnected = !(m_scaleDevice && m_scaleDevice->isConnected());
    const bool wasParked = m_directConnectInProgress && notConnected;
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();
    if (notConnected) {
        auto* transport = m_scaleDevice ? m_scaleDevice->bleTransport() : nullptr;
        // Gate on the transport actually holding something — the perpetual 60s
        // retry ladder hits this timeout on every cycle while the scale is
        // simply absent, and must not log/churn a teardown of nothing.
        if (transport && (wasParked || transport->isConnected())) {
            appendScaleLog(wasParked
                ? QStringLiteral("Scale connection timeout — tearing down parked direct-connect controller")
                : QStringLiteral("Scale connection timeout — tearing down stuck connection setup"));
            transport->disconnectFromDevice();
        }
    }

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        return;  // Connection raced in — nothing to do.
    }

    // Consume the manual-add marker: a manually-typed WiFi address reports
    // "Not found" directly (below) rather than silently switching to a BLE scan.
    const bool manualWifiAttempt = m_manualWifiConnect;
    const QString manualHost = m_pendingWifiHostname;
    m_manualWifiConnect = false;

    qWarning() << "BLEManager: Scale connection timeout - not found";

    // Heartbeat for the BLE-stack-wedge detector (#1309): a scale that keeps
    // failing to connect is one half of the wedge fingerprint. The detector
    // only acts if the DE1 is also down and recently faulting, so a merely
    // absent scale here can't trigger an adapter cycle on its own.
    evaluateBleWedge(QStringLiteral("scale-timeout"));

    // Manual "Add WiFi Scale" entry that didn't validate (no HDS frame within
    // the connection-timer window): surface a clear, user-visible error and
    // return BEFORE the FlowScale-fallback path. The typed address was NOT
    // persisted as the saved primary (main.cpp's scaleDiscovered handler
    // defers persistence for manual entries until DecentScaleWifi recognizes
    // the endpoint as HDS — see #1281), so nothing here needs to undo state.
    // The user can try again with a different address.
    if (manualWifiAttempt) {
        appendScaleLog(QString("Manual WiFi scale validation failed for %1").arg(manualHost));
        emit manualWifiValidationFailed(manualHost);
        emit disconnectScaleRequested();   // tear down the half-open WiFi driver
        return;
    }

    // WiFi-saved scale that didn't connect → fall back to a BLE scan for the
    // same physical scale family. Don't go straight to FlowScale yet — we owe
    // the user one more reasonable attempt before giving up. Only run the
    // fallback once per saved-scale cycle (the `!m_wifiFallbackToBleActive`
    // guard prevents a second fallback if the BLE scan itself times out). A
    // manual "Add WiFi Scale" attempt opts out — it surfaces "Not found" instead.
    if (!manualWifiAttempt && !m_wifiFallbackToBleActive
            && m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        beginWifiFallbackToBleScan();
        return;
    }

    m_scaleConnectionFailed = true;
    emit scaleConnectionFailedChanged();

    if (!m_flowScaleFallbackEmitted) {
        m_flowScaleFallbackEmitted = true;
        appendScaleLog("Scale not found - using FlowScale");
        emit flowScaleFallback();
    }

    // Always emit the retry-arm signal — even when the dialog gate has
    // already fired. flowScaleFallback is the dialog trigger (gated to one
    // shot per saved-scale cycle so the "No Scale Found" notice doesn't
    // re-pop on every retry); scaleRetryNeeded is the retry-ladder trigger
    // and must keep firing. Without this, a WiFi→BLE fallback whose own
    // BLE attempt also times out used to fall into a silent dead end: the
    // reconnect timer in main.cpp had been stopped by the scale-type
    // change, and there was no signal that re-armed it.
    emit scaleRetryNeeded();
}

void BLEManager::beginWifiFallbackToBleScan() {
    const QString hostname = m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)
        ? m_savedScaleAddress.mid(QStringLiteral("wifi:").size())
        : QString();

    // Bail BEFORE arming the fallback state if Bluetooth is unavailable —
    // otherwise m_wifiFallbackToBleActive would stay true and the next
    // user-initiated scan could trip the isFallbackCandidate gate in
    // onDeviceDiscovered, auto-connecting to any Decent BLE scale found.
    if (!isBluetoothAvailable()) {
        qWarning() << "BLEManager: WiFi fallback to BLE skipped - Bluetooth unavailable";
        appendScaleLog(QString("WiFi scale %1 unreachable and Bluetooth unavailable").arg(hostname));
        m_scaleConnectionFailed = true;
        emit scaleConnectionFailedChanged();
        if (!m_flowScaleFallbackEmitted) {
            m_flowScaleFallbackEmitted = true;
            emit flowScaleFallback();
        }
        // Honor the scaleRetryNeeded contract: this is a connection-failure
        // give-up that must re-arm the retry ladder, just like the equivalent
        // branch in onScaleConnectionTimeout. Without this, a user with a
        // saved WiFi scale offline AND Bluetooth disabled would be stuck on
        // FlowScale until they manually re-engaged — the exact bug class this
        // PR is fixing.
        emit scaleRetryNeeded();
        return;
    }

    m_wifiFallbackToBleActive = true;
    appendScaleLog(QString("WiFi scale %1 unreachable — trying Bluetooth").arg(hostname));
    emit wifiUnreachableFallingBackToBle(hostname);

    // Re-arm the connection timer so the fallback BLE scan has a bounded
    // time budget — onScaleConnectionTimeout trips the FlowScale fallback
    // on the second timeout (the m_wifiFallbackToBleActive guard prevents
    // looping back into another WiFi-fallback cycle).
    m_scaleConnectionTimer->start();

    // Start scanning for BLE devices. onDeviceDiscovered sees a Decent BLE
    // scale, observes m_wifiFallbackToBleActive, and emits scaleDiscovered
    // even though the saved address (wifi:...) doesn't match this BLE device.
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
}

void BLEManager::probeWifiPrimaryReachable(const QString& ip) {
    // NON-disruptive HDS identity check: open ws://<ip>/snapshot and require a
    // valid HDS frame (snapshot or status — both per the openscale WS protocol)
    // within the timeout. Does NOT touch the live (BLE backup) scale, so a
    // negative result costs nothing. This is the gate before switchToWifiPrimary():
    // only drop a working backup once we've VERIFIED the primary IP actually
    // hosts an HDS scale.
    //
    // Earlier this was a bare TCP connect to port 80. That was too permissive:
    // a home router (or anything on the LAN listening on 80) would accept the
    // SYN and the probe would falsely report "reachable", which then tore down
    // the working BLE link to chase a phantom WiFi scale. Real-world symptom:
    // user manually typed 192.168.1.1 (their gateway) as the scale address; the
    // app cycled BLE↔WiFi every ~30 s with the gateway answering the probe and
    // returning HTTP 403 to the WS upgrade. (See #1281.)
    constexpr int kProbeTimeoutMs = 3500;

    cancelWifiProbe();  // at most one probe in flight

    if (ip.isEmpty()) {
        emit wifiPrimaryReachable(false);
        return;
    }

    m_wifiProbeWebSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_wifiProbeTimer = new QTimer(this);
    m_wifiProbeTimer->setSingleShot(true);

    // Resolve the probe exactly once: the first of valid-frame / error / timeout
    // wins. Members are nulled up front so any later signal short-circuits
    // instead of emitting twice.
    auto finish = [this](bool reachable) {
        if (!m_wifiProbeWebSocket) return;  // already resolved
        QWebSocket* sock = m_wifiProbeWebSocket;
        QTimer* timer = m_wifiProbeTimer;
        m_wifiProbeWebSocket = nullptr;
        m_wifiProbeTimer = nullptr;
        timer->stop();
        timer->deleteLater();
        sock->disconnect();   // drop our slots before abort()
        sock->abort();
        sock->deleteLater();
        emit wifiPrimaryReachable(reachable);
    };

    connect(m_wifiProbeWebSocket, &QWebSocket::textMessageReceived, this,
            [finish](const QString& message) {
        // Validate the frame is HDS-shaped — same recognition contract as
        // DecentScaleWifi::onRecognizedAsHds. A snapshot frame has no "type"
        // and carries a "grams" number; a status frame has type=="status".
        // Anything else (or non-JSON) means "WS connected to a non-HDS
        // endpoint" — fail the probe.
        const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (!doc.isObject()) { finish(false); return; }
        const QJsonObject obj = doc.object();
        const QString type = obj.value(QStringLiteral("type")).toString();
        const bool isSnapshot = type.isEmpty()
            && obj.value(QStringLiteral("grams")).isDouble();
        const bool isStatus = (type == QStringLiteral("status"));
        finish(isSnapshot || isStatus);
    });
    connect(m_wifiProbeWebSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, [finish](QAbstractSocket::SocketError) { finish(false); });
    connect(m_wifiProbeWebSocket, &QWebSocket::disconnected, this,
            [finish]() { finish(false); });
    connect(m_wifiProbeTimer, &QTimer::timeout, this, [finish]() { finish(false); });

    const QUrl url(QStringLiteral("ws://%1/snapshot").arg(ip));
    appendScaleLog(QString("Probing WiFi primary at %1 (%2 ms, HDS verify)")
                       .arg(ip).arg(kProbeTimeoutMs));
    m_wifiProbeTimer->start(kProbeTimeoutMs);
    m_wifiProbeWebSocket->open(url);
}

void BLEManager::cancelWifiProbe() {
    if (m_wifiProbeTimer) {
        m_wifiProbeTimer->stop();
        m_wifiProbeTimer->deleteLater();
        m_wifiProbeTimer = nullptr;
    }
    if (m_wifiProbeWebSocket) {
        m_wifiProbeWebSocket->disconnect();
        m_wifiProbeWebSocket->abort();
        m_wifiProbeWebSocket->deleteLater();
        m_wifiProbeWebSocket = nullptr;
    }
}

void BLEManager::switchToWifiPrimary() {
    if (!m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        return;  // primary isn't a WiFi scale — nothing to switch back to
    }
    const QString hostname = m_savedScaleAddress.mid(QStringLiteral("wifi:").size());
    qDebug() << "BLEManager: WiFi primary reachable again — switching back from backup to" << hostname;
    appendScaleLog(QString("WiFi primary %1 reachable — switching back from backup").arg(hostname));

    // Drop the current backup scale, then connect the WiFi primary. main.cpp's
    // disconnectScaleRequested handler tears down the live scale; the
    // scaleDiscovered emission then (re)creates the WiFi driver and connects via
    // connectToHost()'s cached-IP fast path (the IP we just probed reachable).
    // Arm the connection timer so a surprise failure (e.g. the cached IP was
    // reassigned between probe and dial) is recovered: onScaleConnectionTimeout
    // routes into beginWifiFallbackToBleScan() (a BLE scan) rather than leaving
    // the scale stranded on FlowScale; FlowScale is reached only if that BLE
    // scan also times out.
    m_wifiFallbackToBleActive = false;
    m_scaleConnectionTimer->start();
    emit disconnectScaleRequested();
    m_pendingWifiHostname = hostname;
    // Nothing fresh to offer here — connectToHost() already dials the
    // persisted cache directly (that's what probeWifiPrimaryReachable just
    // validated), so there's no new resolution result to hand along.
    m_pendingWifiResolvedIp.clear();
    emit scaleDiscovered(QBluetoothDeviceInfo{}, QStringLiteral("decent-wifi"));
}

void BLEManager::setSavedScaleAddress(const QString& address, const QString& type, const QString& name) {
    m_savedScaleAddress = address;
    m_savedScaleType = type;
    m_savedScaleName = name;
}

void BLEManager::resetScaleConnectionState() {
    // Only reset direct-connect state so a fresh attempt can proceed.
    // Do NOT reset m_scaleConnectionFailed or m_flowScaleFallbackEmitted here:
    // - m_scaleConnectionFailed: keeps UI showing "Not found" during retries (no flicker)
    // - m_flowScaleFallbackEmitted: prevents re-showing FlowScale dialog on each retry
    // Both are reset when the scale actually connects (onScaleConnectedChanged).
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();
    m_scaleConnectionTimer->stop();
    m_scaleDirectAbortTimer->stop();
}

void BLEManager::clearSavedScale() {
    m_savedScaleAddress.clear();
    m_savedScaleType.clear();
    m_savedScaleName.clear();
    m_scaleConnectionFailed = false;
    m_scaleConnectionTimer->stop();
    m_flowScaleFallbackEmitted = false;
    emit scaleConnectionFailedChanged();
    // Stop any pending auto-reconnect timer in main.cpp
    emit disconnectScaleRequested();
}

// === Refractometer support ===

QVariantList BLEManager::discoveredRefractometers() const {
    QVariantList result;
    for (const auto& device : m_refractometerDevices) {
        QVariantMap map;
        map["name"] = device.name();
        map["address"] = getDeviceIdentifier(device);
        map["type"] = QStringLiteral("DiFluid R2");
        result.append(map);
    }
    return result;
}

bool BLEManager::isRefractometerConnected() const {
    return m_refractometerDevice && m_refractometerDevice->isConnected();
}

QBluetoothDeviceInfo BLEManager::getRefractometerDeviceInfo(const QString& address) const {
    for (const auto& device : m_refractometerDevices) {
        if (deviceIdentifiersMatch(device, address)) {
            return device;
        }
    }
    return QBluetoothDeviceInfo();
}

void BLEManager::connectToRefractometer(const QString& address) {
    QBluetoothDeviceInfo info = getRefractometerDeviceInfo(address);
    if (info.isValid()) {
        appendScaleLog(QString("Connecting to refractometer: %1 (%2)").arg(info.name(), address));
        emit refractometerDiscovered(info);
    }
}

void BLEManager::setSavedRefractometerAddress(const QString& address, const QString& name) {
    m_savedRefractometerAddress = address;
    m_savedRefractometerName = name;
}

void BLEManager::clearSavedRefractometer() {
    m_savedRefractometerAddress.clear();
    m_savedRefractometerName.clear();
    m_refractometerDevice = nullptr;
    emit refractometerConnectedChanged();
    emit disconnectRefractometerRequested();
}

void BLEManager::setRefractometerDevice(RefractometerDevice* device) {
    qDebug().noquote() << QString("[R2-diag] setRefractometerDevice old=%1 new=%2")
        .arg(m_refractometerDevice ? QString::number(reinterpret_cast<quintptr>(m_refractometerDevice), 16)
                                    : QStringLiteral("none"),
             device ? QString::number(reinterpret_cast<quintptr>(device), 16)
                     : QStringLiteral("none"));
    if (m_refractometerDevice) {
        disconnect(m_refractometerDevice, nullptr, this, nullptr);
    }
    m_refractometerDevice = device;
    if (m_refractometerDevice) {
        connect(m_refractometerDevice, &RefractometerDevice::connectedChanged,
                this, [this]() {
            emit refractometerConnectedChanged();
            // Keep the hunt persistent while the review page is open: onScanFinished
            // only re-chains when a scan is already in flight, and none is once we
            // were connected. So if the R2 drops mid-page, re-kick the scan chain.
            if (m_refractometerHunt && !isRefractometerConnected() && !m_scanningForScales) {
                qDebug().noquote() << "[R2-diag] R2 dropped while hunting — re-kicking scan";
                tryDirectConnectToRefractometer();
            }
        });
    }
    emit refractometerConnectedChanged();
}

void BLEManager::tryDirectConnectToRefractometer() {
    // The R2 is only used to read TDS/EY on the post-shot review page, so its
    // auto-reconnect is scoped to the hunt (review page open). Off that page we
    // don't need the R2, and keeping it scanning/connecting there was pure BLE
    // contention noise — endless failed connect attempts that never help. This
    // is the single chokepoint every auto-reconnect path funnels through (the
    // reconnect tick, startup, resume, screensaver-exit), so gating it here
    // scopes them all without touching the scale's separate, always-on
    // reconnect. Manual pairing from Settings goes through connectToRefractometer()
    // → refractometerDiscovered and is intentionally unaffected.
    if (!m_refractometerHunt) {
        qDebug().noquote() << "[R2-diag] tryDirectConnectToRefractometer no-op (not hunting — review page closed)";
        return;
    }
    if (m_savedRefractometerAddress.isEmpty() || m_disabled) {
        qDebug().noquote() << QString("[R2-diag] tryDirectConnectToRefractometer no-op (savedAddrEmpty=%1 disabled=%2)")
            .arg(m_savedRefractometerAddress.isEmpty() ? QStringLiteral("true") : QStringLiteral("false"),
                 m_disabled ? QStringLiteral("true") : QStringLiteral("false"));
        return;
    }
    // Check availability BEFORE setting the scan flag (as tryDirectConnectToDE1
    // does): with Bluetooth off, startScan() returns without starting anything,
    // and a flag set here would never be cleared — scan finished/error/stop are
    // the only clearers and none would fire — turning every later reconnect
    // attempt into a "scan flag already set" no-op even after Bluetooth returns.
    if (!isBluetoothAvailable()) {
        qDebug().noquote() << "[R2-diag] tryDirectConnectToRefractometer no-op (Bluetooth unavailable) "
                              "— background reconnect tick will retry";
        return;
    }
    // Piggyback on the scale scan infrastructure — set the flag so
    // onDeviceDiscovered processes refractometer advertisements
    qDebug().noquote() << QString("[R2-diag] tryDirectConnectToRefractometer scanningForScales=%1 scanning=%2 -> %3")
        .arg(m_scanningForScales ? QStringLiteral("true") : QStringLiteral("false"),
             m_scanning ? QStringLiteral("true") : QStringLiteral("false"),
             m_scanningForScales ? QStringLiteral("no-op (scan flag already set)")
                                  : QStringLiteral("startScan()"));
    if (!m_scanningForScales) {
        m_scanningForScales = true;
        startScan();
    }
}

void BLEManager::setRefractometerHunt(bool active) {
    if (m_refractometerHunt == active) {
        return;
    }
    m_refractometerHunt = active;
    qDebug().noquote() << QString("[R2-diag] refractometer hunt %1")
        .arg(active ? QStringLiteral("ON — scans will chain back-to-back while a saved refractometer is disconnected")
                    : QStringLiteral("OFF — refractometer reconnect stops until the review page reopens"));
    if (active && !isRefractometerConnected()) {
        tryDirectConnectToRefractometer();
    }
    // Let main.cpp arm/stop the persistent reconnect tick to match the hunt —
    // the tick is the backoff-paced recovery path if the scan chain dies.
    emit refractometerHuntChanged(active);
}

void BLEManager::setSavedDE1Address(const QString& address, const QString& name) {
    m_savedDE1Address = address;
    m_savedDE1Name = name;
}

void BLEManager::clearSavedDE1() {
    m_savedDE1Address.clear();
    m_savedDE1Name.clear();
}

void BLEManager::tryDirectConnectToDE1() {
    if (m_disabled) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - disabled (simulator mode)";
        return;
    }

    if (m_savedDE1Address.isEmpty()) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - no saved DE1 address";
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: tryDirectConnectToDE1 - Bluetooth is powered off, skipping";
        return;
    }

    // Don't attempt if already connected or connecting
    // (the de1Discovered handler in main.cpp checks this before connecting)

    QString deviceName = m_savedDE1Name.isEmpty() ? "DE1" : m_savedDE1Name;

    // Same identifier-driven choice as tryDirectConnectToScale: a direct GATT
    // connect needs a real MAC, and on CoreBluetooth backends (iOS and macOS)
    // the saved identifier is a device UUID. Not an error — just not dialable.
    QString upperAddress = m_savedDE1Address.toUpper();
    QBluetoothAddress address(upperAddress);
    if (address.isNull()) {
        qDebug() << "BLEManager: DE1 direct wake - identifier is not a MAC, scanning for"
                 << deviceName << "id:" << m_savedDE1Address;
        emit de1LogMessage(QString("Direct wake: scanning for %1 (identifier is not a MAC)")
                           .arg(deviceName));
        if (!m_scanning) {
            startScan();
        }
        return;
    }
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    qDebug() << "BLEManager: DE1 direct wake - connecting to" << deviceName << "at" << upperAddress;
    emit de1LogMessage(QString("Direct wake: connecting to %1 at %2").arg(deviceName, upperAddress));

    // A DE1 direct-wake connect is now being initiated — gate the scale's BLE
    // direct-connect behind it (two concurrent GATT connects collide on the
    // Android stack). Arm the 15 s cap here so the gate is always bounded even if
    // the DE1 connect never resolves (no DE1 present); it's cleared in
    // onDe1ConnectionSettled() on success/failure, or by the cap timer. Set only
    // at the point of an actual connect — early-returns above must not gate.
    m_de1DirectConnectInFlight = true;
    if (!m_de1WaitTimer->isActive()) m_de1WaitTimer->start();

    // Emit de1Discovered so main.cpp's handler connects to the device
    emit de1Discovered(deviceInfo);

    // Also start scanning in parallel - if the DE1 is advertising, we'll find it
    if (!m_scanning) {
        startScan();
    }
}

void BLEManager::scanForDevices() {
    // Note: m_disabled is intentionally not checked here — scale and refractometer
    // scanning is allowed in simulator mode so real hardware can be tested against
    // a simulated DE1. Only DE1 BLE (startScan without m_scanningForScales) is suppressed.
    qDebug().noquote() << QString("[R2-diag] scanForDevices (user-initiated) scanning=%1 scanningForScales=%2 (read before stopScan)")
        .arg(m_scanning ? QStringLiteral("true") : QStringLiteral("false"),
             m_scanningForScales ? QStringLiteral("true") : QStringLiteral("false"));
    appendScaleLog("Starting device scan...");
    m_scaleConnectionFailed = false;
    m_flowScaleFallbackEmitted = false;  // User-initiated scan resets the dialog guard
    emit scaleConnectionFailedChanged();

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: scanForDevices - Bluetooth is powered off, skipping";
        return;
    }

    // If already scanning, we need to restart to include scales
    if (m_scanning) {
        stopScan();
    }

    // Set flags AFTER stopScan (which clears them)
    m_scanningForScales = true;
    m_userInitiatedScaleScan = true;
    // A user-initiated scan invalidates any in-flight WiFi-to-BLE fallback —
    // the user is explicitly choosing what to connect to. If we left the flag
    // armed, the next discovered Decent BLE scale would be silently treated
    // as a "temporary fallback" and main.cpp would skip persisting it as the
    // primary, demoting the user's explicit pick.
    m_wifiFallbackToBleActive = false;
    startScan();

    // Fire the WiFi mDNS probe in parallel with the BLE scan. On-demand only;
    // no idle probing per the project requirement. 5 s timeout matches the
    // saved-scale rehydration path — the HDS mDNS responder regularly takes
    // 2-4 s to reply (likely the ESP32 being woken from a power-save state),
    // and the BLE scan is running in parallel for ~10 s anyway so this
    // doesn't slow down the user-perceived scan duration.
    ensureWifiDiscovery();
    m_wifiDiscovery->probe(QStringLiteral("hds.local"), 5000);
}

void BLEManager::ensureWifiDiscovery() {
    if (m_wifiDiscovery) return;
    m_wifiDiscovery = new WifiScaleDiscovery(this);
    // Forward mDNS-layer diagnostics into the user-shareable scale debug log.
    // Without this, "mDNS lookup timed out" / "no responder" lines lived only
    // in qDebug output (Qt Creator console / adb logcat), invisible in the log
    // a user uploads with a bug report.
    connect(m_wifiDiscovery, &WifiScaleDiscovery::logMessage, this,
            [this](const QString& msg) {
        appendScaleLog(QString("[WifiScaleDiscovery] %1").arg(msg));
    });
    // Single unified handler that handles both code paths (user-initiated
    // scan AND saved-scale direct-wake). Before this consolidation, each
    // call site lazy-created the discovery object with a DIFFERENT lambda
    // and the second registration was silently dropped by the
    // `if (!m_wifiDiscovery)` guard — breaking whichever path ran second.
    connect(m_wifiDiscovery, &WifiScaleDiscovery::scaleFound, this,
        [this](const QString& hostname, const QString& resolvedAddress) {
            const QString address = QStringLiteral("wifi:") + hostname;
            qDebug() << "[BLE] WiFi scale found:" << hostname
                     << "->" << resolvedAddress
                     << "address=" << address
                     << "userInitiatedScan=" << m_userInitiatedScaleScan
                     << "saved=" << m_savedScaleAddress;
            // Append to the discovered-scales list if not already present —
            // useful for the user-initiated scan, harmless for the auto-
            // reconnect path (the UI list is hidden during direct-wake).
            qsizetype existingIndex = -1;
            for (qsizetype i = 0; i < m_scales.size(); ++i) {
                if (m_scales[i].transport == QStringLiteral("wifi") && m_scales[i].address == address) {
                    existingIndex = i;
                    break;
                }
            }
            if (existingIndex < 0) {
                ScaleEntry entry;
                entry.type = QStringLiteral("decent-wifi");
                entry.transport = QStringLiteral("wifi");
                entry.name = QStringLiteral("Half Decent Scale (WiFi)");
                entry.address = address;
                entry.resolvedIp = resolvedAddress;
                m_scales.append(entry);
                qDebug() << "[BLE] Added WiFi scale to discovered list; m_scales count=" << m_scales.size();
                appendScaleLog(QString("Found %1 (%2)").arg(entry.name, entry.address));
                emit scalesChanged();
            } else {
                // Refresh the resolved IP even on a repeat find — DHCP may have
                // moved the scale since the last scan, and connectToScale() reads
                // this field to seed the connect.
                m_scales[existingIndex].resolvedIp = resolvedAddress;
                qDebug() << "[BLE] WiFi scale already in discovered list — not re-adding";
            }

            // Auto-connect when the discovered scale matches the saved primary,
            // including during user-initiated scans. Matching the saved primary
            // is itself the anti-hijack guard — see the matching reasoning on
            // the BLE path in onDeviceDiscovered. Covers both paths:
            // saved-scale direct-wake on app start AND a spontaneous (or
            // user-initiated) scan that happens to find the saved scale.
            if (!m_savedScaleAddress.isEmpty()
                    && address.compare(m_savedScaleAddress, Qt::CaseInsensitive) == 0) {
                m_pendingWifiHostname = hostname;
                // This mDNS resolve just happened — hand the IP along so
                // main.cpp can seed DecentScaleWifi's cache and dial it
                // directly instead of re-resolving the hostname itself.
                m_pendingWifiResolvedIp = resolvedAddress;
                emit scaleDiscovered(QBluetoothDeviceInfo{}, QStringLiteral("decent-wifi"));
            }
        });
}

void BLEManager::tryDirectConnectToScale(bool allowDirectConnect) {
    // See connectToSavedScale: the DE1 simulator must not gate real scale connects.
    // This early-return on m_disabled is why a WiFi scale could never recover
    // on its own in simulator mode — the driver correctly deferred its retry to
    // the app-level reconnect loop, and the loop landed here and gave up.
    if (m_scaleSimulated) {
        // appendScaleLog, not qDebug: this is the always-on background reconnect
        // path, so it is the one a user or support engineer reading an exported
        // scale log would be staring at when their scale never connects. qDebug
        // reaches only a console nobody is watching.
        appendScaleLog("Auto-reconnect skipped — simulated scale is active");
        return;
    }

    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - no saved scale address/type";
        return;
    }

    // The simulator's synthetic primary is not dialable. Guarded HERE, at the
    // single chokepoint every reconnect path funnels through, rather than at
    // each caller. Without it a "sim:" address falls past the wifi:/usb: cases
    // into the BLE branch, which builds an invalid QBluetoothAddress, arms the
    // 20 s connection timer, and on timeout raises the "No Scale Found" dialog
    // and re-arms the ladder — forever. Reachable as soon as anything starts
    // the ladder while the simulated scale is switched off.
    if (savedScaleIsSimulated()) {
        appendScaleLog("Auto-reconnect skipped — saved scale is the simulator's "
                       "synthetic entry, nothing to dial");
        return;
    }

    if (!isBluetoothAvailable()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - Bluetooth is powered off, skipping";
        return;
    }

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - scale already connected";
        return;
    }

    // This is a saved-scale (re)connect, not a manual "Add WiFi Scale" attempt —
    // so a timeout here should take the normal WiFi→BLE fallback path.
    m_manualWifiConnect = false;

    // Direct wake strategy:
    // 1. Try direct connection to saved address (may wake sleeping scales that respond to connect requests)
    // 2. Also start scanning in parallel (finds scales that are actively advertising)
    // 3. Whichever succeeds first wins - we don't skip scan results even if direct connect is in progress
    //
    // de1app does both: ble connect + scanning, and calls ble_connect_to_scale again when
    // the device appears in scan results (bluetooth.tcl lines 2032 and 2252-2256)

    QString deviceName = m_savedScaleName.isEmpty() ? m_savedScaleType : m_savedScaleName;

    // WiFi saved-scale path: hand off to the scale driver's connect (cached IP
    // first, mDNS only as fallback) — see the detailed note inside the branch.
    // If the connection doesn't establish before m_scaleConnectionTimer fires
    // (~20 s), we fall back to a BLE scan that auto-connects to a discovered
    // Decent scale (see onScaleConnectionTimeout and beginWifiFallbackToBleScan).
    if (m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        const QString hostname = m_savedScaleAddress.mid(QStringLiteral("wifi:").size());
        qDebug() << "BLEManager: Direct wake (WiFi) - connecting to" << hostname
                 << "(cached IP first, mDNS fallback)";
        appendScaleLog(QString("Direct wake (WiFi): connecting to %1").arg(hostname));

        // Reconnect through the scale driver's own connect path instead of
        // gating on a fresh mDNS probe. DecentScaleWifi::connectToHost() tries
        // the persisted peer IP first (settings key scale/wifiIp/<hostname>) and
        // dials ws://<ip>/snapshot directly — no multicast — falling back to an
        // mDNS resolve only if that cached IP fails the recognition window (DHCP
        // moved it). This makes reconnect resilient to the tablet's mDNS going
        // deaf under BLE-coexistence load: when we already know the scale's IP we
        // don't need to hear a multicast reply to reconnect. With no cached IP it
        // behaves as before (connectToHost falls through to an mDNS resolve). The
        // 20 s connection timer still arms the WiFi->BLE fallback if the cached
        // IP is genuinely unreachable (onScaleConnectionTimeout).
        m_wifiFallbackToBleActive = false;          // Reset per attempt
        m_scaleConnectionTimer->start();            // Fires onScaleConnectionTimeout if WiFi doesn't connect
        m_pendingWifiHostname = hostname;
        // No fresh resolution here — connectToHost() itself reads the
        // persisted cache (see comment above), so there's nothing new to hand along.
        m_pendingWifiResolvedIp.clear();
        emit scaleDiscovered(QBluetoothDeviceInfo{}, QStringLiteral("decent-wifi"));
        return;
    }

    // USB saved-scale path: NOT a BLE address — the USB scale is owned by
    // UsbScaleManager and reconnects via main.cpp's usbScaleAvailable handler
    // (which connects when the saved primary is "usb:decent"). Don't fall
    // through to the BLE connect/scan below — "usb:decent" is not a MAC.
    if (m_savedScaleAddress.startsWith(QStringLiteral("usb:"), Qt::CaseInsensitive)) {
        qDebug() << "BLEManager: tryDirectConnectToScale - saved scale is USB; "
                    "reconnect handled by UsbScaleManager";
        return;
    }

    // Background reconnect ladder (allowDirectConnect=false): scan only. A
    // passive scan coexists with the DE1 link, whereas a direct connectToDevice()
    // to an absent scale parks the Android BLE stack in Connecting for the full
    // ~30s supervision timeout every cycle, starving the DE1 link until it drops
    // and cannot recover (issue #1303). The saved scale still auto-connects via
    // onDeviceDiscovered the instant it's seen advertising — direct-connect buys
    // nothing here (an off scale can't be woken by a connect request), it only
    // costs radio contention. The foreground triggers (switch/startup/DE1-wake)
    // keep the direct-connect fast-path below by passing allowDirectConnect=true.
    if (!allowDirectConnect) {
        // appendScaleLog records to BOTH the user-shareable scale log and the
        // system debug log (it mirrors to qDebug with a [Scale] prefix), so the
        // background reconnect is visible in either capture. The scale log is a
        // 1000-entry ring buffer, so the perpetual 60s ladder can't grow it
        // without bound.
        appendScaleLog("Auto-reconnect: scanning for saved scale (no direct-connect)");
        m_scaleConnectionTimer->start();   // bounded budget; arms WiFi/FlowScale fallback + retry ladder
        m_scanningForScales = true;
        if (!m_scanning) {
            startScan();
        }
        return;
    }

    // Choose the path by what the SAVED IDENTIFIER actually is, not by platform.
    // A direct GATT connect needs a real MAC; on CoreBluetooth backends (iOS and
    // macOS) the identifier is a device UUID, and QBluetoothAddress(uuid) is
    // null — dialling it can only fail. This used to be `#ifdef Q_OS_IOS`, so
    // macOS took the MAC branch and spent ~4 s per startup connecting to the
    // null address before giving up. Behaviour on Android/Linux (real MACs) and
    // on iOS (UUIDs) is unchanged; macOS now correctly joins the scan path.
    if (QBluetoothAddress(m_savedScaleAddress.toUpper()).isNull()) {
        // Direct connect with just a UUID rarely works — find the device by
        // scanning and match on identity when it advertises.
        qDebug() << "BLEManager: Direct wake (no MAC) - scanning for" << deviceName
                 << "id:" << m_savedScaleAddress;
        appendScaleLog(QString("Direct wake: scanning for %1 (identifier is not a MAC)").arg(deviceName));

        m_directConnectInProgress = true;
        m_directConnectAddress = m_savedScaleAddress;  // UUID

        // Start timeout timer
        m_scaleConnectionTimer->start();

        m_scanningForScales = true;
        if (!m_scanning) {
            startScan();
        }
        return;
    }

    // Serialize behind the DE1's direct-wake connect: a second BLE GATT connect
    // while the DE1's is in flight collides on the Android stack — the scale
    // connect dies the instant the DE1 finishes, then sits out the 20 s timeout
    // before retrying. Defer until the DE1 settles (onDe1ConnectionSettled) or a
    // 15 s cap (m_de1WaitTimer), which still connects the scale with no DE1.
    if (m_de1DirectConnectInFlight) {
        m_scaleConnectDeferred = true;
        if (!m_de1WaitTimer->isActive()) m_de1WaitTimer->start();
        qDebug() << "BLEManager: deferring scale direct-connect until DE1 settles (15 s cap)";
        appendScaleLog("Waiting for the DE1 to finish connecting before connecting the scale (15 s cap)");
        return;
    }

    // On Android/desktop, we have a MAC address - try direct connect
    QString upperAddress = m_savedScaleAddress.toUpper();
    QBluetoothAddress address(upperAddress);
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    qDebug() << "BLEManager: Direct wake - connecting to" << deviceName << "at" << upperAddress;
    appendScaleLog(QString("Direct wake: connecting to %1 at %2").arg(deviceName, m_savedScaleAddress));

    // Mark that we're doing a direct connect - but we won't skip scan results
    // Instead, onDeviceDiscovered will check if scale is already connected
    m_directConnectInProgress = true;
    m_directConnectAddress = upperAddress;

    // Start timeout timer
    m_scaleConnectionTimer->start();

    // Try direct connection - this may wake the scale
    emit scaleDiscovered(deviceInfo, m_savedScaleType);

    // Also start scanning in parallel - if the scale is advertising, we'll find it
    // and connect via the scan path (which has a real QBluetoothDeviceInfo)
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }

    // Bound the direct attempt (de1app closes its direct connect after ~4s and
    // relies on the scan): if it hasn't connected by then, abort the parked
    // controller so an absent scale can't hold the Android BLE stack in
    // Connecting for the full ~30s timeout. The scan keeps running. Using the
    // cancellable member timer (restarted here) means a fresh attempt or a
    // successful connect stops any stale pending abort rather than leaking shots.
    m_scaleDirectAbortTimer->start();
}

void BLEManager::onDe1ConnectionSettled() {
    // The DE1's direct-wake connect has resolved (connected, or the attempt
    // ended) — drop the gate and run any scale direct-connect that was deferred
    // behind it to avoid a concurrent-GATT-connect collision on Android.
    m_de1DirectConnectInFlight = false;
    m_de1WaitTimer->stop();
    if (m_scaleConnectDeferred) {
        m_scaleConnectDeferred = false;
        appendScaleLog("DE1 connect settled — starting deferred scale connect");
        tryDirectConnectToScale();
    }
}

void BLEManager::openLocationSettings()
{
#ifdef Q_OS_ANDROID
    QJniObject action = QJniObject::fromString("android.settings.LOCATION_SOURCE_SETTINGS");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && intent.isValid()) {
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    }
#else
    qDebug() << "openLocationSettings is only available on Android";
#endif
}

void BLEManager::openBluetoothSettings()
{
#ifdef Q_OS_ANDROID
    QJniObject action = QJniObject::fromString("android.settings.BLUETOOTH_SETTINGS");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callMethod<QJniObject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && intent.isValid()) {
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    }
#elif defined(Q_OS_IOS)
    // iOS: Open the app's Settings page. iOS doesn't allow deep-linking directly to
    // the Bluetooth settings screen, but UIApplicationOpenSettingsURLString takes the
    // user to Settings > Decenza where they can see Bluetooth permission status.
    NSURL* url = [NSURL URLWithString:UIApplicationOpenSettingsURLString];
    if (url && [[UIApplication sharedApplication] canOpenURL:url]) {
        [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
    }
#elif defined(Q_OS_MACOS)
    // macOS: Open System Settings to Bluetooth privacy pane
    QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth"));
#else
    qDebug() << "openBluetoothSettings is not implemented for this platform";
#endif
}

// Scale debug logging methods
void BLEManager::appendScaleLog(const QString& message) {
    QString timestampedMsg = QDateTime::currentDateTime().toString("[hh:mm:ss.zzz] ") + message;
    m_scaleLogMessages.append(timestampedMsg);
    emit scaleLogMessage(message);
    // Mirror to the system log so the scale narrative is interleaved with
    // qDebug output from the rest of the app — without this, the scale
    // debug log lives only in m_scaleLogMessages (rendered into the user-
    // shareable scale_debug_log.txt) and is invisible during local
    // development unless the developer is staring at the in-app log view.
    // Use a stable [Scale] prefix so the line is grep-friendly in stderr.
    qDebug().noquote() << "[Scale]" << message;

    // Keep log size reasonable (last 1000 messages)
    while (m_scaleLogMessages.size() > 1000) {
        m_scaleLogMessages.removeFirst();
    }
}

void BLEManager::clearScaleLog() {
    m_scaleLogMessages.clear();
    emit scaleLogMessage("Log cleared");
}

QString BLEManager::getScaleLogPath() const {
    return m_scaleLogFilePath;
}

void BLEManager::writeScaleLogToFile() {
    // Get app's cache directory for the log file
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    m_scaleLogFilePath = cacheDir + "/scale_debug_log.txt";

    QFile file(m_scaleLogFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== Decenza Scale Debug Log ===" << Qt::endl;
        out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << Qt::endl;
        out << "================================" << Qt::endl << Qt::endl;

        for (const QString& msg : m_scaleLogMessages) {
            out << msg << Qt::endl;
        }
        file.close();
        qDebug() << "Scale log written to:" << m_scaleLogFilePath;
    } else {
        qWarning() << "Failed to write scale log to:" << m_scaleLogFilePath;
    }
}

void BLEManager::shareScaleLog() {
    // First write the log to a file
    writeScaleLogToFile();

    if (m_scaleLogFilePath.isEmpty()) {
        qWarning() << "No log file path available";
        return;
    }

#ifdef Q_OS_ANDROID
    // Use Android's share intent
    QJniObject context = QNativeInterface::QAndroidApplication::context();

    // Create a file URI using FileProvider for Android 7+
    QJniObject fileObj = QJniObject::fromString(m_scaleLogFilePath);
    QJniObject file("java/io/File", "(Ljava/lang/String;)V", fileObj.object<jstring>());

    // Get the app's package name for FileProvider authority
    QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    QString authority = packageName.toString() + ".fileprovider";
    QJniObject authorityObj = QJniObject::fromString(authority);

    // Get content URI via FileProvider
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(),
        authorityObj.object<jstring>(),
        file.object());

    if (!uri.isValid()) {
        qWarning() << "Failed to get content URI for file";
        // Fallback: just notify user of file location
        emit scaleLogMessage("Log saved to: " + m_scaleLogFilePath);
        return;
    }

    // Create share intent
    QJniObject actionSend = QJniObject::fromString("android.intent.action.SEND");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", actionSend.object<jstring>());

    QJniObject mimeType = QJniObject::fromString("text/plain");
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", mimeType.object<jstring>());

    QJniObject extraStream = QJniObject::getStaticObjectField<jstring>("android/content/Intent", "EXTRA_STREAM");
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                           extraStream.object<jstring>(), uri.object());

    // Add grant read permission flag
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 1);  // FLAG_GRANT_READ_URI_PERMISSION

    // Create chooser
    QJniObject chooserTitle = QJniObject::fromString("Share Scale Debug Log");
    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent",
        "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(),
        chooserTitle.object<jstring>());

    chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    // Start the chooser activity
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());

    emit scaleLogMessage("Opening share dialog...");

#elif defined(Q_OS_IOS)
    // iOS: Use UIActivityViewController for sharing
    NSString* filePath = m_scaleLogFilePath.toNSString();
    NSURL* fileURL = [NSURL fileURLWithPath:filePath];

    if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        qWarning() << "Log file does not exist:" << m_scaleLogFilePath;
        emit scaleLogMessage("Error: Log file not found");
        return;
    }

    // Create activity view controller with the file URL
    NSArray* activityItems = @[fileURL];
    UIActivityViewController* activityVC = [[UIActivityViewController alloc]
        initWithActivityItems:activityItems
        applicationActivities:nil];

    // Get the root view controller to present from
    UIWindow* keyWindow = nil;
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]]) {
            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* window in windowScene.windows) {
                if (window.isKeyWindow) {
                    keyWindow = window;
                    break;
                }
            }
        }
        if (keyWindow) break;
    }

    UIViewController* rootVC = keyWindow.rootViewController;
    if (rootVC) {
        // For iPad, we need to set the popover presentation
        if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) {
            activityVC.popoverPresentationController.sourceView = rootVC.view;
            activityVC.popoverPresentationController.sourceRect = CGRectMake(
                rootVC.view.bounds.size.width / 2,
                rootVC.view.bounds.size.height / 2,
                0, 0);
        }

        [rootVC presentViewController:activityVC animated:YES completion:nil];
        emit scaleLogMessage("Opening share dialog...");
    } else {
        qWarning() << "Could not find root view controller for sharing";
        emit scaleLogMessage("Error: Could not open share dialog");
    }

#else
    // Desktop: just show the file path
    emit scaleLogMessage("Log saved to: " + m_scaleLogFilePath);
    qDebug() << "Scale log saved to:" << m_scaleLogFilePath;
#endif
}

QString BLEManager::translateUiString(const QString& key, const QString& fallback) const {
    if (m_translationManager) {
        return m_translationManager->translateString(key, fallback);
    }
    return fallback;
}
