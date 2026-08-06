# devices_wifi

WiFi scale discovery, without a BLE scan. A diagnostic pair: `action=browse` starts discovery,
`action=results` reads what it found. Wait a few seconds between them.

## browse

Runs DNS-SD for `_decentscale._tcp` plus the `hds` / `hds-2` / `hds-3` A-record fallback.

`backend` selects the mDNS implementation so the two can be compared on the same network:

| Value | Meaning |
|---|---|
| `auto` | bonjour on iOS, mjansson everywhere else — **including macOS** |
| `mjansson` | What Android, Windows and Linux ship, and the macOS default |
| `bonjour` | Apple-only; unavailable elsewhere. The iOS default, and still selectable on macOS |

macOS defaults to **mjansson**, not Bonjour, even though Bonjour is faster there (66-113 ms to a
first row against 160-270 ms, since mDNSResponder is always listening). macOS is the development
platform, not a shipped one — roughly two installs against hundreds on Android — so its default is
chosen to exercise the path most users actually run. Both backends stay compiled into the macOS
binary; only the default moved, and `backend=bonjour` switches back at runtime. Bonjour keeps its
field coverage from iOS; what the default gives up is early warning, so run a browse with
`backend=bonjour` before an iOS release.

`resolver` selects the OTHER half — what resolves `hds.local` to an address. It is separate from
`backend` because a browse and an A-record lookup are different queries: a scale can answer the
DNS-SD browse and ignore a bare A query, and only the pair tells you which happened.

| Value | Meaning |
|---|---|
| `auto` | What ships — mjansson on Android, the system resolver elsewhere |
| `mjansson` | On a desktop, runs the exact A-record path Android ships |
| `system` | QHostInfo / the OS resolver. **Refused on Android** — see below |

Both are reported back as `backendActive` / `resolverActive`, which are what ACTUALLY ran — asking
for one that is not compiled on this platform gives you the substitute, not an error.

`resolver=system` on Android returns an error instead of taking effect, and the asymmetry with
`backend` is deliberate. Pinning the browse backend on Android is harmless because only one is
compiled there, so the request quietly resolves to what already runs. Pinning the resolver really
does take effect, Android's `getaddrinfo` returns NXDOMAIN for every `.local` name, and the setting
is process-wide and sticky — so a single call would leave hostname discovery dead until the app
restarts. Run that comparison on a desktop build.

When comparing, note the system resolver caches: on macOS a browse populates mDNSResponder, so a
`system` lookup seconds later can be answered from that cache rather than by the device. Run the
resolver comparison on its own, not right after a browse, if the question is whether the device
itself answers.

`queryPort` is the third selector and the only one that changes what the **responder** is obliged
to do. mjansson sets the QU (unicast-response) bit unless the socket is bound to 5353, so a query
from an ephemeral port is a "legacy" query under RFC 6762 §6.7 that the responder must answer by
unicast — and an openscale scale has been measured refusing exactly that for a peer it has no fresh
path to, for hours, while answering another host on the same LAN in 272 ms.

| Value | Meaning |
|---|---|
| `auto` | Bind 5353 — except on **Android**, where it binds an ephemeral port (see below) |
| `mdns` | Force 5353. Query is ordinary, answers come back multicast, nothing per-peer in the path |
| `ephemeral` | Force the legacy query. What shipped before |

**On Android, 5353 does not work and `auto` avoids it.** The system mDNS daemon already owns the
port; `SO_REUSEPORT` lets the bind succeed and then every inbound packet is delivered to the daemon
instead of to the app. Measured on-device with a multicast lock held: `records=0` for every host,
including the MQTT broker's `.local` name, which resolves normally from an ephemeral port. In the
same browse, NsdManager — the daemon, on 5353 — resolved a scale in 41 ms while the app's own 5353
socket saw 2 records.

Read the outcome from the log's `srcPort=`, not from the reply: `queryPortRequested` is the policy,
and a 5353 bind can fail on one call and succeed on the next.

`timeoutMs` defaults to 8000. Short windows return the resolver's cache including stale entries;
longer ones let it prune them.

## results

Returns the DNS-SD detail the device list flattens away: `instanceName`, `mdnsName`, `hostname`,
`address`, `port`, `path`, `firmwareVersion`, and `foundBy` (service browse or hostname
fallback).

It also returns `browseRan` and `fallbackProbeRan`. **False means that transport could not run at
all** — no backend, socket refused, Local Network permission denied — which is a completely
different problem from running and finding nothing. A count of 0 cannot make that distinction.
