## Why

Device-to-device migration (Settings → Data → "Import from Another Device") discovers a peer over UDP but then fails to connect with **"Connection failed: Host unreachable"**, even though the peer's server is up and reachable (`curl http://<ip>:8888/` returns HTTP 200 from the same host). This reproduces whenever the local device is **multi-homed on one subnet** — e.g. a Mac on the LAN via both Wi-Fi and a USB Ethernet dongle, both holding an address in `192.168.10.0/24`. Multiple interfaces on a subnet is a legitimate redundancy configuration, so the migration client must connect reliably in it rather than treating it as user error.

## What Changes

- When connecting to a migration peer (both the discovered-device path and the manual `IP:port` path), the client first performs an **interface-aware reachability preflight**: it enumerates the local IPv4 source addresses whose subnet contains the target, orders them (default-route interface first), and probes `target:port` with a source-bound `QTcpSocket` until one succeeds. This deterministically resolves the neighbour on a working link before the HTTP flow runs, eliminating the transient `EHOSTUNREACH` that unbound `QNetworkAccessManager` requests hit on an ambiguous multi-homed subnet.
- The subsequent manifest fetch retries a small, bounded number of times on transient network errors (not on `401`/auth or HTTP errors), so a single cold-neighbour failure no longer aborts the connection.
- Failure messaging becomes accurate: "unreachable" is reported **only** when every candidate local interface fails to reach the peer; a reachable-but-unauthenticated peer still surfaces the auth prompt, and an HTTP/parse error still surfaces as such.
- No change to what data transfers or to the REST contract — this is purely connection establishment.

## Capabilities

### New Capabilities
- `migration-connection-robustness`: How the data-migration client establishes a connection to a discovered or manually-entered peer, including interface-aware reachability selection on multi-homed hosts, bounded retry on transient network errors, and accurate reachable/unauthenticated/unreachable error classification.

### Modified Capabilities
<!-- None: data-transfer-coverage governs WHAT transfers; this change governs connection establishment only and does not alter any existing spec's requirements. -->

## Impact

- **Code**: `src/core/datamigrationclient.{h,cpp}` — `connectToServer()` and manifest-fetch path; new interface-selection + bound-probe helper. Discovery (`onDiscoveryDatagram`) already captures the peer IP; no change to the UDP protocol or `ShotServer`.
- **UI**: `qml/pages/settings/SettingsDataTab.qml` — error text only (reachable-on-no-interface vs auth vs http); no layout change.
- **APIs / dependencies**: none. Uses existing `QNetworkInterface` / `QTcpSocket` (Qt Network, already linked). `QNetworkAccessManager` has no source-binding API (confirmed against Qt 6.11.1 headers), which is why the preflight uses a bound `QTcpSocket`.
- **Platforms**: fix matters most on desktop (macOS/Windows/Linux) where multi-homing is common; harmless on single-homed hosts (one candidate, probe succeeds immediately) and on mobile.
- **Non-goal**: mid-transfer failover to a second link if the chosen link drops during a download — `QNetworkAccessManager` can't bind the data socket, so true per-transfer link failover is out of scope for this change.
