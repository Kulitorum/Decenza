#include "blemanager.h"
#include "blegattqueue.h"

#include "bluetoothlogging.h"
#include "core/fileshare.h"
#include "network/webdebuglogger.h"
#include "refractometers/refractometerlogging.h"
#include "de1logging.h"
#include "scales/scalelogging.h"
#include "blecapability.h"
#include "scaledevice.h"
#include "transport/scalebletransport.h"
#include "protocol/de1characteristics.h"
#include "scales/decentscale.h"
#include "scales/scalefactory.h"
#include "scales/scaletypeids.h"
#include "refractometers/difluidr1.h"
#include "refractometers/difluidr2.h"
#include "refractometers/refractometerdevice.h"
#include "../core/settings_hardware.h"
#include "../core/translationmanager.h"
#include "../network/wifiscalediscovery.h"

namespace {
// How long the reconnect browse runs. Bound to kHdsResolveTimeoutMs in CODE
// rather than by a comment asserting the two agree: the immediately preceding
// change had to collapse three hand-written copies of this same 5000 for
// exactly that reason, and a prose "same budget" note would let them drift
// apart again silently. One number, one place.
//
// It lives here and not in blemanager.h because that header only
// forward-declares WifiScaleDiscovery, and pulling the network header into it
// would add a dependency to a header most of the app includes.
//
// An order of magnitude above the 362 ms in which a browse resolved this scale
// in the field — the case this exists to recover.
constexpr int kReconnectBrowseTimeoutMs = WifiScaleDiscovery::kHdsResolveTimeoutMs;

// The scan-lifecycle texts, defined once because each is now written at TWO
// sites: the cycle that emits it and the flush that closes its run. LogCollapse
// decides "repeat" by comparing the text, so two copies that must stay
// byte-identical is a silent-drift hazard of the worst kind — reword one and
// every cycle reads as a changed line, the collapse quietly stops collapsing,
// and nothing fails.
inline QString de1ScanningText()   { return QStringLiteral("Scanning for devices..."); }
inline QString de1ScanDoneText()   { return QStringLiteral("Scan complete"); }
inline QString scaleScanDoneText() { return QStringLiteral("Scan complete (background)"); }
inline QString huntChainText()     { return QStringLiteral("Hunt active — chaining another scan"); }

// Keys identify the RUN; the texts above say what the run reports. They
// correspond one-to-one today, and they are still separate because keying on the
// text would merge two runs the moment two sites happened to share wording —
// which the two "Scan complete" variants above are one edit away from doing.
constexpr auto kKeyDe1Scanning   = "de1.scanning";
constexpr auto kKeyDe1ScanDone   = "de1.scanDone";
constexpr auto kKeyScaleScanDone = "scale.scanDone";
constexpr auto kKeyHuntChain     = "hunt.chain";
}  // namespace
#include "../network/mdnsresolver.h"
#include "bleepochgate.h"
#include "version.h"
#include <QBluetoothLocalDevice>
#include <QMetaEnum>
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

    // The release event for a scale connect deferred behind GATT traffic. The
    // queue owns the definition of "clear"; BLEManager only acts on it.
    connect(&BleGattQueue::instance(), &BleGattQueue::drained,
            this, &BLEManager::onGattQueueDrained);

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
            BT_WARN_TAGGED("BLEManager", QStringLiteral("adapter still powered off") + QStringLiteral(" ") + QString("%1").arg((kAdapterRecoverySafetyMs / 1000)) + QStringLiteral(" ") + QStringLiteral("s into recovery — forcing power-on (#1309)"));
            setAdapterPower(true);
            finishAdapterRecovery(false);
        } else {
            // Adapter is on but we missed the HostConnectable event (or powerOff
            // was a no-op and it was never actually off). Treat as recovered.
            BT_WARN_TAGGED("BLEManager", QStringLiteral("recovery watchdog — adapter is on without an explicit "
                          "HostConnectable event; treating as recovered (#1309)"));
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
    // Name the mode, don't number it. `QString::arg(mode)` picks arg(int) by
    // integral promotion and prints a bare "0", which is what this line did after
    // it was mechanically converted from `qDebug() << mode` — and qDebug's operator
    // for a Q_ENUM had been spelling it "QBluetoothLocalDevice::HostPoweredOff".
    // This is the one line that answers "is the radio on?", i.e. the whole reason
    // the [Bluetooth] marker exists, so a reader must not need the Qt header to
    // decode it. HostMode is Q_ENUM-registered
    // (qtconnectivity/src/bluetooth/qbluetoothlocaldevice.h:37).
    const char* modeName =
        QMetaEnum::fromType<QBluetoothLocalDevice::HostMode>().valueToKey(mode);
    BT_LOG_TAGGED("BLEManager", QStringLiteral("Bluetooth host mode changed to %1")
                                    .arg(modeName ? QLatin1String(modeName)
                                                  : QLatin1String("unknown")));

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
            BT_LOG_TAGGED("BLEManager", QStringLiteral("adapter powered off during recovery — powering back on (#1309)"));
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

// Android API level, or -1 where it cannot be read (and on every other
// platform). Cached: SDK_INT is a permanent OS characteristic.
//
// Centralised because there are now two callers with different predicates —
// the dual-HIGH seed at SDK < 30 and the adapter-remedy gate at SDK >= 33 —
// and each was otherwise free to hand-roll its own JNI read, its own
// exception check and its own failure behaviour.
int BLEManager::androidSdkInt()
{
#ifdef Q_OS_ANDROID
    static const int cached = []() {
        QJniEnvironment jniEnv;
        const jint v = QJniObject::getStaticField<jint>(
            "android/os/Build$VERSION", "SDK_INT");
        const bool threw = jniEnv.checkAndClearExceptions();
        if (threw || v <= 0) {
            // Logged here, once, rather than at each caller — and it keeps the
            // exception-versus-bogus-value distinction that the callers' own
            // messages used to carry. Without it the two are indistinguishable
            // in a submitted log, which is the diagnostic that centralising
            // this read would otherwise have cost.
            BT_WARN_TAGGED("BLEManager", QStringLiteral(
                "could not read Android SDK_INT (value=%1, jni_exception=%2) — "
                "callers that gate on the API level will treat it as unknown")
                    .arg(static_cast<int>(v)).arg(threw ? "yes" : "no"));
            return -1;
        }
        return static_cast<int>(v);
    }();
    return cached;
#else
    return -1;
#endif
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
        BT_WARN_TAGGED("BLEManager", QStringLiteral("no BluetoothAdapter — cannot") + QStringLiteral(" ") + QString("%1").arg((on ? "enable" : "disable")) + QStringLiteral(" ") + QStringLiteral("(#1309)"));
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
        BT_INFO_TAGGED("BLEManager", QStringLiteral("adapter recovered — re-arming DE1 + scale reconnect (#1309)"));
        emit bleStackRecovered();          // main.cpp resets the DE1 reconnect budget + retries
        if (!m_savedScaleAddress.isEmpty())
            tryDirectConnectToScale();     // scale side re-arm
    } else {
        // We could not bring the radio back up. Flag it (so the next attempt
        // powers it on instead of treating OFF as user intent) and tell the
        // user — do NOT emit bleStackRecovered(), the stack is not recovered.
        m_recoveryLeftAdapterOff = true;
        BT_WARN_TAGGED("BLEManager", QStringLiteral("automatic Bluetooth restart did not bring the adapter "
                      "back up — asking the user to toggle it manually (#1309)"));
        scaleWarn(QStringLiteral("Auto Bluetooth restart failed — adapter still off (#1309)"));
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
        resetRepeatFailureBudget();   // ...and let the next failure of each kind be loud
    }
}

void BLEManager::onDe1Error(const QString& error)
{
    // Debounce: a failing reconnect ladder re-raises the same message on every
    // attempt, so show each distinct message only once until the DE1 reconnects.
    // Mirrors the onScanError debounce. Controller errors no longer reach here at
    // all — the transport keeps that whole path log-only (#1658) — so what lands
    // here is the service-discovery and USB-transport kind, e.g. "DE1 service not
    // found … try toggling Bluetooth off/on".
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
                BT_WARN_TAGGED("BLEManager", QStringLiteral("adapter still off from a prior failed recovery — "
                              "retrying power-on (#1309)"));
                setAdapterPower(true);
            }
        }
        return;
    }

    // API 33+ (Android 13): BluetoothAdapter.disable()/enable() are no-ops for
    // a non-privileged app. The call returns, nothing happens, no hostMode
    // event ever arrives, and ~10 s later the safety watchdog observes the
    // adapter is on — because it was never off — and calls
    // finishAdapterRecovery(true), which logs "adapter recovered". In #1810
    // that is 100 out of 100 recoveries: every one reported success having
    // changed nothing. The only real effect the mechanism had on these devices
    // was the reconnect re-arm inside finishAdapterRecovery(), so that is what
    // is kept, reached directly rather than through ten seconds of pretence.
    //
    // Not a claim that the wedge is imaginary — the detector's fingerprint
    // stands. This retires a remedy that cannot work, not the detection.
    // A failed SDK read (-1) counts as unavailable. That is the safe default in
    // both directions: we do not manipulate the user's radio on a device whose
    // API level we could not establish, and the re-arm below still runs, so the
    // only thing given up is a remedy we could not have known would work.
    const bool adapterRemedyAvailable = androidSdkInt() > 0 && androidSdkInt() < 33;

    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastAdapterRecovery.isValid()
        && m_lastAdapterRecovery.msecsTo(now) < kAdapterRecoveryBackoffMs) {
        BT_LOG_TAGGED("BLEManager", QStringLiteral("BLE stack still appears wedged (") + reason + QStringLiteral(
            ") but within recovery backoff — ") + (adapterRemedyAvailable
                ? QStringLiteral("not cycling adapter yet")
                : QStringLiteral("not re-arming reconnect yet")) + QStringLiteral(" (#1309)"));
        return;
    }

    if (!adapterRemedyAvailable) {
        m_lastAdapterRecovery = now;   // the backoff still paces the re-arm
        m_adapterRecoveryCount++;
        m_wedgeSince = QDateTime();
        m_lastDe1FaultTime = QDateTime();
        // Two different reasons reach this branch and they must not share a
        // sentence. On SDK >= 33 we know the remedy is a no-op. On an
        // unreadable SDK we know nothing — and printing the "-1" sentinel into
        // a claim about the user's Android version would state as fact
        // something we failed to establish, in a log read by users and by their
        // AI assistants. A device on API 28 whose JNI read failed would be told
        // its OS blocks adapter control when the remedy would have worked.
        const QString why = androidSdkInt() > 0
            ? QStringLiteral("this Android version (SDK %1) ignores an app's request "
                             "to turn the radio off and on, so the attempt would "
                             "change nothing and then report success")
                  .arg(androidSdkInt())
            : QStringLiteral("this device's Android API level could not be read, so "
                             "the power-cycle is skipped as a precaution rather than "
                             "manipulating the radio on an unknown platform");
        BT_WARN_TAGGED("BLEManager", QStringLiteral(
            "BLE stack appears wedged (") + reason + QStringLiteral(
            ") — NOT power-cycling the Bluetooth adapter: ") + why + QStringLiteral(
            ". Re-arming the DE1 and scale reconnect paths directly instead. If "
            "this keeps recurring, toggling Bluetooth off and on in system "
            "settings is the equivalent action, and only you can perform it "
            "(#1309)."));
        emit bleStackRecovered();      // the re-arm main.cpp listens for
        if (!m_savedScaleAddress.isEmpty())
            tryDirectConnectToScale();
        return;
    }

    m_adapterRecoveryInFlight = true;
    m_recoverySawPoweredOff = false;
    m_lastAdapterRecovery = now;
    m_adapterRecoveryCount++;
    m_wedgeSince = QDateTime();
    BT_WARN_TAGGED("BLEManager", QStringLiteral("BLE stack appears wedged (") + QStringLiteral(" ") + QString("%1").arg(reason) + QStringLiteral(" ") + QStringLiteral(") — power-cycling Bluetooth adapter, recovery #") + QStringLiteral(" ") + QString("%1").arg(m_adapterRecoveryCount) + QStringLiteral(" ") + QStringLiteral("this session (#1309)"));
    scaleWarn(QStringLiteral("BLE stack wedged — auto power-cycling Bluetooth adapter (#1309)"));
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
    // Announce the mode ONLY when it changes. This function runs on every
    // settings load, and at WARN it produced 11 identical alarms in a 48 h
    // capture for a condition that had not changed once. Still prominent —
    // enforcement being off is worth seeing — but stated once per transition.
    //
    // Guards the LOGGING only. Everything below (the latch, the build-change
    // safety valve) must run on every load regardless, so no early return here.
    const bool observeNow = observeMode();
    if (observeNow != m_loggedObserveMode) {
        m_loggedObserveMode = observeNow;
        if (observeNow) {
            // INFO, not WARN. This is a SETTING reporting itself, and the tier is
            // chosen by audience: WARN means something went wrong, and a mode the
            // user or a developer deliberately persisted did not.
            //
            // Found on a real tablet, where it was the ONLY WARN in an otherwise
            // clean 26-line startup — so the one line at the tier that means "look
            // here" was the line that had nothing to report. That is precisely how
            // a reader learns to skim WARN.
            //
            // It was also asymmetric with its own else-branch: ENFORCE has always
            // been INFO. Reporting one arm of a binary setting as a fault and the
            // other as narrative is the same defect as a WARN whose retraction is
            // DEBUG. Visibility is unchanged — the connections view shows INFO and
            // above — and the dedupe above still limits this to once per transition.
            SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("Backoff policy mode = OBSERVE (persisted) — "
                    "connection-priority detection runs but takes NO action and "
                    "the scale link is forced HIGH (any latch is overridden, not "
                    "erased). Set enforce via MCP to restore the dual-HIGH "
                    "backoff."));
        } else {
            SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("Backoff policy mode = ENFORCE"));
        }
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
        const int sdkInt = androidSdkInt();
        if (sdkInt <= 0) {
            SCALE_WARN_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("First-launch seed: failed to read "
                      "Android SDK_INT via JNI (sdkInt=%1) "
                      "— seed skipped, runtime detector will arm normally.")
                       .arg(sdkInt));
        } else if (sdkInt < kSeedSdkBelow) {
            const QString kind = QStringLiteral("seed:sdk<%1").arg(kSeedSdkBelow);
            latchScaleSkipHighPriority(kind);
            // INFO, not WARN: the seed firing is this feature working as designed
            // on the cohort it was written for. It changes BLE behaviour, which is
            // why it is logged at all, but a WARN here trained readers to skim the
            // tier — the same cry-wolf pattern the repeat-failure budget exists to
            // stop. A seed that FAILS (above) is the problem, and stays WARN.
            SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("First-launch seed: Android SDK %1 < "
                      "%2 (dual-HIGH-incapable cohort, ex-#1097) — skip-HIGH "
                      "latch SET without running the detection window. "
                      "Persisted under epoch %3; both BLE links start at "
                      "BALANCED. Seed re-applies on every launch where the "
                      "latch is absent (SDK_INT is permanent — no in-app "
                      "escape hatch on this SDK cohort).")
                       .arg(sdkInt).arg(kSeedSdkBelow).arg(kBleDetectionEpoch));
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
        // These two branches were both WARN, which flattened the one distinction
        // that matters here: whether anything is WRONG. A deliberate epoch bump
        // discarding the record is a release doing exactly what it was written to
        // do, on every affected install, once. A corrupt epoch is a damaged store.
        // The code already told them apart; the tiers now do too.
        if (storedEpoch >= 0) {
            SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("Persisted connection-priority "
                      "classification was set under detection epoch %1 but "
                      "this build is epoch %2 — discarding and re-detecting "
                      "from scratch (deliberate epoch reset)")
                       .arg(storedEpoch).arg(kBleDetectionEpoch));
        } else {
            // Negative but not the -1 "no key" sentinel ⇒ corrupt record.
            SCALE_WARN_STDERR_TAGGED("ConnectionPriority",
                QStringLiteral("Persisted connection-priority "
                      "detection epoch is corrupt/unrecognized (stored=%1, "
                      "expected %2 or absent) — discarding and re-detecting "
                      "from scratch rather than honoring a damaged record")
                       .arg(storedEpoch).arg(kBleDetectionEpoch));
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
        SCALE_WARN_STDERR_TAGGED("ConnectionPriority",
            QStringLiteral("Persisted connection-priority trigger "
                  "kind was missing/empty — salvaged to \"%1\"; classification "
                  "kept (it is the load-bearing fact)")
                   .arg(m_scaleSkipHigh.triggerKind));
    }
    // INFO: a successful rehydrate is the mechanism working. This fired at WARN on
    // EVERY launch of every device carrying the latch — permanently, since the
    // latch is meant to persist — so on exactly the hardware whose logs need
    // reading it was the most frequent warning in the file and said nothing had
    // gone wrong. The two genuine anomalies around it (a salvaged trigger kind
    // above, a substituted set-time below) stay WARN and are now the only ones,
    // which is what makes them findable.
    SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
        QStringLiteral("Loaded persisted dual-HIGH-incapable "
              "classification (epoch %1, build %2 [diagnostic], trigger=%3) — "
              "BOTH BLE links will start at BALANCED this run (no detection "
              "window)")
               .arg(legacy ? kBleDetectionEpoch : storedEpoch)
               .arg(storedBuild).arg(m_scaleSkipHigh.triggerKind));
    if (!timeOk) {
        SCALE_WARN_STDERR_TAGGED("ConnectionPriority",
            QStringLiteral("Persisted connection-priority set-time "
                  "was invalid/missing (stored=\"%1\") — substituted current "
                  "time; classification kept (it is the load-bearing fact)")
                   .arg(isoIn));
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
        // INFO: a one-time forward migration succeeding. The line it tells you to
        // look for next — "Failed to PERSIST" — is the WARN, and it is in
        // settings_hardware.cpp under the same [Scale][ConnectionPriority] tag, so
        // the two are now one grep apart rather than in unrelated prefixes.
        SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
            QStringLiteral("Legacy (pre-epoch) connection-priority "
                  "record honored; stamping detection epoch %1. NO re-detection "
                  "is incurred regardless (the in-memory latch is already live "
                  "— BALANCED this run). If a 'Failed to PERSIST' warning "
                  "follows, the stamp did not stick and this line will repeat "
                  "next launch (still no re-detection — cosmetic only)")
                   .arg(kBleDetectionEpoch));
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
        // INFO: the user asked for this. An operator action succeeding is not a
        // warning, and its consequences are already stated in the line.
        SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
            QStringLiteral("Scale connection-priority skip-HIGH latch CLEARED via "
               "MCP reset — next (re)connect will request HIGH on both links "
               "and re-enter detection from scratch"));
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
        // INFO: another operator action succeeding. (The persisted OBSERVE state
        // stays WARN where it is announced at load — being left in a mode that
        // takes no action is a standing condition worth flagging; choosing it once
        // is not.)
        SCALE_INFO_STDERR_TAGGED("ConnectionPriority",
            QStringLiteral("Backoff policy mode set to %1 — applies on the next "
               "scale (re)connect (eventually-consistent; the current connection "
               "is not torn down)").arg(backoffModeToString(mode).toUpper()));
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
        de1Debug(QStringLiteral("DE1 BLE operations") + QStringLiteral(" ") + QString("%1").arg((disabled ? "disabled (simulator mode)" : "enabled")));
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
    scaleDebug(QStringLiteral("real-scale connects") + QStringLiteral(" ") + QString("%1").arg((simulated ? "blocked (simulated scale active)" : "allowed")));
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
    // Composite on purpose: "Scan for Devices" searches BLE, WiFi AND USB, so
    // the indicator must answer "is the app still looking", not "is the BLE
    // agent still looking". Reporting only m_scanning made the button revert to
    // its idle text while WiFi results were still arriving.
    //
    // NOTE this is the PROPERTY only. m_scanning stays BLE-agent state and the
    // internal control flow (restart-if-already-scanning, stopScan guards)
    // continues to read it directly — those genuinely mean "the BLE agent is
    // busy" and must not pick up the WiFi/USB meaning.
    return m_scanning
        || (m_wifiDiscovery && (m_wifiDiscovery->isBrowsing() || m_wifiDiscovery->isProbing()))
        || m_usbProbeInFlight;
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

        scaleInfo(QString("Connecting to %1...").arg(entry.name));
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
            setPendingWifiConnect(address.mid(QStringLiteral("wifi:").size()),
                                  entry.resolvedIp, entry.wsPort, entry.wsPath);
            // resolvedIp lets main.cpp seed DecentScaleWifi's IP cache and skip
            // Qt's own hostname resolver; wsPort/wsPath are what the scale
            // advertised over DNS-SD (firmware defaults for a fallback hit).
            emit scaleDiscovered(QBluetoothDeviceInfo{}, entry.type);
        } else {
            emit scaleDiscovered(entry.device, entry.type);
        }
        return;
    }
    scaleWarn(QStringLiteral("Scale not found in discovered list: %1").arg(address));
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
        scaleInfo(QString("Found %1 (%2)").arg(entry.name, entry.address));
        emit scalesChanged();
    } else {
        if (existing < 0) return;  // Nothing to remove.
        m_scales.removeAt(existing);
        scaleInfo(QStringLiteral("USB scale unplugged"));
        emit scalesChanged();
    }
}

void BLEManager::probeMdnsForManualEntry() {
    // Dedicated mDNS probe for the "Add WiFi Scale" dialog. We keep this
    // separate from m_wifiDiscovery (used by the user-initiated scan / saved-
    // scale rehydration path), because m_wifiDiscovery's resultFound handler
    // auto-connects when the discovered hostname matches the saved primary —
    // we explicitly do NOT want that side effect here.
    // (This used to add "and no user-initiated scan is in progress". There is
    // no such condition: maybeAutoConnectBrowsedScale matches on the saved
    // address alone and fires during user scans too, which is deliberate.) The manual flow's contract is "tell the
    // user we found a scale and let them choose"; connect happens only when
    // they tap Use.
    if (!m_manualEntryDiscovery) {
        m_manualEntryDiscovery = new WifiScaleDiscovery(this);
        // No logMessage forwarder. WifiScaleDiscovery logs through the shared
        // helper, so its mDNS-side diagnostics — timeout vs no-responder vs lookup
        // failure, which is what a "I clicked Add WiFi Scale and nothing showed up"
        // report needs — already carry [Scale][WifiScaleDiscovery] and already
        // reached the system log at that class's chosen severity. This connection
        // only ever fed the private buffer, which is gone.
        connect(m_manualEntryDiscovery, &WifiScaleDiscovery::resultFound, this,
                [this](const WifiScaleResult& result) {
            // (Result line — the WifiScaleDiscovery logMessage above already
            // logged the "mDNS resolved …" detail; this is the higher-level
            // event for the user reading the log top-to-bottom.)
            scaleInfo(QString("Manual-entry mDNS found %1 at %2")
                               .arg(result.hostname, result.address));
            m_manualEntryFoundThisProbe = true;
            emit manualWifiMdnsDiscovered(result.hostname, result.address);
        });
        connect(m_manualEntryDiscovery, &WifiScaleDiscovery::probeFinished, this,
                [this](bool /*ran*/) {
            if (!m_manualEntryFoundThisProbe) {
                scaleWarn(QStringLiteral(
                    "Manual-entry mDNS: no HDS scale responded — user can still type an address"));
            }
            emit manualWifiMdnsProbeFinished();
        });
    }
    m_manualEntryFoundThisProbe = false;
    // Deliberately the single default name, not the full fallback list the scan
    // uses: this dialog shows ONE one-tap shortcut, so probing several names
    // would just make which one appears depend on resolution order. The dialog
    // is the manual type-an-address path; discovering everything is the scan's job.
    scaleInfo(QStringLiteral("Probing mDNS for hds.local (manual entry)..."));
    m_manualEntryDiscovery->probe(QStringLiteral("hds.local"));
}

void BLEManager::connectToWifiScale(const QString& hostnameOrIp, const QString& resolvedIp) {
    QString host = hostnameOrIp.trimmed();
    if (host.isEmpty()) return;

    // A bare name with no dot is an mDNS hostname missing its suffix — append
    // ".local" so it resolves (matches the discovery default "hds.local"). IPs
    // and already-dotted/qualified names pass through unchanged.
    if (!host.contains(QLatin1Char('.')))
        host += QStringLiteral(".local");

    scaleInfo(QString("Adding WiFi scale at %1...").arg(host));

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
    setPendingWifiConnect(host, resolvedIp);
    // Callers with a fresh mDNS resolution in hand (the "Add WiFi Scale"
    // dialog's suggested-scale "Use" button) pass it along so main.cpp can
    // seed the IP cache and skip re-resolving the hostname. A genuinely typed
    // address (the dialog's text-field submit) passes nothing — resolvedIp
    // defaults to empty and there's nothing to seed.
    emit scaleDiscovered(QBluetoothDeviceInfo{}, ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi));
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
            scaleWarn(QStringLiteral("USB scale switch ignored — scale not plugged in"));
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
        scaleWarn("Scale switch ignored — simulated scale is active");
        return;
    }
    // The simulator's synthetic entry appears in the Known Devices picker like
    // any other saved scale, so the user can select it — but it is not a
    // dialable address and tryDirectConnectToScale below refuses it. Without
    // this bail we would emit disconnectScaleRequested first and drop a working
    // real scale to connect nothing, which is exactly the failure the comment
    // above says these guards exist to prevent.
    if (savedScaleIsSimulated()) {
        scaleWarn("Scale switch ignored — that entry is the simulator's "
                       "synthetic scale, not a real device");
        return;
    }
    // BLE only. A WiFi saved scale reaches the LAN over the network and needs no
    // Bluetooth adapter — gating it here would refuse a WiFi scale with a
    // misleading "Bluetooth is powered off" dialog whenever the user had BT off.
    // (USB already returned above; anything still here is either wifi: or BLE.)
    if (!m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)
        && !isBluetoothAvailable()) {
        scaleWarn("Cannot switch scale — Bluetooth is powered off");
        emit errorOccurred(translateUiString("ble.error.bluetoothPoweredOff",
                                             "Bluetooth is powered off"));
        return;
    }

    // Fresh user-initiated attempt: clear stale failure state so the status line
    // doesn't flash "Not found" during the new connect (mirrors scanForDevices()).
    m_scaleConnectionFailed = false;
    m_flowScaleFallbackEmitted = false;
    resetRepeatFailureBudget();  // ...and let this attempt's failure be loud
    emit scaleConnectionFailedChanged();

    // Drop the currently-connected scale (if any) so the new primary can take over.
    // main.cpp's disconnectScaleRequested handler runs synchronously (same-thread
    // direct connection) and clears m_scaleDevice, so tryDirectConnectToScale()'s
    // "already connected" guard won't block the new dial.
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        scaleInfo(QString("Switching scale to %1")
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
        de1Debug(QStringLiteral("DE1 scan request ignored (simulator mode)"));
        return;
    }

    if (m_scanning) {
        return;
    }

    if (!isBluetoothAvailable()) {
        BT_INFO_TAGGED("BLEManager", QStringLiteral("Scan request ignored (Bluetooth is powered off)"));
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
    // No "Checking permissions..." line here, deliberately. These lines have
    // never appeared in a log before this change, so each one had to justify
    // itself rather than just being promoted: this one said nothing the next
    // line does not — a request, a denial, or the scan starting.

#ifdef Q_OS_ANDROID
    // First check/request location permission (required for BLE scanning on Android)
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Precise);

    if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Undetermined) {
        // INFO: a system dialog is about to appear and what happens next is the
        // user's answer, so this is the one line that explains a gap in the log.
        // Says WHY the permission is wanted — that is the part nobody knows, and
        // the part that generates the support question.
        de1Info(QStringLiteral("Requesting location permission — Android requires it to scan "
                               "for BLE devices"));
        qApp->requestPermission(locationPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                // DEBUG: granted is the non-event. "Scanning for devices..."
                // follows within milliseconds and says it louder.
                de1Debug(QStringLiteral("Location permission granted"));
                requestBluetoothPermission();  // Continue with Bluetooth permission
            } else {
                de1Warn(QStringLiteral("Location permission DENIED — BLE scanning cannot work "
                                       "until it is granted in Android Settings"));
                clearScanRequestFlags();  // No scan will start; don't latch the request flags
                emit errorOccurred(translateUiString("ble.error.locationPermissionDeniedForBluetooth",
                    "Location permission denied - required for Bluetooth scanning"));
            }
        });
        return;
    } else if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Denied) {
        de1Warn(QStringLiteral("Location permission DENIED — BLE scanning cannot work until it "
                               "is granted in Android Settings"));
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
        de1Info(QStringLiteral("Requesting Bluetooth permission — needed to reach the DE1"));
        qApp->requestPermission(bluetoothPermission, this, [this](const QPermission& permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                // This line is back at INFO, and the reason is a mistake worth
                // recording. It was deleted as a non-event on the grounds that
                // "Scanning for devices..." follows within milliseconds and
                // proves it — and then the very next commit demoted THAT line to
                // DEBUG for overclaiming the [DE1] marker on a shared scan. The
                // cited evidence disappeared, which left the INFO+ log stopping
                // dead right after "Requesting Bluetooth permission", making
                // three states indistinguishable: granted-and-scanning, dialog
                // still waiting on the user, and wedged in this callback. The
                // retained "Requesting" line only works as a signal if a GRANT
                // does not also end the log.
                de1Info(QStringLiteral("Bluetooth permission granted"));
                // isBluetoothAvailable() now switches from the Undetermined bypass
                // to the real hostMode() check. Notify QML bindings so any "Bluetooth
                // unavailable" UI re-evaluates immediately.
                emit bluetoothAvailableChanged();
                doStartScan();
            } else {
                de1Warn(QStringLiteral("Bluetooth permission DENIED — no device can be reached "
                                       "until it is granted in system Settings"));
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
        de1Warn(QStringLiteral("Bluetooth permission DENIED — no device can be reached until it "
                               "is granted in system Settings"));
        clearScanRequestFlags();  // No scan will start; don't latch the request flags
        emit errorOccurred(translateUiString("ble.error.bluetoothPermissionRequired",
            "Bluetooth permission required. Please enable in Settings."));
        return;
    case Qt::PermissionStatus::Granted:
        // Nothing to say here: this is the already-granted steady state, reached
        // on every scan after the first, and it has no prompt to explain. The
        // grant EDGE is what gets an INFO, in the callback above.
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
    // DEBUG, not INFO, and the live log is why. One BLE scan serves the DE1, the
    // scales and the refractometers, so bracketing it under [DE1] claims the app
    // went looking for the machine when usually it did not: a real session showed
    // "[DE1] Scanning for devices..." followed 207 ms later by "[DE1] Scan
    // stopped", which reads in a [DE1] filter as "looked for the machine, gave
    // up" — while the [Scale] lines in between show the scan was a WiFi-to-BLE
    // scale fallback that found its scale and stopped. The machine's story is
    // "Found DE1:" below, which is unambiguous; the scan's story belongs to
    // whoever asked for it.
    //
    // Collapsed: a reconnect ladder or an open review page repeats this line
    // every ~7.5 s for as long as the device stays missing. See m_scanCycleLog.
    logScanCycle(m_scanCycleLog, QLatin1String(kKeyDe1Scanning), de1ScanningText(),
                 ScanLogSink::De1);

    // Scan for BLE devices only
    ensureDiscoveryAgent();
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BLEManager::scanCycleDebug(ScanLogSink sink, const QString& message) {
    switch (sink) {
    case ScanLogSink::De1:           de1Debug(message); break;
    case ScanLogSink::Scale:         scaleDebug(message); break;
    case ScanLogSink::Refractometer: refractometerDebug(message); break;
    }
}

void BLEManager::logScanCycle(LogCollapse& collapse, const QString& key,
                              const QString& text, ScanLogSink sink) {
    LogCollapse::Collapsed collapsed;
    if (collapse.shouldLog(key, text, QDateTime::currentMSecsSinceEpoch(), &collapsed))
        scanCycleDebug(sink, text + LogCollapse::suffix(collapsed));
}

void BLEManager::flushScanCycle(LogCollapse& collapse, const QString& key,
                                const QString& text, ScanLogSink sink) {
    const LogCollapse::Collapsed collapsed =
        collapse.flush(key, QDateTime::currentMSecsSinceEpoch());
    // Nothing was suppressed → the run was a single line that already spoke, and
    // a bare repeat of it would be a second line saying less than the first.
    if (collapsed.suppressed > 0)
        scanCycleDebug(sink, text + LogCollapse::suffix(collapsed));
}

void BLEManager::stopScan() {
    // BLE ONLY — deliberately does NOT stop the WiFi browse.
    //
    // An earlier version called stopBrowse() here, which silently defeated the
    // feature. stopScan() is called whenever something merely *happens* during
    // a scan: the saved primary being rediscovered and auto-connected
    // (main.cpp), the DE1 being found, a USB DE1 transport arriving. The BLE
    // agent typically finds the saved scale 1-2 s into a 15 s browse, so the
    // browse was being killed almost immediately — and the scales that only a
    // browse can find (renamed ones, invisible to the hds/hds-2/hds-3 A-record
    // fallback) are exactly the ones that then never appeared. The whole point
    // of this feature, silently undone by an unrelated auto-connect.
    //
    // A browse ends on its own deadline, or when a new scan supersedes it
    // (browse() calls stopBrowse() first), or at teardown
    // (~WifiScaleDiscovery). Leaving `scanning` true until it does is correct:
    // WiFi discovery genuinely is still running.
    if (!m_scanning) return;

    de1Debug(QStringLiteral("Scan stopped (superseded, or torn down)"));

    // Run end for the scan-cycle collapse, and the reason it is HERE: this
    // function is called when the burst's reason for existing goes away — the
    // saved primary rediscovered and auto-connected, the DE1 found, a USB
    // transport arriving. Flushing after the "Scan stopped" line above puts the
    // tally where a reader is already looking to find out how long the app
    // spent trying.
    //
    // Each key flushes to its OWN subsystem rather than through flushAll(),
    // which would hand back a list this function then has to route by key
    // string. Three named flushes cost three lines and cannot mis-route; the
    // alternative stamps a [Scale] tally onto a [DE1] line the first time
    // someone adds a key, which is the overclaim this file's tier comments
    // exist to prevent.
    //
    // The hunt chain is deliberately NOT flushed here — its run is bounded by
    // the review page, not by any one scan stopping, and stopScan() fires
    // repeatedly inside a live hunt.
    flushScanCycle(m_scanCycleLog, QLatin1String(kKeyDe1Scanning), de1ScanningText(),
                   ScanLogSink::De1);
    flushScanCycle(m_scanCycleLog, QLatin1String(kKeyDe1ScanDone), de1ScanDoneText(),
                   ScanLogSink::De1);
    flushScanCycle(m_scanCycleLog, QLatin1String(kKeyScaleScanDone), scaleScanDoneText(),
                   ScanLogSink::Scale);

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
        de1Info(QStringLiteral("Found DE1: %1 (%2)")
                    .arg(device.name(), getDeviceIdentifier(device)));
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
        const bool savedMatch = !m_savedRefractometerAddress.isEmpty()
            && deviceIdentifiersMatch(device, m_savedRefractometerAddress);
        // One line for one event, carrying the DECISION as well as the find.
        // There were three here: a bare `qDebug() << "[BLE] Found
        // refractometer:"`, this INFO saying the same thing in different words,
        // and a "[R2-diag] R2 advert …" dump that held the only part a reader
        // actually needs — whether this device is about to be connected or was
        // merely listed. Discovery without that verdict does not explain why a
        // saved refractometer showed up in the log and then nothing happened.
        refractometerInfo(QStringLiteral("Found refractometer: %1 (%2) — %3")
            .arg(device.name(), getDeviceIdentifier(device),
                 savedMatch ? QStringLiteral("saved device, connecting")
                            : (m_userInitiatedScaleScan
                                   ? QStringLiteral("listed for the user's scan, no auto-connect")
                                   : QStringLiteral("not the saved device, no auto-connect"))));

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
        // The unmarked `qDebug() << "[BLE] Found scale:"` twin of this line is
        // gone: same event, different words, and only one of the two carried a
        // marker, so a [Scale] search returned half the pair and a reader had no
        // way to know the other half existed.
        scaleInfo(QString("Found %1: %2 (%3)").arg(scaleType).arg(device.name()).arg(getDeviceIdentifier(device)));

        // If we're doing a direct wake and this is our saved scale found via scan,
        // log it and clear the direct connect state. The scan-discovered device has
        // proper BLE metadata which may help with connection.
        if (m_directConnectInProgress && deviceIdentifiersMatch(device, m_directConnectAddress)) {
            scaleDebug("Direct wake: found saved scale in scan, using scanned device");
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
            && m_savedScaleType == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi)
            && scaleType == QStringLiteral("decent");

        // Auto-connect rules (BLE path — WiFi has its own handler in
        // the WifiScaleDiscovery::resultFound lambda further down):
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
                scaleInfo(QString("No saved scale — listing %1 for manual selection (no auto-connect)")
                               .arg(device.name()));
                return;
            }
            scaleDebug(QString("Ignoring non-primary scale: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));
            return;
        }

        if (isFallbackCandidate) {
            scaleInfo(QString("WiFi fallback: connecting to BLE Decent scale %1 (%2)")
                           .arg(device.name(), getDeviceIdentifier(device)));
        }

        // Auto-reconnect to the saved primary, or the WiFi-fallback Decent BLE
        // candidate. (User-scan discoveries returned above; manual selection
        // emits scaleDiscovered via connectToScale().)
        emit scaleDiscovered(device, scaleType);
    }
}

void BLEManager::onScanFinished() {
    // No line for the flag reset. It was "[R2-diag] scan cycle finished —
    // clearing scanning/scanningForScales/userInitiated flags", which announced
    // the same event as the two marked "Scan complete" lines below it in words
    // that named three private members — and it claimed the refractometer's
    // debug prefix for a scan cycle shared by the DE1, the scales and the
    // refractometer. Same overclaim the [DE1] scan lines had.
    // Captured before the flags are cleared: the tier of the completion line
    // depends on who asked for the scan, and by the time it is logged nothing
    // remembers.
    const bool wasUserInitiated = m_userInitiatedScaleScan;
    m_scanning = false;
    m_scanningForScales = false;
    m_userInitiatedScaleScan = false;

    // DEBUG on the DE1 side, as with "Scanning for devices..." above — the scan
    // is rarely the machine's story.
    // Collapsed for the same reason as its "Scanning for devices..." partner —
    // see m_scanCycleLog. The pair is what makes a burst 16 lines/min.
    logScanCycle(m_scanCycleLog, QLatin1String(kKeyDe1ScanDone), de1ScanDoneText(),
                 ScanLogSink::De1);

    // On the scale side, INFO only when the USER started this scan. Flat INFO was
    // wrong in both directions and the view showed it: `Scan complete` arrived
    // with no matching start, over and over, from nothing the user did.
    //
    // The completion has an INFO-worthy partner in exactly one case.
    // scanForDevices() is the only place "Starting device scan..." is logged, and
    // it is the only user-initiated entry point. Every OTHER scan — the reconnect
    // ladder, the refractometer hunt chaining cycles back-to-back for as long as
    // the review page is open, startup, resume, screensaver exit — reaches the
    // agent through startScan() directly and announces nothing, so its completion
    // was an unpaired line appearing from nowhere. Reading it as "the app just
    // finished looking for my scale" was reasonable and usually wrong.
    //
    // Gated on m_userInitiatedScaleScan rather than m_scanningForScales, which
    // looks like the right flag and is not: the refractometer hunt sets it too, so
    // it means "this cycle also processes scale/refractometer adverts", not "a
    // human asked". Gating on it would have kept the worst case — an endless
    // chain of INFO pairs while the review page sits open.
    if (wasUserInitiated) {
        // NOT collapsed. A user-initiated scan is a single cycle the user asked
        // for and is watching for, so it has no run to collapse and its line
        // must appear every time — the tier gate above is what already keeps
        // this rare.
        scaleInfo(QStringLiteral("Scan complete"));
    } else {
        logScanCycle(m_scanCycleLog, QLatin1String(kKeyScaleScanDone), scaleScanDoneText(),
                     ScanLogSink::Scale);
    }

    // State the DE1's OUTCOME, because nothing else did. With the scan-lifecycle
    // lines at DEBUG and "Found DE1:" only firing on success, a machine that was
    // expected and never appeared left ZERO [DE1] lines at INFO or above for the
    // whole session — so "the app can't find my machine" was indistinguishable in
    // the log from "the app never looked". The DE1 has no counterpart to the
    // scale's "connection timeout — not found" anywhere, at any tier.
    //
    // Gated on a machine actually being expected: with no saved DE1 and no
    // discovery, there is nothing to report. One line per cycle.
    //
    // NOT in simulator mode, and that gate is not a nicety — without it this line
    // was a pure false alarm, seen on screen in a running sim session: "Scan
    // finished with no DE1 found (saved machine E5:5A:… did not appear)" at WARN,
    // while the status read "Simulated". The machine is absent because the user
    // replaced it with a simulator. Scale scanning stays enabled in simulator mode
    // on purpose (scanForDevices' note: real scales tested against a simulated
    // DE1), so scans DO complete here with no DE1 — which is the configuration
    // working, not a fault. Warning about it three times per the repeat budget and
    // then falling to DEBUG is exactly the cry-wolf pattern this change exists to
    // end, aimed at the one user who cannot act on it.
    if (!m_disabled && !m_de1Connected && m_de1Devices.isEmpty()
        && !m_savedDE1Address.isEmpty()) {
        de1RepeatFailure(QStringLiteral("Scan finished with no DE1 found (saved machine %1 "
                                        "did not appear)").arg(m_savedDE1Address));
    }

    emit scanningChanged();

    // Hunt mode (post-shot review page): restart the scan back-to-back so a
    // refractometer powered on at an arbitrary moment is seen within one scan
    // cycle instead of waiting out the background reconnect tick's dead window.
    // Scan-finished is the continuation event — no polling timer. Deliberately
    // not restarted from onScanError: the background tick recovers from errors.
    if (m_refractometerHunt && !m_savedRefractometerAddress.isEmpty()
        && !isRefractometerConnected() && !m_disabled && isBluetoothAvailable()) {
        // DEBUG, not INFO: this fires once per scan cycle for as long as the
        // review page is open with the refractometer absent, so at INFO it would
        // be the dominant line in the view. The hunt turning on and off is the
        // user-facing fact, and setRefractometerHunt() states that at INFO.
        // ...and collapsed, because "for as long as the review page is open"
        // is the worst case in the file: the chain has no backoff, so this is
        // one line per cycle for however long the page stays open with the
        // refractometer absent. Its run is bounded by setRefractometerHunt(),
        // which flushes m_huntChainLog on both edges.
        logScanCycle(m_huntChainLog, QLatin1String(kKeyHuntChain), huntChainText(),
                     ScanLogSink::Refractometer);
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
                // Through scaleRepeatFailure, not a flat scaleDebug. The comment
                // above is right that this is USUALLY a transient post-resume
                // hiccup — but m_anyBleSuccessThisSession latches true for the
                // rest of the session, so if the user revokes Bluetooth/Location
                // permission mid-session, or CoreBluetooth wedges non-transiently,
                // every subsequent scan takes this branch forever at a tier the
                // view never shows. A "transient hiccup" that repeats indefinitely
                // is no longer transient, and this is exactly the shape
                // scaleRepeatFailure exists for: warn on the first few, then drop
                // to DEBUG once the ladder has proven it isn't going away, so a
                // permanent denial still surfaces instead of vanishing at DEBUG
                // for the rest of the session.
                scaleRepeatFailure(QStringLiteral(
                    "Bluetooth scan MissingPermissionsError (permission was OK "
                    "earlier this session — a repeat suggests it no longer is, "
                    "not just a post-resume hiccup); next scan tick retries"));
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
    // One wording, both subsystems: a scan error affects the machine and the
    // scale equally, and this used to say it three times in two phrasings.
    const QString scanErrorLine = QStringLiteral("Scan error: %1 (code %2)")
                                      .arg(errorMsg)
                                      .arg(static_cast<int>(error));
    de1Warn(scanErrorLine);
    scaleWarn(scanErrorLine);
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
        // No logMessage forwarder, for the same reason: every source behind that
        // signal has already written the line to the system log at the right
        // severity — the 13 drivers via SCALE_LOG/SCALE_WARN, and the transports
        // (whose logMessage each driver re-emits as its own) via their log()/warn()
        // helpers.
    }
}

void BLEManager::onScaleConnectedChanged() {
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Scale connected - stop timers, clear failure flag, clear direct connect state
        m_scaleConnectionTimer->stop();
        m_scaleDirectAbortTimer->stop();
        m_directConnectInProgress = false;
        m_directConnectAddress.clear();
        resetRepeatFailureBudget();          // Next failure of each kind warns again
        // Captured BEFORE the reset below: the reconnect-browse flag further down
        // needs to know whether this connect was the WiFi->BLE fallback.
        const bool wasWifiFallbackConnect = m_wifiFallbackToBleActive;
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
        // A connect clears the "direct attempt already failed" state, so the
        // reconnect browse goes quiet again until the direct path next fails.
        // This is the event that clears the flag — not a timer.
        //
        // EXCEPT a WiFi->BLE fallback connect, which is not evidence the WiFi
        // primary is back — it is evidence it is still gone. Clearing on that
        // would disarm the browse at the one moment it is needed. main.cpp draws
        // the same distinction (it declines to rewrite the saved primary for a
        // fallback connect). m_wifiFallbackToBleActive is reset earlier in this
        // handler, so read the captured copy, not the member.
        //
        // But "the fallback was active" is a WEAKER question than "the backup is
        // what connected", and reading the first as the second left the flag
        // stuck true for entire sessions. The browse's own SUCCESS path does it:
        // maybeAutoConnectBrowsedScale() dials the PRIMARY through
        // scaleDiscovered without clearing m_wifiFallbackToBleActive, because the
        // BLE fallback scan it is racing is still running. The primary connects,
        // this line reads wasWifiFallbackConnect == true, and the flag survives a
        // connect to the very scale it is about. Nothing clears it afterwards
        // while that scale stays connected — not this handler (no further
        // connect), not setSavedScaleAddress() (the address did not change). So
        // every later browse hit sees a "failed direct attempt" that succeeded.
        //
        // Ask what actually connected instead. Deliberately NOT fixed by clearing
        // m_wifiFallbackToBleActive at the browse's dial: that flag also gates
        // whether an in-flight fallback scan may adopt a BLE scale
        // (onDeviceDiscovered) and whether a second fallback is allowed
        // (onScaleConnectionTimeout), so clearing it there would silently change
        // the ladder. This changes only what the browse gate believes.
        if (connectClearsDirectAttemptFailed(wasWifiFallbackConnect,
                                             connectedScaleIsWifiPrimary()))
            m_wifiDirectAttemptFailed = false;
        // Deliberately NOT stopping an in-flight reconnect browse. The connect
        // that lands here is very often the WiFi->BLE FALLBACK, which begins in
        // the same handler that started the browse — so cancelling here would
        // routinely abort the search for the saved WiFi primary before it has
        // answered, and strand the user on the fallback transport.
        //
        // (An earlier version of this comment justified that with specific
        // durations for the connect and the browse. Both were unsourced for this
        // path — one was lifted from a user-scan measurement — so they are gone.
        // The argument does not need them: whichever finishes first, letting the
        // browse run costs one bounded 5 s window and can only help.)
        //
        // Letting it finish is safe because maybeAutoConnectBrowsedScale acts
        // only on an exact saved-primary match, and with a scale already
        // connected it never emits scaleDiscovered into main.cpp's single-scale
        // guard. Three outcomes there, not two: nothing at all when the primary
        // is what is connected, wifiPrimaryReachable -> switchToWifiPrimary when
        // a backup is, and the plain emit only when nothing is connected. The
        // browse ends on its own deadline in every case.
        scaleDebug(QStringLiteral("Scale connected"));
        emit scaleConnected();  // UI auto-dismisses the scale-disconnect / no-scale notice on reconnect
    } else {
        // Scale disconnected - notify UI immediately
        scaleInfo(QStringLiteral("Scale disconnected"));
        emit scaleDisconnected();
    }
}

void BLEManager::abortScaleDirectConnectIfPending(const QString& reason) {
    m_scaleDirectAbortTimer->stop();
    // Only a foreground direct-connect parks a controller; bail otherwise.
    if (!m_directConnectInProgress) return;
    if (m_scaleDevice && m_scaleDevice->isConnected()) return;  // connect raced in

    scaleWarn(QString("Direct connect not established (%1) — aborting, scan continues").arg(reason));
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
        //
        // isConnecting() is the third shape, and the one the previous two
        // conditions between them missed: a connect started from SCAN discovery
        // rather than the foreground path sets no m_directConnectInProgress, so
        // wasParked is false, and it never reached Connected, so isConnected()
        // is false — yet its controller is still held in Connecting and the
        // platform still considers the device busy, so every retry in that
        // window is rejected as a duplicate. Gating on how the attempt was
        // STARTED rather than on what is still held is what left it out.
        const bool holdingUnresolvedConnect = transport && transport->isConnecting();
        if (transport && (wasParked || transport->isConnected() || holdingUnresolvedConnect)) {
            scaleWarn(wasParked
                ? QStringLiteral("Scale connection timeout — tearing down parked direct-connect controller")
                : holdingUnresolvedConnect
                    ? QStringLiteral("Scale connection timeout — tearing down a connect still held in Connecting")
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

    scaleRepeatFailure(QStringLiteral("Scale connection timeout — not found"));

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
        scaleWarn(QString("Manual WiFi scale validation failed for %1").arg(manualHost));
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
    if (!manualWifiAttempt
            && m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        // The direct attempt for the saved WiFi scale has now definitively
        // failed. Browse HERE, at the failure, rather than arming a flag for the
        // next reconnect tick — measured: that tick may never come. Once the
        // WiFi->BLE fallback below connects a BLE scale, the app has a scale, the
        // app-level ladder stops calling tryDirectConnectToScale, and a browse
        // parked on that path never runs. A log of exactly that shows two
        // timeouts 20 s apart with no second "Direct wake (WiFi)" between them.
        //
        // Set before the browse so the gate reads true, and left set so a later
        // tick that DOES re-enter tryDirectConnectToScale still browses. It is
        // cleared when any scale connects.
        m_wifiDirectAttemptFailed = true;
        startReconnectBrowseIfNeeded();
        if (!m_wifiFallbackToBleActive) {
            beginWifiFallbackToBleScan();
            return;
        }
    }

    m_scaleConnectionFailed = true;
    emit scaleConnectionFailedChanged();

    if (!m_flowScaleFallbackEmitted) {
        m_flowScaleFallbackEmitted = true;
        scaleWarn(QStringLiteral("Scale not found — using FlowScale"));
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
        // One line, marked. The bare qWarning that used to sit here said the same
        // thing in different words AND carried no [Scale] marker, so it was
        // invisible to the marker-filtered query that is the whole point of the
        // marker — while being the only WARN rescuing the line below once its
        // budget was spent.
        scaleRepeatFailure(QString("WiFi scale %1 unreachable and Bluetooth unavailable "
                                   "— fallback to BLE skipped").arg(hostname));
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
    scaleRepeatFailure(QString("WiFi scale %1 unreachable — trying Bluetooth").arg(hostname));
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

    // This probe is about to become the evidence behind wifiPrimaryReachable, so
    // any address left over from a browse-driven request is now the wrong answer.
    // It CAN be left over: maybeAutoConnectBrowsedScale() sets m_browsedPrimaryIp
    // and emits, but main.cpp re-validates and may decline mid-shot, and a
    // declined request consumes nothing. (Its OTHER decline — already on the
    // primary — can no longer leave anything behind, because that case now
    // returns before the assignment. Mid-shot is the whole of it.) Without this the
    // stale browsed address outranks the IP this probe just verified —
    // switchToWifiPrimary() prefers m_browsedPrimaryIp — and the switch-back
    // dials an address known to be older than the one it proved good.
    m_browsedPrimaryIp.clear();

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
    scaleDebug(QString("Probing WiFi primary at %1 (%2 ms, HDS verify)")
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

bool BLEManager::connectedScaleIsWifiPrimary() const {
    if (!m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive))
        return false;
    if (!m_scaleDevice || !m_scaleDevice->isConnected()) return false;
    return m_scaleDevice->type()
           == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi);
}

void BLEManager::switchToWifiPrimary() {
    if (!m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        return;  // primary isn't a WiFi scale — nothing to switch back to
    }
    const QString hostname = m_savedScaleAddress.mid(QStringLiteral("wifi:").size());
    scaleInfo(QStringLiteral("WiFi primary %1 reachable again — switching back from backup").arg(hostname));

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
    // Two callers, two kinds of evidence that the primary is back:
    //  - probeWifiPrimaryReachable() validated the PERSISTED cache, so there is
    //    nothing fresh to hand along and connectToHost() dials that cache.
    //  - the reconnect browse resolved the host itself, and left the address in
    //    m_browsedPrimaryIp. That address must win: a stale cache is why the
    //    browse ran, so redialing the cache would fail identically.
    // Cleared below so a later switch-back can't reuse a stale answer — but that
    // is NOT sufficient on its own, because this function only runs when main.cpp
    // ACCEPTS the request, and a browse-driven one it declines sets the member and
    // never reaches here. probeWifiPrimaryReachable() therefore clears it too, at
    // the start of every probe; see the comment there for what a survivor costs.
    setPendingWifiConnect(hostname, m_browsedPrimaryIp);
    m_browsedPrimaryIp.clear();
    emit scaleDiscovered(QBluetoothDeviceInfo{}, ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi));
}

void BLEManager::setSavedScaleAddress(const QString& address, const QString& type, const QString& name) {
    m_savedScaleAddress = address;
    m_savedScaleType = type;
    m_savedScaleName = name;
    // The reconnect-browse state belongs to the scale that WAS saved. Carried
    // over, a failure recorded against the previous scale would make the next
    // scale's very first attempt browse in parallel with it — the thing the
    // gate exists to prevent. Reset here rather than at the call sites because
    // this is the one funnel every saved-address change goes through.
    m_wifiDirectAttemptFailed = false;
    m_browsedPrimaryIp.clear();
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
    // m_refractometerDevice is a QPointer on purpose — this line is the one a
    // raw pointer crashes on during teardown. See its declaration.
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
        refractometerInfo(QString("Connecting to refractometer: %1 (%2)").arg(info.name(), address));
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
    // Through the setter, not a bare assignment: the setter is what severs the
    // connectedChanged/destroyed handlers installed alongside the pointer. The
    // bare assignment this replaced left them live on a device that outlives
    // this call (main.cpp disconnects it, then destroys it), and was harmless
    // only because clearing the saved address above happens to make the stale
    // handler's re-kick no-op. It also emits refractometerConnectedChanged.
    //
    // This MUST stay ahead of disconnectRefractometerRequested: that emission
    // is what arms the reconnect timer, and the handler's unconditional
    // stop() — which runs first thing — is what tears it down again. Emitting
    // the request first would stop the timer before this armed it, leaving a
    // reconnect running for the device the user just forgot.
    setRefractometerDevice(nullptr);
    emit disconnectRefractometerRequested();
}

void BLEManager::setRefractometerDevice(RefractometerDevice* device) {
    // The instance addresses are the point, not decoration: the bug this line was
    // added for was CHURN — a second Refractometer created while the first was
    // still connected — and "old" and "new" being different non-null values is
    // the only way that reads in a log. Kept at DEBUG; a user never needs it.
    refractometerDebug(QStringLiteral("Holder: old=%1 new=%2")
        .arg(m_refractometerDevice ? QString::number(reinterpret_cast<quintptr>(m_refractometerDevice.data()), 16)
                                    : QStringLiteral("none"),
             device ? QString::number(reinterpret_cast<quintptr>(device), 16)
                     : QStringLiteral("none")));
    // Sever exactly the two handlers we installed — see the note on the
    // Connection members. Disconnecting an already-severed or default
    // Connection is a no-op, so this needs no null guard.
    QObject::disconnect(m_refractometerConnectedConn);
    QObject::disconnect(m_refractometerDestroyedConn);
    m_refractometerDevice = device;
    if (m_refractometerDevice) {
        m_refractometerConnectedConn =
        connect(m_refractometerDevice, &RefractometerDevice::connectedChanged,
                this, [this]() {
            emit refractometerConnectedChanged();
            // Keep the hunt persistent while the review page is open: onScanFinished
            // only re-chains when a scan is already in flight, and none is once we
            // were connected. So if the R2 drops mid-page, re-kick the scan chain.
            if (m_refractometerHunt && !isRefractometerConnected() && !m_scanningForScales) {
                // INFO: the device the user is trying to take a reading with just
                // went away. That is their story, not a developer's.
                refractometerInfo(QStringLiteral("Disconnected while the review page "
                                                 "is open — restarting the scan"));
                tryDirectConnectToRefractometer();
            }
        });
        // Own the "device went away" invariant rather than borrowing QML's. The
        // QPointer above self-nulls, but nothing tells our observers — today the
        // UI only stays correct because QQmlContext::setContextProperty quietly
        // connects to destroyed() too, and its dropDestroyedQObject pokes the
        // context notify, which re-runs the binding that reads us. That is
        // load-bearing, invisible, and rests on a registration Qt's own source
        // discourages; move `Refractometer` to a singleton some day and it
        // vanishes silently. Emitting here means our property is right on our
        // own terms. Every non-teardown path calls setRefractometerDevice(nullptr)
        // before the device dies, so in practice this only fires at app exit —
        // hence qDebug, not qWarning. A future path that forgets gets a log line
        // instead of a silently stale "connected".
        m_refractometerDestroyedConn =
        connect(m_refractometerDevice, &QObject::destroyed, this, [this]() {
            refractometerDebug(QStringLiteral(
                "Destroyed with the holder still set — reporting disconnected. "
                "Expected at app exit; anywhere else means a path skipped "
                "setRefractometerDevice(nullptr)."));
            emit refractometerConnectedChanged();
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
    // Every reason this can decline, decided before anything is mutated, then
    // ONE line naming the one that applied.
    //
    // There were four "no-op (…)" lines here, one per early return. They said the
    // same thing four ways, three of them by printing private members
    // (`savedAddrEmpty=false disabled=false`) and leaving the reader to work out
    // which boolean was the cause — and the fourth announced "no-op (scan flag
    // already set)" and then re-tested the same flag below, so the branch existed
    // twice. Splitting savedAddress from disabled also makes the answer specific:
    // "no refractometer is paired" and "BLE is off" are different problems that
    // used to arrive as one line.
    //
    // Deciding availability up here rather than mid-function is what keeps the
    // scan flag honest, the way tryDirectConnectToDE1 does it: with Bluetooth off
    // startScan() returns without starting anything, so a flag set before that
    // check would never be cleared — scan finished/error/stop are the only
    // clearers and none would fire — and every later attempt would decline with
    // "a scan is already in flight" even after Bluetooth came back. Here nothing
    // is set until all the checks have passed, so that trap is structural rather
    // than a rule to remember.
    QString skipReason;
    if (!m_refractometerHunt) {
        skipReason = QStringLiteral("the post-shot review page is closed");
    } else if (m_savedRefractometerAddress.isEmpty()) {
        skipReason = QStringLiteral("no refractometer is paired");
    } else if (m_disabled) {
        skipReason = QStringLiteral("BLE is off (simulator mode)");
    } else if (!isBluetoothAvailable()) {
        skipReason = QStringLiteral("Bluetooth is unavailable — the reconnect tick will retry");
    } else if (m_scanningForScales) {
        skipReason = QStringLiteral("a scan is already in flight");
    }
    if (!skipReason.isEmpty()) {
        refractometerDebug(QStringLiteral("Auto-reconnect skipped: %1").arg(skipReason));
        return;
    }

    // Piggyback on the scale scan infrastructure — set the flag so
    // onDeviceDiscovered processes refractometer advertisements
    refractometerDebug(QStringLiteral("Auto-reconnect scanning for %1")
                           .arg(m_savedRefractometerAddress));
    m_scanningForScales = true;
    startScan();
}

void BLEManager::setRefractometerHunt(bool active) {
    if (m_refractometerHunt == active) {
        return;
    }
    m_refractometerHunt = active;
    // Run end for the chaining collapse, before the transition line below so the
    // tally reads as the hunt's closing fact rather than as the start of
    // whatever comes next. Flushed on BOTH edges, not just OFF: hunt going ON
    // while a tally is pending would otherwise carry the previous review
    // session's count into this one — the misattribution logcollapse.h warns
    // about, and the ON edge is a run boundary just as much as the OFF edge is.
    flushScanCycle(m_huntChainLog, QLatin1String(kKeyHuntChain), huntChainText(),
                   ScanLogSink::Refractometer);
    // INFO, and the only INFO in this mechanism: it is the transition that
    // explains everything downstream. Hunt ON means the radio scans back-to-back
    // — visible to a user as battery drain and as scale/DE1 BLE contention — and
    // hunt OFF means a refractometer will not reconnect no matter how long they
    // wait. The per-cycle consequences of both stay at DEBUG.
    refractometerInfo(QStringLiteral("Hunt %1")
        .arg(active ? QStringLiteral("ON — scans will chain back-to-back while a saved refractometer is disconnected")
                    : QStringLiteral("OFF — refractometer reconnect stops until the review page reopens")));
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
        de1Debug(QStringLiteral("tryDirectConnectToDE1 - disabled (simulator mode)"));
        return;
    }

    if (m_savedDE1Address.isEmpty()) {
        de1Debug(QStringLiteral("tryDirectConnectToDE1 - no saved DE1 address"));
        return;
    }

    if (!isBluetoothAvailable()) {
        de1Debug(QStringLiteral("tryDirectConnectToDE1 - Bluetooth is powered off, skipping"));
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
        de1Info(QStringLiteral("Direct wake: scanning for %1 (identifier %2 is not a MAC)")
                    .arg(deviceName, m_savedDE1Address));
        if (!m_scanning) {
            startScan();
        }
        return;
    }
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    de1Info(QStringLiteral("Direct wake: connecting to %1 at %2").arg(deviceName, upperAddress));

    // No gate is set here. This emit is a REQUEST, and the handler in main.cpp
    // declines it whenever the DE1 is already connected or connecting — so a flag
    // set at this point is a claim about something that may not happen, which is
    // what the 15 s cap that used to sit here existed to undo. The gate is
    // driven by noteDe1Connecting() from the DE1's own state instead.

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
    // No pre-scan flag dump. It was "[R2-diag] scanForDevices (user-initiated)
    // scanning=… scanningForScales=…", which named two private members of the
    // SCALE scan under the refractometer's debug prefix, immediately above the
    // marked line that already announces the event.
    scaleInfo("Starting device scan...");
    m_scaleConnectionFailed = false;
    m_flowScaleFallbackEmitted = false;  // User-initiated scan resets the dialog guard
    resetRepeatFailureBudget();           // ...and the failure-warning budget
    emit scaleConnectionFailedChanged();

    // NOTE: Bluetooth availability gates ONLY the BLE leg, below. It used to
    // return outright here, which meant a machine with Bluetooth off — or no
    // adapter at all, common on desktop — got no WiFi browse, no A-record
    // fallback and no USB probe either. "Scan for Devices" means find my
    // devices; two of the three transports do not need a radio.

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
    if (isBluetoothAvailable()) {
        startScan();
    } else {
        // No radio: skip the BLE leg but still run WiFi and USB. Clear the
        // request flags startScan() would otherwise have consumed, so they
        // don't leak into the next scan.
        BT_INFO_TAGGED("BLEManager", QStringLiteral("Bluetooth unavailable — scanning WiFi and USB only"));
        scaleInfo(QStringLiteral("Bluetooth unavailable — scanning WiFi and USB only"));
        m_scanningForScales = false;
        m_userInitiatedScaleScan = false;
    }

    // Fire WiFi discovery in parallel with the BLE scan. On-demand only; no
    // idle probing per the project requirement.
    //
    // BOTH mechanisms run, every time — the browse is not a replacement and the
    // fallback is not a fallback-if-the-browse-fails. A LAN can hold a
    // v3.0.9+ scale that advertises DNS-SD and an older one that only answers
    // to its hostname, and running the fallback only when the browse came back
    // empty would hide the old scale whenever a new one is present.
    //
    // The A-record timeout is kHdsResolveTimeoutMs, shared with the reconnect
    // path; the responder latency that sets it is documented on the constant.
    // The browse runs longer, alongside the ~15 s BLE scan, because a DNS-SD
    // browse's first callback is a dump of the resolver's cache — stale
    // instances included — and the resolver's own pruning of those arrives
    // seconds later.
    ensureWifiDiscovery();
    m_wifiDiscovery->browse(15000);
    m_wifiDiscovery->probe(WifiScaleDiscovery::defaultFallbackHostnames(),
                           WifiScaleDiscovery::kHdsResolveTimeoutMs);

    // USB is otherwise a free-running background poll that the scan button never
    // touched, so a scale plugged in just before a scan appeared only when the
    // 2 s timer next came round. Ask for an immediate pass and count it as part
    // of the scan, so "Scan for Devices" really does mean all three transports.
    // Fresh start for this scan: drop both the results and the rows derived
    // from them, so a departed scale stops being listed. Within the cycle the
    // list is then add-only, so nothing vanishes while the user reads it.
    m_wifiResults.clear();
    clearWifiScaleRows();

#ifndef Q_OS_IOS
    m_usbProbeInFlight = true;
    emit usbProbeRequested();
#endif
    // iOS has no USB scale support at all, so main.cpp wires
    // usbProbeRequested/probeFinished only under the same guard. Setting the
    // flag there would latch it: nothing can ever call onUsbProbeFinished(),
    // so the composite `scanning` below stays true for the rest of the process
    // and the Scan button is dead after one tap.

    // The composite `scanning` property just gained more contributors.
    emit scanningChanged();
}

void BLEManager::browseWifiScales(int timeoutMs) {
    ensureWifiDiscovery();
    m_wifiResults.clear();
    clearWifiScaleRows();
    scaleDebug(QString("WiFi-only discovery requested (backend=%1, %2 ms)")
                       .arg(MdnsResolver::activeBrowseBackendName())
                       .arg(timeoutMs));
    m_wifiDiscovery->browse(timeoutMs);
    m_wifiDiscovery->probe(WifiScaleDiscovery::defaultFallbackHostnames(),
                           WifiScaleDiscovery::kHdsResolveTimeoutMs);
    emit scanningChanged();
}

QVariantList BLEManager::wifiScaleResults() const {
    QVariantList out;
    for (const WifiScaleResult& r : m_wifiResults) {
        QVariantMap m;
        m["instanceName"] = r.instanceName;
        m["mdnsName"] = r.mdnsName;
        m["hostname"] = r.hostname;
        m["address"] = r.address;
        m["port"] = r.port;
        m["path"] = r.path;
        m["firmwareVersion"] = r.firmwareVersion;
        m["foundBy"] = r.foundBy == WifiScaleResult::Source::Browse
            ? QStringLiteral("browse") : QStringLiteral("fallback");
        out.append(m);
    }
    return out;
}

void BLEManager::rebuildWifiScaleRows() {
    // Project m_wifiResults onto the WiFi rows of m_scales.
    //
    // Rebuild rather than patch: a label that was unambiguous when its row was
    // created can be made ambiguous by a later arrival (the SECOND unrenamed
    // scale is what creates the collision), and a patch pass has to find every
    // affected row to stay correct. Deriving cannot miss one.
    //
    // Add-only within a scan still holds: m_wifiResults only grows until the
    // next scan clears it, so no row a user is looking at disappears.
    const QHash<QString, QString> labels = WifiScaleResultUtil::labelsByHostname(m_wifiResults);

    bool changed = false;
    for (const WifiScaleResult& r : m_wifiResults) {
        const QString key = WifiScaleResultUtil::normalizeHostname(r.hostname);
        const QString address = QStringLiteral("wifi:") + key;
        const QString name = QString("%1 (WiFi)").arg(labels.value(key, key));

        // Match on the saved-address handle, which is hostname-derived and so
        // survives a DHCP lease change. Matching on resolvedIp would strand the
        // old row and add a second one for the same scale.
        qsizetype existing = -1;
        for (qsizetype i = 0; i < m_scales.size(); ++i) {
            if (m_scales[i].transport == QStringLiteral("wifi")
                    && m_scales[i].address == address) {
                existing = i;
                break;
            }
        }

        if (existing < 0) {
            ScaleEntry entry;
            entry.type = ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi);
            entry.transport = QStringLiteral("wifi");
            entry.name = name;
            entry.address = address;
            entry.resolvedIp = r.address;
            entry.wsPort = r.port;
            entry.wsPath = r.path;
            m_scales.append(entry);
            scaleInfo(QString("Found %1 (%2)").arg(entry.name, entry.address));
            changed = true;
        } else {
            // Refresh in place. resolvedIp deliberately tracks the latest
            // address — DHCP moves it and connectToScale() seeds the connect
            // from this field — while `address` (the identity) stays put.
            ScaleEntry& e = m_scales[existing];
            if (e.name != name || e.resolvedIp != r.address
                    || e.wsPort != r.port || e.wsPath != r.path) {
                e.name = name;
                e.resolvedIp = r.address;
                e.wsPort = r.port;
                e.wsPath = r.path;
                changed = true;
            }
        }
    }

    if (changed)
        emit scalesChanged();
}

void BLEManager::clearWifiScaleRows() {
    // Drop WiFi rows at the START of a user-initiated scan, so a scale that has
    // gone away stops being listed. This is the same semantics doStartScan()
    // already applies to BLE rows, and it is what makes "add-only within a scan"
    // safe: nothing vanishes while the user is reading the list, but a departed
    // scale does not linger forever either.
    //
    // Deliberately NOT done in doStartScan(): the periodic refractometer
    // auto-reconnect calls that every ~30 s, and wiping WiFi rows there would
    // erase a freshly-discovered scale before the user could tap it.
    bool removed = false;
    for (qsizetype i = m_scales.size() - 1; i >= 0; --i) {
        if (m_scales[i].transport == QStringLiteral("wifi")) {
            m_scales.removeAt(i);
            removed = true;
        }
    }
    if (removed)
        emit scalesChanged();
}

QString BLEManager::pendingWifiDisplayName() const {
    // ALWAYS derived from the hostname, never from the DNS-SD instance name.
    //
    // Known Devices persists this label, and every entry must be
    // self-distinguishing there: two unrenamed scales both advertise the
    // instance name "Half Decent Scale", so using it produced two identical
    // rows with no way to tell which was which. The hostname is the identity
    // this app already keys on ("wifi:<hostname>"), it is unique by
    // construction, and it is available even on a saved-scale reconnect where
    // no scan has run — which the previous discovery-row lookup was not, so it
    // fell back to a generic label and OVERWROTE an already-correct stored name.
    return WifiScaleResultUtil::wifiScaleDisplayName(m_pendingWifiHostname);
}

void BLEManager::setPendingWifiConnect(const QString& hostname, const QString& resolvedIp,
                                       quint16 port, const QString& path) {
    // All four move together. They were previously assigned at five separate
    // sites and only one set the endpoint, so four paths silently dialled the
    // previous connect's port and path.
    m_pendingWifiHostname = hostname;
    m_pendingWifiResolvedIp = resolvedIp;
    m_pendingWifiPort = port;
    m_pendingWifiPath = path;
}

void BLEManager::onUsbProbeFinished() {
    if (!m_usbProbeInFlight) return;
    m_usbProbeInFlight = false;
    emit scanningChanged();
}

void BLEManager::ensureWifiDiscovery() {
    if (m_wifiDiscovery) return;
    m_wifiDiscovery = new WifiScaleDiscovery(this);
    // Each transport finishing changes the composite `scanning` property, so
    // the "Scanning..." indicator clears only once ALL of them are done rather
    // than when the BLE agent alone finishes.
    // `ran` is not decoration: false means the transport could not run at all
    // (no backend, socket refused, Local Network denied) as opposed to running
    // and finding nothing. Both leave the list empty, so keep the distinction
    // where a diagnostic can read it — otherwise devices_wifi action=results reports
    // "0 results" for a permission denial exactly as it does for a quiet LAN.
    connect(m_wifiDiscovery, &WifiScaleDiscovery::probeFinished, this,
            [this](bool ran) { m_lastWifiProbeRan = ran; emit scanningChanged(); });
    connect(m_wifiDiscovery, &WifiScaleDiscovery::browseFinished, this,
            [this](bool ran) { m_lastWifiBrowseRan = ran; emit scanningChanged(); });
    // No logMessage forwarder — see the manual-entry probe above. The mDNS-layer
    // diagnostics reach the system log from WifiScaleDiscovery itself, which is
    // also the log a user uploads, so there is nothing left to forward them to.
    // Single unified handler that handles both code paths (user-initiated
    // scan AND saved-scale direct-wake). Before this consolidation, each
    // call site lazy-created the discovery object with a DIFFERENT lambda
    // and the second registration was silently dropped by the
    // `if (!m_wifiDiscovery)` guard — breaking whichever path ran second.
    connect(m_wifiDiscovery, &WifiScaleDiscovery::resultFound, this,
        [this](const WifiScaleResult& result) {
            const QString hostname = result.hostname;
            const QString resolvedAddress = result.address;
            const QString address = QStringLiteral("wifi:") + hostname;
            // INFO and marked, matching the BLE path's "Found <type>: …" — a
            // discovery outcome is what a user reads the log for, and the WiFi leg
            // previously had no counterpart at any tier, only this unmarked dump.
            // Fields kept in full rather than trimmed to a headline: on this
            // transport the mismatch between hostname, resolved IP and saved
            // address IS the usual bug, and a browse fires per user action or
            // reconnect tick, not per advertisement, so it is not a firehose.
            scaleInfo(QStringLiteral("Found WiFi scale: %1 -> %2 (address=%3, "
                                     "instance=%4, fw=%5, userInitiatedScan=%6, "
                                     "saved=%7)")
                          .arg(hostname, resolvedAddress, address,
                               result.instanceName, result.firmwareVersion,
                               m_userInitiatedScaleScan ? QStringLiteral("true")
                                                        : QStringLiteral("false"),
                               m_savedScaleAddress));

            // One authoritative collection. The rows below are DERIVED from it,
            // never maintained alongside it — an earlier version kept both and
            // the two dedupes drifted apart, which is what let a browse hit
            // rewrite a row's address out from under the saved-scale matcher.
            if (WifiScaleResultUtil::upsertByHostname(m_wifiResults, result))
                rebuildWifiScaleRows();

            maybeAutoConnectBrowsedScale(result);
        });
}

void BLEManager::maybeAutoConnectBrowsedScale(const WifiScaleResult& result) {
    // Auto-connect when the discovered scale matches the saved primary,
    // including during user-initiated scans. Matching the saved primary
    // is itself the anti-hijack guard — see the matching reasoning on
    // the BLE path in onDeviceDiscovered. Covers three paths now:
    // saved-scale direct-wake on app start, a spontaneous (or user-initiated)
    // scan that happens to find the saved scale, and the reconnect browse.
    //
    // Deliberately ONE implementation shared by every discovery handler. A
    // second copy of this match is how the saved-scale matcher and the row
    // dedupe drifted apart before.
    if (!browsedScaleIsSavedPrimary(result.hostname, m_savedScaleAddress)) return;

    // Already on the primary: nothing to do, and nothing to say. Every caller
    // reaches here — the user scan's unified handler, browse and A-record probe
    // alike, as well as the reconnect browse's — so re-finding a healthy
    // primary is routine, not exceptional. Both remaining branches below are
    // wrong for it: the switch-back would ask to leave the scale it is trying
    // to reach, and the plain emit is dropped by main.cpp's single-scale
    // invariant anyway.
    //
    // The visible cost of not checking was the log, which is how this subsystem
    // is diagnosed. A user pressing Scan with the primary connected got, twice,
    // "found the saved primary … while a backup scale is connected — requesting
    // switch-back", naming a backup that did not exist and a switch that never
    // happened (main.cpp declines it at onWifiBackupAndIdle()'s decent-wifi
    // test). A reader reasonably takes that as the app dropping their scale.
    if (connectedScaleIsWifiPrimary()) return;

    // A scale is connected and it is not a WiFi one — so, normally, the
    // WiFi->BLE fallback started by the same timeout that started this browse.
    // The early return above rules out a connected WiFi scale, not every other
    // way a BLE scale could be connected, so this stays a "normally", not an
    // assertion. (It used to carry a duration for how soon the fallback lands.
    // Unsourced for this path — the same defect already deleted from the sibling
    // comment in onScaleConnectedChanged — and the argument never needed it.)
    //
    // The plain scaleDiscovered emit below would be SILENTLY DROPPED here:
    // main.cpp's handler early-returns on its single-scale invariant (at most one physical
    // scale at a time, capping forced-HIGH BLE links at two). That invariant is
    // deliberate and must not be bypassed.
    //
    // Route through the app's existing switch-back instead. wifiPrimaryReachable
    // is re-validated in main.cpp against the machine phase — so a shot in
    // progress is never interrupted — and lands in switchToWifiPrimary(), which
    // drops the backup scale BEFORE emitting scaleDiscovered. That ordering is
    // what gets past the invariant, and it is why this cannot just emit harder.
    //
    // Without this branch the feature does nothing whenever a BLE fallback is
    // available: the browse resolves, the emit is dropped, and the reconnect
    // ladder has already stopped.
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Hand the freshly browsed address to the switch-back. It cannot use the
        // persisted cache here: a stale cached IP is the whole reason this browse
        // ran, so dialing it again would fail the same way.
        m_browsedPrimaryIp = result.address;
        scaleInfo(QStringLiteral("Reconnect browse found the saved primary %1 at %2 while a "
                                 "backup scale is connected — requesting switch-back")
                      .arg(result.hostname, result.address));
        emit wifiPrimaryReachable(true);
        return;
    }
    setPendingWifiConnect(result.hostname, result.address, result.port, result.path);
    // This mDNS resolve just happened — hand the IP along so
    // main.cpp can seed DecentScaleWifi's cache and dial it
    // directly instead of re-resolving the hostname itself.
    emit scaleDiscovered(QBluetoothDeviceInfo{}, ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi));
}

void BLEManager::ensureReconnectDiscovery() {
    if (m_reconnectDiscovery) return;
    m_reconnectDiscovery = new WifiScaleDiscovery(this);
    // resultFound only. Deliberately NOT wired to browseFinished/probeFinished:
    // those drive m_lastWifiBrowseRan/m_lastWifiProbeRan and emit
    // scanningChanged(), which is user-scan state. A background browse must not
    // rewrite what devices_wifi action=results reports about the user's last scan, and
    // must not touch the Scan button.
    connect(m_reconnectDiscovery, &WifiScaleDiscovery::resultFound, this,
        [this](const WifiScaleResult& result) {
            scaleInfo(QStringLiteral("Reconnect browse found WiFi scale: %1 -> %2 (saved=%3)")
                          .arg(result.hostname, result.address, m_savedScaleAddress));
            // NOT upserted into m_wifiResults and NOT rebuilding rows: those are
            // the user's scan output. A background browse that rewrote them
            // would make rows change while the user is reading the list.
            maybeAutoConnectBrowsedScale(result);
        });
}

void BLEManager::startReconnectBrowseIfNeeded() {
    // Gating lives in the pure predicate so it is assertable without a
    // BLEManager; see its comment for what each gate protects.
    const bool browsing = m_reconnectDiscovery && m_reconnectDiscovery->isBrowsing();
    if (!shouldBrowseOnReconnect(m_savedScaleAddress, m_wifiDirectAttemptFailed, browsing))
        return;

    ensureReconnectDiscovery();
    // Browse only — no A-record probe alongside. The fallback names exist for
    // pre-v3.0.9 firmware that answers A-queries, and answering A-queries is
    // precisely the thing that already works; the driver's own resolve covers
    // it. What is missing is the browse, so that is what this adds.
    scaleInfo(QStringLiteral("Reconnect: browsing for %1 (a direct attempt already failed)")
                  .arg(m_savedScaleAddress));
    m_reconnectDiscovery->browse(kReconnectBrowseTimeoutMs);
}

void BLEManager::requestScaleReconnectRampRestart() {
    emit scaleReconnectRampRestartRequested();
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
        scaleInfo("Auto-reconnect skipped — simulated scale is active");
        return;
    }

    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) {
        scaleDebug(QStringLiteral("tryDirectConnectToScale - no saved scale address/type"));
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
        scaleInfo("Auto-reconnect skipped — saved scale is the simulator's "
                       "synthetic entry, nothing to dial");
        return;
    }

    // The Bluetooth-availability gate is NOT here — it moved below the WiFi and
    // USB branches so it only gates the BLE path. A WiFi scale reaches the
    // network over the LAN and shares no radio with Bluetooth; gating its
    // reconnect on the BT adapter meant a WiFi scale could not reconnect while
    // the user had Bluetooth turned off, which is the exact recovery this whole
    // change is about. USB is likewise radio-independent.

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        scaleDebug(QStringLiteral("tryDirectConnectToScale - scale already connected"));
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
        // One line. This was a drift pair that survived the earlier sweep and was
        // caught by reading a real log rather than the source: an unmarked
        // `qDebug() << "BLEManager: Direct wake (WiFi) - connecting to" ...` and a
        // marked scaleInfo saying the same thing in different words, 1 ms apart. A
        // [Scale] search returned one of them, so the marker did not in fact return
        // the whole story, and an unfiltered reader saw the event twice.
        scaleInfo(QStringLiteral("Direct wake (WiFi): connecting to %1 "
                                 "(cached IP first, mDNS fallback)").arg(hostname));

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
        // Alongside the direct attempt, once a previous one has failed.
        //
        // The direct A-query the driver falls back to can enter a state where it
        // returns NOTHING for hours while the scale is plainly reachable: 82
        // consecutive reconnects received ZERO mDNS records over 7.5 h with the
        // scale awake and serving WebSocket traffic, while a browse resolved the
        // same host in 362 ms. Raising that deadline (#1737) did not change it.
        //
        // That state is host-side, NOT a property of the responder — a later run
        // on the same tablet resolved the same host by A-query in 357 ms, the
        // difference being a reboot in between. Do not "simplify" this away on
        // the grounds that the A-query works; it works until it doesn't, the app
        // cannot tell which, and the browse covers both.
        //
        // Parallel rather than sequential: the direct leg has already failed by
        // the time this runs, so waiting on it again would only add its timeout.
        startReconnectBrowseIfNeeded();
        setPendingWifiConnect(hostname);
        // No fresh resolution here — connectToHost() itself reads the
        // persisted cache (see comment above), so there's nothing new to hand along.
        m_pendingWifiResolvedIp.clear();
        emit scaleDiscovered(QBluetoothDeviceInfo{}, ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi));
        return;
    }

    // USB saved-scale path: NOT a BLE address — the USB scale is owned by
    // UsbScaleManager and reconnects via main.cpp's usbScaleAvailable handler
    // (which connects when the saved primary is "usb:decent"). Don't fall
    // through to the BLE connect/scan below — "usb:decent" is not a MAC.
    if (m_savedScaleAddress.startsWith(QStringLiteral("usb:"), Qt::CaseInsensitive)) {
        scaleDebug(QStringLiteral("Direct connect skipped — saved scale is USB; "
                                  "reconnect is handled by UsbScaleManager"));
        return;
    }

    // --- BLE only past this point. Now the Bluetooth-availability gate applies:
    // everything below (the passive scan and the direct connectToDevice) needs a
    // powered BLE adapter, whereas the WiFi and USB paths above did not.
    if (!isBluetoothAvailable()) {
        scaleDebug(QStringLiteral("tryDirectConnectToScale - Bluetooth is powered off, skipping"));
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
        scaleInfo("Auto-reconnect: scanning for saved scale (no direct-connect)");
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
        scaleInfo(QStringLiteral("Direct wake: scanning for %1, id %2 (identifier is not a MAC)")
                      .arg(deviceName, m_savedScaleAddress));

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

    // Backpressure gate, de1app's rule (bluetooth.tcl:2276): a BLE connect is
    // not a queued operation, so it competes with queued GATT traffic for the
    // radio rather than waiting its turn. Defer it while the shared queue has
    // anything going on, and release when the queue says it is clear.
    //
    // Gating on the contention rather than on a device means no DE1 present is
    // no wait at all: the queue is empty, and the scale connects now.
    if (auto& q = BleGattQueue::instance(); q.isBusy() || q.pendingCount() > 0) {
        if (!m_scaleConnectDeferred) {
            m_scaleConnectDeferred = true;
            m_scaleConnectDeferSince.start();
        }
        scaleInfo(QStringLiteral("Scale connect deferred: the BLE radio is busy — "
                                 "%1 GATT operation(s) queued, '%2' in flight%3")
                      .arg(q.pendingCount())
                      .arg(q.inFlightLabel().isEmpty() ? QStringLiteral("none")
                                                       : q.inFlightLabel(),
                           m_de1Connecting ? QStringLiteral(", DE1 connecting") : QString()));
        return;
    }

    // On Android/desktop, we have a MAC address - try direct connect
    QString upperAddress = m_savedScaleAddress.toUpper();
    QBluetoothAddress address(upperAddress);
    QBluetoothDeviceInfo deviceInfo(address, deviceName, QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    // The radio context at the instant a scale connect is issued. This is the
    // line that makes the inherited "two concurrent connects collide" claim
    // testable from a submitted log: correlate scale connect outcomes against
    // the DE1 phase recorded here. No capture so far carries both.
    scaleInfo(QString("Direct wake: connecting to %1 at %2 [radio: %3 queued, DE1 %4]")
                  .arg(deviceName, upperAddress)
                  .arg(BleGattQueue::instance().pendingCount())
                  .arg(m_de1Connecting ? QStringLiteral("connecting")
                                       : (m_de1Connected ? QStringLiteral("connected")
                                                         : QStringLiteral("down"))));

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

void BLEManager::noteDe1Connecting(bool connecting) {
    if (m_de1Connecting == connecting) return;
    m_de1Connecting = connecting;
    // Recorded only. Nothing is gated on it — see the declaration for why, and
    // for what the log lines it feeds are meant to settle.
    de1Debug(QString("DE1 link is now %1").arg(connecting ? "connecting" : "not connecting"));
}

void BLEManager::onGattQueueDrained() {
    if (!m_scaleConnectDeferred) return;
    m_scaleConnectDeferred = false;
    const qint64 waitedMs = m_scaleConnectDeferSince.isValid()
                                ? m_scaleConnectDeferSince.elapsed() : -1;
    scaleInfo(QString("BLE radio clear after %1 ms — starting deferred scale connect")
                  .arg(waitedMs));
    tryDirectConnectToScale();
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
    // log-marker-exempt: platform-availability note about a UI action, not a
    // subsystem event — no device or radio state is being reported.
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
    // log-marker-exempt: as above — a note that a settings deep-link has no
    // implementation here, not a report about the radio.
    qDebug() << "openBluetoothSettings is not implemented for this platform";
#endif
}

// BLEManager's own narrative. One call per event, writing the system log at the
// right severity with the subsystem marker — and nothing else.
//
// Each of these used to ALSO record into m_scaleLogMessages, a private in-memory
// buffer that fed the connections-page view and a shareable scale_debug_log.txt.
// That whole channel is gone: the views now read the system log through
// WebDebugLogger, and Share sends the system log. So the second write disappeared
// from these five functions rather than from ~60 call sites, which is exactly why
// the call sites were routed through helpers first.
void BLEManager::scaleDebug(const QString& message, const QString& source) {
    SCALE_LOG_STDERR_DYN(source, message);
}

void BLEManager::scaleInfo(const QString& message, const QString& source) {
    SCALE_INFO_STDERR_DYN(source, message);
}

void BLEManager::scaleWarn(const QString& message, const QString& source) {
    SCALE_WARN_STDERR_DYN(source, message);
}

// This class's own convenience form. Private, so "BLEManager wrote it" is a
// fact here rather than a default that any caller could inherit by omission.
void BLEManager::scaleRepeatFailure(const QString& message) {
    scaleRepeatFailure(message, RepeatTier::Warn, QStringLiteral("BLEManager"));
}

void BLEManager::scaleRepeatFailure(const QString& message, RepeatTier tier,
                                    const QString& source) {
    // Counted PER MESSAGE, not per subsystem. A single subsystem counter looked
    // simpler and was wrong twice over:
    //
    //   - It suppressed NOVELTY. A genuinely different failure arriving mid-run
    //     — "WiFi scale X unreachable and Bluetooth unavailable", i.e. the user
    //     just turned the radio off, which is actionable — was logged at DEBUG
    //     because an unrelated timeout had already spent the budget.
    //   - Its numbers skipped, because one attempt reports several messages, so
    //     "(repeat 4)" appeared on a line that had been printed twice.
    //
    // Per-message fixes both: each distinct failure gets its own first few
    // warnings, and the count means what it says.
    //
    // Cardinality WAS bounded — "literals with at most a host name interpolated"
    // — and that is no longer true, so do not rely on it. DecentScaleWifi's
    // failure line now interpolates the error string, an error code, the target,
    // the phrase describing where the target came from, and the local address.
    // The target-source phrase varies WITHIN one dead ladder (remembered, then
    // freshly resolved, then remembered-after-a-failed-resolve), so a single
    // repeating failure claims a separate budget per phrasing and can emit
    // roughly three times the intended warnings before going quiet.
    //
    // Left as-is deliberately: the provenance on that line is what makes
    // "the scale moved" separable from "the scale is off", which was the point of
    // adding it, and three cycles of a real failure is a tolerable price. Keying
    // the budget on a stable substring while logging the full line is the fix if
    // it ever becomes a problem. Recorded rather than silently accepted, because
    // the old sentence would have been read as a guarantee.
    const int count = ++m_repeatFailureCounts[message];
    if (count <= kScaleFailuresAtWarn) {
        // At the caller's own tier, not always WARN. A failing cycle emits
        // narrative as well as problems, and promoting the narrative to WARN to
        // budget it would trade one kind of noise for a worse one.
        if (tier == RepeatTier::Warn)
            scaleWarn(message, source);
        else
            scaleInfo(message, source);
    } else {
        // Same event, still true, nothing new — DEBUG, so the log still proves the
        // ladder is running without another alarm.
        //
        // But NOT DEBUG forever, and this is a correction to the first cut of this
        // budget. Once DecentScaleWifi's warning was routed in here too, a
        // permanently-absent scale produced nothing whatsoever above DEBUG — so at
        // the tier debug_get_log's INFO view and the connections page both read,
        // "retrying every 60 s for the last eight hours" and "gave up hours ago"
        // became byte-identical: empty. That is the same fault this subsystem was
        // just fixed for in the other direction. Silence is not honest while the
        // condition persists; it only looks tidy.
        //
        // Milestones, not a period: the gaps widen, so an overnight failure costs a
        // handful of INFO lines rather than one every 60 s, and the reader still
        // gets proof of life with a repeat count that says how long it has been.
        const bool milestone = (count == 10 || count == 30 || count == 100
                                || (count % 500) == 0);
        const QString line = QString("%1 (repeat %2)").arg(message).arg(count);
        if (milestone)
            scaleInfo(line, source);
        else
            scaleDebug(line, source);
    }
}

// Clears the per-message warn budgets so the next failure of each kind is loud
// again. Called on a successful connect AND on any fresh user-initiated attempt:
// a user who plugs the scale in and taps Connect after an hour of a dead ladder
// is asking a new question, and the answer must not arrive at DEBUG because the
// ladder had already used up the budget. Missing the user-initiated half was the
// actual defect — the two sibling latches (m_scaleConnectionFailed,
// m_flowScaleFallbackEmitted) were reset on those paths and this was not.
// Same budget, [DE1] marker. Needed because the DE1's "no machine found" outcome
// is reported once per scan cycle and the reconnect ladder scans forever, so a
// flat WARN there would be the cry-wolf pattern all over again.
void BLEManager::de1RepeatFailure(const QString& message) {
    const int count = ++m_repeatFailureCounts[message];
    if (count <= kScaleFailuresAtWarn) {
        de1Warn(message);
    } else {
        de1Debug(QString("%1 (repeat %2)").arg(message).arg(count));
    }
}

void BLEManager::resetRepeatFailureBudget() {
    m_repeatFailureCounts.clear();
}

// The DE1 tiers. One write, to the system log, carrying the marker.
//
// Each of these also emitted de1LogMessage, a signal whose only consumer was the
// connections-page DE1 window. That is gone: the view reads the system log now, so
// the second write is not just redundant but was the thing keeping a bare,
// level-less copy of every line alive.
void BLEManager::de1Debug(const QString& message, const QString& source) {
    DE1_LOG_STDERR_DYN(source, message);
}

void BLEManager::de1Info(const QString& message, const QString& source) {
    DE1_INFO_STDERR_DYN(source, message);
}

void BLEManager::de1Warn(const QString& message, const QString& source) {
    DE1_WARN_STDERR_DYN(source, message);
}

// The refractometer tiers. Same shape as the scale ones, different marker.
//
// Both record into the scale log because that is the view the refractometer is
// shown in — it is listed on the connections page beside the scales, and its
// "Share Log" is the same export. The marker is what keeps the two subsystems
// separable in the system log despite sharing that one sink.
void BLEManager::refractometerDebug(const QString& message, const QString& source) {
    REFRACTOMETER_LOG_STDERR_DYN(source, message);
}

void BLEManager::refractometerInfo(const QString& message, const QString& source) {
    REFRACTOMETER_INFO_STDERR_DYN(source, message);
}

// The private scale-log channel that used to live here is GONE:
// appendScaleLog(), m_scaleLogMessages, clearScaleLog(), getScaleLogPath(),
// writeScaleLogToFile(), shareScaleLog() and scale_debug_log.txt.
//
// It existed because the connections-page view and the "Share Log" button had no
// other source. Both now read the system log — the view through
// WebDebugLogger::sessionLinesMatching(), Share through shareSystemLog() below —
// so the buffer was a second copy of lines already on disk, capped at 1000, and
// the file was a scale-only subset that omitted the DE1 and everything else that
// ran alongside a failure. Users have been asked for the system log for a while;
// this removes the thing that made the other file look like an alternative.
//
void BLEManager::shareSystemLog() {
    // The system log is what users are asked for now, and it is already on disk —
    // nothing to assemble first, unlike the scale log above.
    WebDebugLogger* logger = WebDebugLogger::instance();
    if (!logger) {
        scaleWarn(QStringLiteral("Cannot share the debug log: the logger is not installed"));
        return;
    }
    const FileShare::Result r =
        FileShare::shareFile(logger->logFilePath(), QStringLiteral("Share Debug Log"));
    if (!r.ok) {
        scaleWarn(QStringLiteral("Share failed: %1").arg(r.message));
    } else if (!r.message.isEmpty()) {
        scaleInfo(r.message);
    }
}

QString BLEManager::translateUiString(const QString& key, const QString& fallback) const {
    return translateOrFallback(m_translationManager, key, fallback);
}
