#pragma once

#include <QString>

/**
 * Android-only mDNS ".local" hostname resolver.
 *
 * Android's stock resolver (getaddrinfo, used by QHostInfo::lookupHost) does
 * NOT reliably resolve ".local" mDNS hostnames — it returns NXDOMAIN. We send
 * a direct mDNS A-record query via the mjansson/mdns library and parse the
 * answer. This is pure hostname→IP resolution (the same thing QHostInfo does
 * on desktop/iOS where the OS resolver speaks mDNS: nss-mdns on Linux,
 * Bonjour on macOS/iOS).
 *
 * NOTE: this is DNS hostname resolution, NOT DNS-SD service discovery. The
 * Half Decent Scale (and the MQTT broker, etc.) publish a plain ".local"
 * hostname, not necessarily a "_http._tcp" service record — so NsdManager's
 * discoverServices() is the wrong tool and finds nothing. An A-record query
 * is the correct approach.
 *
 * On non-Android platforms resolveHostname() is a no-op stub returning "" —
 * callers there should use QHostInfo::lookupHost directly.
 *
 * Multicast reception on Android requires a held WifiManager.MulticastLock.
 * ShotServer acquires one for the whole app lifetime (start()→stop()), so the
 * process-wide lock is in effect; MqttClient relies on the same arrangement.
 */
namespace MdnsResolver {

/**
 * Resolve `hostname` (e.g. "hds.local") to a dotted-quad IPv4 string via a
 * direct mDNS A-record query. Blocks up to `timeoutMs`. MUST be called off
 * the main thread. Returns an empty string on timeout / failure.
 */
QString resolveHostname(const QString& hostname, int timeoutMs = 2000);

}  // namespace MdnsResolver
