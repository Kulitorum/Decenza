## Why

`replace-scale-log-with-system-log-filter` (#1707) made one grep return a whole
subsystem — for the four device subsystems it registered. Reading 24 hours of real
device logs over MCP afterwards found the convention holding exactly where the gate
covers it and nowhere else, plus three defects in the log's own honesty that source
review had again failed to catch.

The worst is not a marker problem. `WebDebugLogger::trimLogFile()` re-emits a
`SESSION START` marker stamped with the **current** run's start time into the middle
of surviving **older** content, so the persisted log's session boundaries are
fabricated and misdated. In today's log that produces two sessions claiming the same
start time and a session list that is not in chronological order:

```
idx 0  line 1      3249 lines  2026-07-29T18:17:55   <- forged by a trim
idx 1  line 3250   9089 lines  2026-07-28T10:23:52
idx 2  line 12339  5526 lines  2026-07-29T08:21:46
idx 3  line 17865  2852 lines  2026-07-29T18:17:55   <- the real one
idx 4  line 20717  1297 lines  2026-07-30T08:20:06
```

Every `session=N` address is wrong, and old lines carry a timestamp of when the trim
ran. That is precisely the hazard `LOGGING.md` tells readers `session=-1` protects
them from — "a scale connecting and disconnecting yesterday looks like it happened
just now" — reintroduced by the logger itself, beneath the guidance.

## What Changes

**Log session boundaries stop being fabricated.**

- `trimLogFile()` writes a trim banner only. It no longer synthesizes a
  `SESSION START` marker, and no marker is ever written carrying a time other than
  the moment the session it names began.
- A log whose first session header was trimmed away exposes its leading fragment as
  having an *unknown* start time rather than borrowing another session's.
- Session enumeration reports boundaries in the order they were recorded and never
  reports two sessions with the same start time.

**Log honesty defects found in the real log are fixed.**

- **The WiFi scale claims a re-resolve it did not perform.** At INFO the log reads
  "Previous attempt found hds.local unreachable — re-resolving before retry", then
  fails against the same cached IP for eight minutes. The truth — resolution failed
  and the stale cache was re-dialled — is only at DEBUG. The narrative reports the
  outcome it actually took.
- **The reconnect ladder's repeat budget is asymmetric.** `scaleRepeatFailure`
  correctly drops BLEManager's repeating lines to DEBUG on the endless tail, but the
  driver's three per-cycle lines have no budget and keep firing at INFO/WARN forever.
  The observable result is a repeating trio with no attempt number and no outcome —
  worse than either policy alone. Driver per-cycle failures share the budget.
- **`Scale not found — using FlowScale` warns about a transition that did not
  happen**, logged while FlowScale had been connected since t=0.4 s.

**The marker convention extends past the device subsystems.**

- Marker-shaped prefixes that are *not* registered — `[SAW]`, `[SAW-Worker]`,
  `[SAW-Latency]`, `[ScaleFeed]`, `[discard-classifier]`, `SteamHealth [trend]` —
  either become registered subsystems or stop looking like markers. Today they are
  caught by no rule (documented in `LOGGING.md`, "caught by nothing here") while
  reading exactly like a marker a query would honour.
- The refractometer reconnect ladder in `main.cpp` — the same story #1707 rescued for
  scales — moves under the existing `[Refractometer]` marker. It is bare today.
- Enforcement widens to cover `src/main.cpp`, whose ~127 bare log calls drive both
  reconnect ladders, and the enforcement check gains a rule for marker-shaped
  prefixes that the registry does not declare.

**Non-goals for this change.** The ~40 non-device `Class:` prefixes (`SteamPage:`,
`MqttClient:`, `ShotDataModel:` …), the bare QML `console.log` lines, and the WARN
tier misuse in `ShotDataModel`'s spike rejection are recorded in `design.md` as
surveyed but deliberately deferred: they are a bulk conversion with no device-log
consumer waiting on them, and folding them in here would bury the three defects above.

## Capabilities

### New Capabilities
- `log-session-boundaries`: how the persisted log delimits sessions, what a session
  marker asserts about time, what trimming may and may not synthesize, and how a
  reader addresses a session by index.

### Modified Capabilities
- `log-tagging-convention`: the registry's scope grows beyond device subsystems, and
  the enforcement contract adds marker-shaped prefixes that the registry does not
  declare — currently the one documented hole in the check.
- `device-log-views`: a subsystem's narrative must report the action actually taken,
  not the action attempted; and a repeating failure's suppression applies to the
  whole cycle rather than to one emitter within it.
- `wifi-scale-discovery`: the driver's retry narrative states which address it dialled
  and why, so a stale cached IP is visible at INFO instead of only at DEBUG.

## Impact

- `src/network/webdebuglogger.cpp` — `trimLogFile()`, session enumeration and
  timestamp parsing.
- `src/ble/scales/decentscalewifi.cpp` — retry-path tiers and wording.
- `src/ble/blemanager.cpp` — repeat-failure budget reaching driver lines;
  `Scale not found — using FlowScale` state guard.
- `src/main.cpp` — refractometer reconnect ladder onto `[Refractometer]`.
- `src/core/logtags.h` — registry entries for whichever of `[SAW]`/`[ScaleFeed]` earn
  one.
- `scripts/check_log_markers.py` — new rule, widened `COVERED_GLOBS`.
- `docs/CLAUDE_MD/LOGGING.md` — the trim/session contract, and the "caught by
  nothing" note becomes a rule.
- `tests/tst_webdebuglogger.cpp` — session enumeration across a trim.
- MCP `debug_get_log` behaviour is corrected, not changed in shape: `session=N` starts
  addressing what it always claimed to.
