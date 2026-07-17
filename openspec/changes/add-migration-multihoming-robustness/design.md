## Context

`DataMigrationClient` (`src/core/datamigrationclient.cpp`) finds a peer via UDP discovery, then fetches `GET /api/backup/manifest` through the app's shared `QNetworkAccessManager` (wired in `maincontroller.cpp:682`). On a host with **two interfaces on the same subnet** (observed: macOS on `en0` Wi-Fi `192.168.10.183` and `en7` USB-Ethernet `192.168.10.195`, both on `192.168.10.0/24`), the manifest fetch fails with `QNetworkReply` error string "Host unreachable" (`EHOSTUNREACH`) — reported at `datamigrationclient.cpp:160`.

Diagnosis on the running build:
- Discovery **succeeds**: `Found device: "Android (16.0)" at "http://192.168.10.163:8888"`.
- The peer is genuinely up: `curl http://192.168.10.163:8888/` → HTTP 200 from the same Mac; `ping` 0% loss; binding curl to either source IP (`--interface 192.168.10.183` and `.195`) → HTTP 200.
- `EHOSTUNREACH` on a directly-connected destination is the signature of **neighbour (ARP) resolution failing**, not "no route". With two interfaces on one subnet, resolving `192.168.10.163` is ambiguous — the ARP request can leave one link while the route expects the other — so the first connect to a not-yet-resolved neighbour fails. Once a `curl` warmed the caches, `arp -n 192.168.10.163` showed the entry on **both** `en0` and `en7`.

Constraint that shapes the design: **`QNetworkAccessManager` has no public source-address / interface binding** (confirmed against the Qt 6.11.1 `qnetworkaccessmanager.h` header). `QAbstractSocket`/`QTcpSocket` **does** expose `bind(const QHostAddress&)`. So an interface-deterministic connection must be driven at the socket layer, while the existing HTTP transfers (auth POST, chunked downloads, SSL-ignore, download progress, background `shots.db` import) should stay on `QNetworkAccessManager` to avoid reimplementing an HTTP client.

## Goals / Non-Goals

**Goals:**
- Migration connects reliably when the host is multi-homed on the peer's subnet (the reported bug).
- Connection logic is interface-aware and deterministic: probe candidate interfaces, default-route first, and only declare "unreachable" when all fail.
- Failure messages distinguish unreachable / needs-auth / bad-response.
- Applies identically to the discovered-device path and the manual `IP:port` path.
- No change to the REST contract, the discovery protocol, or what data transfers.

**Non-Goals:**
- Mid-transfer failover to another link if the selected link drops during a download (would require binding the data socket, which `QNetworkAccessManager` cannot do). Documented as a limitation.
- Rewriting the migration HTTP transfers onto `QTcpSocket`.
- IPv6 peer connectivity (discovery advertises IPv4 literals today; unchanged).
- Any change to `ShotServer` (the server side is already reachable — it responded to discovery and serves HTTP).

## Decisions

### Decision 1: Bound-`QTcpSocket` reachability preflight before the manifest fetch
Before issuing the manifest request, run a preflight that: (1) parses the target host+port from `m_serverUrl`; (2) if the host is an IPv4 literal on a directly-connected subnet, builds the candidate list of local source addresses whose `QNetworkAddressEntry` subnet contains it (via `QNetworkInterface::allInterfaces()` — already used in this file for discovery); (3) orders candidates with the default-route interface first; (4) for each candidate, opens a `QTcpSocket`, `bind(sourceAddr)`, `connectToHost(host, port)` with a short timeout, and on the first success records the winning source and tears the probe down. A successful probe resolves the neighbour on a known-good link, so the subsequent unbound `QNetworkAccessManager` manifest fetch no longer hits a cold neighbour. If no host is a directly-connected literal (e.g. a routed address or hostname), the candidate list is a single "default" entry and the probe is a plain connect — a no-op behaviourally for the common single-homed case.

- **Why over alternatives:**
  - *Rewrite transfers on bound `QTcpSocket`* — would give true per-transfer link binding/failover, but means reimplementing HTTP (chunked decode, cookies, SSL-ignore, progress) for ~10 request sites; high risk, large surface, rejected as disproportionate to the reported bug (and captured as the Non-Goal above).
  - *Set the OS default route / tell the user to unplug a link* — rejected; multi-homing for redundancy is a supported setup, per the proposal.
  - *Retry alone with no probe* — helps, but is non-deterministic about which interface warms; the bound probe makes neighbour resolution deterministic on a link we verified.

### Decision 2: Bounded retry of the manifest fetch on transient errors
Keep the manifest fetch on `QNetworkAccessManager`, but on a transient network error (the `EHOSTUNREACH`-class failures — `QNetworkReply::HostNotFoundError`, `QNetworkReply::UnknownNetworkError`, and the connection/timeout errors) retry up to a small fixed cap. Do **not** retry on HTTP 401 (auth), other HTTP status codes, or JSON-parse failures — those mean the connection itself succeeded. This is complementary to the probe: the probe makes the first attempt likely to succeed; the retry absorbs a residual cold-neighbour race.

- **Why:** cheap, stays on the existing QNAM flow, and directly targets the transient nature of `EHOSTUNREACH`. Bounded so a truly-down peer still fails promptly. Retry is a genuine transient-error recovery, not a timer-as-guard — it is driven by the error signal, consistent with the project's "no timers as guards" rule (a short inter-attempt delay, if any, is event-scheduled, not a heuristic guard).

### Decision 3: Classify the failure before choosing the message
`onManifestReply`'s error branch (and the preflight outcome) drive three distinct paths: all-candidates-failed → unreachable; HTTP 401 → existing auth path (unchanged); any other reply error or parse failure → a response error. The user-facing "unreachable" string is emitted only from the all-candidates-failed path.

- **Why:** the current code funnels every reply error through one "Connection failed: <errorString>" line, so a reachable-but-broken peer reads as "unreachable". Splitting the paths makes the UI truthful and matches the spec's classification requirement.

## Risks / Trade-offs

- **[Warming one interface may not bind the QNAM request to that same interface]** → In the reported case the default-route interface is a working candidate and is probed first, so warming it lines up with the route QNAM will use. Decision 2's bounded retry covers the residual race. Full determinism (binding the data socket) is the documented Non-Goal.
- **[Preflight adds latency / could hang on a filtered port]** → Use a short per-candidate connect timeout and cap the candidate count; single-homed hosts probe exactly once. Net latency is bounded and small.
- **[`QTcpSocket::bind()` to a source address on macOS/Windows/Linux behaves differently]** → Verify on each desktop platform during apply; binding to a concrete local `QHostAddress` (not `SO_BINDTODEVICE`) is portable Qt API. Mobile is effectively single-homed, so the path is a no-op there.
- **[Misclassifying a candidate subnet]** → Reuse the same `QNetworkInterface` iteration the file already uses for discovery; when in doubt fall back to a single default candidate (current behaviour), so the change can only add reachability, never remove it.

## Migration Plan

1. Land behind no flag — it is a strict robustness improvement with a single-homed no-op path; rollback is reverting the commit.
2. Manual verification: on the multi-homed Mac (Wi-Fi + USB-Ethernet, both on `192.168.10.0/24`), confirm import connects to the Android peer at `192.168.10.163:8888`; then repeat with one link down to confirm the surviving link is selected; then on a single-homed host to confirm no regression/added delay.
3. Update the wiki Manual's device-migration section only if user-visible wording changes (error text); the happy path is unchanged.

## Open Questions

- Exact transient-error set and retry cap/delay — settle against observed `QNetworkReply::NetworkError` values during apply (start: 2 retries; treat `HostNotFoundError`, `UnknownNetworkError`, `TimeoutError`, `TemporaryNetworkFailureError`, connection-refused/-closed as transient).
- Whether to fold the same preflight into the auth POST retry path, or rely on the connection already being warmed by the manifest preflight (leaning: rely on it — auth only runs after a reachable manifest attempt).
