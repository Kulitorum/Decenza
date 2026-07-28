# Discover WiFi scales by DNS-SD service browse, not one fixed hostname

## Why

WiFi scale discovery resolves exactly one name — the literal `hds.local`
(`WifiScaleDiscovery::kDefaultHostname`, hardcoded at both call sites in
`blemanager.cpp`). Any scale that is not answering to that name is invisible, and
the only recovery is for the user to type an address by hand.

Two things make that a real gap rather than a theoretical one:

1. **The scale is renameable.** openscale stores a user-settable mDNS name
   (`params.getMdnsName()`, default `hds`, `[a-z0-9-]`, max 24 chars). A user who
   renames their scale — the natural thing to do when a second one arrives —
   makes it undiscoverable.
2. **The firmware already advertises a browsable service and we ignore it.**
   openscale has published `_decentscale._tcp` with a full TXT record since
   commit `3ec285b` (2026-05-21), shipping in firmware **v3.0.9** onward:

   ```
   MDNS.addService("decentscale", "tcp", 80);
   TXT: fw=<version>  model=hds  name=<mdns_name>  proto=ws  path=/snapshot
   ```

   The DNS-SD instance name is already user-friendly — `Half Decent Scale`, or
   `Half Decent Scale (<name>)` when renamed — which is exactly what a
   multi-scale picker needs to show.

A browse finds every scale on the LAN regardless of name, and asks the network
one question instead of guessing names one at a time.

## What Changes

- **Add a DNS-SD service browse for `_decentscale._tcp`** as the primary WiFi
  discovery mechanism. Results carry instance display name, the TXT `name`,
  resolved host and IP, port, WebSocket path, and firmware version.
- **Keep the A-record path as a fallback, and widen it** from the single
  `hds.local` to a small probed set: `hds.local`, `hds-2.local`, `hds-3.local`.
  This covers firmware older than v3.0.9, which advertises no service at all.
  The `-2` / `-3` names are a *heuristic for a common user naming habit*, not
  protocol behaviour — see design.md; neither openscale nor esp-idf ever
  generates them.
- **Add a browse transport on every platform.** `QHostInfo::lookupHost` cannot
  browse, and the vendored mjansson/mdns is currently fetched `if(ANDROID)`
  only. Apple platforms get a native Bonjour shim (avoids the iOS multicast
  entitlement); Android/Windows/Linux use the mjansson PTR/SRV/TXT path.
- **Turn the "Add WiFi Scale" dialog's single found/not-found shortcut into a
  result list**, labelled by DNS-SD instance name, deduped against the A-record
  fallback hits.
- **Make one "Scan for Devices" press cover all three transports.** It searches
  BLE and WiFi today; USB is a free-running 2 s background poll that the button
  does not touch. USB gains a one-shot probe on scan, and the "Scanning…"
  indicator becomes a composite that stays up until BLE, WiFi and USB have all
  finished — rather than clearing when the BLE agent alone is done.
- **Make the discovered-devices list add-only within a scan cycle.** No row
  appears and then vanishes while the user is reading it; the list is rebuilt on
  the next scan. (Unplugging a USB scale still removes its row immediately —
  that is a device going away, not discovery churn.)
- Existing behaviour that does **not** change: manual host/IP entry (the Add
  WiFi Scale dialog keeps its current design), the `wifi:<hostname>`
  saved-address scheme, and the `ws://<host>/snapshot` HDS-frame validation gate
  that every discovered endpoint still passes before being persisted.

No breaking changes. Discovery is strictly additive — a scale found today is
still found.

## Capabilities

### New Capabilities

None. This extends existing WiFi scale discovery rather than introducing a new
capability area.

### Modified Capabilities

- `wifi-scale-discovery`: the "On-demand WiFi scale discovery" and "WiFi scale
  appears in the unified scale list" requirements change from *resolve the fixed
  name `hds.local`* to *browse `_decentscale._tcp`, plus a multi-name A-record
  fallback*, and from *at most one WiFi row* to *one row per discovered scale*.
  Adds requirements for browse-result identity/dedupe and for the unchanged
  HDS-frame validation gate applying to browse results.

## Impact

**Code**
- `src/network/wifiscalediscovery.{h,cpp}` — probe() becomes a browse plus a
  multi-name fallback; new result type and signal shape.
- `src/usb/usbmanager.{h,cpp}`, `src/usb/usbscalemanager.{h,cpp}` — add a
  one-shot probe entry with a completion signal, so the scan can drive USB
  instead of only its 2 s background poll.
- `src/network/mdnsresolver.{h,cpp}` — add PTR/SRV/TXT browse alongside the
  existing A-record query.
- New Apple Bonjour shim (`DNSServiceBrowse`/`NSNetServiceBrowser`), covering
  both iOS and macOS.
- `src/ble/blemanager.{h,cpp}` — the two hardcoded `hds.local` call sites;
  discovered-scales list gains one row per browse hit.
- `qml/pages/settings/SettingsConnectionsTab.qml` — result list replaces the
  single shortcut.

**Build / platform**
- `CMakeLists.txt` — widen the mjansson/mdns FetchContent and SYSTEM-include
  guards beyond `if(ANDROID)`.
- `ios/Info.plist` — add `_decentscale._tcp` to `NSBonjourServices` (currently
  `_http._tcp` only). `NSLocalNetworkUsageDescription` already exists.
- Apple and Android paths are compiled only by the tag-push release workflows,
  so both need a CI test build to verify.

**Docs**
- Wiki manual: the Connections / Add WiFi Scale section gains the scale list and
  the renamed-scale story.

**Dependencies**
- No new dependency. mjansson/mdns 1.4.3 is already declared; only its platform
  guard changes.

**Firmware**
- Best results need openscale ≥ v3.0.9. Older firmware still works via the
  A-record fallback.
