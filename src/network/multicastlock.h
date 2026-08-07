#pragma once

#include <QString>

/**
 * Android's Wi-Fi `WifiManager.MulticastLock`, reference-counted across every
 * part of the app that needs to RECEIVE multicast.
 *
 * The Wi-Fi driver drops multicast and broadcast frames that are not addressed
 * to this device unless such a lock is held. That filter is invisible: the
 * socket opens, the group join succeeds, queries go out normally, and nothing
 * ever arrives. A no-op on every other platform.
 *
 * WHY THIS IS SHARED RATHER THAN PER-FEATURE. It used to belong to ShotServer,
 * acquired in start() and released in stop(), and three comments elsewhere in
 * the tree described it as being held "for the whole app lifetime". It was not:
 * `shotServer/enabled` defaults to **false**, so on a default install no lock
 * was ever taken, and every other multicast reader inherited a guarantee that
 * did not exist. mDNS is the reader that pays for it — see MdnsResolver.
 *
 * It was OFFERED as the explanation for an otherwise contradictory pair of
 * measurements, and it is NOT the explanation. The reasoning was that a query
 * from an ephemeral source port is answered by UNICAST (RFC 6762 §6.7), needing
 * no lock, while a query from 5353 is answered by MULTICAST, which needs one —
 * so a 5353 socket on Android with no lock held would see zero records while an
 * ephemeral socket worked, which is what had been recorded.
 *
 * That was retested with the lock demonstrably held and 5353 on Android STILL
 * receives zero records, from any host — see mdnsresolver.cpp, which carries the
 * same retraction next to the code that acts on it. Acting on this reading is
 * what briefly broke every ".local" lookup on Android. The lock is still
 * necessary; it is simply not sufficient, and it is not why 5353 fails there.
 *
 * COST. The lock disables a hardware filter, so the CPU wakes for multicast
 * traffic addressed to the whole LAN. That is why this is scoped to the
 * operations that need it rather than taken once at startup: a Holder lives for
 * one browse or one lookup, and the underlying lock is released when the last
 * Holder goes away.
 *
 * Safe from any thread — the mDNS workers that use it are not the main thread.
 */
namespace MulticastLock {

/**
 * Holds the lock for its lifetime. Nesting and overlap are expected: several
 * browses and lookups run concurrently, and the underlying Android lock is
 * taken on the first Holder and released with the last.
 *
 * Construction never throws and never blocks on anything but the internal
 * mutex. If the lock cannot be taken — no context, no WifiManager — that is
 * logged once and the Holder is inert, because a discovery that runs without
 * the lock is still better than one that does not run.
 */
class Holder {
public:
    Holder();
    ~Holder();
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    Holder(Holder&&) = delete;
    Holder& operator=(Holder&&) = delete;
};

/** True while the Android lock is actually held. Always false off Android. */
bool isHeld();

/** How many Holders exist. For diagnostics and tests. */
int holderCount();

}  // namespace MulticastLock
