#pragma once

#include <QObject>

#include <atomic>
#include <memory>
#include <QStringList>
#include <QString>

#include "wifiscaleresult.h"

class QTimer;

/**
 * On-demand discovery of WiFi scales on the LAN. Two mechanisms, run together
 * by a user-initiated scan:
 *
 *  - browse(): DNS-SD service browse for `_decentscale._tcp`. Finds every
 *    advertising scale regardless of its mDNS name, which is the only way to
 *    see a scale the user has renamed. Needs openscale >= v3.0.9.
 *  - probe(): direct A-record lookup of specific hostnames. Covers older
 *    firmware, which advertises no service at all.
 *
 * Neither does background work: nothing runs until a caller asks, and a browse
 * stops when the scan cycle that started it ends.
 *
 * Results arrive incrementally via resultFound() as each scale resolves, rather
 * than in one batch at the end — a live scale typically answers in well under a
 * second while the browse keeps running to its deadline.
 *
 * On non-Android the A-record path uses QHostInfo (the OS resolver speaks
 * mDNS); on Android it uses MdnsResolver on a worker thread, since Android's
 * getaddrinfo does not resolve ".local".
 *
 * The browse always goes through MdnsResolver::browseService() on every
 * platform; which backend that picks (system Bonjour vs the mjansson raw-socket
 * implementation) is decided inside. QHostInfo cannot browse on ANY platform —
 * it resolves a name you already know. See mdnsresolver.h.
 */
class WifiScaleDiscovery : public QObject {
    Q_OBJECT

public:
    explicit WifiScaleDiscovery(QObject* parent = nullptr);
    ~WifiScaleDiscovery() override;

    static constexpr int kDefaultTimeoutMs = 2000;

    // The DNS-SD service openscale advertises (v3.0.9+).
    static constexpr const char* kServiceType = "_decentscale._tcp.local";

    // Hostnames tried by the A-record fallback when no specific name is known.
    //
    // "hds" is the firmware default. "hds-2"/"hds-3" are a HEURISTIC ABOUT USER
    // HABIT, not protocol: nothing generates them. Neither openscale nor esp-idf
    // renames on collision — esp-idf only detects collisions — so a second scale
    // never becomes "hds-2.local" by itself. They are here because "hds-2" is a
    // legal name (mdnsNameNormalize allows a-z 0-9 '-') and the obvious thing a
    // person types for their second scale. Do not extend this list believing it
    // mirrors firmware behaviour; it does not.
    static QStringList defaultFallbackHostnames();

    /**
     * Resolve specific hostnames to addresses. Cancels any previous in-flight
     * probe. Emits resultFound() per hostname that resolves, then
     * probeFinished() exactly once when all have completed or timed out.
     */
    Q_INVOKABLE void probe(const QStringList& hostnames, int timeoutMs = kDefaultTimeoutMs);

    /** Convenience single-name overload. */
    Q_INVOKABLE void probe(const QString& hostname, int timeoutMs = kDefaultTimeoutMs);

    /**
     * Start a DNS-SD browse. Runs until stopBrowse() or `timeoutMs` elapses,
     * emitting resultFound() as instances resolve, then browseFinished().
     *
     * Only fully-resolved instances are reported. A browse routinely returns
     * instance names that never resolve — stale registrations from a device that
     * rebooted or was renamed without sending a goodbye — and those are dropped
     * rather than shown. Observed: four instances for two live scales.
     */
    Q_INVOKABLE void browse(int timeoutMs = 15000);

    /** Stop an in-flight browse. Safe to call when none is running. */
    Q_INVOKABLE void stopBrowse();

    /** True iff an A-record probe is currently in flight. */
    bool isProbing() const { return m_outstanding > 0; }

    /** True iff a browse is currently in flight. */
    bool isBrowsing() const { return m_browseInFlight; }

signals:
    /**
     * One discovered scale. May fire several times per scan — once per scale,
     * as each resolves. A result is never retracted: callers append to their
     * list and rebuild it on the next scan.
     */
    void resultFound(const WifiScaleResult& result);

    /**
     * An A-record probe finished. `ran` distinguishes "probed and found
     * nothing" from "never probed" (e.g. cancelled before starting), which
     * callers need in order to log the difference rather than conflating a
     * silent network with a skipped step.
     */
    void probeFinished(bool ran);

    /**
     * A browse finished. `ran` is false when the browse could not actually run
     * — no backend available, socket refused, Local Network denied — as opposed
     * to running and finding nothing. Those look identical in the device list,
     * so the difference has to travel with the signal.
     */
    void browseFinished(bool ran);

    // User-facing diagnostic stream — forwarded by BLEManager into the
    // shareable scale debug log, so an mDNS-layer failure (multicast filtered
    // by the AP, scale on the wrong SSID, captive portal) is visible in a
    // user-shared log rather than only in a console.
    void logMessage(const QString& message);

private:
    void cancelInFlight();
    void finishOneLookup();

    int m_outstanding = 0;      // A-record lookups still pending
    bool m_anyProbeRan = false; // whether the current probe actually started
    QList<int> m_lookupIds;     // QHostInfo lookup ids (non-Android)
    QTimer* m_timeoutTimer = nullptr;

    // Same role as m_browseCancel below, for the Android A-record workers.
    // Without it, cancelInFlight() bumped the generation so the RESULT was
    // discarded, but three pool threads still blocked for the full timeout —
    // and app quit waits on the pool.
    std::shared_ptr<std::atomic<bool>> m_probeCancel;

    bool m_browseInFlight = false;
    int m_browseGeneration = 0;
    // Polled by the blocking worker so stopBrowse() can actually stop it.
    // Without this the worker holds a QThreadPool thread for its full deadline,
    // and ~QCoreApplication's unconditional waitForDone() turns that into a
    // multi-second hang on quit with the UI already gone.
    std::shared_ptr<std::atomic<bool>> m_browseCancel;

    // Monotonically increasing generation, bumped by cancelInFlight(), so a
    // late worker result from a cancelled or timed-out probe is dropped. The
    // blocking mDNS worker cannot be interrupted mid-query, so it always
    // finishes on its own and its result must be discarded by this check.
    int m_probeGeneration = 0;
};
