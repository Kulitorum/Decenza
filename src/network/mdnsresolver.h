#pragma once

#include <QMap>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

/**
 * Direct mDNS client for platforms where we cannot use the system resolver for
 * what we need. Built on the header-only mjansson/mdns library.
 *
 * Two distinct jobs, with different platform coverage:
 *
 *  - resolveHostname(): ".local" hostname → IPv4. Needed on Android ONLY,
 *    because Android's stock resolver (getaddrinfo, used by
 *    QHostInfo::lookupHost) does not reliably resolve ".local" mDNS names — it
 *    returns NXDOMAIN. Everywhere else the OS resolver speaks mDNS (nss-mdns on
 *    Linux, Bonjour on macOS/iOS) and QHostInfo is the right call.
 *
 *  - browseService(): DNS-SD service enumeration. Needed on every non-Apple
 *    platform, because QHostInfo cannot browse at all — it resolves a name you
 *    already know and has no way to ask "what services are out there".
 *
 * Apple platforms DEFAULT to the system Bonjour APIs (DNSServiceBrowse),
 * because a raw multicast socket to 224.0.0.251 on iOS requires the
 * com.apple.developer.networking.multicast entitlement, which Apple grants only
 * by per-app application.
 *
 * Only iOS is compiled out — `#ifndef Q_OS_IOS` below, `if(NOT IOS)` in
 * CMakeLists.txt. macOS deliberately builds BOTH backends so the mjansson path
 * (what Android and Windows/Linux actually ship) can be exercised on the machine
 * it is developed on; see BrowseBackend. Do not narrow these guards to
 * `NOT APPLE` — that was the original shape and it removed that capability.
 *
 * Multicast reception on Android requires a held WifiManager.MulticastLock.
 * ShotServer acquires one for the whole app lifetime (start()→stop()), so the
 * process-wide lock is in effect; MqttClient relies on the same arrangement.
 */
namespace MdnsResolver {

/**
 * One DNS-SD service instance, fully resolved.
 *
 * Only instances whose SRV and address records were both obtained are ever
 * produced. A browse routinely returns instance names that never resolve —
 * stale registrations left behind when a device rebooted or was renamed
 * without sending a goodbye. On a network with two live Half Decent Scales,
 * four instances answered the PTR browse and only two resolved. A PTR hit is
 * not a device; a resolved instance is.
 */
struct ServiceInstance {
    // DNS-SD instance label with the service type stripped, e.g.
    // "Half Decent Scale (hdstest)". NOT unique: DNS-SD resolves instance-name
    // collisions by appending a suffix, so two unrenamed scales appear as
    // "Half Decent Scale" and "Half Decent Scale-2". Disambiguate with address.
    QString instanceName;
    QString hostname;   // SRV target, trailing dot stripped, e.g. "hdstest.local"
    QString address;    // resolved IPv4 dotted quad
    quint16 port = 0;
    // TXT key/value pairs, lowercased keys. EVERY key is optional — a Half
    // Decent Scale on firmware 3.1.12 publishes no "name" key at all despite
    // the firmware appearing to always set it. Never assume a key is present.
    QMap<QString, QString> txt;
};

/**
 * Why a resolveHostname() call produced nothing.
 *
 * The return value alone cannot say: "could not open a socket", "asked and
 * every send failed", and "asked fine, nobody answered" all collapse to an
 * empty string, and the caller then logs one guess for all three. On Android —
 * the platform that actually uses this path — the first two are real and have
 * completely different fixes (multicast lock / permissions vs. the scale being
 * asleep or on another SSID), so the difference has to reach the user's log.
 *
 * `error` is set only when the lookup could not be performed. An empty `error`
 * with an empty result means the query went out and nothing came back.
 */
struct ResolveStats {
    bool socketOpened = false;
    int queries = 0;        // A-record queries sent (including retransmits)
    int sendsOk = 0;        // of those, how many the socket accepted
    int recordsSeen = 0;    // any mDNS record reaching our socket
    int aRecordsSeen = 0;   // of those, A records
    QString error;
};

/**
 * Resolve `hostname` (e.g. "hds.local") to a dotted-quad IPv4 string via a
 * direct mDNS A-record query. Blocks up to `timeoutMs`. MUST be called off
 * the main thread. Returns an empty string on timeout / failure.
 *
 * `cancel`, if set, is polled between retransmits and ends the query early —
 * same reasoning as browseService(): this blocks a QThreadPool thread, and
 * ~QCoreApplication waits for the pool unconditionally.
 */
QString resolveHostname(const QString& hostname, int timeoutMs = 2000,
                        ResolveStats* stats = nullptr,
                        const std::atomic<bool>* cancel = nullptr);

/**
 * Browse for DNS-SD service instances of `serviceType` (e.g.
 * "_decentscale._tcp.local"). Blocks up to `timeoutMs`. MUST be called off the
 * main thread.
 *
 * Returns only fully-resolved instances, in no particular order. Instances
 * that answered the PTR query but never yielded an SRV + address within the
 * timeout are dropped, and logged.
 *
 * `onResolved`, if set, is invoked ON THE CALLING (worker) THREAD as soon as
 * each instance becomes complete, rather than making every caller wait for the
 * full timeout. A live scale typically resolves in well under a second while
 * the browse keeps running to its deadline, so this is the difference between
 * a list that fills in immediately and one that appears all at once at the end.
 * Callers must marshal to their own thread themselves. Each instance is
 * reported at most once.
 */
/**
 * What a browse actually did, returned as data rather than left for a caller to
 * scrape out of log text. `error` is non-empty when the browse could not run,
 * which is otherwise indistinguishable from an empty network — the same list of
 * zero scales either way.
 *
 * (An earlier version of this comment claimed worker-thread qDebug does not
 * reach the app's debug log. That is false — every installed handler is
 * mutex-guarded and thread-agnostic. The struct is still worth having for the
 * reason above.)
 */
struct BrowseStats {
    QString backend;        // "bonjour" or "mjansson"
    int instancesSeen = 0;  // named by the browse, resolved or not
    int resolved = 0;       // complete enough to be a result
    int dropped = 0;        // named but never resolved — stale registrations
    // Reported gone mid-browse (logged, never applied — the list is add-only
    // within a scan). BONJOUR ONLY: the mjansson path has no withdrawal notion,
    // being a one-shot query rather than a live subscription, so -1 there means
    // "not measured" and must not be rendered as "none".
    int withdrawals = -1;
    qint64 elapsedMs = 0;
    QString error;
};

/**
 * `cancel`, if set, is polled each loop iteration and ends the browse early.
 *
 * This is not a nicety: the browse blocks a QThreadPool thread for its full
 * deadline, and ~QCoreApplication calls QThreadPool::waitForDone()
 * unconditionally. Without a way to cut it short, quitting mid-scan holds the
 * process open for the rest of the browse with the UI already gone.
 */
QVector<ServiceInstance> browseService(const QString& serviceType, int timeoutMs = 5000,
                                       const std::function<void(const ServiceInstance&)>& onResolved = {},
                                       BrowseStats* stats = nullptr,
                                       const std::atomic<bool>* cancel = nullptr);

/**
 * The join predicate: is a browsed instance complete enough to be a row?
 *
 * A DNS-SD browse routinely returns instance names whose SRV and A records have
 * expired while the PTR is still cached — a device that rebooted, was renamed,
 * or was unplugged without sending a goodbye. Half the instances on the
 * reference network were exactly this. Those are ghosts, and showing them as
 * selectable scales means offering the user a device that cannot be connected
 * to.
 *
 * An instance qualifies only with ALL THREE: an SRV target, a nonzero port, and
 * an address for that target. Named rather than inlined because the mjansson
 * browse applies it twice — once for the incremental report, once for the final
 * sweep — and the two drifting apart would mean a scale reported mid-browse
 * that then vanishes from the returned vector.
 *
 * The Bonjour backend enforces the same rule structurally: an instance only
 * enters its results map once DNSServiceResolve and the address lookup have
 * both replied, so a never-resolving instance simply has no entry.
 */
bool browseInstanceResolved(const QByteArray& srvTarget, quint16 port, bool haveAddress);

/**
 * Which implementation browseService() uses.
 *
 * Auto is what ships: Bonjour on Apple, mjansson everywhere else. The explicit
 * values exist so a macOS build can drive the mjansson path on demand.
 *
 * That matters because of an awkward asymmetry: mjansson is the backend Android
 * and Windows/Linux actually use, but the machine it is developed on is a Mac,
 * which by default never compiles or runs it. Being able to switch at runtime
 * means the two backends can be pointed at the same LAN and their results
 * compared — if they disagree, one is wrong and the difference says which.
 *
 * Selecting Mjansson on iOS does nothing (it is not compiled there).
 */
enum class BrowseBackend {
    Auto,
    Bonjour,
    Mjansson,
};

void setBrowseBackend(BrowseBackend backend);
BrowseBackend browseBackend();

/**
 * Name of the backend a browse would ACTUALLY use right now — not merely the one
 * requested. Requesting Bonjour off Apple silently runs mjansson, so reporting
 * the request would make the diagnostic tools compare a backend against itself
 * and label both runs "bonjour".
 */
QString activeBrowseBackendName();

}  // namespace MdnsResolver
