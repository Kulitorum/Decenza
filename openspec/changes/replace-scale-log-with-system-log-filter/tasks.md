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
- [ ] 2b.9 Decide whether `[SAW]` / `[SAW-Worker]` (218 lines) should join the registry. Already DEBUG so out of the views and out of INFO queries; it is shot logic rather than a device, so it is out of this change's scope by design. Left as an explicit decision, not an oversight.

## 3. DE1 helpers, marker and tiers

- [ ] 3.1 Add DE1 logging helpers with the `[DE1]` marker from the registry, three tiers plus stderr-only variants, mirroring the scale set. Alias the shared macro bodies — do not copy them.
- [ ] 3.2 `bletransport.cpp` — its existing `log()`/`warn()` helpers gain the marker and an INFO tier; `[BLE DE1]` becomes `[DE1][BLE]`.
- [ ] 3.3 `serialtransport.cpp` — add the helpers it lacks; convert its 11 hand-rolled `[USB]` sites to `[DE1][Serial]`.
- [ ] 3.4 `usbmanager.cpp` — add helpers; convert its 23 hand-rolled `[USB]` sites to `[DE1][USB]`.
- [ ] 3.5 `de1device.cpp` — route its 10 hand-rolled `[BLE DE1]` prefixes and ~33 unprefixed stderr lines through the helpers, assigning a tier to each.
- [ ] 3.6 Convert the 17 `emit de1LogMessage(...)` sites in `blemanager.cpp` to INFO helper calls (permissions, scan lifecycle, DE1 found, direct-wake, errors). Keep the signal emitting for now so the existing window is unaffected.
- [ ] 3.7 Review the DE1 INFO set as a narrative, as in 2.7 — this is the first time these lines land in a log, so read them as a stranger would.
- [ ] 3.8 Build and run the full suite.

## 4. Logger plumbing

- [ ] 4.1 Add `lineAppended(QtMsgType, QString)` to `WebDebugLogger`. Snapshot under the existing mutex and emit **after** releasing it — emitting under the lock deadlocks any slot that logs.
- [ ] 4.2 Add a session-scoped filtered accessor returning the current session's lines matching a marker at or above a minimum level, implemented with `McpLogFilter` so the view and MCP share one definition of "a scale line".
- [ ] 4.3 Expose `WebDebugLogger` to QML by macro in the header — never `setContextProperty`, never a runtime `qmlRegisterType` (see `QML_GOTCHAS.md`); add the `qt_add_qml_module` `DEPENDENCIES` entry if needed.
- [ ] 4.4 Extend `tst_webdebuglogger`: the accessor returns only the current session, only matching markers, only at/above the level; `lineAppended` fires once per line with the right type; a slot that logs does not deadlock or recurse.
- [ ] 4.5 Build and run the full suite.

## 5. Re-source the two views

- [ ] 5.1 `SettingsConnectionsTab.qml` — DE1 view: populate from the accessor on build (`[DE1]`, INFO+), then append on `lineAppended` for matching lines. Ensure the append handler logs nothing.
- [ ] 5.2 Same for the scale view, matching **both** `[Scale]` and `[Refractometer]` at INFO+ — the refractometers appear here on screen even though they are a separate subsystem for querying.
- [ ] 5.3 Make Clear view-local: hide what is shown, keep following new lines, touch neither the log nor the other view.
- [ ] 5.4 Retarget the share action from `scale_debug_log.txt` to the system log, reusing the existing platform share plumbing.
- [ ] 5.5 Fix any pre-existing accessibility violations on the two views while in the file, per the CLAUDE.md rule.
- [ ] 5.6 Manually verify on device: both views populate when the page is opened after activity, follow live, survive leaving and returning, exclude prior sessions, show no DEBUG chatter, and Clear behaves. QML has no test harness — this step is the coverage.

## 6. Remove the private channels

- [ ] 6.1 Delete `appendScaleLog`, `m_scaleLogMessages`, `m_scaleLogFilePath`, `writeScaleLogToFile`, `clearScaleLog`, `shareScaleLog`, `getScaleLogPath`, the `scaleLogMessage` signal and the `mirrorToSystemLog` parameter; convert every remaining caller to a helper call.
- [ ] 6.2 Delete the `de1LogMessage` signal and its remaining forwards in `main.cpp`.
- [ ] 6.3 Confirm `scale_debug_log.txt` is no longer created, and that nothing reads it (including ShotServer endpoints and any data-migration payload).
- [ ] 6.4 Build and run the full suite.

## 7. Documentation, discoverability and enforcement

- [ ] 7.1 Build `debug_get_log`'s tool description from the registry so the markers and tier convention are named without being restated; include the warning that a bracketed marker is a substring, not a regex (`[Scale]` under `regex: true` is a character class matching almost every line).
- [ ] 7.2 Write `docs/CLAUDE_MD/LOGGING.md`: marker grammar, the three tiers with guidance, how to add a helper, how to register a subsystem, and how to retrieve a subsystem's narrative from a log and over MCP. Write it as the blueprint for new logging, not as a record of this change.
- [ ] 7.3 Add `LOGGING.md` to the reference-document table in `CLAUDE.md`.
- [ ] 7.4 Add `scripts/check_log_markers.py`: verify every covered-subsystem logging helper applies a registered marker and that no covered call site hand-rolls a bracketed prefix. Parse the registry rather than hard-coding markers. Must run with no compiler and no Qt.
- [ ] 7.5 Wire the check into `text-invariants.yml` (the build-free PR gate) and confirm it fails, not warns, on a deliberately broken helper.
- [ ] 7.6 Update the wiki manual: it must ask users for the system log, not a scale log. Check for any other reference to the removed file.

## 8. Close out

- [ ] 8.1 Run the full suite via Qt Creator (`run_tests`, scope `all`) and confirm zero failures and zero warnings.
- [ ] 8.2 Open a PR.
- [ ] 8.3 Run the automated `/pr-review-toolkit:review-pr` and address findings.
- [ ] 8.4 Archive the change with spec-sync as the final commit on the PR.
