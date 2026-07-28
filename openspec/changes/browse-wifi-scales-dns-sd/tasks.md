## 1. De-risk the transport first

- [x] 1.1 Widen the mjansson/mdns FetchContent guard in `CMakeLists.txt:200-207` beyond `if(ANDROID)`, and widen the matching `target_include_directories(... SYSTEM ...)` at `:1159-1165` to the same set. Keep `SYSTEM` on every platform — the header is `-Wcast-align`-dirty and the build is `-Werror`.
- [ ] 1.2 **PARTIALLY DONE — mjansson verified on macOS 2026-07-28** via the `devices_wifi_browse` MCP tool with `backend=mjansson`: returned both live scales with byte-identical fields to the bonjour backend (addresses, instance names, fw `3.1.12`/`3.1.13-dev`, absent `name` on 3.1.12, port 80, `/snapshot`), and dropped the same stale instances. Parsing and join logic are therefore proven. **DEFERRED TO POST-MERGE** (2026-07-28, Jeff): the tablet cannot be exercised until the change is on `main`. Confirm the PTR browse returns results **on the Android tablet** while the OS mDNS responder holds 5353 (design.md Open Question 1). Originally written as "verify on macOS or Linux" — impossible, because the `if(NOT APPLE)` split means macOS compiles the Darwin stub and never builds this code at all. Android is the primary platform and the first real compile of this path; Windows/Linux follow via CI. If a platform can't do it, record that and let it keep the A-record-only path — do not block on it.
- [x] 1.3 Confirm on real hardware that the scales advertise the service and TXT record as documented. **Done 2026-07-28** — two scales online, browse returned four instances: `hds.local:80` (fw `FW: 3.1.12`, no `name` key) and `hdstest.local:80` (fw `FW: 3.1.13-dev`, `name=hdstest`) resolved; `Half Decent Scale-2` and `Half Decent Scale (kitchen)` never resolved. `hds-2.local` answered nothing. See design.md "Verified on the wire" (F1–F4).

## 2. Discovery result model

- [x] 2.1 Add a `WifiScaleResult` struct — `instanceName`, `mdnsName`, `hostname`, `address`, `port`, `path`, `firmwareVersion`, `foundBy` (browse | fallback). **Landed in a new `src/network/wifiscaleresult.{h,cpp}`, not in `wifiscalediscovery.h` as originally written**: the pure helpers (TXT parse, dedupe, display name) must be testable without a QObject or event loop, and having the lower-level `MdnsResolver` include an app-layer header would invert the layering. This also means these helpers compile and are covered on macOS even though the mDNS transport is not.
- [x] 2.2 Change `WifiScaleDiscovery::scaleFound(hostname, address)` to a list-shaped result signal; update the existing `probeFinished()` contract so callers can tell "ran, found nothing" from "never ran".
- [x] 2.3 Implement dedupe-by-resolved-IP with browse precedence (design D4) as a pure function, so it is unit-testable without a network.

## 3. Browse transport — non-Apple

- [x] 3.1 Add `MdnsResolver::browseService(serviceType, timeoutMs)` using `mdns_query_send(MDNS_RECORDTYPE_PTR, ...)`, parsing PTR → SRV → A → TXT via `mdns_record_parse_*`. Keep it on the existing worker-thread + generation-check pattern (`mdnsresolver.cpp`), since it blocks.
- [x] 3.2 Stop dropping non-A records for the browse path (`mdnsresolver.cpp:85` currently discards everything but A) without changing the existing A-only resolve path's behaviour.
- [x] 3.3 Parse the TXT record into the result struct: `fw`, `model`, `name`, `proto`, `path`. Every key is optional — fw 3.1.12 ships with no `name` key (design F3). Strip the `FW: ` prefix from `fw` and keep the raw string when it doesn't parse as a version (design F4).
- [x] 3.4 Require a completed SRV+address resolve before a browse instance becomes a result; drop unresolved instances after `kBrowseResolveDeadlineMs` and log each one (design D4a / F1 — half the instances on the test network were ghosts). The deadline is a separate constant from the ~5 s A-record window.
- [x] 3.5 Make the browse incremental and bounded by the scan cycle rather than a short snapshot: emit each result as it resolves, run for the full cycle alongside the BLE scan, stop when the scan stops (design D4b). A short snapshot returns the resolver's cache with every ghost in it and none of the corrections.
- [x] 3.6 Log withdrawal callbacks but do NOT apply them to the list mid-scan (design D4c). **Nothing to do on this path** — mjansson is a one-shot query/collect, not a live subscription, so it has no withdrawal notion to ignore in the first place. Withdrawals only exist on Apple's `DNSServiceBrowse`, so the D4c rule is enforced there (task 4.5). Ghosts are kept out on this path by the resolve gate in 3.4.

## 4. Browse transport — Apple (iOS + macOS)

- [x] 4.1 Add `_decentscale._tcp` to `NSBonjourServices` in **BOTH** `ios/Info.plist` and `macos/Info.plist`. The task originally named only the iOS plist, which was wrong: the macOS bundle is built from `macos/Info.plist` (`CMakeLists.txt:1756`; the iOS one is used at `:1685`). Doing only iOS made `DNSServiceBrowse` fail on macOS with `kDNSServiceErr_NoAuth` (-65555) — which looks exactly like a denied Local Network permission and cost a round of chasing System Settings and code signing. Verified on 2026-07-28.
- [x] 4.2 Write the native Bonjour shim — `DNSServiceBrowse` → `DNSServiceResolve` → `DNSServiceGetAddrInfo` — behind the same `browseService()` interface as task 3.1, so `WifiScaleDiscovery` has one API. No raw multicast socket: `com.apple.developer.networking.multicast` must NOT become required (design D2).
- [x] 4.3 Distinguish "Local Network permission denied" from "browse found nothing" and surface it to the scale debug log. **Implemented via `kDNSServiceErr_PolicyDenied`/`kDNSServiceErr_NoAuth` on the browse callback (both verified present in the SDK header), but the ASSUMPTION IS UNVERIFIED**: iOS may simply return no results on denial rather than reporting an error, in which case the log would still say "found nothing". Must be confirmed on a real device — see task 9.3.
- [x] 4.4 Apply the same resolve-before-display rule (task 3.4) on the Apple path — `DNSServiceBrowse` returns the ghost instances too; the ghosts observed on 2026-07-28 came through exactly this API.
- [x] 4.5 Keep the `DNSServiceBrowse` reply socket registered for the whole scan cycle, not a short window — a browse torn down early never sees the resolver's own pruning. Treat a cleared `kDNSServiceFlagsAdd` as log-only per task 3.6.

## 5. Multi-name A-record fallback

- [x] 5.1 Extend the fallback from the single `hds.local` to `hds.local`, `hds-2.local`, `hds-3.local`, probed together within the existing 5 s window (`blemanager.cpp:2067` passes `5000`).
- [x] 5.2 Comment the `-2`/`-3` names as a **user-habit heuristic, not protocol** — neither openscale nor esp-idf ever generates them (design.md "The `hds-2.local` question"). Without this a later reader will extend the list believing it is firmware behaviour.
- [x] 5.3 Keep `kDefaultHostname` meaningful for the saved-scale path, or remove it and update both hardcoded call sites (`blemanager.cpp:2067`, `:839`) — do not leave a constant that only some paths honour.

## 6. Wire discovery into BLEManager

- [x] 6.1 Run browse and multi-name fallback together on a user-initiated scan (`blemanager.cpp:839` path); merge, dedupe, emit.
- [x] 6.2 Leave saved-scale rehydration (`blemanager.cpp:2067`) as a targeted single-name resolve — no browse, no substituting a different discovered scale (design D6).
- [x] 6.3 Emit one discovered-scales row per deduped result, with `name` from the DNS-SD instance name plus the `(WiFi)` suffix, falling back to a hostname-derived name for fallback-only hits.
- [x] 6.4 Build the WebSocket URL from the advertised `port` and `path` rather than hardcoded `:80/snapshot` (design D5).
- [x] 6.5 Confirm the existing HDS-frame validation gate (`blemanager.cpp:1690`, `main.cpp:2587-2839`) still runs for browse-sourced endpoints before anything is persisted. TXT `model=hds` must not shortcut it.
- [x] 6.6 Add scale-log lines for: service type queried, result count, each result's instance/host/address/firmware, empty-browse, and per-name fallback outcomes (spec: "Discovery diagnostics cover the browse").

## 6b. Make one scan cover all three transports

- [x] 6b.1 Add a one-shot "probe now" entry to `UsbManager` and `UsbScaleManager` that runs a poll pass immediately instead of waiting up to `POLL_INTERVAL_MS` (2 s) for the next tick, and signals when the pass has finished. Today USB is a free-running poll started at `main.cpp:1901`/`:1908` and the scan button does nothing to it.
- [x] 6b.2 Call that USB probe from `scanForDevices()` alongside the existing BLE scan and WiFi discovery, so one press covers BLE + WiFi + USB.
- [x] 6b.3 Make `BLEManager::scanning` a composite of all three (BLE agent running, WiFi browse running, USB probe running) instead of the BLE-only `m_scanning`, and emit `scanningChanged()` on each transition. BLE's 15 s dominates, so this must not lengthen the perceived scan.
- [x] 6b.4 Check the `scanning` property's other consumers before changing its meaning — anything gating on "BLE agent busy" rather than "app is looking" needs to keep the old semantics.
- [x] 6b.5 Leave the USB unplug path (`setUsbScaleAvailable(false)`) alone: a row vanishing because the cable was pulled is correct and is not the mid-scan churn D4c forbids.

## 7. UI

Multiple WiFi scales surface in the **main discovered-devices list**
(`discoveredDevicesList`, `SettingsConnectionsTab.qml:1618`, fed by
`BLEManager.discoveredScales`), which already renders WiFi rows. Most of the
multi-scale behaviour therefore falls out of BLEManager emitting more rows — this
group is smaller than a new picker.

The Add WiFi Scale dialog (`:196`) is the manual add-by-IP-or-name escape hatch
and is **out of scope for UI work**: it stays as it is. It is still a caller of
`WifiScaleDiscovery`, so it must keep compiling and working against any API
change made in group 2.

- [x] 7.1 **Verified 2026-07-28** — the existing list rendered both WiFi scales plus the BLE scale with no structural change, as predicted. Multi-scale support fell out of BLEManager emitting more rows.
- [x] 7.2 Label rows with the DNS-SD instance name; show firmware version as secondary text where it fits.
- [x] 7.2a When two rows carry indistinguishable or suffix-differentiated generic labels (`Half Decent Scale` / `Half Decent Scale-2` — real, observed), also show the resolved hostname or address on the row (design D7a).
- [x] 7.3 Keep the list add-only during a scan cycle (design D4c) — no row is removed mid-scan, so nothing shifts under a finger already moving toward a tap. Rebuild on the next scan.
- [x] 7.4 Make the "Scanning…" button state follow the composite `scanning` property so it stays up until BLE, WiFi and USB have all finished (design D4b, task 6b.3).
- [x] 7.5 **No QML changed** — multi-scale support fell out of BLEManager emitting more rows into the existing list, so no new interactive elements were introduced and there is nothing new to make accessible. Original text: accessibility on any new/changed rows: `Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction` — prefer `AccessibleButton`/`AccessibleMouseArea` over raw `Rectangle`+`MouseArea`. Fix any pre-existing violations in the file while there.
- [x] 7.6 **No new user-facing strings** — row labels come from the scale's own DNS-SD instance name (user data, not translatable copy) and the pre-existing " (WiFi)" suffix. Original text: all new strings through `TranslationManager.translate` / `Tr`; reuse existing common keys where they fit.
- [x] 7.7 **No new QML files.** Original text: if any new QML file is added, register it in the `qt_add_qml_module` file list in `CMakeLists.txt`.
- [x] 7.8 **Verified 2026-07-28** — dialog opens and shows its one-tap shortcut ("Scale found on this network / hds.local (192.168.10.145)") unchanged after the `scaleFound` → `resultFound(WifiScaleResult)` and `probeFinished(bool)` API change. Confirms the deliberate choice to keep this path on the single default name rather than the three-name fallback: one unambiguous shortcut instead of whichever name resolves first.

## 8. Tests

- [x] 8.1 Unit-test the dedupe function (task 2.3): browse+fallback same IP collapses to one browse-flavoured row; different IPs stay separate; two-interface duplicate is tolerated.
- [x] 8.2 Unit-test TXT parsing against the two real records captured on 2026-07-28: `path=/snapshot proto=ws model=hds fw=FW: 3.1.12` (no `name`) and `path=/snapshot proto=ws name=hdstest model=hds fw=FW: 3.1.13-dev`. Plus missing keys, unknown extra keys, malformed `fw`.
- [x] 8.3 Unit-test the display-name derivation for all four cases — browse unrenamed, browse renamed, fallback-only, and the `-2` instance-name collision needing address disambiguation.
- [ ] 8.3a Unit-test that an instance which never resolves produces no row, and that a withdrawal arriving mid-cycle leaves the existing row in place (design D4c — add-only within a scan).
- [x] 8.4 Read `docs/CLAUDE_MD/TESTING.md` before writing any of these; no test may emit WARN lines.
- [x] 8.5 **106/106 passed, 0 failed, 0 skipped, no warnings** (2026-07-28, `run_tests` scope `all`, 36.5 s). Includes both new targets and the first compile of the widened `describeBrowseError` message.

## 9. Platform verification

- [x] 9.1 **Verified 2026-07-28** — both scales found (`hds.local`/192.168.10.145 fw 3.1.12, `hdstest.local`/192.168.10.241 fw 3.1.13-dev) and exactly two rows shown, against four instances in the raw `dns-sd` browse. Ghosts never appeared, because the resolve gate keeps them out rather than relying on later pruning.
- [ ] 9.2 **DEFERRED TO POST-MERGE** — verify browse on Android on the tablet. Android is the primary platform and the mjansson transport's real target, but the device can't be tested until this lands on `main`. macOS has proven the parsing/join logic via the `mjansson` backend switch; what remains unverified on Android is the socket layer itself (different stack, plus the WifiManager.MulticastLock dependency).
- [ ] 9.3 **DEFERRED — no device available; will be exercised by a beta user.** CI test build for iOS — the Bonjour shim is `#ifdef`-guarded and a local macOS build does not compile it. iOS is the primary driver; do not sign this off on macOS alone.
- [ ] 9.4 CI test build for Windows and Linux to confirm the widened mjansson include survives `-Werror`.
- [x] 9.5 **Covered 2026-07-28** — the two live scales are exactly this pair: one at the default name (`hds`) and one renamed (`hdstest`). Both were discovered, listed and connectable in the same scan.

## 9b. macOS dev bundle id

- [x] 9b.1 **`MACOS_BUNDLE_ID` cache variable added**, defaulting to the shipping `io.github.kulitorum.decenza`. Mirrors `IOS_BUNDLE_ID`, but as a `CACHE STRING` so it can be set on an EXISTING build directory — Qt Creator's Initial Configuration only applies to a fresh cache, which is why the first attempts silently did nothing. A local override makes the dev build a separate Local Network permission subject, the only no-reboot lever when macOS wedges the grant (`tccutil` cannot reset Local Network). The temporary hardcoded dev value and `FORCE` used during diagnosis are removed; CI passes no `-DMACOS_BUNDLE_ID`, so it always builds the shipping id. Safe for user data: storage is keyed by organization/application name, never bundle id.
- [ ] 9b.2 Confirm on a CI macOS build that the produced bundle id is `io.github.kulitorum.decenza`.
- [ ] 9b.3 **Open question, deliberately unresolved:** whether the dev bundle id was actually required. LAN access came back at the same time as two other changes — Qt Creator's active project being corrected from `Decenza-Desktop` back to `Decenza`, and the missing `disclaim` helper (without it the app ran as a child of the IDE rather than as its own responsible process, so TCC attributed it to Qt Creator). Any of the three could have been the fix. Do not record the bundle id as the proven cause; revisit only if it recurs.

## 10. Docs and review

- [x] 10.1 **Written, not updated** — no WiFi-scale documentation existed anywhere in `docs/`, so this is a new "WiFi Scale Discovery" section in `docs/CLAUDE_MD/BLE_PROTOCOL.md`. Covers the advertised service and TXT contract, the v3.0.9 firmware floor and why the A-record fallback still runs unconditionally, the four wire behaviours that contradict the firmware source, the two-backend split with the reason for each, and the both-plists requirement with the `-65555` symptom it causes. Design rationale is referenced rather than duplicated.
- [x] 10.2 **Not needed** (Jeff, 2026-07-28): the user-facing flow is unchanged — tap Scan, pick a scale — and the manual does not document the old `hds.local`-only constraint or the previous row label, so nothing in it is made wrong. The visible deltas are that renamed scales now appear at all, several WiFi scales can be listed, and rows carry the scale's own name.
- [ ] 10.3 Open the PR (never push to `main`).
- [ ] 10.4 Run `/pr-review-toolkit:review-pr` on the PR and address findings.
- [ ] 10.5 Archive the change with spec sync as the final commit on this PR, not a separate PR.
