#pragma once

#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QList>
#include <QVariant>
#include <QPermissions>
#include <QTimer>
#include <QStringList>
#include <QFile>
#include <QDateTime>
#include <QMutex>
#include <atomic>

#include "blecapability.h"

class ScaleDevice;
class DiFluidR2;
class SettingsHardware;
class WifiScaleDiscovery;
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
class AppleBtState;
#endif

// Per-discovered-scale record. Carries the BLE device info for BLE entries
// (default-constructed for WiFi entries — no QBluetoothDeviceInfo exists for
// a WS endpoint) plus canonical `name`/`address` fields so iteration sites
// don't need to branch on transport just to render the row.
struct ScaleEntry {
    QBluetoothDeviceInfo device;  // Valid for BLE; default-constructed for WiFi.
    QString type;                 // e.g. "decent", "acaia", "decent-wifi"
    QString transport;            // "ble" or "wifi"
    QString name;                 // Display name (carries " (WiFi)" suffix for WiFi entries)
    QString address;              // Routing handle: BLE MAC/UUID, or "wifi:<hostname>"
};

// Helper to get device identifier - iOS uses UUID, others use MAC address
inline QString getDeviceIdentifier(const QBluetoothDeviceInfo& device) {
#ifdef Q_OS_IOS
    // iOS doesn't expose MAC addresses, use UUID instead
    return device.deviceUuid().toString();
#else
    return device.address().toString();
#endif
}

// Helper to compare device identifiers
inline bool deviceIdentifiersMatch(const QBluetoothDeviceInfo& device, const QString& identifier) {
#ifdef Q_OS_IOS
    return device.deviceUuid().toString() == identifier;
#else
    return device.address().toString().compare(identifier, Qt::CaseInsensitive) == 0;
#endif
}

class BLEManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(bool bluetoothAvailable READ isBluetoothAvailable NOTIFY bluetoothAvailableChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList discoveredScales READ discoveredScales NOTIFY scalesChanged)
    Q_PROPERTY(bool scaleConnectionFailed READ scaleConnectionFailed NOTIFY scaleConnectionFailedChanged)
    Q_PROPERTY(QVariantList discoveredRefractometers READ discoveredRefractometers NOTIFY refractometersChanged)
    Q_PROPERTY(bool refractometerConnected READ isRefractometerConnected NOTIFY refractometerConnectedChanged)
    Q_PROPERTY(bool hasSavedDE1 READ hasSavedDE1 CONSTANT)
    Q_PROPERTY(bool disabled READ isDisabled WRITE setDisabled NOTIFY disabledChanged)
    Q_PROPERTY(bool linuxBleCapabilityMissing READ linuxBleCapabilityMissing CONSTANT)
    Q_PROPERTY(QString linuxBleSetcapCommand READ linuxBleSetcapCommand CONSTANT)

public:
    explicit BLEManager(QObject* parent = nullptr);
    ~BLEManager();

    bool isScanning() const;
    bool isBluetoothAvailable() const;
    bool isScanningForScales() const { return m_scanningForScales; }
    bool isDisabled() const { return m_disabled; }
    void setDisabled(bool disabled);  // Disable all BLE operations (for simulator mode)
    QVariantList discoveredDevices() const;
    QVariantList discoveredScales() const;
    bool scaleConnectionFailed() const { return m_scaleConnectionFailed; }
    bool hasSavedScale() const { return !m_savedScaleAddress.isEmpty(); }
    // For WiFi scale selections: the hostname to dial. Set by connectToScale()
    // and tryDirectConnectToScale() immediately before emitting scaleDiscovered()
    // with a default-constructed device + type=="decent-wifi". The main.cpp
    // handler reads this after the factory creates the DecentScaleWifi driver.
    QString pendingWifiHostname() const { return m_pendingWifiHostname; }
    bool hasSavedDE1() const { return !m_savedDE1Address.isEmpty(); }
    bool linuxBleCapabilityMissing() const { return BleCapability::linuxMissing(); }
    QString linuxBleSetcapCommand() const { return BleCapability::linuxSetcapCommand(); }

    // Singleton accessor so the transport layer can surface the BlueZ-cache
    // hint without plumbing a BLEManager* through every constructor. Only
    // one BLEManager is ever constructed (see main.cpp).
    static BLEManager* instance() { return s_instance; }

    // Emit linuxBlueZCacheHintNeeded at most once per session. Invoked from
    // the transport layer when UnknownRemoteDeviceError fires and the
    // capability check indicates caps are effective (so the cause is
    // almost certainly a stale BlueZ cache or similar host-side state).
    void requestBluezCacheHint();

    // Build-scoped dual-HIGH backoff latch (#1093/#1176). The contention is
    // a property of this device's BT radio + the DE1 link, not of any one
    // scale — so once any scale's transport detects it, EVERY scale this run
    // (including one connected after a scale-type change, which builds a fresh
    // transport) must skip CONNECTION_PRIORITY_HIGH. Lives on the BLEManager
    // singleton so it outlives per-scale transport objects.
    //
    // This struct itself is in-memory only. D9 adds build-scoped PERSISTENCE
    // externally (BLEManager::setSettings → SettingsHardware): latch/clear
    // write through to QSettings, and a same-build restart rehydrates the
    // struct before the first BLE connect (so it skips HIGH with no detection
    // window). A DIFFERENT build, or an explicit MCP reset, discards the
    // persisted record and re-detects from scratch (the build-scoped safety
    // valve). So: not "cleared by every app restart" anymore — cleared by a
    // build change or an MCP reset.
    //
    // The latch carries minimal diagnostic metadata for the MCP read (D3/D4):
    // the trigger kind ("de1-fault-cluster" / "scale-feed-stall") and the
    // wall-clock time it was set, from which the MCP derives "elapsed since
    // app start when latched".
    //
    // The three correlated fields are one value type with the enforced
    // invariant "triggerKind non-empty AND setTime valid IFF latched": they
    // are mutated ONLY via set()/clear()/rehydrate(), so the correlation
    // cannot drift (D7) — including across the persistence trust boundary
    // (rehydrate() sanitizes possibly-malformed persisted input). m_appStartTime
    // is deliberately NOT part of this — it is a process-lifetime fact.
    struct ScaleSkipHighLatch {
        bool      latched = false;
        QString   triggerKind;   // non-empty iff latched
        QDateTime setTime;       // valid    iff latched
        void set(const QString& kind) {
            latched = true;
            // Belt-and-suspenders: the public API mandates a kind (no
            // default), so an empty kind here would be an internal bug.
            triggerKind = kind.isEmpty() ? QStringLiteral("unknown") : kind;
            setTime = QDateTime::currentDateTime();
        }
        // Rehydrate from a persisted record. UNLIKE set(), preserves the
        // original set-time (the diagnostic value of "when did this device
        // first prove weak"). Sanitises possibly-corrupt persisted input so
        // the "kind non-empty AND time valid IFF latched" invariant holds even
        // on a partial write / manual edit / format drift: an empty kind
        // becomes "unknown"; an invalid time falls back to now (the
        // classification is the load-bearing fact — do NOT discard a valid
        // same-epoch/legacy latch over a bad diagnostic timestamp). Returns false iff
        // the time had to be substituted, so the caller can log the anomaly.
        bool rehydrate(const QString& kind, const QDateTime& time) {
            latched = true;
            triggerKind = kind.isEmpty() ? QStringLiteral("unknown") : kind;
            if (time.isValid()) { setTime = time; return true; }
            setTime = QDateTime::currentDateTime();
            return false;
        }
        void clear() { latched = false; triggerKind.clear(); setTime = QDateTime(); }
    };

    // --- BLE detection epoch (scale-priority-epoch-scope-and-stall-confirm) ---
    // The persisted dual-HIGH-incapable classification is scoped to THIS
    // constant, NOT to versionCode/build. The gate (decideBleEpochGate, the
    // function setSettings dispatches on) is a trichotomy, NOT a biconditional:
    //   • stored epoch == kBleDetectionEpoch        → rehydrate
    //   • legacy record (no epoch key; cpEpoch -1)  → rehydrate + migrate fwd
    //   • a DIFFERENT non-negative epoch, or corrupt → discard + re-detect
    // i.e. a legacy record is rehydrated, NOT discarded — discard happens
    // only on a deliberate epoch bump (or corruption). CI / versioncode.txt
    // MUST NOT touch this — it is the single, deliberate "re-classify every
    // device once on this release" lever (replaces the old per-build reset).
    //
    // BUMP THIS (by one) ONLY when a release intentionally changes BLE
    // connection behaviour (connection parameters / priority handling) OR you
    // explicitly want every device to re-run detection once on that release.
    // Bumping it on a release that fixes the dual-HIGH contention is how the
    // fix reaches already-latched devices. A legacy pre-epoch record (no
    // stored epoch) is migrated forward, NOT re-detected (see setSettings).
    static constexpr int kBleDetectionEpoch = 1;

    bool scaleSkipHighPriority() const { return m_scaleSkipHigh.latched; }
    // Latch the skip-HIGH decision with a mandatory trigger kind (no default —
    // "latch without a reason" is a compile error, not a silent "unknown").
    void latchScaleSkipHighPriority(const QString& triggerKind);
    // Clear the in-memory latch AND the persisted (build-scoped) record (the
    // MCP reset escape hatch — the reset is durable: a same-build restart will
    // NOT rehydrate it). Takes effect on the next scale (re)connect's
    // detection pass — eventually-consistent, no forced teardown of a live
    // connection.
    void clearScaleSkipHighPriority();
    QString scaleSkipHighTriggerKind() const { return m_scaleSkipHigh.triggerKind; }
    QDateTime scaleSkipHighSetTime() const { return m_scaleSkipHigh.setTime; }
    // Diagnostic only (NOT a gate): the versionCode that last set/rehydrated
    // the current latch. 0 when not latched. Surfaced in the MCP read so the
    // "last classified by build N" trail survives the build→epoch demotion.
    int scaleSkipHighBuildCode() const { return m_scaleSkipHighBuildCode; }
    QDateTime appStartTime() const { return m_appStartTime; }

    // --- Backoff policy mode (observe-mode change) ---
    // A persistent, MCP-controlled policy dimension layered on the dual-HIGH
    // backoff. `Enforce` (default) is byte-identical to the pre-change
    // behavior. `Observe` makes detection inert-but-observable: the transport
    // forces HIGH (overriding, but not erasing, any persisted latch) and logs
    // "would back off" / recovery events instead of acting. The mode is
    // deliberately NOT build-scoped (unlike the latch) — it survives restarts
    // and build upgrades until explicitly changed.
    enum class BackoffMode { Enforce, Observe };
    static BackoffMode backoffModeFromString(const QString& s) {
        return s == QLatin1String("observe") ? BackoffMode::Observe
                                             : BackoffMode::Enforce;
    }
    static QString backoffModeToString(BackoffMode m) {
        return m == BackoffMode::Observe ? QStringLiteral("observe")
                                         : QStringLiteral("enforce");
    }
    // m_backoffMode is written via a queued invoke on the BLEManager thread
    // (setBackoffMode) and read from the transport + MCP threads, so it is
    // std::atomic — a lock-free, eventually-consistent read. (The skip-HIGH
    // latch two members up is read the same way and was historically
    // unsynchronised; this closes that class of race for the new field.)
    BackoffMode backoffMode() const {
        return m_backoffMode.load(std::memory_order_relaxed);
    }
    bool observeMode() const { return backoffMode() == BackoffMode::Observe; }
    // Set + write through to the (non-build-scoped) persisted store. Does NOT
    // touch the latch (observe overrides it at the transport; the latch value
    // is preserved so switching back to Enforce honours it honestly).
    void setBackoffMode(BackoffMode mode);

    // One recent observe-mode event for the MCP read (the durable record is
    // the debug log). Construction is ONLY via the two named factories
    // (mirrors ScaleSkipHighLatch's set()/clear() discipline): they stamp the
    // time and clamp the duration non-negative, so the kind ↔ duration-meaning
    // correlation (stallSec for wouldBackoff, gapSec for recovered) cannot be
    // set wrong at a call site.
    struct ObserveEvent {
        QDateTime time;
        QString triggerKind;    // "scale-feed-stall" | "de1-fault-cluster"
        QString kind;           // "wouldBackoff" | "recovered"
        double durationSec = 0; // stallSec (wouldBackoff) / gapSec (recovered)

        static ObserveEvent wouldBackoff(const QString& triggerKind,
                                         double stallSec) {
            return { QDateTime::currentDateTime(), triggerKind,
                     QStringLiteral("wouldBackoff"),
                     stallSec < 0 ? 0.0 : stallSec };
        }
        static ObserveEvent recovered(const QString& triggerKind,
                                      double gapSec) {
            return { QDateTime::currentDateTime(), triggerKind,
                     QStringLiteral("recovered"), gapSec < 0 ? 0.0 : gapSec };
        }
    };

    // Bounded, thread-safe ring. Header-inline (like ScaleSkipHighLatch) so it
    // is unit-testable without linking blemanager.cpp. append() runs on the
    // transport thread, snapshotNewestFirst() on the MCP thread — the mutex
    // makes the lock contract un-bypassable: the buffer cannot be touched
    // except through these two methods, and the bound + newest-first reversal
    // are owned here, not re-implemented per call site.
    class ObserveEventRing {
    public:
        static constexpr int kCapacity = 20;
        void append(const ObserveEvent& e) {
            QMutexLocker lock(&m_mutex);
            m_events.append(e);
            while (m_events.size() > kCapacity) m_events.removeFirst();
        }
        QList<ObserveEvent> snapshotNewestFirst() const {
            QMutexLocker lock(&m_mutex);
            QList<ObserveEvent> out;
            out.reserve(m_events.size());
            for (auto it = m_events.crbegin(); it != m_events.crend(); ++it)
                out.append(*it);
            return out;
        }
    private:
        QList<ObserveEvent> m_events;
        mutable QMutex m_mutex;
    };

    void recordObserveEvent(const ObserveEvent& e) {
        m_observeEvents.append(e);
    }
    // Most-recent-first snapshot (copy — safe to read off-thread).
    QList<ObserveEvent> recentObserveEvents() const {
        return m_observeEvents.snapshotNewestFirst();
    }

    // D9: wire the persisted (build-scoped) classification store. Called once
    // at startup BEFORE any BLE connect. Loads a prior classification: if it
    // was set by the CURRENT build it seeds the in-memory latch so the first
    // connect of the run already skips HIGH on both links (no detection
    // window); if it was set by a DIFFERENT build it is discarded + wiped
    // (the build-scoped safety valve — every new build re-detects).
    // latch/clear then write through to this store.
    void setSettings(SettingsHardware* settings);

    Q_INVOKABLE QBluetoothDeviceInfo getScaleDeviceInfo(const QString& address) const;
    Q_INVOKABLE QString getScaleType(const QString& address) const;
    Q_INVOKABLE void connectToScale(const QString& address);  // Manual scale selection

    ScaleDevice* scaleDevice() const { return m_scaleDevice; }
    void setScaleDevice(ScaleDevice* scale);

    // Scale address management
    Q_INVOKABLE void setSavedScaleAddress(const QString& address, const QString& type, const QString& name);
    Q_INVOKABLE void clearSavedScale();

    // Refractometer support
    QVariantList discoveredRefractometers() const;
    bool isRefractometerConnected() const;
    QBluetoothDeviceInfo getRefractometerDeviceInfo(const QString& address) const;
    Q_INVOKABLE void connectToRefractometer(const QString& address);
    Q_INVOKABLE void setSavedRefractometerAddress(const QString& address, const QString& name);
    Q_INVOKABLE void clearSavedRefractometer();
    void setRefractometerDevice(DiFluidR2* device);
    Q_INVOKABLE void tryDirectConnectToRefractometer();

    // DE1 address management
    void setSavedDE1Address(const QString& address, const QString& name);
    Q_INVOKABLE void clearSavedDE1();

    Q_INVOKABLE void openLocationSettings();
    Q_INVOKABLE void openBluetoothSettings();

    // Reset connection state flags so retry attempts can proceed
    void resetScaleConnectionState();

    // Scale debug logging
    Q_INVOKABLE void clearScaleLog();
    Q_INVOKABLE void shareScaleLog();
    Q_INVOKABLE QString getScaleLogPath() const;
    void appendScaleLog(const QString& message);  // For use by scale implementations

public slots:
    Q_INVOKABLE void tryDirectConnectToDE1();
    Q_INVOKABLE void tryDirectConnectToScale();
    Q_INVOKABLE void scanForDevices();  // User-initiated scan for DE1, scales, and refractometers
    Q_INVOKABLE void startScan();  // Start scanning for DE1 and scales
    void stopScan();
    void clearDevices();

signals:
    void scanningChanged();
    void bluetoothAvailableChanged();
    void devicesChanged();
    void scalesChanged();
    void scaleConnectionFailedChanged();
    void de1Discovered(const QBluetoothDeviceInfo& device);
    // For BLE entries `device` carries the real QBluetoothDeviceInfo. For
    // WiFi entries (type == "decent-wifi") `device` is default-constructed
    // and the routing hostname lives in pendingWifiHostname() — the main.cpp
    // handler reads it after the factory creates the scale.
    void scaleDiscovered(const QBluetoothDeviceInfo& device, const QString& type);
    void errorOccurred(const QString& error);
    void de1LogMessage(const QString& message);
    void scaleLogMessage(const QString& message);
    void flowScaleFallback();  // Emitted when no physical scale found, using FlowScale
    void scaleDisconnected();  // Emitted when physical scale disconnects
    void scanStarted();  // Emitted when BLE scan actually begins
    // Emitted when a saved WiFi scale fails to connect within the connection
    // timeout and BLEManager has started a BLE scan as a fallback. UI binds
    // this to a toast/banner so the user knows what's happening.
    void wifiUnreachableFallingBackToBle(const QString& hostname);
    void disabledChanged();
    void disconnectScaleRequested();  // Emitted when switching to a different scale, BLE is disabled, or saved scale is cleared
    void refractometersChanged();
    void refractometerConnectedChanged();
    void refractometerDiscovered(const QBluetoothDeviceInfo& device);
    void disconnectRefractometerRequested();
    void linuxBlueZCacheHintNeeded();  // Request the BlueZ-cache recovery dialog (Linux, caps OK).


private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo& device);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onScaleConnectedChanged();
    void onScaleConnectionTimeout();
    void onHostModeStateChanged(QBluetoothLocalDevice::HostMode mode);

private:
    bool isDE1Device(const QBluetoothDeviceInfo& device) const;
    QString getScaleType(const QBluetoothDeviceInfo& device) const;
    void requestBluetoothPermission();
    void doStartScan();
    void ensureDiscoveryAgent();
    // Lazy-create m_wifiDiscovery once with a single unified scaleFound
    // handler. Both scan-for-devices and try-direct-connect paths call this
    // before invoking probe(); registering the lambda only on first call
    // (previously done at TWO sites with DIFFERENT lambdas — whichever ran
    // first wiped out the other, breaking either the list-populate path or
    // the auto-reconnect path depending on order).
    void ensureWifiDiscovery();
    // WiFi-saved-scale fallback: when the WiFi connection timer fires without
    // a successful connect, kick off a BLE scan that auto-connects to the
    // first Decent-family scale found. Toast surfaces the fallback to the
    // user. Cleared on the next successful scale connect.
    void beginWifiFallbackToBleScan();

#ifndef Q_OS_IOS
    QBluetoothLocalDevice* m_localDevice = nullptr;
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // Lazy — created on the first non-simulator isBluetoothAvailable()
    // query so CoreBluetooth initialisation (and its permission prompt)
    // doesn't fire at app launch when the user has simulator mode on.
    mutable AppleBtState* m_appleBtState = nullptr;
#endif
    QBluetoothDeviceDiscoveryAgent* m_discoveryAgent = nullptr;
    QList<QBluetoothDeviceInfo> m_de1Devices;
    QList<ScaleEntry> m_scales;
    WifiScaleDiscovery* m_wifiDiscovery = nullptr;  // Lazy-created on first scanForDevices
    // WiFi-to-BLE fallback: set when m_scaleConnectionTimer fires for a saved
    // WiFi scale and we start a BLE scan as a fallback. Lets onDeviceDiscovered
    // auto-connect to a discovered Decent BLE scale even though the saved
    // address is a WiFi one. Cleared once a scale connects.
    bool m_wifiFallbackToBleActive = false;
    bool m_scanning = false;
    bool m_permissionRequested = false;
    bool m_scanningForScales = false;  // True when scanning for scales (user or auto-reconnect)
    bool m_userInitiatedScaleScan = false;  // True only for user-initiated scan (show all scales)
    bool m_scaleConnectionFailed = false;
    ScaleDevice* m_scaleDevice = nullptr;
    QTimer* m_scaleConnectionTimer = nullptr;

    // Saved scale for direct wake connection
    QString m_savedScaleAddress;
    QString m_savedScaleType;
    QString m_savedScaleName;

    // Hostname carried with the most recent scaleDiscovered emission for a
    // WiFi scale (so main.cpp can route the connect after the factory creates
    // the driver). Set immediately before emitting, read immediately after.
    QString m_pendingWifiHostname;

    // Saved DE1 for direct wake connection
    QString m_savedDE1Address;
    QString m_savedDE1Name;

    // Prevents showing "No Scale Found" dialog more than once per session
    bool m_flowScaleFallbackEmitted = false;

    // App-run dual-HIGH backoff latch + diagnostic metadata (in-memory only;
    // see scaleSkipHighPriority()). m_appStartTime is captured at construction
    // (process start) so the MCP read can report "elapsed since app start" —
    // intentionally separate from the latch value (different lifetime).
    ScaleSkipHighLatch m_scaleSkipHigh;
    // Diagnostic: versionCode that last set/rehydrated the latch (0 = none).
    // NOT part of the invariant-bearing latch struct (it is informational and
    // no longer the gate — the epoch is). Set on latch/rehydrate, 0 on clear.
    int m_scaleSkipHighBuildCode = 0;
    QDateTime m_appStartTime;
    // Backoff policy mode (observe-mode change). Loaded from SettingsHardware
    // in setSettings() (not build-scoped); default Enforce until then.
    // Atomic: written on the BLEManager thread, read on transport + MCP threads.
    std::atomic<BackoffMode> m_backoffMode { BackoffMode::Enforce };
    ObserveEventRing m_observeEvents;             // self-locking bounded ring
    // D9: persisted (build-scoped) classification store. Non-owning; the
    // SettingsHardware domain object outlives BLEManager (main()-scoped).
    // Null until setSettings() is wired (and on platforms/tests that don't
    // wire it — then the classification is in-memory-only, as before D9).
    SettingsHardware* m_settings = nullptr;

    // Simulator mode - disable all BLE operations
    bool m_disabled = false;

    // Direct connect state - prevents duplicate connections from scan
    bool m_directConnectInProgress = false;
    QString m_directConnectAddress;

    // Refractometer
    QList<QBluetoothDeviceInfo> m_refractometerDevices;
    QString m_savedRefractometerAddress;
    QString m_savedRefractometerName;
    DiFluidR2* m_refractometerDevice = nullptr;

    // Scale debug log
    QStringList m_scaleLogMessages;
    QString m_scaleLogFilePath;
    void writeScaleLogToFile();

    static BLEManager* s_instance;
};
