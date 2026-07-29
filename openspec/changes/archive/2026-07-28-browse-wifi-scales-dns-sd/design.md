## Context

WiFi scale discovery today is one mDNS A-record query for the literal string
`hds.local`:

- `WifiScaleDiscovery::kDefaultHostname` (`src/network/wifiscalediscovery.h:31`),
  also the `probe()` default argument (`:38`).
- Both call sites hardcode it: `blemanager.cpp:2067` (saved-scale rehydration)
  and `blemanager.cpp:839` (the probe fired alongside the manual-entry dialog).
- The manual-entry escape hatch appends `.local` to a bare name
  (`blemanager.cpp:848`) and validates the endpoint by opening
  `ws://<host>/snapshot` and requiring an HDS-shaped frame before persisting
  (`blemanager.cpp:1690`, `main.cpp:2587`–`2839`).

The transport underneath is split, and neither half can browse:

| Platform | Today | Can browse? |
|---|---|---|
| Android | vendored mjansson/mdns, raw UDP socket, `MDNS_RECORDTYPE_A` only (`mdnsresolver.cpp:157`; non-A records dropped at `:85`) | library can, our code doesn't |
| Everything else | `QHostInfo::lookupHost` (`wifiscalediscovery.cpp:107`) | **no** — resolves names, cannot enumerate services |

### What the firmware actually publishes

Verified in `~/Development/GitHub/openscale`, `src/wifi_setup.cpp` `setupMdns()`:

```cpp
MDNS.begin(params.getMdnsName());          // default "hds"
MDNS.setInstanceName("Half Decent Scale"); // or "Half Decent Scale (<name>)"
MDNS.addService("decentscale", "tcp", 80);
MDNS.addServiceTxt("decentscale","tcp","fw",    FIRMWARE_VER);
MDNS.addServiceTxt("decentscale","tcp","model", "hds");
MDNS.addServiceTxt("decentscale","tcp","name",  name);
MDNS.addServiceTxt("decentscale","tcp","proto", "ws");
MDNS.addServiceTxt("decentscale","tcp","path",  "/snapshot");
```

Added in commit `3ec285b` (2026-05-21), shipping since tag **v3.0.9** (also
v3.1.11, v3.1.12). Everything a picker needs — friendly label, port, WebSocket
path, firmware version — is already on the wire and we throw it away.

### The `hds-2.local` question, answered precisely

The scale name is user-settable: `include/mdns_name.h`, `MDNS_NAME_DEFAULT =
"hds"`, `mdnsNameNormalize()` accepting `[a-z0-9-]`, max 24 chars, no leading or
trailing hyphen. So `hds-2` is a **legal name a user can type**.

It is **not** a name anything generates. Neither openscale nor esp-idf renames on
collision — the shipped `libespressif__mdns.a` exports `check_a_collision`,
`check_srv_collision`, `check_txt_collision` and contains no rename or `-%d`
suffix string. Two default-named scales both claim `hds.local` and fight; the
loser does not become `hds-2.local`.

This matters for how the fallback is justified. `hds-2.local` / `hds-3.local` are
worth probing because they are the obvious thing a person types for their second
and third scale, and each costs one extra UDP query. They are a **habit
heuristic**, and the code comment must say so — a future reader who believes they
are protocol will draw wrong conclusions when they don't fire.

### Verified on the wire (2026-07-28, macOS, two scales online)

Browsed via `dns-sd -B _decentscale._tcp local`, resolved with `dns-sd -L`. This
went through mDNSResponder with no multicast entitlement, which validates D2 as
well as the firmware contract.

Two scales were online. **The browse returned four instances.**

| Instance name | Resolves to | TXT |
|---|---|---|
| `Half Decent Scale` | `hds.local:80` | `path=/snapshot proto=ws model=hds fw=FW: 3.1.12` |
| `Half Decent Scale (hdstest)` | `hdstest.local:80` | `path=/snapshot proto=ws name=hdstest model=hds fw=FW: 3.1.13-dev` |
| `Half Decent Scale-2` | **never resolves** (10 s) | — |
| `Half Decent Scale (kitchen)` | **never resolves** (10 s) | — |

Four findings that change the design, none of which were predictable from the
firmware source:

- **F1 — Half the browse hits are ghosts.** Two of four instances answer the PTR
  browse and never resolve (10 s resolve attempts, repeated). Stale
  registrations from earlier names or sessions live in the responder cache. So a
  PTR hit is *not* a scale; only a completed SRV+A resolve is. See D4a.
- **F1a — A sustained browse partially self-cleans, on its own schedule.** Over a
  100 s continuous browse, `Half Decent Scale (kitchen)` was withdrawn with an
  `Rmv` event **8.9 s** after the browse started — mDNSResponder re-queried,
  nobody answered, the record expired. `Half Decent Scale-2` was still present at
  100 s. So the pruning is real but neither immediate nor uniform; it depends on
  each record's remaining cache TTL. A short snapshot scan gets none of it. See
  D4b.
- **F2 — DNS-SD instance names collide and get a `-2` suffix.** `Half Decent
  Scale-2` is instance-level conflict resolution. Note this does **not**
  contradict the section above: the *hostname* is never suffixed (`hds-2.local`
  still answers nothing), only the service instance name. Consequence: with two
  unrenamed scales the user is offered "Half Decent Scale" and "Half Decent
  Scale-2" and cannot tell which is which. See D7a.
- **F3 — TXT `name` can be absent.** The unrenamed scale on fw 3.1.12 publishes
  no `name` key at all, even though `setupMdns()` appears to always set it. Any
  code that reads `name` must tolerate its absence rather than treating it as
  guaranteed.
- **F4 — `fw` is not a bare version string.** The actual value is `FW: 3.1.12` —
  a prefix plus a literal space. Anything comparing against the v3.0.9 floor must
  strip it, and must not assume semver parses cleanly.

### D4a: A browse hit is only a scale once it resolves

Do not render a row for a PTR hit. Require the SRV+A resolve to complete first,
and drop instances that fail to resolve within the scan window. Without this the
user sees four rows for two scales, two of which fail on tap — which is worse
than today's single-shortcut behaviour.

This also makes D4's dedupe-on-resolved-IP well-defined: a ghost has no address,
so it never reaches the merge.

### D4b: One scan covers all three transports, and "Scanning…" lasts until all three finish

"Scan for Devices" means *find my devices*, not *find my Bluetooth devices*. It
covers BLE, WiFi and USB, and the indicator stays up until every one of them is
done.

Where each stands today:

| Transport | Today | Change |
|---|---|---|
| BLE | `startScan()`, 15 s (`blemanager.cpp:395`) | none |
| WiFi | mDNS probe fired right after, 5 s (`blemanager.cpp:2062`-`2067`) | becomes the browse |
| USB | **not in the scan at all** — free-running 2 s poll started at app launch (`main.cpp:1901`, `:1908`) | gets a one-shot probe on scan |

The BLE and WiFi halves already run in parallel; the comment at
`blemanager.cpp:2062` says so. USB is the odd one out: `UsbManager` and
`UsbScaleManager` poll every `POLL_INTERVAL_MS` (2 s) from app start and add or
remove a synthetic `usb:decent` row via `setUsbScaleAvailable()`. Pressing Scan
does nothing to USB — a USB scale appears because the poll noticed it, whether
or not anyone scanned.

So USB needs a one-shot "probe now" entry that fires a pass immediately instead
of waiting up to 2 s for the next tick, and reports completion. Bounded by
`PROBE_TIMEOUT_MS` (3 s for the scale, 2 s for the DE1), it finishes well inside
BLE's 15 s and never extends the user-perceived scan.

**`scanning` becomes a composite: true while the BLE scan, the WiFi browse, or
the USB probe is still running; false only when all three have finished.** The
indicator answers "is the app still looking", not "is the BLE agent still
looking". In practice BLE's 15 s dominates, so this costs the user nothing.

The browse is bounded by that same scan cycle — it starts with the scan and stops
with it. That is what makes the composite indicator possible, and it is why the
earlier picker-lifetime idea was wrong.

*Note on the existing timeout:* the user-scan WiFi probe is **5 s**, not the
~2 s the published spec claims — `blemanager.cpp:2067` passes `5000`, with a
comment that the HDS responder regularly takes 2-4 s to reply. The spec text was
stale before this change; the delta corrects it.

### D4c: Within a scan cycle the list only grows — no row appears and then vanishes

A row that materialises and then evaporates while the user is reading the list is
indistinguishable from a bug, and it moves the tap target under a finger already
in motion. So within one scan cycle the discovered-devices list is **add-only**,
matching what BLE already does.

Concretely:

- A browsed instance is added only once it has fully resolved (D4a). A ghost
  never resolves, so it never appears in the first place — the resolve gate, not
  a later removal, is what keeps ghosts out.
- Withdrawal callbacks arriving mid-scan are **not** applied to the list. They
  are logged and otherwise ignored until the cycle ends.
- The list is rebuilt on the next scan, which is where a departed device
  actually disappears — exactly the BLE semantics today (`doStartScan()` clears
  and repopulates, `blemanager.cpp:1070`-`1086`).

This supersedes the earlier "honour `Rmv` events live" idea. The 8.9 s
self-pruning measured in F1a is still useful — it is why the browse should stay
open for the full cycle rather than snapshot early — but its benefit is that a
ghost is *never resolved and therefore never shown*, not that a shown row gets
retracted.

**One pre-existing exception, deliberately left alone:** unplugging a USB scale
removes its row immediately via `setUsbScaleAvailable(false)`, scan or no scan.
That is a physical device going away, not discovery churn, and removing the row
is the correct response. Changing it is out of scope here.

*Alternative considered:* a one-shot snapshot probe, filtering ghosts purely by
the resolve deadline. Rejected — it works, but a browse's first callback is a
dump of the resolver's cache, ghosts included, and the pruning that removes them
arrives seconds later (8.9 s in the one case measured, F1a). A short snapshot
returns the worst view available: every stale instance, none of the corrections.
The resolver does the pruning for free; the only thing needed to benefit is to
still be listening when it happens.

*Alternative considered:* scoping the browse to the Add WiFi Scale picker's
lifetime instead of the scan cycle. Rejected — WiFi scales surface on two
surfaces (the main discovered-devices list and that dialog), so "the picker"
does not name one thing, and a browse outliving the scan cycle is exactly what
makes the "Scanning…" indicator lie.

*Note:* this does not reintroduce background probing. The browse is bounded by
the user-initiated scan cycle. The existing "no background, idle, or app-start
probing" requirement stands.

### D7a: Disambiguate rows by address, not by instance name alone

Because of F2, the instance name is not sufficient identity. When two rows carry
the same or a suffix-differentiated generic instance name, the row label SHALL
include the resolved host or address so the user can tell them apart. Prefer the
TXT `name` when present (F3 says it may not be), then the SRV target hostname,
then the IP.

## Goals / Non-Goals

**Goals:**

- Find every advertising scale on the LAN regardless of its mDNS name.
- Show multiple scales as distinct, human-labelled rows.
- Keep working against firmware older than v3.0.9.
- Ship on iOS without an Apple entitlement request gating the release.

**Non-Goals:**

- Changing the `wifi:<hostname>` saved-address scheme, the WebSocket driver, the
  cached-IP reconnect logic, or the HDS-frame validation gate.
- Removing manual host/IP entry. It stays as the last resort for a scale on a
  network segment mDNS doesn't cross.
- Background or periodic browsing. Discovery stays user-initiated, per the
  existing requirement.
- Auto-connecting to a browsed scale that isn't the saved one.
- Any firmware change. Everything needed already ships.

## Decisions

### D1: Browse `_decentscale._tcp` directly, not the DNS-SD meta-query

Query PTR for `_decentscale._tcp.local` rather than enumerating
`_services._dns-sd._udp.local` and filtering. We know the service type; the
meta-query doubles the round trips and returns every printer and TV on the
network to be discarded.

*Alternative considered:* browsing `_http._tcp` (already declared in
`ios/Info.plist`) and probing each hit. Rejected — a typical LAN has dozens of
`_http._tcp` responders and probing each with a WebSocket is both slow and rude.

### D2: Two transports — native Bonjour on Apple, mjansson elsewhere

This is the load-bearing decision.

**Apple (iOS + macOS): system Bonjour.** `DNSServiceBrowse` +
`DNSServiceResolve` + `DNSServiceGetAddrInfo` (or `NSNetServiceBrowser`), with
`_decentscale._tcp` added to `NSBonjourServices` in `ios/Info.plist` — which
currently lists only `_http._tcp` (`ios/Info.plist:67`);
`NSLocalNetworkUsageDescription` already exists at `:65`.

The reason is not elegance, it is shipping: opening a raw socket to
`224.0.0.251` on iOS requires `com.apple.developer.networking.multicast`, an
entitlement Apple grants per-app by written application. Going through
mDNSResponder needs no entitlement — the daemon holds the multicast privilege and
the app declares which services it wants. iOS is the primary target for this
project, so the path that can be released today wins. macOS shares the API, so
one shim covers both, mirroring the existing native-CoreBluetooth-scale-transport
precedent.

**Android / Windows / Linux: mjansson/mdns.** Already a declared dependency and
already the Android resolver. Version 1.4.3 has everything needed —
`mdns_query_send(MDNS_RECORDTYPE_PTR, ...)`, `mdns_record_parse_ptr`,
`_parse_srv`, `_parse_a`, `_parse_txt` — so this is new call-site code, not a new
library. It currently comes in behind `if(ANDROID)` at `CMakeLists.txt:200`–`207`
with a matching SYSTEM include at `:1159`–`:1165`; both guards widen. The
`SYSTEM` marking must be preserved on the new platforms — the header is
`-Wcast-align`-dirty BSD socket code and our build is `-Werror`.

*Alternative considered:* mjansson everywhere, and apply for the iOS multicast
entitlement. Rejected — one transport is genuinely simpler, but it puts an Apple
review gate on the release date for no user-visible gain.

*Alternative considered:* Qt's `QZeroConf` / `QDnsLookup`. `QDnsLookup` does
unicast DNS, not mDNS multicast; `QZeroConf` is a third-party module we don't
ship and would still wrap Bonjour on Apple.

### D3: Fallback always runs; it is not a browse-failure path

Both the browse and the `hds{,‑2,‑3}.local` A-record lookups fire on every
user-initiated scan. Running the fallback only when the browse comes back empty
would hide an old-firmware scale sitting on the same LAN as a new-firmware one,
which is exactly the household this feature is for.

Cost is two extra UDP queries inside the existing 5 s window.

### D4: Dedupe on resolved IP, browse wins

The two paths establish exactly one common identity: the resolved address.
Hostname is not usable (browse gives the SRV target, the fallback gives the
queried name, and they can differ in case or trailing dot), and TXT `name` is
absent from the fallback path entirely.

The browse row wins the merge because it carries instance name, port, path and
firmware version; the fallback row carries a hostname and nothing else.

*Edge case accepted:* a scale with two interfaces (AP + STA) could appear twice
at two addresses. Rare, harmless, and both rows work.

### D5: Use TXT `port` and `path` for the WebSocket URL, but do not trust `model`

Build the URL from the advertised `port` and `path` rather than hardcoding
`:80/snapshot`, so a firmware that relocates the endpoint keeps working.

`model=hds` is unauthenticated — anyone on the LAN can advertise it. It selects
which rows to *show*; it never substitutes for the existing HDS-frame validation
before connecting or persisting (`blemanager.cpp:1690`). That gate is the change's
security boundary and does not move.

### D6: Saved-scale rehydration keeps resolving one name

`blemanager.cpp:2067` stays a targeted lookup of the saved hostname. Browsing
there would invite "the saved scale is offline, connect to this other one
instead", which contradicts the existing requirement that an unreachable saved
scale must not silently fall through to a different device.

### D7: Result type is a struct, and the discovery API becomes list-shaped

`WifiScaleDiscovery::scaleFound(hostname, address)` becomes a result list of
`{instanceName, mdnsName, hostname, address, port, path, firmwareVersion,
foundBy}`. `foundBy` (browse vs fallback) drives both the dedupe precedence and
the debug log, and makes "why is this row unlabelled" answerable from a
user-shared log.

## Risks / Trade-offs

- **iOS Local Network permission is a hard gate.** A denied grant makes the
  browse silently return nothing, indistinguishable from "no scales here." →
  Log the distinction in the scale debug log (spec requires it), and keep manual
  entry reachable. Note the known local-dev wrinkle: ad-hoc-signed macOS debug
  builds lose the Local Network grant on every rebuild, which is a signing
  artefact, not a bug in this code (see `CLAUDE.local.md`).

- **Multicast is dropped by many APs and by most guest/IoT VLAN setups.** A
  browse can legitimately find nothing on a network where the scale is
  reachable by IP. → Manual entry stays; the log says the browse ran and found
  nothing.

- **Widening the mjansson include to desktop exposes new compilers to that
  header.** `-Werror` plus `-Wcast-align`-dirty BSD socket casts. → Keep
  `SYSTEM` on the include directory for every platform, not just Android; a
  first Windows/Linux build is the check.

- **Two transports means two code paths to keep correct**, and the Apple one is
  compiled only by the tag-push release workflow — a local macOS build does not
  cover the iOS shim. → CI test builds for iOS and Android are explicit tasks,
  not an afterthought.

- **Ghost registrations may outlive this change's testing.** Two of the four
  instances seen on the wire were stale, and there is no way to tell a stale
  registration from a scale that is merely slow to answer a resolve. → The
  resolve deadline (D4a) is the only discriminator; make it a named constant and
  log every dropped instance, so a user reporting "my scale doesn't appear" can
  be told whether it was dropped at resolve.

- **`hds-2` / `hds-3` may fire for nothing.** Confirmed on the wire: `hds-2.local`
  answered nothing while two scales were online. They are a guess at user habit. →
  Cost is bounded (two extra queries), and the comment states plainly that
  nothing generates these names, so nobody later "fixes" the fallback by
  extending it to `hds-4..N` believing it is protocol.

- **The result list changes the Add-WiFi-Scale dialog from a one-tap shortcut to
  a picker**, which is more UI for the single-scale user who was well served
  before. → With exactly one result, present it as the same one-tap affordance;
  only render a list at two or more.

## Migration Plan

No data migration. Saved `wifi:<hostname>` addresses keep working untouched —
the browse only affects what a *new* scan surfaces.

Rollback is a revert: the fallback path is a superset of today's behaviour, so
reverting to the single `hds.local` query loses discovery of renamed scales and
nothing else.

## Open Questions

- **Does the mjansson PTR path survive an OS mDNS responder already holding
  5353?** The library binds an ephemeral port and requests unicast replies, and
  our Android A-record query works that way today, so the mechanism is proven —
  but it has never been exercised on Windows or Linux in this codebase. Verify
  early, before the UI work; if a desktop platform misbehaves, that platform can
  fall back to the existing A-record-only path without blocking iOS.
- **Should a resolved-but-HDS-unvalidated scale be shown greyed rather than
  normally?** Proving a row is really an HDS costs a WebSocket probe per row at
  scan time. Deferred: show every *resolved* browse hit as a normal row (the
  resolve gate of D4a is separate and still applies) and run the HDS-frame
  validation on selection, which is what manual entry already does.
