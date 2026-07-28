#pragma once

#include <QMap>
#include <QString>
#include <QVector>

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
 * Apple platforms use neither: they browse through the system Bonjour APIs
 * (DNSServiceBrowse), because a raw multicast socket to 224.0.0.251 on iOS
 * requires the com.apple.developer.networking.multicast entitlement, which
 * Apple grants only by per-app application. This whole file is therefore
 * compiled out on macOS/iOS — see the guard in mdnsresolver.cpp and the
 * `if(NOT APPLE)` blocks in CMakeLists.txt.
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
 * Resolve `hostname` (e.g. "hds.local") to a dotted-quad IPv4 string via a
 * direct mDNS A-record query. Blocks up to `timeoutMs`. MUST be called off
 * the main thread. Returns an empty string on timeout / failure.
 */
QString resolveHostname(const QString& hostname, int timeoutMs = 2000);

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
 * What a browse actually did. Returned by reference because the browse runs on
 * a worker thread whose qDebug output does NOT reach the app's debug log — only
 * main-thread messages are captured — so without this the browse is invisible
 * in exactly the log a user would share. `error` is non-empty when the browse
 * could not run at all, which is otherwise indistinguishable from an empty
 * network.
 */
struct BrowseStats {
    QString backend;        // "bonjour" or "mjansson"
    int instancesSeen = 0;  // named by the browse, resolved or not
    int resolved = 0;       // complete enough to be a result
    int dropped = 0;        // named but never resolved — stale registrations
    int withdrawals = 0;    // reported gone mid-browse (logged, never applied)
    qint64 elapsedMs = 0;
    QString error;
};

QVector<ServiceInstance> browseService(const QString& serviceType, int timeoutMs = 5000,
                                       const std::function<void(const ServiceInstance&)>& onResolved = {},
                                       BrowseStats* stats = nullptr);

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

/** Human-readable name of the backend a browse would actually use right now. */
QString activeBrowseBackendName();

}  // namespace MdnsResolver
