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
class RefractometerDevice;
class SettingsHardware;
class WifiScaleDiscovery;
class TranslationManager;
class QWebSocket;
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
    QString resolvedIp;           // WiFi entries only: the IP WifiScaleDiscovery's mDNS
                                   // query resolved `address`'s hostname to. Empty for BLE/USB.
                                   // Lets connectToScale() seed DecentScaleWifi's IP cache so
                                   // the connect dials the already-known IP instead of making
                                   // Qt's own resolver re-resolve ".local" (unreliable on
                                   // non-Android — see connectToScale()).
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

    // Optional TranslationManager — when set, user-visible error strings
    // (those emitted via errorOccurred) are run through translate() with
    // a stable i18n key + the existing English text as fallback. Scale
    // debug-log lines stay in English regardless (they're diagnostic).
    void setTranslationManager(TranslationManager* tm) { m_translationManager = tm; }
    // For WiFi scale selections: the hostname to dial. Set by connectToScale()
    // and tryDirectConnectToScale() immediately before emitting scaleDiscovered()
    // with a default-constructed device + type=="decent-wifi". The main.cpp
    // handler reads this after the factory creates the DecentScaleWifi driver.
    QString pendingWifiHostname() const { return m_pendingWifiHostname; }
    // Companion to pendingWifiHostname(): the IP a just-completed mDNS
    // discovery already resolved that hostname to, if any (empty when no
    // fresh resolution happened at this call site — e.g. the manual-entry and
    // persisted-cache-driven paths, which have nothing new to offer). main.cpp
    // seeds DecentScaleWifi's IP cache with it before dialing, so the connect
    // skips Qt's own (mDNS-unreliable on non-Android) hostname resolver.
    QString pendingWifiResolvedIp() const { return m_pendingWifiResolvedIp; }
    // True between beginWifiFallbackToBleScan and the next successful connect.
    // main.cpp reads this when a BLE Decent scale connects during the fallback
    // window — in that case the user's saved WiFi primary address is preserved
    // (the BLE connect is treated as a temporary substitute, not a permanent
    // primary-scale change).
    bool isWifiFallbackToBleActive() const { return m_wifiFallbackToBleActive; }

    // Proactive WiFi-primary switch-back (driven by main.cpp's idle poll when
    // we're on the BLE backup but the saved primary is a WiFi scale):
    //  - probeWifiPrimaryReachable() does a NON-disruptive HDS identity check:
    //    opens ws://<ip>/snapshot and requires a valid HDS frame (snapshot or
    //    status) within ~3.5 s. It never touches the live BLE link, so a failed
    //    probe leaves the working backup untouched. Reports the outcome via
    //    wifiPrimaryReachable() at most once per probe (a probe superseded by a
    //    later call is cancelled without emitting). A bare TCP-open on port 80
    //    is NOT enough — any LAN device listening on 80 (router, printer, NAS)
    //    would pass that gate; #1281 needed the actual HDS-frame validation.
    //  - switchToWifiPrimary() drops the current backup scale and connects the
    //    saved WiFi primary via the cached-IP fast path. Call only after a
    //    reachable probe (and a re-check that we're still idle on the backup).
    //    Side effects: clears m_wifiFallbackToBleActive and starts
    //    m_scaleConnectionTimer, so a failed reconnect routes back through the
    //    WiFi->BLE fallback path.
    void probeWifiPrimaryReachable(const QString& ip);
    void switchToWifiPrimary();

    bool hasSavedDE1() const { return !m_savedDE1Address.isEmpty(); }

    // BLE-stack-wedge recovery (#1309). On some tablets (Teclast P80X) the
    // Android BLE stack wedges so every connect fails — DE1 and scale alike —
    // and survives even an app restart; only cycling the Bluetooth adapter
    // off/on clears it. main.cpp feeds the detector its two inputs:
    //  - noteDe1Connected(): the DE1 link's connected state (BLEManager doesn't
    //    own DE1Device, so it can't observe this directly).
    //  - onDe1LinkFault(): controller-error faults re-emitted by DE1Device.
    // See onDe1LinkFault()/onScaleConnectionTimeout() for the discriminator and
    // maybeRecoverWedgedStack() for the Android-only adapter power-cycle.
    void noteDe1Connected(bool connected);
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
    // Connect to a WiFi scale by a manually-entered IP or mDNS name (the "Add
    // WiFi Scale" dialog), without requiring it to be in the discovered list. A
    // bare name with no dot gets ".local" appended (matching the discovery
    // default "hds.local"); IPs and dotted names pass through. Arms the connection
    // timer so a wrong/unreachable host surfaces as `manualWifiValidationFailed`
    // (driving the QML "Couldn't verify a scale at <address>" dialog) instead
    // of silently — WiFi socket errors are otherwise log-only (#1253). Unlike a
    // saved WiFi scale this does NOT fall back to a BLE scan on failure (the
    // user asked for a specific WiFi address).
    //
    // Unlike BLE or saved-scale WiFi connects, persistence is DEFERRED for
    // manual entries: main.cpp's scaleDiscovered handler does NOT call
    // addKnownScale / setPrimaryScale / setSavedScaleAddress at connect
    // initiation. Instead, those run only after `DecentScaleWifi::recognizedAsHds`
    // fires (validating that the typed endpoint is really an HDS scale). If
    // recognition never arrives, `manualWifiValidationFailed` is emitted and
    // the address is NOT saved as the primary — a typo or wrong IP can't
    // poison the saved state. (#1281)
    // `resolvedIp`: pass the IP if the caller already has a fresh mDNS
    // resolution for `hostnameOrIp` (e.g. the "Add WiFi Scale" dialog's
    // mDNS-suggested "Use" button — see manualWifiMdnsDiscovered). Leave empty
    // for a genuinely typed address, where nothing has been resolved yet.
    Q_INVOKABLE void connectToWifiScale(const QString& hostnameOrIp, const QString& resolvedIp = QString());
    // Fire an mDNS probe for the HDS in parallel with the "Add WiFi Scale"
    // dialog. If the scale is on the LAN, this surfaces it to the user so
    // they don't have to type its address. Emits manualWifiMdnsDiscovered on
    // success and (regardless) manualWifiMdnsProbeFinished when done. Safe to
    // call multiple times; a new probe supersedes any in-flight one.
    Q_INVOKABLE void probeMdnsForManualEntry();
    // True while a manual "Add WiFi Scale" attempt is in flight (set by
    // connectToWifiScale, cleared on connect success or timeout). main.cpp
    // reads this in the scaleDiscovered handler to defer persisting the typed
    // address as the saved primary until DecentScaleWifi confirms it's a
    // real scale (see #1281).
    bool isManualWifiConnect() const { return m_manualWifiConnect; }
    // Switch the LIVE connection to the current saved primary scale (set via
    // setSavedScaleAddress just before calling). If a scale is connected it is
    // disconnected first, then the saved primary is direct-woken (BLE) /
    // cached-IP-connected (WiFi) via tryDirectConnectToScale(). Unlike
    // connectToScale() this does NOT require the scale to be in the discovered
    // list, so the Known Devices picker can switch to a known scale that isn't
    // currently being scanned. Requires the saved address AND type to be set; if
    // the switch can't proceed (Bluetooth off / simulator mode) it no-ops with a
    // log/error and does NOT drop the currently-connected scale.
    Q_INVOKABLE void connectToSavedScale();

    ScaleDevice* scaleDevice() const { return m_scaleDevice; }
    void setScaleDevice(ScaleDevice* scale);

    // Add (available==true) or remove (available==false) a synthetic USB scale
    // entry in the discovered-scales list so it shows up as a selectable row,
    // exactly like the WiFi synthetic entry. The entry uses the STABLE address
    // "usb:decent" (transport "usb", type "decent-usb"). Selecting it routes
    // through connectToScale()'s usb branch → usbConnectRequested(); the actual
    // open is done by UsbScaleManager, not here. Driven by main.cpp from
    // UsbScaleManager::usbScaleAvailable/Unavailable.
    void setUsbScaleAvailable(bool available, const QString& name);

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
    void setRefractometerDevice(RefractometerDevice* device);
    Q_INVOKABLE void tryDirectConnectToRefractometer();
    // Hunt mode: while active (post-shot review page open), scans restart
    // back-to-back from onScanFinished until the saved refractometer connects,
    // instead of waiting out the background reconnect tick. Activation kicks
    // an immediate scan when a saved refractometer is not connected and
    // Bluetooth is up; otherwise the reconnect tick resumes the hunt later.
    Q_INVOKABLE void setRefractometerHunt(bool active);
    // True while the post-shot review page is open. The R2 is only used to
    // capture TDS/EY on that page, so its auto-reconnect is scoped to the hunt:
    // tryDirectConnectToRefractometer() no-ops when this is false, and the
    // app-wide reconnect tick self-stops. The scale has no such scoping — it is
    // needed everywhere and keeps its own always-on reconnect.
    bool isRefractometerHunt() const { return m_refractometerHunt; }

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
    // allowDirectConnect=true (foreground triggers: device-picker switch, app
    // startup, DE1 wake) issues a single direct connectToDevice() to the saved
    // address for a fast connect, alongside a scan. allowDirectConnect=false
    // (the 60s background reconnect ladder) scans only — it never parks a direct
    // connect against an absent scale, which on Android holds the BLE stack in
    // Connecting for ~30s and starves the DE1 link (issue #1303). A saved scale
    // still auto-connects via onDeviceDiscovered when it's seen advertising.
    Q_INVOKABLE void tryDirectConnectToScale(bool allowDirectConnect = true);
    // Release a scale direct-connect that was deferred to avoid colliding with
    // the DE1's BLE GATT connect (Android serializes concurrent connects badly).
    // Called by main.cpp when the DE1's direct-wake connection resolves
    // (connected, or the attempt ended).
    void onDe1ConnectionSettled();
    // Track whether the DE1 transport is in BLE service+characteristic discovery
    // and forward the state to a co-resident DecentScale so its heartbeat pauses
    // for the duration. Wired from DE1Device::serviceDiscoveryActiveChanged in
    // main.cpp. See #1176 — scale heartbeat writes that race DE1 char discovery
    // fail with CharacteristicWriteError on weaker radios (Samsung Tab A8).
    void setDe1ServiceDiscoveryActive(bool active);
    // DE1 controller-error fault, re-emitted from DE1Device::de1LinkFault. Feeds
    // the BLE-stack-wedge detector (#1309). These faults fire ONLY for real
    // controller errors (Connection/Authorization/RemoteHostClosed/write-failed)
    // — an absent or sleeping DE1 produces a watchdog timeout with no fault — so
    // a recent fault is a reliable "the stack is in trouble" signal, not mere
    // device absence.
    void onDe1LinkFault(const QString& kind);
    // Surface a DE1 BLE error to the UI. DE1Device::errorOccurred was previously
    // a dead-end signal — nothing consumed it — so DE1 connection problems
    // (including the "service not found … try toggling Bluetooth off/on" hint)
    // never reached the user; only scale + scan errors did. Forwarded here so
    // the same QML error dialog shows DE1 faults too, debounced to once per
    // distinct message until the DE1 next connects (the reconnect ladder would
    // otherwise pop the same "Connection error" dialog every ~60s).
    void onDe1Error(const QString& error);
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
    // Emitted when the user selects the synthetic USB scale entry (transport
    // "usb") in the discovered list. Unlike BLE/WiFi this does NOT carry a
    // device — the USB connect goes through UsbScaleManager::connectToScale(),
    // which creates + opens the UsbDecentScale and emits its own scaleDiscovered.
    void usbConnectRequested();
    void errorOccurred(const QString& error);
    void de1LogMessage(const QString& message);
    void scaleLogMessage(const QString& message);
    void flowScaleFallback();  // Emitted when no physical scale found, using FlowScale (gated to fire once per saved-scale cycle so the "No Scale Found" dialog doesn't re-show on every retry)
    void scaleRetryNeeded();   // Emitted on EVERY connection-failure path (including the post-WiFi→BLE-fallback give-up), regardless of the flowScaleFallback gate, so the persistent reconnect ladder in main.cpp survives the scale-type-change timer stop. Don't bind UI to this — it's for re-arming the retry timer only.
    void scaleDisconnected();  // Emitted when physical scale disconnects
    void scaleConnected();     // Emitted when a physical scale (re)connects — lets the UI dismiss the scale-disconnect / no-scale notice
    void scanStarted();  // Emitted when BLE scan actually begins
    // Emitted when a saved WiFi scale fails to connect within the connection
    // timeout and BLEManager has started a BLE scan as a fallback. UI binds
    // this to a toast/banner so the user knows what's happening.
    void wifiUnreachableFallingBackToBle(const QString& hostname);
    // Emitted when a manual "Add WiFi Scale" connect attempt fails to verify
    // (timeout without HDS recognition, or socket error before any frame).
    // The QML layer binds this to a user-visible dialog so a typo / wrong IP
    // doesn't silently strand the user on a phantom WiFi scale. The address
    // is NOT persisted as the saved primary in this case (see main.cpp's
    // scaleDiscovered handler — manual entries defer persistence until the
    // scale is recognized as HDS).
    void manualWifiValidationFailed(const QString& hostnameOrIp);
    // Emitted when a manual "Add WiFi Scale" connect attempt succeeds (the
    // WS endpoint validated as HDS). main.cpp uses this to commit the deferred
    // persistence (addKnownScale + setPrimaryScale + setSavedScaleAddress).
    void manualWifiValidationSucceeded(const QString& hostnameOrIp);
    // Result of probeMdnsForManualEntry: emitted at most once per probe with
    // the discovered hostname + IP if an HDS replied to the mDNS query.
    void manualWifiMdnsDiscovered(const QString& hostname, const QString& ip);
    // Always fired when probeMdnsForManualEntry finishes (whether or not the
    // scale was found). QML uses this to drop a "Searching..." indicator.
    void manualWifiMdnsProbeFinished();
    // Result of a probeWifiPrimaryReachable() call (emitted at most once per
    // probe — zero times if a later probeWifiPrimaryReachable() supersedes it
    // via cancelWifiProbe()). main.cpp switches to the WiFi primary when reachable.
    void wifiPrimaryReachable(bool reachable);
    void disabledChanged();
    void disconnectScaleRequested();  // Emitted when switching to a different scale, BLE is disabled, or saved scale is cleared
    void refractometersChanged();
    void refractometerConnectedChanged();
    void refractometerDiscovered(const QBluetoothDeviceInfo& device);
    void disconnectRefractometerRequested();
    // Emitted when the review-page refractometer hunt turns on/off. The R2 is
    // only pursued while the hunt is active, so main.cpp arms the persistent
    // reconnect tick on activation (giving the hunt a backoff-paced recovery
    // path if the scan chain dies, e.g. via onScanError) and stops it on
    // deactivation. The scale's reconnect is independent and unaffected.
    void refractometerHuntChanged(bool active);
    void linuxBlueZCacheHintNeeded();  // Request the BlueZ-cache recovery dialog (Linux, caps OK).
    // Emitted when an automatic BLE-adapter power-cycle begins / completes
    // (#1309). main.cpp uses bleStackRecovered() to reset the DE1 reconnect
    // budget and kick a fresh reconnect once the adapter is back, mirroring the
    // AutoWake re-arm path. bleStackRecoveryStarted() is informational (UI/log).
    void bleStackRecoveryStarted();
    void bleStackRecovered();


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
    // Reset the caller-set scan-request flags when a requested scan will not
    // start (Bluetooth off, permission denied). Only finished/error/stop clear
    // them otherwise, and none of those fire for a scan that never began.
    void clearScanRequestFlags();
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
    // Abort a foreground scale direct-connect that hasn't completed, tearing
    // down the parked QLowEnergyController so it can't hold the Android BLE
    // stack in Connecting for the full ~30s supervision timeout (issue #1303).
    // No-op unless a direct connect is in progress and the scale isn't already
    // connected. The parallel scan keeps running, so a present scale still
    // auto-connects when it's seen advertising.
    void abortScaleDirectConnectIfPending(const QString& reason);
    // How long a foreground direct-connect may sit in Connecting before we abort
    // it and fall back to the scan (mirrors de1app's ~4s ble-close-then-scan).
    static constexpr int kScaleDirectConnectAbortMs = 4000;
    // Tear down any in-flight WiFi-primary reachability probe (socket + timeout
    // timer) without emitting a result. Safe to call when no probe is active.
    void cancelWifiProbe();

    // --- BLE-stack-wedge auto-recovery (#1309) ---------------------------------
    // Re-evaluate whether the BLE stack is wedged and, if so, trigger an adapter
    // power-cycle. Called from the two failure heartbeats (de1LinkFault and the
    // scale connection timeout). `reason` is for the debug log only.
    void evaluateBleWedge(const QString& reason);
    // Power-cycle the Bluetooth adapter to clear a wedged stack. Android-only
    // (the only platform with the wedge, and the only one where a non-privileged
    // app can toggle the adapter — silently on API ≤ 32, via a system consent
    // dialog on 33+). No-op on other platforms. Respects a user-disabled
    // adapter and a per-cycle backoff.
    void maybeRecoverWedgedStack(const QString& reason);
    // Terminate an in-flight adapter power-cycle. `adapterOn` == the adapter is
    // confirmed back up: clears recovery state, re-arms DE1 + scale reconnect,
    // and emits bleStackRecovered(). `adapterOn` == false means we could not
    // bring the radio back: we surface an actionable error (never leave BT off
    // silently) and flag it so the next attempt powers it on rather than
    // mistaking it for a user-disabled adapter — and we do NOT claim recovery.
    void finishAdapterRecovery(bool adapterOn);
    // Turn the Bluetooth adapter on/off for the wedge power-cycle. Android-only:
    // QBluetoothLocalDevice has NO powerOff() (it exists on neither Android nor
    // macOS in this Qt build; powerOn() also routes through a consent dialog), so
    // we toggle the framework adapter directly via JNI — BluetoothAdapter
    // .disable()/enable(), the silent path on API ≤ 32. No-op on every other
    // platform (wedge recovery is Android-only — see maybeRecoverWedgedStack).
    void setAdapterPower(bool on);
    // True for ≥45s of sustained both-links-down + recent DE1 controller fault
    // before we treat it as a wedge — avoids cycling on a one-off blip.
    static constexpr int kWedgeConfirmMs = 45 * 1000;
    // A DE1 controller fault older than this no longer counts as "the stack is
    // in trouble". Sized above the slow DE1 reconnect cadence (main.cpp) so a
    // persistently-wedged stack stays flagged between slow retries.
    static constexpr int kWedgeFaultFreshnessMs = 7 * 60 * 1000;
    // Minimum gap between automatic adapter power-cycles.
    static constexpr int kAdapterRecoveryBackoffMs = 5 * 60 * 1000;
    // Fail-safe: if powerOff() never reports HostPoweredOff, force powerOn()
    // after this so a wedged adapter is never left switched off.
    static constexpr int kAdapterRecoverySafetyMs = 10 * 1000;

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
    // Dedicated probe instance for the manual "Add WiFi Scale" flow. Keeps the
    // UX-only mDNS probe separate from m_wifiDiscovery (which carries an
    // auto-connect-to-saved-primary handler). Lazy-created on first call.
    WifiScaleDiscovery* m_manualEntryDiscovery = nullptr;
    // Set true when the current manual-entry probe fires scaleFound; consumed
    // by probeFinished to decide whether to log "no responder" — the probe
    // doesn't carry a "found anything" return code, so we have to track it
    // out of band.
    bool m_manualEntryFoundThisProbe = false;
    // Non-disruptive WiFi-primary HDS identity probe: a per-probe WebSocket +
    // timeout timer (both owned, recreated each probe and torn down on the first
    // of valid-frame / error / timeout so exactly one wifiPrimaryReachable() is
    // emitted). The probe opens ws://<ip>/snapshot and requires an HDS-shaped
    // frame within the timeout — a bare TCP connect on port 80 was too permissive
    // (any LAN device listening on 80 passed it; see probe impl for the bug).
    QWebSocket* m_wifiProbeWebSocket = nullptr;
    QTimer* m_wifiProbeTimer = nullptr;
    TranslationManager* m_translationManager = nullptr;  // For i18n of user-visible error strings
    // Helper: translate `key` with `fallback`, or just return `fallback` if no
    // TranslationManager has been wired. Use ONLY for user-visible strings
    // (errorOccurred payloads, dialog text). Diagnostic logs stay in English.
    QString translateUiString(const QString& key, const QString& fallback) const;
    // WiFi-to-BLE fallback: set when m_scaleConnectionTimer fires for a saved
    // WiFi scale and we start a BLE scan as a fallback. Lets onDeviceDiscovered
    // auto-connect to a discovered Decent BLE scale even though the saved
    // address is a WiFi one. Cleared once a scale connects.
    bool m_wifiFallbackToBleActive = false;
    // True while a manually-entered WiFi scale (connectToWifiScale, the "Add WiFi
    // Scale" dialog) connect attempt is pending. Tells onScaleConnectionTimeout to
    // report "Not found" directly instead of starting a WiFi→BLE fallback scan —
    // the user asked for a specific WiFi address, so we don't silently switch
    // transports. Set when the attempt starts; cleared on connect success, on
    // timeout (consumed), and reset when a non-manual reconnect begins.
    bool m_manualWifiConnect = false;
    // Debounces user-visible scan-error popups. Without this, repeated scan
    // attempts (refractometer auto-reconnect ticks, scale reconnect retries)
    // would re-fire the same error toast indefinitely. We pop a given error
    // string at most once between successful connects.
    QString m_lastScanErrorShown;
    // Same debounce, for DE1 errors forwarded via onDe1Error(): show each
    // distinct message at most once until the DE1 connects (cleared in
    // noteDe1Connected()). Separate from m_lastScanErrorShown so a DE1 fault
    // and a scan error don't suppress each other.
    QString m_lastDe1ErrorShown;
    // True once ANY BLE device (DE1 or scale) has been successfully seen this
    // session. Used to suppress transient QBluetoothDeviceDiscoveryAgent
    // MissingPermissionsError reports that fire on macOS Tahoe + Qt 6.11
    // after app-resume — CoreBluetooth's permission grant takes a moment to
    // re-establish post-suspend, even though the user-level grant is intact.
    // If we've ever had BLE success this session, treat MissingPermissionsError
    // as a transient hiccup (log only). If BLE has NEVER worked, it might be
    // a real permission denial → still pop the dialog (existing behavior).
    bool m_anyBleSuccessThisSession = false;
    bool m_scanning = false;
    bool m_permissionRequested = false;
    bool m_scanningForScales = false;  // True when scanning for scales (user or auto-reconnect)
    bool m_userInitiatedScaleScan = false;  // True only for user-initiated scan (show all scales)
    bool m_refractometerHunt = false;  // Review page open: keep scans back-to-back until R2 connects
    bool m_scaleConnectionFailed = false;
    ScaleDevice* m_scaleDevice = nullptr;
    QTimer* m_scaleConnectionTimer = nullptr;
    // Bounds a foreground direct-connect to ~4s (see kScaleDirectConnectAbortMs).
    // A cancellable member (not a fire-and-forget singleShot) so a new connect
    // attempt or a successful connect stops any stale pending abort.
    QTimer* m_scaleDirectAbortTimer = nullptr;
    bool m_de1ServiceDiscoveryActive = false;

    // --- BLE-stack-wedge auto-recovery state (#1309) ---------------------------
    bool m_de1Connected = false;            // Fed by noteDe1Connected() from main.cpp
    QDateTime m_lastDe1FaultTime;           // Last DE1 controller fault (onDe1LinkFault)
    QDateTime m_wedgeSince;                 // When the wedge condition first held (invalid = not currently wedged)
    bool m_adapterRecoveryInFlight = false; // A powerOff→powerOn cycle is underway
    bool m_recoverySawPoweredOff = false;   // powerOff took effect; now awaiting power-on
    bool m_recoveryLeftAdapterOff = false;  // A cycle ended with the adapter still off (our doing, not the user's)
    QDateTime m_lastAdapterRecovery;        // For the inter-cycle backoff
    int m_adapterRecoveryCount = 0;         // Diagnostic: cycles this session
    QTimer* m_adapterRecoverySafetyTimer = nullptr;  // Fail-safe watchdog for each power-cycle leg

    // Saved scale for direct wake connection
    QString m_savedScaleAddress;
    QString m_savedScaleType;
    QString m_savedScaleName;

    // Hostname carried with the most recent scaleDiscovered emission for a
    // WiFi scale (so main.cpp can route the connect after the factory creates
    // the driver). Set immediately before emitting, read immediately after.
    QString m_pendingWifiHostname;
    // Companion to m_pendingWifiHostname — see pendingWifiResolvedIp().
    QString m_pendingWifiResolvedIp;

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

    // Serialize the scale's BLE direct-connect behind the DE1's: two concurrent
    // GATT connects collide on the Android stack (the scale connect dies when
    // the DE1's completes). Set while a DE1 direct-wake is in flight; the scale
    // connect defers until onDe1ConnectionSettled() or the 15 s cap below — the
    // cap still connects the scale when no DE1 is present (debugging).
    bool m_de1DirectConnectInFlight = false;
    bool m_scaleConnectDeferred = false;
    QTimer* m_de1WaitTimer = nullptr;

    // Refractometer
    QList<QBluetoothDeviceInfo> m_refractometerDevices;
    QString m_savedRefractometerAddress;
    QString m_savedRefractometerName;
    RefractometerDevice* m_refractometerDevice = nullptr;

    // Scale debug log
    QStringList m_scaleLogMessages;
    QString m_scaleLogFilePath;
    void writeScaleLogToFile();

    static BLEManager* s_instance;
};
