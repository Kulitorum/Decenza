## Context

#1707 established the marker convention, the three tiers, the registry as single source
of truth, and `scripts/check_log_markers.py` as a per-PR gate. It closed with a
methodological claim worth taking seriously: *every* defect it found was found by reading
a running app's log, not by reading the tree. This change is the next application of that
method — 24 hours of real device logs read over `debug_get_log` — and it found three
things review would not have.

**Current state, established from the log and then confirmed in source:**

1. `WebDebugLogger::trimLogFile()` (`src/network/webdebuglogger.cpp:239-243`) writes
   `"========== SESSION START: " + m_startTime` at the head of the surviving content.
   `m_startTime` is the **currently running** session's start; the content it introduces
   belongs to an older one. `rebuildSessionIndex()` (`:311`) treats every `SESSION START`
   line as a boundary, so the forgery becomes a real session in every enumeration.

   The live log shows the consequence exactly: index 0 (3249 lines, file line 1) carries
   `2026-07-29T18:17:55`, which is also index 3's timestamp — index 3 being the run that
   performed the trim. Indices 0..4 read `07-29 18:17`, `07-28 10:23`, `07-29 08:21`,
   `07-29 18:17`, `07-30 08:20`: not chronological, one timestamp duplicated.

2. `DecentScaleWifi::connect()` (`src/ble/scales/decentscalewifi.cpp:145-151`) logs
   *"Previous attempt found %1 unreachable — re-resolving before retry"* at `WIFI_INFO`,
   then calls `attemptHostname()`. When resolution fails,
   `dialCachedIpAfterResolveFailure()` (`:165-181`) dials the persisted cache and logs at
   `WIFI_LOG` — DEBUG. At INFO the session therefore reads as a fresh resolve failing
   against a fresh address, for eight minutes, against the same stale `192.168.10.145`.

3. `BLEManager::scaleRepeatFailure()` (`src/ble/blemanager.cpp:2864`) is per-message and
   correct, and the ladder tail in `main.cpp:2205-2215` deliberately drops
   *"Auto-reconnect attempt N"* to DEBUG. But `DecentScaleWifi` has no budget, so on the
   endless tail three manager lines go quiet while three driver lines keep firing at
   INFO/WARN every 60 s — carrying neither the attempt number nor the outcome.

4. `[SAW]` (53 sites), `[SAW-Worker]` (13), `[SAW-Latency]` (3), `[ScaleFeed]` (1),
   `[discard-classifier]` (1) are marker-shaped and unregistered. `[SAW]` appears in
   `src/ble/de1device.cpp` — a **covered** file — passing rule 1 (it uses a helper) and
   rule 2 (unregistered token), which is the hole `LOGGING.md:224` documents in prose.

**Constraints.** The gate stays build-free (`text-invariants.yml`, no Qt, ~1 min).
Markers are published names — renaming one breaks saved queries. The persisted log is the
one sink; nothing here may introduce a second.

## Goals / Non-Goals

**Goals:**

- A `SESSION START` marker never carries a time other than that session's start, and
  trimming never fabricates one.
- `session=N` addresses what it has always claimed to, including after a trim.
- The WiFi scale's INFO narrative states which address it dialled and why.
- Repeat suppression covers a failing cycle rather than one emitter within it.
- Marker-shaped prefixes are registered or stop being marker-shaped, and the check
  enforces that rather than documenting the gap.
- `src/main.cpp` comes under the gate, so the ladders it drives cannot log unmarked.

**Non-Goals:**

- The ~40 non-device `Class:` prefixes (`SteamPage:` 44 lines/session, `MqttClient:` 30,
  `ShotDataModel:` 22, `Visualizer:`, `BatteryManager:`, `ShotReporter:`, …). Surveyed,
  deferred: a bulk conversion with no device-log consumer waiting on it, and folding it in
  would bury the three defects above.
- Bare QML `console.log` lines (`Phase Idle/Ready:`, `FRAME CHANGE:`, `Auto flow cal:`,
  `Stop overlay:`). Same reasoning; they also need a QML-side helper that does not exist.
- `ShotDataModel`'s spike rejection at WARN — dozens per normal, kept, saved shot. A real
  tier defect and a real cry-wolf source, but it sits on top of an open question about
  whether the spike filter is mangling curves that `[SAW]` then refuses to learn from.
  Fixing the tier would hide that. Left for its own change with the SAW disagreement.
- `BatteryManager`'s *"cycle 1 of 5"* that never reaches 2, the `QSslSocket: device not
  open` warning, and `McpRemoteAccess`'s unattributed localhost rejections. Logged here as
  found; none is a logging-convention matter.

## Decisions

### Trimming writes a banner and nothing else

`trimLogFile()` drops the `SESSION START` write. The banner stays and becomes the only
thing trimming adds.

The existing comment — *"re-emit the session marker so it survives the trim"* — names a
real concern: without it, a trim that removes the running session's own marker leaves the
current session unaddressable. That concern is real but the remedy inverts the meaning of
a marker. Two better options:

- **Chosen: treat a headless leading fragment as unknown-start.** `rebuildSessionIndex()`
  synthesizes a boundary at line 0 with an empty timestamp when the file's first
  `SESSION START` is not its first content line. `session=0` addresses it; it enumerates
  with a null start time; `session=-1` is unaffected because the running session's marker
  is at the *end* of the file and is the last thing a front-trim removes.
- Rejected: re-emit the marker with the *trimmed session's* start time. Requires knowing
  which session the surviving content belongs to, which is exactly the information the
  trim destroyed.
- Rejected: never trim across a boundary. Would let one enormous session defeat the size
  cap.

The current session losing its own marker is only possible if that session alone exceeds
80 % of the 2 MB cap, at which point the fragment-at-index-0 path covers it correctly —
the fragment *is* the current session, and `-1` resolves to it.

### `[SAW]` registers; `[ScaleFeed]` folds into `[Scale]`; `[discard-classifier]` stops looking like a marker

#1707 left this open explicitly ("shot logic, not a device"). Resolving it by the spec's
own test — *does it answer a different diagnostic question?* — rather than by whether it
owns hardware:

- **`[SAW]` registers.** "Why did my shot stop at the wrong weight" is a distinct question
  from "why did my scale disconnect", it is asked from submitted logs, and 69 sites across
  six files already behave as though the marker works. `[SAW-Worker]` and `[SAW-Latency]`
  become **source tags** — `[SAW][Worker]`, `[SAW][Latency]` — because they answer the
  same question from different threads. This also removes three markers rather than adding
  three.
- **`[ScaleFeed]` folds into `[Scale]`** as a source tag. Its one line is about whether the
  weight feed is streaming — the same question as "my weight is wrong". A second marker
  here would split one narrative across two greps, which the spec forbids.
- **`[discard-classifier]` stops being marker-shaped.** One line per shot recording a
  save/discard verdict. Nobody greps it as a subsystem; it becomes a plain prefix.

### The gate gains a rule for unregistered leading brackets, scoped to the message start

New rule 5: in a covered file, a log message beginning with `[token]` where `token` is not
registered fails. Scoping to the **start of the message** is what makes this safe — `[M]`
is a DE1 protocol byte and `[observe]` a mode qualifier, and both appear mid-message.
#1707 narrowed rule 2 to registered tokens for exactly that reason and paid for it with
this hole; anchoring is the cheaper discriminator than a token allowlist.

Rejected: an allowlist of permitted non-marker tokens. It is a second registry, free to
drift from the first — the failure mode this whole convention exists to prevent.

### `COVERED_GLOBS` gains `src/main.cpp` only

Not `src/**`. `main.cpp` earns it specifically: it drives the scale *and* refractometer
reconnect ladders, and `LOGGING.md:246` already names it as the largest known gap
(~127 bare calls). Widening to `src/**` would fail on the ~40 deferred `Class:` prefixes
and force the non-goals into scope.

The refractometer ladder (`main.cpp:3295,3323,3343`) moves onto `REFRACTOMETER_*` in the
same pass — it is the reason `main.cpp` is being covered. The remaining bare calls in
`main.cpp` need per-line triage; where a line genuinely belongs to no registered
subsystem, `// log-marker-exempt:` with a real reason is the documented answer.

### The WiFi retry narrative: log the outcome, not the intent

`dialCachedIpAfterResolveFailure()` moves `WIFI_LOG` → `WIFI_INFO`, and the
*"re-resolving before retry"* line is reworded so it does not read as a completed action.
Chosen over deferring the announcement until the branch is known, because the announcement
also marks the attempt's start and a reader tracing a 60 s cycle uses it as the anchor.

### Repeat suppression via a shared budget the driver can reach

`scaleRepeatFailure`'s per-message counter lives on `BLEManager`. `DecentScaleWifi`'s
repeating lines route through it rather than gaining a second counter — a second counter
is a second policy, free to drift, and `resetRepeatFailureBudget()` would not reach it,
so a scale that reconnected would re-arm only half its messages.

**Correction, made during implementation.** This section originally said the driver
"reaches it through the callback it already holds for logging, so no new ownership edge
is introduced". That was wrong: the driver logs through the `SCALE_*` macros, which emit
a signal — it holds no logging callback. A new injected `RepeatFailureSink` was needed,
following the `setIpResolver`/`setIpCacheUpdate` pattern the driver already uses. The
conclusion (one shared store, injected rather than duplicated) survived; the stated
reason did not.

Two further things the implementation found:

- **The budget had to gain a tier parameter.** It logged everything at WARN, but a
  failing cycle emits narrative as well as problems, so routing the driver's INFO lines
  through a WARN-only budget would have made the quiet ones loud.
- **It had to gain a `source` parameter too**, or the driver's suppressed lines would be
  stamped `BLEManager` and send a reader to the wrong file.

Per-message counting means the driver's messages get independent budgets, which is
correct: "resolution failed" and "host unreachable" are different facts.

## Risks / Trade-offs

- **[Removing the trim marker makes an over-cap single session addressable only as index 0]**
  → The synthesized unknown-start fragment covers it, and `session=-1` still resolves to
  it because it is the only session. Covered by a test that trims mid-session and asserts
  `-1` returns the current run's lines.

- **[Rule 5 false-positives on a legitimate leading bracket]** → Anchoring to message start
  makes this narrow, but a genuine case (a protocol dump whose first token is bracketed)
  takes `// log-marker-exempt:` with a reason, same as every other rule. Expect a small
  number on first run; #1707's first run found 40 and fixing them rather than making the
  check advisory was the right call, so the same applies.

- **[Registering `[SAW]` puts 69 sites under rules they have never been checked against]**
  → They must move onto an aliased helper in the same change, and `de1device.cpp`'s share
  is already in a covered file. The tier audit is the real work: SAW lines are currently a
  mix of WARN and INFO chosen by feel, and `[SAW] Settled weight unreasonable` is arguably
  a genuine WARN while `[SAW] Cup removed during settling` is arguably narrative.

- **[Covering `main.cpp` surfaces ~127 bare calls at once]** → Most are not device lines and
  take exemptions or move to the two ladders' helpers. If triage proves larger than
  expected, the fallback is covering `main.cpp` with a rule-1-only exception list rather
  than dropping coverage — but that list is a drift risk and is the option of last resort.

- **[Renaming `[SAW-Worker]` → `[SAW][Worker]` changes a published name]** → It was never
  registered, so no documented query depends on it, and `[SAW]` now *returns* those lines
  where before it did not. Strictly more retrievable.

- **[The log read that motivated this is one device over one day]** → Samsung SM-X210,
  Android 16, one DE1, one WiFi scale, one R2. Findings 1–3 are structural and
  device-independent; the tier judgements in the SAW audit are not, and should be checked
  against a second session before landing.

## Migration Plan

No data migration. Existing `debug.log` files already containing forged markers will
enumerate with the duplicate until they are rotated out; the parser change does not
retroactively repair them, and should not — the information needed to date those lines
correctly is gone.

Rollback is per-item: each of the five workstreams is independent, and the trim fix in
particular touches one function plus one parser branch.

## Open Questions

- Should `[SAW]`'s registry description mention that it covers both the prediction model
  and the learning store, or is that detail better left to `SAW_LEARNING.md`? The registry
  description reaches the MCP tool description verbatim, so it is written for a reader who
  has never seen the code.
- Does `ScaleDevice`'s `DISCONNECTED` at WARN still deserve WARN? Carried over from
  #1707's open list; the log shows it firing on an expected disconnect
  (`WebSocket disconnected (expected)` immediately above it), which is the cry-wolf shape.
- The `[DE1][MMR] keepalive:` volume judgement, also carried from #1707, still wants a
  48 h capture rather than arithmetic.
