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
#include <QPointer>
#include <atomic>

#include "blecapability.h"
#include "bledeviceid.h"
#include "network/wifiscaleresult.h"

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
    // WiFi entries discovered by DNS-SD carry the endpoint the scale actually
    // advertised, rather than us assuming :80/snapshot. Defaults match the
    // firmware's current advertisement, so a scale found by the A-record
    // fallback (which has no TXT data) still connects.
    quint16 wsPort = 80;
    QString wsPath = QStringLiteral("/snapshot");
    QString resolvedIp;           // WiFi entries only: the IP WifiScaleDiscovery's mDNS
                                   // query resolved `address`'s hostname to. Empty for BLE/USB.
                                   // Lets connectToScale() seed DecentScaleWifi's IP cache so
                                   // the connect dials the already-known IP instead of making
                                   // Qt's own resolver re-resolve ".local" (unreliable on
                                   // non-Android — see connectToScale()).
};

// getDeviceIdentifier() / deviceIdentifiersMatch() moved to ble/bledeviceid.h so
// layers below BLEManager can use them instead of hand-copying the expression.
// Still reachable through this header for every existing includer.

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
    // Disable DE1 BLE operations (DE1 simulator mode — "no machine attached").
    // Deliberately does NOT gate scale connectivity: the scale simulator is a
    // separate user-facing switch (see setScaleSimulated). Scale scanning has
    // always been allowed here; scale *connecting* used to be blocked by this
    // flag too, which meant running the DE1 simulator silently disabled real
    // scales even with the Simulated Scale switch off.
    void setDisabled(bool disabled);
    bool isScaleSimulated() const { return m_scaleSimulated; }
    // True when a simulated scale is actually driving the weight stream, in
    // which case connecting a real one would fight it. This is the only place
    // the SIMULATOR blocks a real scale connect — other, unrelated guards
    // (Bluetooth powered off, an already-connected scale, a non-dialable saved
    // address) apply independently.
    //
    // NOT simply the "Simulated Scale" setting. main.cpp composes this from
    // simulationMode() AND simulatedScaleEnabled(), because the SimulatedScale
    // object is only constructed under simulation mode and simulatedScaleEnabled
    // defaults to true — reading that setting alone would block every real scale
    // on builds that have no simulated scale at all. Callers must not
    // re-derive it from settings; take it from here.
    void setScaleSimulated(bool simulated);
    QVariantList discoveredDevices() const;
    QVariantList discoveredScales() const;
    bool scaleConnectionFailed() const { return m_scaleConnectionFailed; }
    bool hasSavedScale() const { return !m_savedScaleAddress.isEmpty(); }
    // True when the saved primary is the debug simulator's synthetic entry
    // ("sim:..."), which main.cpp promotes to primary when no real scale has
    // ever been paired. It is NOT a connectable address: every real connect
    // path would treat it as a BLE MAC and dial nonsense. Callers that act on
    // "a scale is saved" must exclude it — hasSavedScale() alone is true for it.
    bool savedScaleIsSimulated() const {
        return m_savedScaleAddress.startsWith(QStringLiteral("sim:"), Qt::CaseInsensitive);
    }

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
    // Display name of the WiFi scale being connected to, e.g.
    // "Half Decent Scale (hdstest) (WiFi)". Derived from the hostname, so it is
    // available even on a saved-scale reconnect where no scan has run. Without
    // it every WiFi scale is saved under one identical label and a user with
    // two of them cannot tell which is connected.
    QString pendingWifiDisplayName() const;

    // WebSocket endpoint advertised by the scale being connected to. Defaults
    // to the firmware's :80/snapshot when discovery had no TXT data to go on.
    quint16 pendingWifiPort() const { return m_pendingWifiPort; }
    QString pendingWifiPath() const { return m_pendingWifiPath; }
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

    // Called by main.cpp when a scan-initiated USB probe pass completes, so the
    // composite `scanning` property can drop its USB third. Safe to call when
    // no scan-initiated probe is outstanding.
    void onUsbProbeFinished();

    // Set every field of the pending-WiFi-connect state together.
    //
    // Exists because they must move as a unit: an earlier version assigned
    // m_pendingWifiHostname at five sites but the endpoint at only one, so four
    // paths silently dialled the PREVIOUS connect's port and path. Funnelling
    // through one call makes forgetting a field impossible rather than invisible.
    void setPendingWifiConnect(const QString& hostname,
                               const QString& resolvedIp = QString(),
                               quint16 port = 80,
                               const QString& path = QStringLiteral("/snapshot"));

    // WiFi-only discovery, without the BLE scan. Exists for the MCP diagnostic
    // tools: the mjansson browse is the backend Android and Windows/Linux use,
    // but it is developed on a Mac that ships the Bonjour backend, so being able
    // to drive either one against the same LAN is the only way to compare them
    // without deploying to a tablet.
    Q_INVOKABLE void browseWifiScales(int timeoutMs);

    // Raw results of the most recent WiFi discovery, for the MCP diagnostic
    // tools. Each entry carries the DNS-SD detail the discovered-scales list
    // flattens away (instance name, TXT name, port, path, firmware, source).
    QVariantList wifiScaleResults() const;

    // Whether the last browse / A-record probe actually ran, as opposed to
    // running and finding nothing. Without these an empty result list is
    // ambiguous, and the ambiguity hides the failure that matters most on this
    // codebase: macOS silently denying Local Network to the app.
    bool lastWifiBrowseRan() const { return m_lastWifiBrowseRan; }
    bool lastWifiProbeRan() const { return m_lastWifiProbeRan; }

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

    // Share the system log (debug.log) — the only log share there is now.
    // clearScaleLog(), shareScaleLog(), getScaleLogPath() and appendScaleLog() are
    // gone with the private scale-log channel; see the note where they used to be
    // defined in blemanager.cpp. Clearing is now view-local (SubsystemLogView).
    Q_INVOKABLE void shareSystemLog();

    // Public, unlike their scale siblings below, because BLEManager is not the
    // only narrator of this subsystem: main.cpp owns the Refractometer instance
    // and drives its creation, teardown and reconnect tick. Those lines are part
    // of the same story as the ones in here and must carry the same marker and
    // land in the same view, so they go through the same two tiers rather than a
    // second copy of the shape. See the tier note under `private:`.
    //
    // `source` is the bracketed source tag and therefore MUST name who actually
    // wrote the line — it is defaulted only for this class's own calls. A shared
    // forwarder that hard-coded its own class name would stamp main.cpp's
    // lifecycle lines "BLEManager", and nothing in the log would contradict it.
    void refractometerDebug(const QString& message,
                            const QString& source = QStringLiteral("BLEManager"));
    void refractometerInfo(const QString& message,
                           const QString& source = QStringLiteral("BLEManager"));

    // How the scale subsystem's narrative is logged. Pick by AUDIENCE, per the
    // tier rules in core/logtags.h:
    //
    //   scaleDebug  developer detail — periodic probes, ignored transients,
    //               mechanism notes. Below what the connections view shows.
    //   scaleInfo   the narrative a user needs: scanning, found, connecting,
    //               connected, disconnected, transport fallback, reconnects.
    //   scaleWarn   problems: refusals, timeouts, unreachable, teardowns.
    //
    // Public for the same reason as the refractometer pair above: main.cpp drives
    // the scale reconnect ladder and its lines belong to this subsystem's story.
    // They used to go through appendScaleLog(), which reached only the in-app view
    // — so the ladder was absent from every submitted log. `source` must name who
    // actually wrote the line; it is defaulted only for this class's own calls.
    //
    // BLEManager cannot use the SCALE_* macros directly: those `emit
    // logMessage(...)`, which this class does not have.
    void scaleDebug(const QString& message,
                    const QString& source = QStringLiteral("BLEManager"));
    void scaleInfo(const QString& message,
                   const QString& source = QStringLiteral("BLEManager"));
    void scaleWarn(const QString& message,
                   const QString& source = QStringLiteral("BLEManager"));

    // The same three tiers for the DE1 half, carrying [DE1] instead of [Scale].
    //
    // BLEManager narrates the machine too — permissions, scan lifecycle, "found
    // DE1", direct wake. MOST of those lines went only to de1LogMessage, i.e.
    // only to the connections-page window, so they were absent from every
    // submitted log and the machine's discovery story could not be read after the
    // fact. Four DID also reach stderr — "Found DE1", the two direct-wake lines
    // and the scan error — but each did so in DIFFERENT WORDS from its emitted
    // twin, which is the drift these helpers exist to make impossible. Both
    // problems have the one fix: log once, at a tier, from one call.
    // `source` defaulted the same way as scaleDebug/Info/Warn above, and for the
    // same reason: main.cpp drives the DE1 reconnect ladder through these
    // forwarders too, and its lines must not be stamped "BLEManager". Public for
    // the same reason scaleDebug/Info/Warn are — main.cpp calls it directly.
    void de1Debug(const QString& message,
                  const QString& source = QStringLiteral("BLEManager"));
    void de1Info(const QString& message,
                 const QString& source = QStringLiteral("BLEManager"));
    void de1Warn(const QString& message,
                 const QString& source = QStringLiteral("BLEManager"));

private:
    //
    // refractometerDebug/refractometerInfo (declared public above) exist because
    // BLEManager narrates both subsystems and a refractometer line must carry
    // [Refractometer], not [Scale]. There are two tiers rather than the INFO-only
    // one this class used to have because the refractometer's own story — hunt
    // on/off, instance churn, why an auto-reconnect did nothing — has a developer
    // half too, and that half was written as bare `qDebug() << "[R2-diag] ..."`:
    // 17 lines under an ad-hoc debug-session prefix that no registered marker
    // matched, so a [Refractometer] search returned the driver's packets and NOT
    // the connect/churn story they were added to explain.
    //
    // There is deliberately no refractometerWarn: nothing BLEManager or main.cpp
    // reports about this device is a problem — the failures all belong to the
    // driver, which has R2_WARN. Add one when a call site needs it, not before.

    // A connect failure that REPEATS while nothing changes. Logs at its normal
    // tier for the first few, then drops to DEBUG until the next successful
    // connect.
    //
    // The failure is real every time, but the reconnect ladder retries forever,
    // so at a flat WARN an absent scale produced 46 "connection timeout" and 24
    // "unreachable" warnings in one 48 h capture — enough to train a reader to
    // skim past the tier that is supposed to mean "look here". The first ones
    // carry the diagnosis; the rest only carry "still absent", which the ladder
    // lines already say.
    //
    // `tier` is the level the message would carry if it were not repeating, so
    // the budget suppresses WITHOUT re-tiering: a WARN-only budget would have to
    // promote any narrative routed through it, making the quiet lines loud.
    //
    // Accuracy note, because the first version of this comment justified `tier`
    // with lines that do not use it: it cited the WiFi driver's "resolving again"
    // and "dialing remembered address" as the INFO half of a failing cycle. Those
    // are DEBUG (WIFI_LOG), were demoted in the same change that wrote this, and
    // do not go through the sink at all. RepeatTier::Info is therefore reachable
    // in source (main.cpp translates the sink's bool) but never produced at
    // runtime — the sole sink call passes warn=true. The enum is kept because the
    // no-re-tiering property is the right design and a second caller is cheap to
    // add; it is NOT kept because something currently needs it. Wire a narrative
    // line through the sink or delete the enum, but do not read this paragraph as
    // evidence that the INFO path is exercised.
    //
    // `source` names who wrote the line, so a driver routing through this class's
    // budget still reads as the driver. Without it the driver's suppressed lines
    // would be stamped "BLEManager" and a reader would go looking in the wrong
    // file.
    // Public for the same reason scaleDebug/Info/Warn are: code outside this
    // class emits lines belonging to this subsystem's failing cycle, and the
    // budget only works if it sees ALL of them.
    //
    // That was the defect. The manager's three ladder lines were budgeted and
    // DecentScaleWifi's three were not, so past the budget the manager fell
    // silent while the driver kept warning every 60 s — a repeating fragment
    // carrying neither the attempt number nor the outcome. Noisier than
    // suppressing nothing and less useful than suppressing everything.
    //
    // ONE store, deliberately: a second counter in the driver would be a second
    // policy, and resetRepeatFailureBudget() would not reach it, so a scale that
    // reconnected would re-arm half its messages.
    //
    // Two overloads, and the split is the point: the defaults that are correct
    // for this class are WRONG for everyone else, and a default cannot tell which
    // caller it has. While `source` defaulted on the public signature,
    // `scaleRepeatFailure(msg)` from any other file compiled cleanly and stamped
    // the line "BLEManager" — sending a reader to the wrong file, which is
    // verbatim the hazard logtags.h documents for a shared forwarder that
    // hard-codes its own name. The default was safe only while this was private,
    // and it stopped being private in the same change that kept it.
    //
    // So: the convenience form is private and means "this class wrote it"; every
    // caller outside states both tier and source, because outside this class
    // neither has a defensible default.
private:
    void scaleRepeatFailure(const QString& message);

public:
    enum class RepeatTier { Info, Warn };
    void scaleRepeatFailure(const QString& message,
                            RepeatTier tier,
                            const QString& source);
private:
    // The DE1 equivalent, sharing the budget map. Same shape, [DE1] marker.
    void de1RepeatFailure(const QString& message);
    // Clears every message's warn budget. Call on a successful connect (either
    // device) and on any fresh user-initiated attempt — see the definition for
    // why the latter is not optional.
    //
    // Coarse on purpose: it clears both subsystems' budgets. Re-arming a warning
    // that did not need re-arming costs one line; failing to re-arm one costs a
    // silent failure, so the coarse direction is the safe one.
    void resetRepeatFailureBudget();
    // Keyed per MESSAGE, deliberately: a subsystem-wide counter suppressed a
    // genuinely NEW failure arriving mid-run, because an unrelated repeat had
    // already spent the budget.
    QHash<QString, int> m_repeatFailureCounts;
    static constexpr int kScaleFailuresAtWarn = 3;

public:

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
    // distinct message until the DE1 next connects (a failing ladder would
    // otherwise re-pop the same dialog on every attempt). Controller errors never
    // get this far — the transport keeps that path log-only, because the ladder
    // recovers them and the resulting dialog said nothing useful (#1658).
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

    // Emitted when a scan wants USB probed alongside BLE and WiFi. main.cpp
    // owns UsbScaleManager, so BLEManager asks rather than calling directly.
    void usbProbeRequested();
    void errorOccurred(const QString& error);
    // No de1LogMessage / scaleLogMessage. Both existed only to feed the two
    // connections-page views, which now read the system log directly, and being
    // view-only was their defect: everything sent through them was absent from
    // every log a user submitted. Their content reaches the log through the tier
    // helpers instead.
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
    // Emitted on BOTH edges of setScaleSimulated — see its implementation for
    // why the falling edge matters (nothing else re-arms the scale reconnect).
    void scaleSimulatedChanged();
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
    // Lazy-create m_wifiDiscovery once with a single unified resultFound
    // handler. Both scan-for-devices and try-direct-connect paths call this
    // before invoking probe(); registering the lambda only on first call
    // (previously done at TWO sites with DIFFERENT lambdas — whichever ran
    // first wiped out the other, breaking either the list-populate path or
    // the auto-reconnect path depending on order).
    void ensureWifiDiscovery();
    // Lazy-create m_reconnectDiscovery — the browse the RECONNECT ladder uses,
    // deliberately a second instance rather than m_wifiDiscovery. Two reasons,
    // both load-bearing:
    //  - browse() calls stopBrowse() first, so sharing would let a reconnect
    //    tick cancel a scan the user just started — the very action a user
    //    takes when their scale is missing.
    //  - isScanning() folds m_wifiDiscovery->isBrowsing() into the composite
    //    property behind the Scan button. A shared instance would make the
    //    button read "Scanning..." on every reconnect tick.
    // Same reasoning that split m_manualEntryDiscovery out; see that comment.
    void ensureReconnectDiscovery();

public:
    // The two invariants of the reconnect browse, as pure predicates.
    //
    // They are static and header-inline ON PURPOSE: BLEManager cannot be
    // constructed in the test suite (it owns the BLE stack), so the only way
    // these rules become assertable is to state them as functions of their
    // inputs. The alternative was a fault-injection harness to reach the
    // branches, which the project treats as a stop sign rather than a testing
    // problem. Everything else about the browse — timing, socket ownership —
    // stays untested here and rides on the field log instead.

    /// Whether a reconnect tick should open a browse. Three gates, in the order
    /// they matter: a saved WiFi primary must exist (otherwise this is the
    /// absolute no-background-discovery case), a direct attempt must already
    /// have failed (so the healthy cached-IP path stays silent), and no browse
    /// may already be running (re-browsing restarts the window a reply needs).
    static bool shouldBrowseOnReconnect(const QString& savedScaleAddress,
                                        bool directAttemptFailed,
                                        bool browseAlreadyRunning) {
        if (!savedScaleAddress.startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive))
            return false;
        if (!directAttemptFailed) return false;
        if (browseAlreadyRunning) return false;
        return true;
    }

    /// Whether a browsed instance IS the saved primary. This is the
    /// anti-substitution rule: a browse finds every scale on the LAN, and only
    /// an exact match on the saved address may be auto-connected. Never relax
    /// this to a "looks like a Decent scale" test.
    static bool browsedScaleIsSavedPrimary(const QString& hostname,
                                           const QString& savedScaleAddress) {
        if (savedScaleAddress.isEmpty() || hostname.isEmpty()) return false;
        const QString address = QStringLiteral("wifi:") + hostname;
        return address.compare(savedScaleAddress, Qt::CaseInsensitive) == 0;
    }

private:
    // Auto-connect a browsed instance when it IS the saved primary. One
    // implementation, called from both discovery handlers — the scan's and the
    // reconnect's. Not duplicated: this file already carries a bug report about
    // two dedupes that drifted apart and let a browse hit rewrite a row's
    // address out from under the saved-scale matcher.
    void maybeAutoConnectBrowsedScale(const WifiScaleResult& result);
    // Start the reconnect browse for the saved WiFi primary. No-op unless a
    // direct attempt has already failed (m_wifiDirectAttemptFailed).
    void startReconnectBrowseIfNeeded();
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
    // Dedicated browse instance for the reconnect ladder. See
    // ensureReconnectDiscovery() for why this is not m_wifiDiscovery. It is
    // deliberately absent from isScanning(), so a reconnect browse never
    // touches the Scan button.
    WifiScaleDiscovery* m_reconnectDiscovery = nullptr;
    // "A direct attempt for the saved WiFi scale has already failed." Set when
    // the connection timer gives up on a wifi: primary, cleared the moment a
    // scale connects. Gates the reconnect browse so the healthy path — cached
    // IP answers immediately — never puts multicast traffic on the network.
    //
    // An event flag, not a timer: the condition is "until a scale connects",
    // and that event is exactly what clears it.
    bool m_wifiDirectAttemptFailed = false;
    // How long the reconnect browse runs. Shorter than the 15 s user scan
    // because this repeats on the reconnect ladder rather than running once
    // per user action, so it converges across ticks instead of within one.
    // 5 s is the same budget kHdsResolveTimeoutMs gives the A-query, and is an
    // order of magnitude above the 362 ms in which a browse resolved this
    // scale in the field — the case this exists to recover.
    static constexpr int kReconnectBrowseTimeoutMs = 5000;
    // Set true when the current manual-entry probe fires resultFound; consumed
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
    quint16 m_pendingWifiPort = 80;
    QString m_pendingWifiPath = QStringLiteral("/snapshot");

    // True while a scan-initiated USB probe pass is outstanding. Feeds the
    // composite `scanning` property so "Scanning..." covers all three
    // transports. Set by scanForDevices(), cleared by the USB probeFinished
    // signal that main.cpp forwards in.
    bool m_usbProbeInFlight = false;

    // Every WiFi result seen in the current scan, keyed by normalized hostname
    // (see WifiScaleResultUtil::upsertByHostname — the address is DHCP-mutable
    // cache data and deliberately NOT the identity).
    // Kept because a row's label depends on the OTHER rows: DNS-SD suffixes
    // colliding instance names ("Half Decent Scale" / "Half Decent Scale-2"),
    // which tells the user nothing about which scale is which, so those rows
    // have to show their address too. That decision can't be made from one
    // result in isolation. Cleared at the start of each scan.
    QVector<WifiScaleResult> m_wifiResults;

    // Did the last browse / A-record probe actually run? An empty result list
    // means nothing on its own — "asked, nobody there" and "could not ask"
    // look identical in it. Start as true so a diagnostic run before any scan
    // does not report a failure that never happened.
    bool m_lastWifiBrowseRan = true;
    bool m_lastWifiProbeRan = true;

    // Rebuild the WiFi rows of m_scales from m_wifiResults. The rows are a
    // projection of that set, not a second collection kept in step with it.
    void rebuildWifiScaleRows();
    // Remove all WiFi rows. Called only at the start of a user-initiated scan —
    // see the note in the implementation about the refractometer tick.
    void clearWifiScaleRows();

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
    // Set from the "Simulated Scale" switch, NOT from DE1 simulation mode.
    // Gates the two real-scale connect paths (connectToSavedScale,
    // tryDirectConnectToScale) that used to consult m_disabled.
    bool m_scaleSimulated = false;

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
    // QPointer, not a raw pointer: we do not own this device and cannot see it
    // die. Same reason MachineState::m_scale and ShotTimingController::m_scale
    // are QPointer (noted at the end of main()) — house style, not a one-off.
    //
    // What made it necessary: main() used to declare its `refractometer`
    // unique_ptr AFTER the QML engine, breaking the convention stated at the
    // engine's own declaration ("declared before the engine ... so it outlives
    // the engine at scope unwind"). That has since been fixed — the declaration
    // now sits above the engine — so the specific crash below is no longer
    // reachable at app exit. This stays because it is the second line of
    // defence, and because it still covers the runtime recreate path (a new
    // device discovered while one is live) that ordering does nothing for.
    //
    // What the ordering bug looked like, so nobody reintroduces it: the device
    // died while the engine and every binding reading
    // `BLEManager.refractometerConnected` were still live: ~QObject emits
    // destroyed(), QML drops the `Refractometer` context property, and the
    // resulting binding re-evaluation lands in isRefractometerConnected() on an
    // object whose derived part is already gone. Through a raw pointer, that
    // call is a pure-virtual dispatch on a vptr already rewound to QObject's:
    // it indexes past the end of QObject's vtable into adjacent data and jumps
    // there. On macOS/arm64 that was a SIGBUS instruction-abort with the PC
    // inside QtCore's non-executable __DATA; other platforms may land on a
    // plausible-looking pointer and not trap at all, so "no crash here" is not
    // evidence the fault is gone.
    //
    // Why QPointer answers it: ~QObject stores 0 into sharedRefcount->strongref
    // before the `emit destroyed(this)` a few lines below (qtbase qobject.cpp,
    // verified against Qt 6.11.1), so the pointer already reads null when that
    // binding runs and the getter just reports "not connected".
    //
    // Bound worth knowing: this only covers the window from ~QObject onward. A
    // signal emitted from a concrete driver's OWN destructor body would still
    // find a non-null QPointer and a half-destroyed object. That window is
    // empty today — neither transport emits synchronously from
    // disconnectFromDevice() — but it is not something QPointer protects.
    QPointer<RefractometerDevice> m_refractometerDevice;

    // Handles for the two connections setRefractometerDevice() installs, so it
    // can sever exactly those. It must NOT use a blanket
    // disconnect(device, nullptr, this, nullptr): main.cpp also connects the
    // device's errorOccurred to our errorOccurred, and that has to survive until
    // the device is actually destroyed. Forget nulls the holder BEFORE disconnecting the device (that
    // order is load-bearing — see the timer-stop note on
    // disconnectRefractometerRequested in main.cpp), so a blanket disconnect
    // there silently swallowed every log line the disconnect itself produced.
    QMetaObject::Connection m_refractometerConnectedConn;
    QMetaObject::Connection m_refractometerDestroyedConn;

    // Last observe/enforce value we announced, so the mode is logged on
    // transition instead of on every settings load (it was 11 identical WARNs
    // in a 48 h capture). Not the mode itself — that is m_backoffMode.
    bool m_loggedObserveMode = false;

    static BLEManager* s_instance;
};
