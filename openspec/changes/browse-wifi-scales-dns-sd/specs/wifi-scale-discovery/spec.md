## MODIFIED Requirements

### Requirement: On-demand WiFi scale discovery

The app SHALL probe for Half Decent Scales (HDS) on the local network only when the user initiates a device scan from the Connections page. No background, idle, or app-start probing of WiFi scales SHALL occur unless a WiFi scale is saved as the primary scale.

A user-initiated probe SHALL consist of two parts run together:

1. A **DNS-SD service browse** for `_decentscale._tcp.local`, which finds every advertising scale on the LAN regardless of its mDNS name. This is the primary mechanism.
2. A **multi-name A-record fallback** covering `hds.local`, `hds-2.local` and `hds-3.local`, for firmware older than openscale v3.0.9 that advertises no service.

Both parts SHALL run for every probe — the fallback is not conditional on the browse returning nothing, because a LAN can hold both new and old firmware at once.

A single "Scan for Devices" press SHALL search every transport the app supports — Bluetooth, WiFi and USB — not Bluetooth alone. The three run concurrently. The A-record fallback SHALL complete within approximately 5 seconds, matching the timeout the code already uses for this path. The browse SHALL run for the whole scan cycle rather than a short snapshot, delivering rows incrementally, and SHALL stop when the scan stops. This does not constitute background probing — no browse SHALL be open outside a user-initiated scan cycle.

The scanning indicator SHALL remain active until **all three** transports have finished, so it answers "is the app still looking" rather than "is the Bluetooth agent still looking".

#### Scenario: User taps Scan for devices
- **WHEN** the user taps "Scan for devices" on the Connections page
- **THEN** the app starts the BLE scan, a DNS-SD browse of `_decentscale._tcp.local`, A-record lookups of `hds.local`, `hds-2.local` and `hds-3.local`, and a USB probe pass, all concurrently

#### Scenario: Scanning indicator spans all three transports
- **WHEN** the BLE scan finishes while the WiFi browse or the USB probe is still running
- **THEN** the control still reads "Scanning…" and remains disabled, clearing only once all three have finished

#### Scenario: USB is probed on demand, not only by its background poll
- **WHEN** the user taps "Scan for devices" with a USB scale attached
- **THEN** a USB probe pass starts immediately rather than waiting for the next poll tick, and its completion is one of the three conditions clearing the scanning indicator

#### Scenario: Results appear as they resolve
- **WHEN** one browsed instance resolves quickly and another is slow
- **THEN** the fast one's row appears immediately rather than waiting for the slow one or for the scan cycle to end

#### Scenario: Scan ends
- **WHEN** the scan cycle finishes
- **THEN** the browse is closed and no further mDNS traffic is generated

#### Scenario: Renamed scale is discovered
- **WHEN** a scale whose mDNS name has been changed from the default (so it answers at `<name>.local`, not `hds.local`) is on the LAN running firmware v3.0.9 or newer
- **THEN** the browse returns it and it appears in the discovered-scales list, even though no A-record fallback name matches it

#### Scenario: Several scales on one network
- **WHEN** two or more scales advertising `_decentscale._tcp` are on the LAN
- **THEN** the browse returns all of them and each appears as its own row in the discovered-scales list

#### Scenario: Pre-v3.0.9 firmware with the default name
- **WHEN** a scale runs firmware older than v3.0.9 (no DNS-SD advertisement) and still answers at `hds.local`
- **THEN** the A-record fallback resolves it and it appears in the discovered-scales list

#### Scenario: App starts with no saved WiFi scale
- **WHEN** the app starts and the saved primary scale address does not begin with `"wifi:"`
- **THEN** the app does not perform any WiFi scale discovery — no browse and no A-record lookup

#### Scenario: App starts with a saved WiFi scale
- **WHEN** the app starts and the saved primary scale address begins with `"wifi:"`
- **THEN** the app performs a single mDNS lookup of the saved hostname with a timeout of approximately 5 seconds, and on success opens a WebSocket to the scale; no browse is performed, because the saved address already names the intended scale

#### Scenario: Saved WiFi scale unreachable on app start
- **WHEN** the app starts with a saved WiFi scale address but the mDNS lookup fails or the WebSocket fails to connect
- **THEN** the app does not auto-connect to any other scale (mirroring current BLE behavior for an unreachable saved BLE scale per #440); the user must manually rescan to recover. In particular the app SHALL NOT browse and silently substitute a different discovered scale for the saved one.

#### Scenario: Discovery finds nothing
- **WHEN** the browse returns no services and every A-record fallback name times out or returns `HostNotFound`
- **THEN** no WiFi scale entry appears in the discovered-scales list and no error toast is shown (an offline WiFi scale is not an error)

### Requirement: WiFi scale appears in the unified scale list

The discovered-scales list SHALL contain one WiFi scale entry per discovered scale, alongside BLE scale entries, with a display name that distinguishes multiple scales and a transport tag.

The display name SHALL come from the DNS-SD instance name when the scale was found by browse — openscale publishes `Half Decent Scale` for an unrenamed scale and `Half Decent Scale (<name>)` for a renamed one — so the user sees the same label they set on the scale. A scale found only by the A-record fallback has no instance name available and SHALL fall back to a name derived from the hostname.

Instance names are not unique. DNS-SD resolves instance-name collisions by appending a suffix, so two unrenamed scales appear as `Half Decent Scale` and `Half Decent Scale-2` — observed on the wire. When two or more rows would otherwise carry indistinguishable or suffix-differentiated generic labels, the row SHALL additionally show the resolved hostname or address so the user can tell them apart.

#### Scenario: Scale discovered by browse, unrenamed
- **WHEN** the browse returns a service whose instance name is `Half Decent Scale`, resolving to host `hds.local`
- **THEN** the discovered-scales list contains an entry with `name = "Half Decent Scale (WiFi)"`, `address = "wifi:hds.local"`, `type = "decent-wifi"`, and `transport = "wifi"`

#### Scenario: Scale discovered by browse, renamed
- **WHEN** the browse returns a service whose instance name is `Half Decent Scale (kitchen)`, resolving to host `kitchen.local`
- **THEN** the discovered-scales list contains an entry with `name = "Half Decent Scale (kitchen) (WiFi)"`, `address = "wifi:kitchen.local"`, `type = "decent-wifi"`, and `transport = "wifi"`

#### Scenario: Scale discovered only by the A-record fallback
- **WHEN** `hds-2.local` resolves but no matching `_decentscale._tcp` service was browsed for that host
- **THEN** the discovered-scales list contains an entry with `address = "wifi:hds-2.local"`, `type = "decent-wifi"`, `transport = "wifi"`, and a display name derived from the hostname

#### Scenario: Two unrenamed scales collide on instance name
- **WHEN** two scales both publish the instance name `Half Decent Scale` and DNS-SD suffixes one of them to `Half Decent Scale-2`
- **THEN** both rows appear and each additionally shows its resolved hostname or address, so the user can distinguish them

#### Scenario: BLE scale entries are unchanged
- **WHEN** the BLE scan discovers a Decent Scale at a MAC/UUID address
- **THEN** the discovered-scales entry retains its existing shape, with `transport = "ble"` added; the `name` field has no suffix

#### Scenario: Same physical scale on both transports
- **WHEN** the user runs a scan and both BLE and WiFi paths resolve the same physical HDS unit
- **THEN** the discovered-scales list contains two distinct rows — one BLE, one WiFi — that can each be selected independently

## ADDED Requirements

### Requirement: Browse and fallback results are deduplicated

A scale that answers both the DNS-SD browse and an A-record fallback name SHALL produce exactly one row in the discovered-scales list. Deduplication SHALL key on the resolved IP address, because that is the one identity both paths establish; the browse row SHALL win, since it carries the instance name, port, path and firmware version that the A-record path cannot supply.

#### Scenario: Default-named scale found by both paths
- **WHEN** an unrenamed scale on v3.0.9+ firmware is returned by the browse at `192.168.1.50` and also resolved by the `hds.local` A-record fallback to the same `192.168.1.50`
- **THEN** exactly one row appears in the discovered-scales list, carrying the browse result's instance name and TXT metadata

#### Scenario: Two scales at different addresses
- **WHEN** the browse returns one scale at `192.168.1.50` and the fallback resolves `hds-2.local` to `192.168.1.51`
- **THEN** two rows appear, one per address

### Requirement: Only resolved browse instances are shown

A DNS-SD browse returns service instance names, some of which are stale registrations that never resolve — on a network with two live scales, four instances were observed, of which two never resolved within 10 seconds and did so persistently across repeated browses.

An instance SHALL therefore appear in the discovered-scales list only after its SRV and address records resolve successfully within a named per-instance resolve deadline. An instance that fails to resolve SHALL be dropped, not rendered as an unselectable or failing row. The resolve deadline is independent of the A-record fallback's ~5 second window.

Within a scan cycle the discovered-devices list SHALL be add-only: a row that has been shown to the user SHALL NOT be removed before the scan ends. Keeping ghosts out is the job of the resolve gate above, not of retracting a row the user has already seen. Withdrawal callbacks arriving mid-scan SHALL be logged and otherwise ignored; the list is rebuilt on the next scan, which is where a departed device disappears.

This does not apply to a USB scale being physically unplugged, which removes its row immediately as it does today — that is a device going away, not discovery churn.

#### Scenario: Stale instance that never resolves
- **WHEN** the browse returns an instance name whose SRV/address resolution does not complete within the resolve deadline
- **THEN** no row appears for that instance, and the scale log records the instance name and that it was dropped at resolve

#### Scenario: Instance withdrawn mid-scan
- **WHEN** an instance is listed and the system resolver subsequently withdraws it because nobody answered a re-query
- **THEN** its row remains in the list for the rest of the scan cycle, and the withdrawal is recorded in the scale log only

#### Scenario: Departed device disappears on the next scan
- **WHEN** a scale that was listed in the previous scan is no longer present and the user scans again
- **THEN** the list is rebuilt without it

#### Scenario: USB scale unplugged
- **WHEN** a connected USB scale is physically unplugged
- **THEN** its row is removed immediately, whether or not a scan is in progress

#### Scenario: Live instances alongside stale ones
- **WHEN** a browse returns four instances of which two resolve to live scales
- **THEN** exactly two rows appear in the discovered-scales list

### Requirement: TXT fields are optional and loosely formatted

The driver SHALL treat every TXT key as optional and SHALL NOT assume a strict value format. Observed on shipping firmware: an unrenamed scale on v3.1.12 publishes no `name` key at all, and the `fw` value is `FW: 3.1.12` — a prefixed string containing a space, not a bare version.

#### Scenario: TXT record with no name key
- **WHEN** a browse result's TXT record contains `path`, `proto`, `model` and `fw` but no `name`
- **THEN** the result is accepted, the row falls back to the instance name or hostname for its label, and no error is logged

#### Scenario: Firmware version carries a prefix
- **WHEN** a browse result's TXT record contains `fw=FW: 3.1.12`
- **THEN** the displayed and compared firmware version is the parsed `3.1.12`, and a value that does not parse as a version is displayed verbatim rather than discarded or treated as an error

#### Scenario: Unknown TXT keys
- **WHEN** a browse result's TXT record contains keys the app does not consume
- **THEN** they are ignored without error

### Requirement: TXT metadata is not proof of identity

The DNS-SD TXT record (`model=hds`, `proto=ws`, `path=/snapshot`, `fw=<version>`) is unauthenticated and SHALL be treated as a hint only. Before an endpoint discovered by browse is connected to or persisted as the saved scale, it SHALL pass the same `ws://<host><path>` HDS-frame validation that manually entered addresses already pass — a valid snapshot or `type:"status"` frame within the recognition window.

#### Scenario: Browse hit that is not really an HDS
- **WHEN** the user selects a browsed entry whose host accepts the WebSocket but sends no recognizable HDS frame within the recognition window
- **THEN** the connection is rejected with the existing "did not respond as HDS" error and the address is not persisted as the saved scale, despite the TXT record having claimed `model=hds`

#### Scenario: Browse hit that validates
- **WHEN** the user selects a browsed entry and the WebSocket delivers a valid HDS frame within the recognition window
- **THEN** the connection is established and `"wifi:<hostname>"` is persisted as the saved primary scale address, unchanged from the existing manual-entry behavior

#### Scenario: TXT path is used for the WebSocket URL
- **WHEN** a browse result carries `path=/snapshot` and `port=80`
- **THEN** the app connects to `ws://<host>:<port><path>` using those values rather than hardcoded ones, so a future firmware that moves the endpoint still works

### Requirement: Service browse works on every supported platform without an entitlement request

The DNS-SD browse SHALL be available on Windows, macOS, Linux, Android and iOS. On Apple platforms it SHALL be implemented through the system Bonjour APIs with `_decentscale._tcp` declared in `NSBonjourServices`, and SHALL NOT require the `com.apple.developer.networking.multicast` entitlement, which is granted only by per-app application to Apple and would block shipping.

#### Scenario: Browse on iOS
- **WHEN** the user taps "Scan for devices" on iOS with Local Network access granted
- **THEN** advertising scales are discovered, using the system Bonjour browse, with no multicast entitlement present in the app

#### Scenario: Local Network permission denied on iOS
- **WHEN** the user has denied Local Network access
- **THEN** the browse returns no results and the app surfaces the existing scale-log diagnostic rather than failing silently or crashing

#### Scenario: Browse on Android
- **WHEN** the user taps "Scan for devices" on Android
- **THEN** advertising scales are discovered via the mDNS PTR/SRV/TXT query path, consistent with the existing Android A-record resolver

### Requirement: Discovery diagnostics cover the browse

The scale debug log SHALL record what the browse did, at the same level of detail the existing A-record probe records, so a user-shared log explains a discovery failure without needing a console.

#### Scenario: Browse produces results
- **WHEN** a browse completes and returns one or more services
- **THEN** the scale log records the service type queried, the number of results, and each result's instance name, host, address and firmware version

#### Scenario: Browse returns nothing
- **WHEN** a browse completes with no results
- **THEN** the scale log records that the browse ran and found nothing, distinguishably from the browse not having been attempted

#### Scenario: Fallback names are logged individually
- **WHEN** the multi-name A-record fallback runs
- **THEN** the scale log records the outcome for each of `hds.local`, `hds-2.local` and `hds-3.local` separately, so a partial result is diagnosable
