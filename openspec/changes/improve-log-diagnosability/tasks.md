# Tasks

## 1. Make every line placeable in time

- [x] 1.1 Replace the elapsed-seconds prefix with a wall clock (`[HH:mm:ss.zzz]`) in `WebDebugLogger::handleMessage()`. Keep it inside ONE bracket so `lineLevel()` and `stripTimestampPrefix()` (`mcp/mcplogfilter.h`) are unchanged and logs already on disk still parse.
- [x] 1.2 Remove `m_timer` (`QElapsedTimer`), now dead, rather than leaving a member nothing reads.
- [x] 1.3 Update the format comments in `mcplogfilter.h` that name `[<elapsed>]`.
- [x] 1.4 Add a `tst_webdebuglogger` case pinning the emitted prefix shape, so a future format change has to be deliberate.

## 2. Tell the reader what it cannot see

- [x] 2.1 Add `linePrefix()` to `mcp/mcplogfilter.h`: classify a line as registered marker / unregistered bracket / `Class:` prefix / none. Pure, so it is testable without a log file.
- [x] 2.2 Add `debug_get_log families=true` — a census computed from the log in hand, never from a list in the source. A static list would drift and would describe the build rather than the file the reader is holding.
- [x] 2.3 Name mode 0 in the tool description, saying plainly that the registered markers are a MINORITY of the log.
- [x] 2.4 **Correct the census note.** Its first version said the unregistered families "are real subsystems that simply never got a marker". False, and caught by running it: `[R2-diag]`, `[USB Scale]`, `[FontProbe]`, `[SAW-Worker]`, `[Startup]` and `[AppState]` are all CONVERTED — they are historical residue in a ring buffer spanning app versions. The note now says the census describes the file, not the build.
- [x] 2.5 Add `tst_mcplogfilter` cases for `linePrefix()` across all four kinds, including the trap that the level field is padded to 5 characters (`INFO  [Font]` has two spaces) — the shape that broke my first hand-rolled analysis of a real log.

## 3. Stop the loudest repeaters

- [x] 3.1 `TranslationManager`: the post-scan report joined 560 registry keys into ONE `qDebug()` with embedded newlines — 561 physical lines, only the first carrying a timestamp and level. Count stays inline; the list goes to a file named in the line. Fires for any user who opens the Language settings tab.
- [x] 3.2 `BatteryManager`: 643 BYTE-IDENTICAL poll lines in one user capture (2.5% of the whole log) — a tablet that sat on the charger. The existing "log every 5th cycle" throttles rate, not redundancy. Now `LogCollapse`, which is silent while nothing changes and immediate when something does.
- [x] 3.3 `MqttClient`: 1,582 lines across three messages, 527 of them at WARN, for a broker deliberately switched off. `LogCollapse` on all three, keyed on text so a CHANGED reason still warns at once. The one-shot "backing off" warning is untouched — it is the line that says the ladder stopped trying.
- [x] 3.4 `McpRemoteAccess`: the funnel probe warned through its whole five-attempt grace window, but a healthy start recovers on the third — so a working configuration logged two warnings. DEBUG while the outcome is open; the first WARN is the one that accompanies the Error status and carries the attempt count.
- [ ] 3.5 **Not written, deliberately.** `LogCollapse` itself is already covered by `tst_logcollapse` (the decision logic, including that a changed text always emits). Testing the MqttClient wiring means standing up a client and provoking real connection failures, which tests Qt's MQTT stack more than our three call sites. Recorded rather than silently skipped.

## 4. Group 10, carried from #1716 — validated against a real user log first

Four of the seven had a wrong stated symptom, and two were artifacts of one machine. Recorded because the list was written from a single developer's log, and that is exactly what benchmarking against a user's log corrected.

- [x] 4.1 **10.9 `ScaleDevice DISCONNECTED` at WARN.** Half wrong: all eight disconnects in the user log are preceded by a genuine `CONTROLLER ERROR: ConnectionError` and are correctly WARN. The maintainer's log has the opposite — `WebSocket disconnected (expected) — scale power-off` followed immediately by the same line at WARN. A blanket demotion breaks the first, a blanket WARN breaks the second. Tiered on intent instead: `markExpectedDisconnect()`, set by the driver that already computes it.
- [x] 4.2 **10.4 `BatteryManager` "cycle 1 of 5" never reaching 2.** Not a counter bug. The count is right and the condition is genuinely transient — five separate one-minute blips that each cleared. The real defect was the tier: each blip raised a WARN while the line saying it resolved was DEBUG, so a user saw five power warnings and not one of the five resolutions. Below-threshold is now DEBUG; the resolution is INFO, matching the tier that would have announced it.
- [x] 4.3 **10.5 unattributed `QIODevice::read (QSslSocket): device not open`.** Not a logging defect — a read-after-close. `UpdateChecker::onDownloadFinished()` guards only against null pointers, then calls `readAll()` unconditionally, including after a `TimeoutError` where Qt has already closed the device. The flush is now guarded on `error() == NoError`; the error branch below already discarded the file.
- [x] 4.4 **10.6 `McpRemoteAccess` rejections** — ZERO occurrences in the user log. A localhost-MCP artifact of one machine. Dropped from scope rather than "fixed" for nobody.
- [x] 4.5 **10.8 float noise in payloads** — ZERO occurrences. Same. Dropped.
- [x] 4.6 **10.7 steam logged from two places** — ZERO steam events in the user log, so unvalidatable. Reading the code, both call sites pass a distinct `reason` and the parameter exists precisely to attribute them, so two lines may be correct. The real question is whether both should fire `setShotSettings` + `writeMMR` (duplicate BLE traffic, #1711) — a behaviour question, not a logging one. Left alone.
- [x] 4.7 Add tests for the expected/unexpected disconnect tiering, via a `DisconnectTierProbe` subclass of `SimulatedScale` (both members are protected). Two cases: the tier branch itself, and that the flag is ONE-SHOT — a stale flag would silently downgrade the next genuine failure, turning the fix into a way to hide faults.

## 5. #1713 — the benchmark that produced all of the above

- [x] 5.1 Diagnose. The grind step is derived from the user's OWN shot history; `deriveGrindStep()` returns 0 below two distinct settings, and this user has one. Not reproducible from a version number, which is why it needed the log — and the log could not answer it: `grind` appears three times in 25,720 lines.
- [x] 5.2 Fix the mechanism that actually destroyed the value: decimals came from the STEP alone, so a fallback step of 1.0 meant zero decimals and reformatted `1.1` to `1`. `_stepDecimals(step, value)` now takes whichever has more precision.
- [x] 5.3 **Reverted two wrong fixes for the step fallback**, both recorded because each looked right: conditioning on `grinderIsClickIndexed()` (which is `notation == Compound` and therefore true for every Eureka Mignon — it would have skipped the reporter's own grinder), and a flat 0.1 (wrong for letter-notation and true detent grinders). The fallback stays 1.0. Grind settings are not uniformly numeric and the registry has no granularity field, so any blanket default is a guess.
- [x] 5.4 Log the derivation — `ShotHistoryStorage: grind step for X = N, derived from M distinct setting(s)` — deduped per (grinder, answer) since it is called from a QML binding. This one line would have answered the ticket immediately.
- [ ] 5.5 Manual QML check: enter a decimal grind on a thin-history grinder and confirm it survives. No QML harness exists and logic is deliberately not extracted to C++ to create one.

## 6. Verification

- [x] 6.1 Full suite green via Qt Creator (Jeff runs it; QC is shared).
- [x] 6.2 `scripts/check_log_markers.py` clean.
- [x] 6.3 Read a live session at `minLevel="INFO"` and confirm the wall clock and the quieter ladders. Done: 18 INFO+ lines for a startup, `[13:05:49.776]` prefixes, and the funnel probe now silent until attempt 5.
- [x] 6.4 Run `families=true` against a real log. Done, and it caught the wrong note in 2.4.
- [ ] 6.5 Re-run `families=true` after a fresh log has accumulated, to confirm the collapsed families actually shrank rather than merely moving.

## 7. Ship

- [ ] 7.1 Open the PR.
- [ ] 7.2 Run `/pr-review-toolkit:review-pr` and address findings.
- [ ] 7.3 Archive and sync specs as the final commit on the PR.
- [ ] 7.4 Squash-merge and delete the branch.

## 8. Recorded, not started

- [ ] 8.1 The remaining `Class:` families — argued against converting rather than merely deferred. The device story is complete on `main`; the domains where user bugs live have too few lines to mark, and a marker over 26 lines repeats the `[Network]` defect.
- [ ] 8.2 QML `console.log` lines — still need a QML-side helper that does not exist.
- [ ] 8.3 **4,406 lines with no prefix at all** (19% of the census) — now the single largest attribution gap, larger than any family except `[Scale]`.
- [ ] 8.4 A granularity field on `GrinderEntry`. `notation`, `positionsPerRev`, `burrSizeMm` and `variableRpm` do not say how fine a dial goes, which is why 5.3 had to guess twice and stop. It would let the grind fallback and the calibration block both stop inferring.
