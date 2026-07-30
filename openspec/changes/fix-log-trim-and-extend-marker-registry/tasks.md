## 1. Log session boundaries

- [x] 1.1 Remove the `SESSION START` write from `WebDebugLogger::trimLogFile()` (`src/network/webdebuglogger.cpp:239-243`), leaving only the trim banner. Replace the stale comment — it asserts the re-emit preserves something, and it destroys it.
- [x] 1.2 In `rebuildSessionIndex()` (`src/network/webdebuglogger.cpp:~310`), synthesize a boundary at line 0 with an empty timestamp when the first `SESSION START` marker is not the file's first content line, so a headless leading fragment is addressable and explicitly undated.
      **Trap found:** the marker is written with a LEADING NEWLINE (`webdebuglogger.cpp:77`), so line 0 of a healthy fresh log is blank. Testing "line 0 is not a marker" would have fabricated a phantom one-blank-line session on every new log — the same defect class being fixed. The check requires a non-blank line before the first marker; pinned by `sessionIndex_leadingBlankLineIsNotAFragment`.
- [x] 1.3 Confirm `session=-1` still resolves to the running session when a trim removed that session's own marker (single session larger than 80 % of `MAX_LOG_FILE_SIZE`) — the fragment path must cover it.
- [x] 1.4 Make `debug_get_log`'s `sessions=true` output report an absent start time as such rather than as an empty string that reads like a parse failure (`src/mcp/`). Reports `timestamp: null` + `startTimeKnown: false` + a `startTimeNote` saying the information was destroyed by a trim, via one `describeStart()` writer shared by both report sites. Tool description updated to say null means unknown, never "just now".
- [x] 1.5 Add `tst_webdebuglogger` cases: a trim adds no session; a mid-session trim leaves `-1` resolving to the current run; a headless fragment enumerates with no timestamp; no two enumerated sessions share a start time.
      Also **corrected `sessionIndex_rebuildsAfterTruncateAndRewrite`**, which asserted the forged-marker behaviour as correct and would have stayed green over the bug. `trim_doesNotFabricateASession` drives the real `trimLogFile()` over a >2 MB fixture rather than simulating its output — the bug lived in the writer, so a test that only simulated it could never have caught it.

## 2. WiFi scale retry narrative

- [x] 2.1 Promote `dialCachedIpAfterResolveFailure()`'s line (`src/ble/scales/decentscalewifi.cpp:~176`) from `WIFI_LOG` to `WIFI_INFO`, and state that resolution failed and which cached address is being dialled.
- [x] 2.2 Reword the `m_retryShouldReresolve` line (`src/ble/scales/decentscalewifi.cpp:147`) so it does not read as a completed re-resolve. It marks the attempt's start; the outcome line now carries the result.
- [x] 2.3 Check the caller-supplied (`preferredIp`) and successful-re-resolve paths are distinguishable at INFO, so all three address sources are identifiable from the narrative alone.
- [x] 2.4 Re-read a live session over `debug_get_log` with `filter="[Scale]" minLevel="INFO"` and confirm a stale cached address is diagnosable without the DEBUG tier.

## 3. Repeat suppression across the failing cycle

- [x] 3.1 Route `DecentScaleWifi`'s repeating per-cycle failures (`WebSocket error`, the re-resolve/cache-dial line) through `BLEManager::scaleRepeatFailure` rather than logging flat, so manager and driver quiet together.
- [x] 3.2 Verify per-message counting still holds — each driver message gets its own budget, so a new failure arriving mid-tail is still warned about.
- [x] 3.3 Confirm the budget reset on successful connect and on user-initiated attempts reaches the driver's messages too (`BLEManager`'s reset path).
- [x] 3.4 Guard `Scale not found — using FlowScale` (`src/ble/blemanager.cpp:1796`) on actual state, so it is not logged while FlowScale has been in use since startup.
- [x] 3.5 Read a full 60 s tail cycle from a live log and confirm it is quiet at INFO/WARN and complete at DEBUG, with attempt numbers and repeat counts intact.

## 4. Marker registry: `[SAW]`, `[ScaleFeed]`, `[discard-classifier]`

- [x] 4.1 Register `DECENZA_LOG_MARKER_SAW` and its `DECENZA_LOG_SUBSYSTEMS` row in `src/core/logtags.h`, with a description written for a reader who has never seen the code (it reaches the MCP tool description verbatim).
- [x] 4.2 Add `src/machine/sawlogging.h` aliasing `DECENZA_SUBSYS_LOG*`, following the four existing helper headers. Include a `*_STDERR_*` variant — the worker and latency sources have no `logMessage` in scope.
- [x] 4.3 Convert the 53 `[SAW]` sites across `src/main.cpp`, `src/core/settings_calibration.cpp`, `src/ble/de1device.cpp`, `src/machine/weightprocessor.cpp`, `src/controllers/maincontroller.cpp`, `src/controllers/shottimingcontroller.cpp` onto the helper.
- [x] 4.4 Convert `[SAW-Worker]` (13 sites) and `[SAW-Latency]` (3 sites) to source tags `[SAW][Worker]` and `[SAW][Latency]`, so one `[SAW]` grep returns all three.
- [x] 4.5 Audit SAW tiers by audience while converting. Specifically decide `Settled weight unreasonable` (likely WARN — learning was lost) versus `Cup removed during settling` (likely INFO — narrative). Record the reasoning, not just the outcome.
- [x] 4.6 Fold `[ScaleFeed]` into `[Scale]` as a source tag, and re-tier: the "alive: constant weight" line fires every ~2 s during a shot at INFO and is developer detail.
- [x] 4.7 Make `[discard-classifier]` non-marker-shaped — one line per shot, not a subsystem anyone greps.
- [x] 4.8 Confirm `debug_get_log`'s generated tool description picks up `[SAW]` with no second edit.

## 5. Refractometer reconnect ladder onto `[Refractometer]`

- [x] 5.1 Convert the four bare `qDebug() << "Refractometer reconnect: …"` sites (`src/main.cpp:3295,3323,3343` and the attempt line at `:3260`) onto `REFRACTOMETER_*_STDERR_DYN`, matching how the scale ladder logs from `main.cpp`.
- [x] 5.2 Tier by audience. **Kept at DEBUG, against this task's own assumption.** These three are mechanism ("I started a timer"); the user-facing facts are already at INFO via `Hunt ON` and `Auto-reconnect attempt N`. The live log shows Hunt ON/OFF firing 4x in 5 minutes as the review page opens and closes, so an INFO line per arming would be one more line per cycle for no new information.
- [x] 5.3 Confirm `filter="[Refractometer]" minLevel="INFO"` on a live session now returns a continuous reconnect story rather than only the BLEManager half.

## 6. Enforcement

- [x] 6.1 Add rule 5 to `scripts/check_log_markers.py`: in a covered file, a log message **beginning** with `[token]` where `token` is not registered fails. Anchor to message start so `[M]` and `[observe]` mid-message stay legal.
- [x] 6.2 Add `src/main.cpp` to `COVERED_GLOBS` with a comment saying why this file specifically (it drives both reconnect ladders) and why not `src/**`.
- [x] 6.3 Triage `main.cpp`'s remaining bare log calls. **Resolved by scoping rather than by 118 exemptions.** Rule 1 assumes every line in a covered file belongs to a covered subsystem — true for `src/ble/**`, false for `main.cpp`, which drives the reconnect ladders *and* initialises fonts, translations, TTS and accessibility. Applying it there produced 118 "violations" that were mostly lines with no subsystem to belong to, and a check reporting a hundred non-defects is one people switch off. Split into `COVERED_GLOBS` (all rules) and `MARKER_ONLY_GLOBS` (rules 2 and 5 only). 171 -> 40 real defects.
- [x] 6.4 Run the check and fix every violation rather than making any rule advisory. **Final: 0 violations.** Rule 5's first run found six unregistered bracketed families (`[Weight-Worker]`, `[SAW-Worker]`, `[SAW-Latency]`, `[TextRender]`, `[Startup]`, `[AppState]`) that every other rule had missed, seven device lines under hand-typed `[USB Scale]`/`[BLE DE1]` that no `[Scale]`/`[DE1]` search returned, two `[Refractometer]` double-markers, and five `[SAW]` sites my own single-line grep had missed (they were `qDebug().nospace()` with the string on the next line).
      Two of my own design claims were disproved by that run and corrected in the script's comments: anchoring to message start is NOT sufficient to tell a log message from any other string literal (`m_probeBuffer.contains("[M]")`), so rule 5 also requires a log call on the line; and a leading bracketed token must start uppercase to be marker-shaped, which is what makes `[observe]` legal.
- [x] 6.5 Verify the check still runs build-free in `text-invariants.yml` and within its ~1 min budget.

## 7. Documentation

- [x] 7.1 Add a "Session boundaries and trimming" section to `docs/CLAUDE_MD/LOGGING.md`: what a `SESSION START` marker asserts, that trimming never synthesizes one, and how an undated leading fragment presents.
- [x] 7.2 Replace the `LOGGING.md:224` "caught by nothing here" note with the new rule, and update the enforcement section's rule list to five.
- [x] 7.3 Update the "Where the log goes" and `COVERED_GLOBS` gap notes (`LOGGING.md:246`) to reflect `main.cpp` now being covered, and restate what remains uncovered.
- [x] 7.4 Add `[SAW]`, `[Font]` and `[Network]` to the subsystem helper-header table, and record why `[SAW]` earned a marker while `[ScaleFeed]` folded into `[Scale]` — stated as "the test is the question, not the hardware", since #1707 had framed it as a question about ownership ("shot logic, not a device").
- [x] 7.5 Add a "Don't announce an intent as an outcome" entry to the failure-modes section, citing the WiFi re-resolve case — it is a fourth recurring shape alongside the existing three.

## 8. Verification

- [x] 8.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before building; Qt Creator is shared. Zero failures, zero warnings.
- [x] 8.2 Run `scripts/check_log_markers.py` clean, and qmllint clean.
- [x] 8.3 **Done 2026-07-30 against the macOS build** (`decenza` MCP, Decenza 2.0.1, session 68 / `2026-07-30T10:47:31`). 34 INFO+ lines across a 60 s shot, reading as a narrative: font registered, Flow Scale connected, simulator attached, WiFi target unreachable, Bluetooth fallback, shot, saved. No event logged twice, no bare marker, no source-less line. The WiFi failure carried its provenance on the existing line — `WebSocket error: Host unreachable (code 7) — target=192.168.10.241 (freshly resolved via QHostInfo) local=<unbound>` — confirming group 3 works on live data at zero added lines.

      **The read found what the gate could not**, which is the point of doing it: four `[FontProbe]` lines in `src/screensaver/iosbrightness.mm`, an unregistered bracketed family sitting outside both glob sets (they reach `src/ble/**/*.mm` and no other `.mm`). Two of them were INFO and fired on *every* Apple-platform startup — the probe set includes ⚡ ☀ ☁ ❄, which Apple Color Emoji always covers — so the always-true branch stood permanently in the user-facing narrative saying "this is normal". Their existence also made this change's own `[Font]` registry description false: a `[Font]` search did not return the font story. Converted to `FONT_WARN`/`FONT_LOG` with tag `Probe`; the two paragraphs dropped to DEBUG, which costs their intended reader nothing because `debug_get_log` returns DEBUG by default. Gap documented in `LOGGING.md` alongside why the directory was not simply added to the globs (`iosbrightness.mm` also hosts `[Screensaver]` and `[Theme]`).
- [x] 8.4 **Done.** Sessions across 07-29 and 07-30 were read at INFO during the review pass, including a full 60 s shot and several clean startups. The 4.5 judgements held; no line judged INFO turned out to be per-shot noise, and no line left at DEBUG turned out to be something a user needed. Two tier errors WERE found, both in the opposite direction and both introduced by this change rather than by 4.5: the `[FontProbe]` paragraphs at INFO on every startup (see 8.3), and a dead reconnect ladder going completely silent above DEBUG once the driver's warning joined the repeat budget.
- [x] 8.5 **Done 2026-07-30**, same session as 8.3, then **re-done after a correction**. The first read reported 69 sessions in recorded order and I recorded session 0 as "the headless-fragment case, working correctly" — `timestamp: null`, `startTimeKnown: false`, with the trim note.

      **That reading was wrong, and the thing it was praising was a bug.** Session 0 had `lineCount: 1`. Its single line was the trim banner. `trimLogFile()` writes `... [log trimmed] ...` unconditionally, so when a trim lands just before a marker the surviving head is banner-then-marker with nothing orphaned — and the fragment scan, which only skipped BLANK lines, counted the banner as content and invented a session. The fix for fabricated sessions fabricated a session, and every test fixture had hidden it by putting a real orphaned line after the banner. Fixed by sharing `kTrimBanner` between writer and reader and skipping it alongside blanks; three tests added. **Re-verified on the live file: 74 sessions, session 0 now starts at line 1 (the marker) with a real timestamp, and the banner belongs to no session.**

      **One duplicate start time remains in the file and is expected: it is pre-fix residue, not a live defect.** Sessions 1 and 65 both read `2026-07-30T10:28:23`. Session 1's *content* is old-format (`[BLE DecentScaleWifi]` doubled with a `[Scale] [BLE …]` echo, unprefixed `BLEManager:`), so it was written long before that timestamp — a trim under the old binary stamped the then-current run's start onto surviving old content, exactly the forgery this change removes. Session 68's content shows the new single-line format. Nothing written after the fix has forged a marker; the stale pair ages out of the ring buffer on its own.

## 9. Ship

- [x] 9.1 Open the PR against `main` from a feature branch. — [#1716](https://github.com/Kulitorum/Decenza/pull/1716)
- [x] 9.2 **Done.** Five agents in parallel (CLAUDE.md audit, deep bug scan, git-history context, prior-PR context, comment accuracy), plus type-design, test-coverage and silent-failure passes. Findings were verified before acting — several did not survive checking and were dropped, and one ("RepeatTier::Info has zero callers") was wrong as stated though right in substance.

      Roughly twenty real defects, all fixed on this branch in `67a89a68` rather than filed. The categories worth remembering:
      - **Bugs this change introduced:** the trim-banner phantom session (8.5); a dead ladder silent above DEBUG; `clear()` producing a session blamed on a trim; a `Q_ENUM` logged as `4` instead of `Online`; and **rule 5 unable to see a message on a continuation line** — weakened by the `*_STDERR` rename in the same change that added it.
      - **Silent failures in touched code:** `trimLogFile()` running unsynchronised on arbitrary threads; both trim `write()` results discarded after the truncate; the trim's read-open failure a bare `return` that retried forever while the file grew unbounded.
      - **Wrong claims in prose** — the worst category by track record: `fontlogging.h` justified itself with the exact opposite of what the substring filter does; `[Network]`'s description promised a ShotServer story it had zero lines of; two cited commit hashes unreachable from `main`; `rebuildSessionIndex()` cited twice and existing nowhere; the script's own docstring calling rule 5's hole "caught by nothing" after rule 5 existed; and five wrong counts.

      **The gap that hid most of it is now rule 6**: any file including a logging helper must be in a glob set. Its first run caught two files added by this change, then exposed `[Steam]`, `[HW-Tare]`, `[Screensaver]` and `[Theme]`. `[Screensaver]` and `[Theme]` were registered properly (37 sites); `[HW-Tare]` folded into `[SAW]`; `[Steam]`'s four DEBUG lines were rewritten non-marker-shaped rather than minting a marker that would carry only part of its story.

      No review comment was posted to the PR: every finding was fixed in the same PR, so a list of resolved issues would be noise.
- [ ] 9.3 Archive this change (`/opsx:archive`) and sync `openspec/specs/` as the **final commit on this PR**, not a separate one.
- [ ] 9.4 Squash-merge and delete the branch (`/merge-pr`).

## 10. Recorded but deferred

Not tasks — findings from the same log read, deliberately out of scope per `design.md`.
Kept here so they are decisions rather than oversights.

- [x] 10.1 ~~Decide whether `ShotDataModel`'s spike rejection drops to DEBUG, and whether the filter is mangling curves `[SAW]` then refuses to learn from.~~ **Investigated 2026-07-30. No change. Closed.**

      **The filter works.** Shots 1071–1075 (real pulls, ~1.2 g/s) produced zero rejections. Every rejection in the 24 h window came from shot 1076, which ran with an **empty basket** — no puck, pressure never above 0.36 bar against a 6 bar goal, 64.3 g in 8.5 s.

      **Verified against independent ground truth**, not against the filter's own surviving points. `waterDispensed` comes from the pump, so with no puck the scale should trail it by a near-constant retention offset. Offset held 3.4–5.0 g across the shot except at t=2.888, where it dropped to 2.81 — that sample really was 1.3 g high, and the filter correctly rejected it. (An earlier pass here interpolated each rejected point between its surviving neighbours and concluded all six were false positives. That test is circular — it places any bracketed point near the line by construction — and the conclusion was wrong.)

      **Two candidate fixes rejected on the evidence:**
      - A minimum absolute-jump gate (`deltaWeight > 5 g`) would have passed the one genuine spike (Δ2.96 g). Wrong fix.
      - Retuning the 10 g/s threshold or the 0.05 s deltaTime floor would trade the normal case for the abnormal one. An empty basket is adversarial on both axes at once — flow genuinely near threshold *and* water hammering an undamped basket — and is not a basis for tuning.

      **Known structural weakness, accepted:** the anchor is the last *accepted* point, so when the scale under-reads one sample the correction reads as a spike and gets rejected while the bad sample is kept (visible at t=5.181→5.393). Inherent to the anchoring scheme; not worth redesigning for input this rare.

      **Also accepted:** `m_weightFlowRatePoints` is appended before the filter, so a rejected sample survives in flow-by-weight but not in the weight curve — 36 vs 41 points on shot 1076, and the Visualizer export ships them mismatched. Only reachable on shots whose data you would discard anyway. Judged not worth fixing (Jeff, 2026-07-30).

      **The SAW overshoot was not the filter.** SAW reads the scale path, not `ShotDataModel` — its settling trace records 63.9/64.5/64.6 g, the exact samples the filter rejected. Cause is `[SAW-Worker] Flow became valid: … weight=60.61 at "8.6" s`: the flow-validity gate opened after 60.61 g had already passed a 31.79 g threshold. Separate subsystem, separate defect, still open.
- [ ] 10.2 Decide the fate of ~40 non-device `Class:` prefixes (`SteamPage:` 44 lines/session, `MqttClient:` 30, `ShotDataModel:` 22, `Visualizer:`, `BatteryManager:`, `ShotReporter:`, `ShotHistoryStorage:`, `LocationProvider:`, `WeatherManager:`, …).
- [ ] 10.3 Decide the fate of bare QML `console.log` lines (`Phase Idle/Ready:`, `FRAME CHANGE:`, `Auto flow cal:`, `Stop overlay:`) — needs a QML-side helper that does not exist.
- [ ] 10.4 `BatteryManager`'s "cycle 1 of 5" that never reaches cycle 2 across five firings 2.5 h apart.
- [ ] 10.5 `QIODevice::read (QSslSocket): device not open` — read after close, unattributed.
- [ ] 10.6 `McpRemoteAccess: rejected unauthorized request from "127.0.0.1"` — 21 in 8 s, never says what was rejected.
- [ ] 10.7 Same-event-twice pairs outside this change's scope: steam heating logged from both `de1-state-steam` and `steampage-activated`; `SteamPage:` scaling-decision payload logged identically at `syncSteamTimeout` and at steam-start (#1711).
- [ ] 10.8 Float noise in log payloads (`sessionMeasuredMilkG= 59.55000000000001`, `steamSecondsPerGram= 0.25428328060355315`) — round at the call site.
- [ ] 10.9 Carried from #1707, still open: `ScaleDevice`'s `DISCONNECTED` at WARN (the log shows it firing on an *expected* disconnect), and the `[DE1][MMR] keepalive:` volume judgement pending a 48 h capture.
