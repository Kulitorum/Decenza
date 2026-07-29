## 1. Registry and the INFO tier

- [x] 1.1 Create the marker registry header (`src/core/logtags.h`): one entry per subsystem carrying its token constant and a one-line description of what it covers. Seed it with `[Scale]`, `[DE1]` and `[Refractometer]`. This is the single source every other surface derives from — no other file may hold a list of markers.
- [x] 1.2 Add the INFO tier to the scale helpers in `src/ble/scales/scalelogging.h`: `SCALE_INFO_TAGGED` / `SCALE_INFO` (qInfo), plus the stderr-only `SCALE_INFO_STDERR_TAGGED` for symmetry with the existing set. Reference the registry constants rather than re-spelling `"[Scale]"`.
- [x] 1.3 Document in `scalelogging.h` how a tier is chosen (audience, not authorship) so the next author picks one deliberately rather than copying the nearest call.
- [x] 1.4 Add the shared `DECENZA_SUBSYS_LOG` / `DECENZA_SUBSYS_LOG_STDERR` base to the registry header so the `[marker][tag] ` shape and the write-then-emit pairing exist once, and rebase the scale helpers onto it.
- [x] 1.5 Give the refractometers their own marker: `refractometerlogging.h` with the three tiers, and R1/R2 aliasing it instead of `SCALE_LOG`. The shared transports keep `[Scale]` — they are scale plumbing the refractometers borrow.
- [x] 1.6 Build and run the full suite — no behaviour change expected yet.

## 2. Assign scale tiers

- [x] 2.1 `blemanager.cpp` — the ~60 `appendScaleLog` sites: promote the connection narrative (scan lifecycle, device found, connecting, connected/disconnected, transport selection and WiFi→BLE fallback, reconnect scheduling, the slow-tail crossing) to INFO; leave internal detail at DEBUG. Keep every existing WARN at WARN.
- [x] 2.2 The 13 BLE scale drivers — keep frame/protocol traffic at DEBUG; promote connect/disconnect/identify outcomes to INFO.
- [x] 2.3 The transports (`qtscalebletransport.cpp`, `corebluetoothscalebletransport.mm`) — DEBUG for characteristic and discovery mechanics, INFO for link established/lost, existing WARN unchanged.
- [x] 2.4 `usbscalemanager.cpp` / `usbdecentscale.cpp` — INFO for device found, probe confirmed, connect/disconnect, unplug; DEBUG for probe byte-level detail.
- [x] 2.5 The refractometers (`difluidr1.cpp`, `difluidr2.cpp`) — assign tiers now that they carry `[Refractometer]`: INFO for connect/disconnect and completed measurements, DEBUG for packet framing and checksum detail, WARN for no-liquid/beyond-range/decode failures.
- [x] 2.6 `wifiscalediscovery.cpp` — INFO for the browse/lookup outcomes the `wifi-scale-discovery` spec requires to be diagnosable from a shared log.
- [x] 2.7 Review the resulting INFO set as a whole: read the INFO-only scale lines for a full scan → connect → shot → disconnect cycle and judge it as a narrative. Fix anything that reads as noise or has a hole. Do not evaluate sites in isolation.
- [x] 2.8 Build and run the full suite; confirm no test newly trips `QTest::failOnWarning()`.

## 2b. Act on the 48 h capture (build 3507)

- [x] 2b.1 Demote `[ScaleFeed] alive: constant weight …` from qInfo to qDebug — 59 lines proving a NON-bug, on a 2 s dedupe window.
- [x] 2b.2 Demote `Battery byte changed: … (62 → 61)` from WARN to DEBUG; ordinary discharge read as a fault in every log. Test expectation updated.
- [x] 2b.3 Log the backoff policy mode only on transition, not on every settings load (11 identical WARNs for an unchanged condition). Guards the logging only — the latch load below must still run.
- [x] 2b.4 Add `scaleRepeatFailure()`: WARN for the first 3 of a failure run, DEBUG after, reset on connect. Fixes 46 × "connection timeout" + 24 × "unreachable" at flat WARN.
- [x] 2b.5 Demote the driver-level "Connecting to X" lines back to DEBUG — BLEManager already logs one INFO per attempt, so at INFO these repeated once per retry forever.
- [x] 2b.6 Unify the multi-model wording: canonical `DECENZA_BLE_MSG_*` constants in the registry header for the events every driver reports, replacing 4 different spellings of the connect moment and one disconnect outlier.
- [x] 2b.7 Route the last hand-rolled markers through helpers (both BLE transports, machinestate's weight trace) so no call site composes `[Scale]` itself.
- [ ] 2b.8 Re-capture 48 h on a build with #1700/#1703 + this work and re-measure. The 1,147 duplicate lines and the 20 s/60 s-forever reconnect cadence are already fixed on main, so the baseline is stale — confirm the new shape rather than assuming it.
- [ ] 2b.9 Decide whether `[SAW]` / `[SAW-Worker]` (218 lines) — and `[SAW-Latency]`, a third SAW prefix with one site at `de1device.cpp`'s `onTransportWriteComplete`, not included in that count — should join the registry. Already DEBUG so out of the views and out of INFO queries; it is shot logic rather than a device, so it is out of this change's scope by design. Left as an explicit decision, not an oversight.

## 3. DE1 helpers, marker and tiers

- [x] 3.1 Add DE1 logging helpers with the `[DE1]` marker from the registry, three tiers plus stderr-only variants, mirroring the scale set. Alias the shared macro bodies — do not copy them. (`src/ble/de1logging.h`.)
- [x] 3.2 `bletransport.cpp` — its existing `log()`/`warn()` helpers gain the marker and an INFO tier; `[BLE DE1]` becomes `[DE1][BLE]`. The four Android JNI shims go to `[DE1][Android]`.
- [x] 3.3 `serialtransport.cpp` — add the helpers it lacks; convert its 21 hand-rolled `[USB]` sites to `[DE1][Serial]`. (21, not the estimated 11.) Ten of them were `emit logMessage` with no stderr write at all, so they reached the window and never the log.
- [x] 3.4 `usbmanager.cpp` — add helpers; convert its 41 hand-rolled `[USB]` sites to `[DE1][USB]`. (41, not 23 — 15 were qDebug/emit PAIRS describing one event in two different wordings, now one call each.) Also removed an unreachable `if (ports.isEmpty())` nested inside `if (!ports.isEmpty())`.
- [x] 3.5 `de1device.cpp` — route its hand-rolled prefixes and unprefixed stderr lines through the helpers, assigning a tier to each. The five existing sub-prefixes (`[MMR]`, `[firmware]`, `[Phase]`, `[WaterLevel]`, `[ShotSettings]`) became tags inside `[DE1]`, so the per-area greps still work. DEBUG uses the stderr-only variants deliberately — but note the reason is forward-looking, not current: the DE1 view has NO level threshold today (it is an uncapped `de1LogText.text += message`; INFO+ sourcing is 5.1/5.2 below), so this anticipates that contract and meanwhile stops DEBUG detail accumulating in an unbounded QML string. Separately, the two `*_WARN_STDERR` aliases are *required*, not chosen: their only call sites are `const` members, and moc generates signal emitters as non-const.
- [x] 3.6 Convert the 18 `emit de1LogMessage(...)` sites in `blemanager.cpp` to tiered helper calls (permissions, scan lifecycle, DE1 found, direct-wake, errors). The signal still carries the BARE message so the existing window renders unchanged. Also collapsed three more qDebug/emit drift pairs and a triple (`scan error` said itself three times in two phrasings).
- [x] 3.7 Review the DE1 INFO set as a narrative, as in 2.7 — this is the first time these lines land in a log, so read them as a stranger would. Found three holes and fixed them: (a) no canonical "the machine is usable now" line at all — every transport announced its own connection in its own terms and none said the DE1 was up, so `DE1Device` now logs `DE1 CONNECTED (<transport>)` / `DE1 DISCONNECTED`; (b) no INFO for the connect *attempt* on the scan-discovered path, so a connect that never completed read as a scan that found the machine and stopped; (c) `MMR-write guard ENGAGED` was my own over-promotion — flash mechanism, not narrative, back to DEBUG.
- [x] 3.8 Build and run the full suite. 108/108, 0 warnings.
- [x] 3.9 `DE1 DISCONNECTED` is INFO, not WARN (unlike `ScaleDevice`'s counterpart). A bare disconnect line cannot tell a mid-shot drop from the app closing — it fires on every shutdown — so warning on it is the cry-wolf pattern 2b removed. What makes a disconnect a problem is warned where that is known (`de1LinkFault`, write-abandoned, the reconnect ladder). Recorded rather than copied to the scale side; see 3.11.
- [x] 3.10 Deleted `src/ble/new_permission_func.txt` and `src/ble/blemanager_patch.txt` — a tracked, stale copy of the very permission function being edited here, plus a one-line note. A checked-in copy of live code is the drift hazard this change exists to remove.
- [ ] 3.11 Decide whether `ScaleDevice`'s `DISCONNECTED` should follow 3.9 down to INFO. It is long-shipped at WARN and the two subsystems now disagree; the argument for INFO is identical. Not changed blind.
- [x] 3.12 Audit every line that has never been in a log before deciding to keep it — the goal is a smaller, wholly useful log, not a tagged version of the old one. Net result: **six sites deleted, not re-tagged**, and the remaining ones made to say something.
  - `serialtransport.cpp`'s per-frame `RX [M] 19 bytes` — **deleted.** One line per notification. `subscribeAll()` subscribes to **seven** characteristics; the periodically-notifying ones (ShotSample alone is ~5 Hz per `BLE_PROTOCOL.md`) put it on the order of 20 lines/s, ~600 per shot. It reached only the connections-page DE1 window, which is **not** a bounded ring — it is an uncapped `de1LogText.text += message` with no Clear button, so those 600 lines a shot grew a QML string for the process lifetime. Routing it to the log would have made DE1 telemetry the largest single thing in a submitted log while proving only that a frame arrived — which the parsed values and the shot record already do, and which `BleTransport::onCharacteristicChanged` logs no equivalent for. The two anomaly cases (`RX unknown`, `RX unknown letter`) are kept: those are what a reader is hunting.
  - `"Checking permissions..."`, `"Permissions OK"`, `"Location permission granted"`, `"Bluetooth permission granted"` — **deleted.** Each is a non-event whose successor line says it louder ("Scanning for devices..." follows within milliseconds).
  - The two `"Requesting <X> permission..."` lines — **kept at INFO and rewritten to say why** the permission is wanted. They are the only explanation for a log that stops dead because a system dialog is waiting on the user.
  - The four `"denied"` lines — **kept at WARN and rewritten to say it is terminal** until changed in OS Settings. "Denied" alone does not earn a WARN; the consequence is the content.
  - `bletransport.cpp`'s `"Connecting to DE1 at %1"` — **reverted to DEBUG.** I promoted it, then checked what precedes it: `"Found DE1: <name> (<id>)"` on the scan path and `"Direct wake: connecting to <name> at <addr>"` on the other, both with the same identifier. A third telling of one fact.
- [x] 3.14 Read a whole real session at INFO+ on the running build (28 lines for 38 s of startup) rather than judging sites in isolation. It caught a mistake the code review could not: `[DE1] Scanning for devices...` followed 207 ms later by `[DE1] Scan stopped`, which in a `[DE1]` filter reads as "looked for the machine, gave up" — while the `[Scale]` lines in between show the scan was a WiFi→BLE **scale** fallback that found its scale and stopped. One BLE scan serves DE1, scales and refractometers, so bracketing it under `[DE1]` overclaims. All three DE1 scan-lifecycle lines demoted to DEBUG; the scale side keeps INFO, where the scan cycle genuinely is the narrative. `Found DE1:` remains the machine's unambiguous story.
- [ ] 3.15 Non-device INFO+ noise the same session exposed. **Out of this change's scope** — recorded so it is not lost, and MQTT is explicitly excluded.
  - `WARN QSocketNotifier::Exception is not supported on iOS`, twice, **on macOS**. A Qt-internal warning with the wrong platform in it, unactionable, at the tier that means "look here". Wants a logging-filter rule.
  - `INFO [FontProbe]` ×2 at every startup, one of them ~300 characters, both of whose content is "this is normal and NOT a crash indicator". A line that says "ignore me" has not earned INFO.
  - `INFO McpTunnelTsnet: backend state -> "NoState"` — a state machine's internal enum name; "NoState" tells a reader nothing.
  - `WARN McpRemoteAccess: Funnel reachability probe failed … (attempt 1/2/3)` then success at 24.9 s. Three WARNs for a probe that always takes a few tries while the tunnel comes up; the success line is the story. Same repeat-failure shape as 2b.4.
- [ ] 3.13 Measure `[DE1][MMR] keepalive:` before deciding on it. BatteryManager's 60 s USB-charger refresh means one identical line a minute — an estimated ~2,900 lines in a 48 h capture (~14% of the last one). It is NOT deleted, because the three-way MMR tag split is documented as load-bearing for #1309 (a wedge log showed a 5 s write timeout with nothing logged for the write that started it), and `debug_get_log`'s `dedupe` collapses consecutive repeats at read time. Judge it with the re-capture in 2b.8, not by arithmetic.

## 3c. From the PR #1705 review

Three reviewers ran on #1705. Nine findings were real; seven are fixed in the PR (per-message repeat budget + its missing user-initiated reset, the Android probe-timeout tier, the lost `SerialPortError` enum name, unescaped probe bytes splitting a line, the state-gated `DE1 DISCONNECTED`, the restored permission-grant INFO, the missing "no DE1 found" outcome, plus six wrong comment claims). These two are not.

- [x] 3c.1 Port `BleTransport`'s notification-liveness idea to `SerialTransport`. There is no stall detector on the serial path at all — `m_notificationLiveness` / `NOTIFICATION_STALE_MS` exist only in `bletransport.cpp`. So a USB DE1 whose port opens and whose subscribes are written but which never starts notifying (or stops mid-session) produces: `Port opened`, maybe `Machine info`, then silence forever. No WARN, and `Disconnected:` never fires because the port is still open. Deleting the per-frame RX line (3.12) was right on volume, but it was the only evidence of the negative case. One monotonic timestamp restarted in `processLine`, checked on the existing poll, one `SERIAL_WARN` past a threshold — strictly better than 600 DEBUG lines a shot.
- [x] 3c.2 Finish centralising `getDeviceIdentifier()`. It moved out of `blemanager.h` into `ble/bledeviceid.h` in #1705 and `bletransport.cpp`'s hand-copy now calls it, but the expression is still copied in `difluidr1.cpp`, `difluidr2.cpp`, `qtscalebletransport.cpp` and `bookooscale.cpp` (the canonical header names them). Route those through the shared helper too — the copies are why the settings-writing versions were the ones that got the macOS null-address bug.
- [x] 3c.3 `[Scale]` INFO+ shows `Scan complete` with no matching start. `doStartScan()`/`stopScan()` log only on the DE1 side (now DEBUG), and ladder-driven scans never pass through `scanForDevices()`, which is the only place `Starting device scan...` is logged. So the scale view shows unpaired completions appearing from nowhere. Add scale-side counterparts gated on `m_scanningForScales`, or drop `Scan complete` to DEBUG there too.

## 3b. The last two prefix families

Found while converting group 3: `[Scale]`/`[DE1]`/`[Refractometer]` are not yet the only prefixes in `blemanager.cpp`.

- [x] 3b.1 `[R2-diag]` — 21 sites (12 in `blemanager.cpp`, 5 in `main.cpp`, 4 in `difluidr2.cpp`). Refractometer diagnostics with a bare hand-rolled prefix; route through the `[Refractometer]` helpers so the registered marker returns them.
- [x] 3b.2 `[BLE]` — the remaining bare-`[BLE]` sites in `blemanager.cpp` (backoff policy, skip-HIGH latch, found refractometer/scale/WiFi scale, transient permission error). These are scale/refractometer lines group 2 should have caught; assign the right marker per line rather than mapping the family wholesale.
- [x] 3b.3 Build and run the full suite.

## 4. Logger plumbing

- [x] 4.1 Add `lineAppended(QtMsgType, QString)` to `WebDebugLogger`. Snapshot under the existing mutex and emit **after** releasing it — emitting under the lock deadlocks any slot that logs.
- [x] 4.2 Add a session-scoped filtered accessor returning the current session's lines matching a marker at or above a minimum level, implemented with `McpLogFilter` so the view and MCP share one definition of "a scale line".
- [x] 4.3 Expose `WebDebugLogger` to QML by macro in the header — never `setContextProperty`, never a runtime `qmlRegisterType` (see `QML_GOTCHAS.md`); add the `qt_add_qml_module` `DEPENDENCIES` entry if needed.
- [x] 4.4 Extend `tst_webdebuglogger`: the accessor returns only the current session, only matching markers, only at/above the level; `lineAppended` fires once per line with the right type; a slot that logs does not deadlock or recurse.
- [x] 4.5 Build and run the full suite.

## 5. Re-source the two views

- [x] 5.1 `SettingsConnectionsTab.qml` — DE1 view: populate from the accessor on build (`[DE1]`, INFO+), then append on `lineAppended` for matching lines. Ensure the append handler logs nothing.
- [x] 5.2 Same for the scale view, matching **both** `[Scale]` and `[Refractometer]` at INFO+ — the refractometers appear here on screen even though they are a separate subsystem for querying.
- [x] 5.3 Make Clear view-local: hide what is shown, keep following new lines, touch neither the log nor the other view. The DE1 view has **no Clear button at all** today (the scale view does) and no cap either — it is `de1LogText.text += message` growing for the process lifetime, which is why 3.12's deleted per-frame RX line mattered. Both views need the bounded behaviour, not just the button.
- [x] 5.4 Retarget the share action from `scale_debug_log.txt` to the system log, reusing the existing platform share plumbing.
- [x] 5.5 Fix any pre-existing accessibility violations on the two views while in the file, per the CLAUDE.md rule.
- [ ] 5.6 Manually verify on device: both views populate when the page is opened after activity, follow live, survive leaving and returning, exclude prior sessions, show no DEBUG chatter, and Clear behaves. QML has no test harness — this step is the coverage.

## 6. Remove the private channels

- [x] 6.1 Delete `appendScaleLog`, `m_scaleLogMessages`, `m_scaleLogFilePath`, `writeScaleLogToFile`, `clearScaleLog`, `shareScaleLog`, `getScaleLogPath`, the `scaleLogMessage` signal and the `mirrorToSystemLog` parameter; convert every remaining caller to a helper call.
- [x] 6.2 Delete the `de1LogMessage` signal and its remaining forwards in `main.cpp`.
- [x] 6.3 Confirm `scale_debug_log.txt` is no longer created, and that nothing reads it (including ShotServer endpoints and any data-migration payload).
- [x] 6.4 Build and run the full suite.

## 7. Documentation, discoverability and enforcement

- [x] 7.1 Build `debug_get_log`'s tool description from the registry so the markers and tier convention are named without being restated; include the warning that a bracketed marker is a substring, not a regex (`[Scale]` under `regex: true` is a character class matching almost every line).
- [x] 7.2 Write `docs/CLAUDE_MD/LOGGING.md`: marker grammar, the three tiers with guidance, how to add a helper, how to register a subsystem, and how to retrieve a subsystem's narrative from a log and over MCP. Write it as the blueprint for new logging, not as a record of this change.
- [x] 7.3 Add `LOGGING.md` to the reference-document table in `CLAUDE.md`.
- [x] 7.4 Add `scripts/check_log_markers.py`: verify every covered-subsystem logging helper applies a registered marker and that no covered call site hand-rolls a bracketed prefix. Parse the registry rather than hard-coding markers. Must run with no compiler and no Qt.
- [x] 7.5 Wire the check into `text-invariants.yml` (the build-free PR gate) and confirm it fails, not warns, on a deliberately broken helper.
- [x] 7.6 Update the wiki manual: it must ask users for the system log, not a scale log. Check for any other reference to the removed file.

## 8. Close out

- [ ] 8.1 Run the full suite via Qt Creator (`run_tests`, scope `all`) and confirm zero failures and zero warnings.
- [ ] 8.2 Open a PR.
- [ ] 8.3 Run the automated `/pr-review-toolkit:review-pr` and address findings.
- [ ] 8.4 Archive the change with spec-sync as the final commit on the PR.
