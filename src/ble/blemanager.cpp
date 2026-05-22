#include "blemanager.h"
#include "blecapability.h"
#include "scaledevice.h"
#include "protocol/de1characteristics.h"
#include "scales/scalefactory.h"
#include "refractometers/difluidr2.h"
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
#include <QDateTime>

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
    emit bluetoothAvailableChanged();
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
            m_scaleConnectionTimer->stop();
            // Disconnect physical scale so FlowScale takes over
            emit disconnectScaleRequested();
        }
        qDebug() << "BLEManager: BLE operations" << (disabled ? "disabled (simulator mode)" : "enabled");
        emit disabledChanged();
    }
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

        if (entry.transport == QStringLiteral("wifi")) {
            // Strip the "wifi:" prefix to get the bare hostname.
            m_pendingWifiHostname = address.mid(QStringLiteral("wifi:").size());
            emit scaleDiscovered(QBluetoothDeviceInfo{}, entry.type);
        } else {
            emit scaleDiscovered(entry.device, entry.type);
        }
        return;
    }
    qWarning() << "Scale not found in discovered list:" << address;
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
        return;
    }

    // Check and request Bluetooth permission on Android
    requestBluetoothPermission();
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
                emit errorOccurred(translateUiString("ble.error.locationPermissionDeniedForBluetooth",
                    "Location permission denied - required for Bluetooth scanning"));
            }
        });
        return;
    } else if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Denied) {
        emit de1LogMessage("Location permission denied");
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
                emit errorOccurred(translateUiString("ble.error.bluetoothPermissionDenied",
                    "Bluetooth permission denied"));
            }
        });
        return;
    case Qt::PermissionStatus::Denied:
        emit de1LogMessage("Bluetooth permission denied");
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

    // Check if it's a refractometer BEFORE scale detection (prevents R2 misclassification)
    if (DiFluidR2::isR2Device(device.name())) {
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

        // A user-initiated scan only POPULATES the discovered list (m_scales,
        // above) for the user to choose from — it must NOT auto-connect. Auto-
        // connecting here would grab the first scale found (hijacking the user's
        // pick — e.g. connecting the BLE Decent scale when the user wants the
        // WiFi one) and, via the scaleDiscovered handler's unconditional
        // addKnownScale(), silently re-save a scale the user just forgot. So a
        // forgotten scale would reappear on the next scan. Explicit selection
        // from the list goes through connectToScale(), which emits its own
        // scaleDiscovered.
        if (m_userInitiatedScaleScan) {
            return;
        }

        // Auto-reconnect path: only connect to the saved primary scale — #440:
        // don't let a nearby non-primary scale hijack auto-reconnect — unless
        // this is the WiFi-to-BLE fallback accepting a Decent BLE substitute.
        if (!isFallbackCandidate) {
            // With no saved primary (e.g. the user just forgot the scale) there
            // is nothing to auto-reconnect to. Leave the scale in the discovered
            // list (appended above) for explicit selection, but do NOT emit —
            // emitting auto-connects and, via the scaleDiscovered handler's
            // addKnownScale()/setPrimaryScale(), silently re-saves the scale the
            // user just forgot. It would then reappear as a Known Device on the
            // next *background* scan (refractometer/DE1 scan, startup probe) and,
            // because buildCombinedModel filters Known Devices out of the
            // discovered list, never show up there again — so "Forget" appears
            // not to stick. Explicit selection from the list goes through
            // connectToScale(), which emits its own scaleDiscovered.
            if (m_savedScaleAddress.isEmpty()) {
                appendScaleLog(QString("No saved scale — listing %1 for manual selection (no auto-connect)")
                               .arg(device.name()));
                return;
            }
            if (!deviceIdentifiersMatch(device, m_savedScaleAddress)) {
                appendScaleLog(QString("Ignoring non-primary scale: %1 (%2)").arg(device.name(), getDeviceIdentifier(device)));
                return;
            }
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
    return ScaleFactory::scaleTypeName(type);
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
    }
}

void BLEManager::onScaleConnectedChanged() {
    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        // Scale connected - stop timers, clear failure flag, clear direct connect state
        m_scaleConnectionTimer->stop();
        m_directConnectInProgress = false;
        m_directConnectAddress.clear();
        m_wifiFallbackToBleActive = false;  // Reset for the next saved-scale cycle
        m_lastScanErrorShown.clear();       // Healthy state — allow a future fresh scan error to pop again
        m_anyBleSuccessThisSession = true;  // Permission proven good (WiFi scales hit this too — see note below)
        m_flowScaleFallbackEmitted = false;  // Allow dialog again if scale disconnects and reconnect fails
        if (m_scaleConnectionFailed) {
            m_scaleConnectionFailed = false;
            emit scaleConnectionFailedChanged();
        }
        qDebug() << "BLEManager: Scale connected";
    } else {
        // Scale disconnected - notify UI immediately
        qDebug() << "BLEManager: Scale disconnected";
        appendScaleLog("Scale disconnected");
        emit scaleDisconnected();
    }
}

void BLEManager::onScaleConnectionTimeout() {
    // Clear direct connect state on timeout
    m_directConnectInProgress = false;
    m_directConnectAddress.clear();

    if (m_scaleDevice && m_scaleDevice->isConnected()) {
        return;  // Connection raced in — nothing to do.
    }

    qWarning() << "BLEManager: Scale connection timeout - not found";

    // WiFi-saved scale that didn't connect → fall back to a BLE scan for the
    // same physical scale family. Don't go straight to FlowScale yet — we owe
    // the user one more reasonable attempt before giving up. Only run the
    // fallback once per saved-scale cycle (the `!m_wifiFallbackToBleActive`
    // guard prevents a second fallback if the BLE scan itself times out).
    if (!m_wifiFallbackToBleActive
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

void BLEManager::setRefractometerDevice(DiFluidR2* device) {
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
        connect(m_refractometerDevice, &DiFluidR2::connectedChanged,
                this, &BLEManager::refractometerConnectedChanged);
    }
    emit refractometerConnectedChanged();
}

void BLEManager::tryDirectConnectToRefractometer() {
    if (m_savedRefractometerAddress.isEmpty() || m_disabled) {
        qDebug().noquote() << QString("[R2-diag] tryDirectConnectToRefractometer no-op (savedAddrEmpty=%1 disabled=%2)")
            .arg(m_savedRefractometerAddress.isEmpty() ? QStringLiteral("true") : QStringLiteral("false"),
                 m_disabled ? QStringLiteral("true") : QStringLiteral("false"));
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

#ifdef Q_OS_IOS
    // On iOS, we have a UUID, not a MAC address.
    // Direct connect with just a UUID rarely works - scan and match by UUID.
    qDebug() << "BLEManager: DE1 direct wake (iOS) - scanning for" << deviceName << "UUID:" << m_savedDE1Address;
    emit de1LogMessage(QString("Direct wake (iOS): scanning for %1").arg(deviceName));

    if (!m_scanning) {
        startScan();
    }
#else
    // On Android/desktop, we have a MAC address - try direct connect
    QString upperAddress = m_savedDE1Address.toUpper();
    QBluetoothAddress address(upperAddress);
    if (address.isNull()) {
        qWarning() << "BLEManager: tryDirectConnectToDE1 - invalid saved address:" << m_savedDE1Address;
        emit de1LogMessage(QString("Direct wake failed: invalid saved address"));
        if (!m_scanning) startScan();
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
#endif
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
            bool alreadyListed = false;
            for (const auto& entry : m_scales) {
                if (entry.transport == QStringLiteral("wifi") && entry.address == address) {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed) {
                ScaleEntry entry;
                entry.type = QStringLiteral("decent-wifi");
                entry.transport = QStringLiteral("wifi");
                entry.name = QStringLiteral("Decent Scale (WiFi)");
                entry.address = address;
                m_scales.append(entry);
                qDebug() << "[BLE] Added WiFi scale to discovered list; m_scales count=" << m_scales.size();
                appendScaleLog(QString("Found %1 (%2)").arg(entry.name, entry.address));
                emit scalesChanged();
            } else {
                qDebug() << "[BLE] WiFi scale already in discovered list — not re-adding";
            }

            // Auto-connect when the discovered scale matches the saved primary
            // AND we're not in a user-initiated scan (where the user is choosing
            // explicitly — list only, don't auto-connect). Covers both paths:
            // saved-scale direct-wake on app start AND a spontaneous scan that
            // happens to find the saved scale.
            if (!m_userInitiatedScaleScan
                    && !m_savedScaleAddress.isEmpty()
                    && address.compare(m_savedScaleAddress, Qt::CaseInsensitive) == 0) {
                m_pendingWifiHostname = hostname;
                emit scaleDiscovered(QBluetoothDeviceInfo{}, QStringLiteral("decent-wifi"));
            }
        });
}

void BLEManager::tryDirectConnectToScale() {
    if (m_disabled) {
        qDebug() << "BLEManager: tryDirectConnectToScale - disabled (simulator mode)";
        return;
    }

    if (m_savedScaleAddress.isEmpty() || m_savedScaleType.isEmpty()) {
        qDebug() << "BLEManager: tryDirectConnectToScale - no saved scale address/type";
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

    // Direct wake strategy:
    // 1. Try direct connection to saved address (may wake sleeping scales that respond to connect requests)
    // 2. Also start scanning in parallel (finds scales that are actively advertising)
    // 3. Whichever succeeds first wins - we don't skip scan results even if direct connect is in progress
    //
    // de1app does both: ble connect + scanning, and calls ble_connect_to_scale again when
    // the device appears in scan results (bluetooth.tcl lines 2032 and 2252-2256)

    QString deviceName = m_savedScaleName.isEmpty() ? m_savedScaleType : m_savedScaleName;

    // WiFi saved-scale path: do an mDNS probe + WS connect. If the connection
    // doesn't establish before m_scaleConnectionTimer fires (~20 s), we fall
    // back to a BLE scan that auto-connects to a discovered Decent scale (see
    // onScaleConnectionTimeout and beginWifiFallbackToBleScan).
    if (m_savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive)) {
        const QString hostname = m_savedScaleAddress.mid(QStringLiteral("wifi:").size());
        qDebug() << "BLEManager: Direct wake (WiFi) - resolving" << hostname;
        appendScaleLog(QString("Direct wake (WiFi): resolving %1").arg(hostname));

        ensureWifiDiscovery();
        m_wifiFallbackToBleActive = false;          // Reset per attempt
        m_scaleConnectionTimer->start();            // Fires onScaleConnectionTimeout if WiFi doesn't connect
        // 5 s mDNS timeout, matching the user-initiated scan path — empirically
        // the HDS mDNS responder regularly takes 2-4 s on first contact (ESP32
        // wake from power-save), and the 20 s connection timer above gives the
        // probe a comfortable window before the WiFi-to-BLE fallback engages.
        m_wifiDiscovery->probe(hostname, 5000);
        return;
    }

#ifdef Q_OS_IOS
    // On iOS, we have a UUID, not a MAC address
    // Direct connect with just a UUID rarely works - we need to find the device via scanning
    // Just start scanning and match by UUID when found
    qDebug() << "BLEManager: Direct wake (iOS) - scanning for" << deviceName << "UUID:" << m_savedScaleAddress;
    appendScaleLog(QString("Direct wake (iOS): scanning for %1").arg(deviceName));

    m_directConnectInProgress = true;
    m_directConnectAddress = m_savedScaleAddress;  // UUID on iOS

    // Start timeout timer
    m_scaleConnectionTimer->start();

    // On iOS, we skip the direct connect attempt and rely on scanning
    m_scanningForScales = true;
    if (!m_scanning) {
        startScan();
    }
#else
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
#endif
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
        return m_translationManager->translate(key, fallback);
    }
    return fallback;
}
