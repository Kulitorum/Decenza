## Context

The dual-HIGH backoff (`scale-ble-priority-backoff` #1185, `scale-priority-backoff-preheat-persist` #1202/#1209) is built around a fire-once, latch-and-act detector:

- `BlePriorityDetector` (`src/ble/transport/bleprioritydetector.h`) — pure logic, per-transport. `armWindow()` on HIGH connect; `onDe1Fault()` (≥2/20 s sliding) and `onScaleStall()` (single in-cycle stall) call `fire()`, which sets `m_skipHighPriority`/`m_backoffTriggered`/`m_armed=false` so it triggers at most once per transport.
- `QtScaleBleTransport::triggerScaleBackoff()` — latches `BLEManager::latchScaleSkipHighPriority()` (app-run-wide, build-scoped persisted) then disconnects + reconnects at BALANCED.
- `WeightProcessor::checkScaleFeedStall()` emits `scaleFeedStalled()` after a >2000 ms gap during extraction/preheat with tare complete. `processWeight()` silently clears `m_scaleFeedStale` on the next genuine sample — there is no "resumed" signal today (the structural assumption is that the backoff confirms itself by the reconnect).
- Persistence: `SettingsHardware` QSettings `connectionPriority/` group (`latched`, `triggerKind`, `setTimeIso`, `buildCode`), build-scoped. `clearConnectionPriorityLatch()` does `m_settings.remove("connectionPriority")` — removes the **whole group**.
- MCP: `devices_connection_status` (read) and `devices_reset_scale_priority` (clear, eventually-consistent).

The evaluation problem: acting changes the behavior under study, so a production run cannot answer "how often would this fire, and does the scale recover on its own at HIGH?" The locked decisions (confirmed with the user) are: (1) two modes only — `enforce`/`observe`; (2) observe forces HIGH and overrides any existing BALANCED latch; (3) the mode persists until explicitly changed, across restarts and build upgrades.

## Goals / Non-Goals

**Goals:**
- A persistent, MCP-settable policy mode that makes detection inert-but-observable without altering `enforce` behavior at all.
- Capture the two evidentiary signals the evaluation needs: *would-have-fired* events and *recovered-on-its-own* events, both timestamped and machine-readable via MCP.
- Keep observe detection running for an entire multi-day production test (repeating, never latching).
- Zero behavioral drift in `enforce` mode (byte-identical detector/latch/reconnect path).

**Non-Goals:**
- No `off`/disabled mode (locked: exactly two values).
- No auto-tuning of thresholds, no decay/auto-clear of the real latch, no changes to the 2000 ms stall threshold or the ≥2/20 s cluster rule.
- No UI surface — control and read are MCP-only.
- Not deciding *whether* the backoff is too aggressive — this change produces the evidence; the verdict is a later, separate decision.

## Decisions

### D1 — Mode lives on `BLEManager`, not on the per-transport detector
`BLEManager` is the existing app-run-wide owner of the latch and is the singleton every transport seeds from on connect. The mode is a policy, not per-connection state, so it belongs next to the latch. The detector stays a pure value type; it receives an `observe` bool at `armWindow()` time (seeded by the transport from `BLEManager::backoffMode()`), exactly mirroring how it is already seeded from the latch. *Alternative considered:* a global/Settings flag read directly by the detector — rejected: the detector is deliberately Qt-free and clock-free for deterministic tests; threading a bool through `armWindow()` keeps that property.

### D2 — Observe is a separate non-latching detector path, not a branch inside `fire()`
Add `observe` state to the detector. When armed-observe, `onDe1Fault()`/`onScaleStall()` evaluate the **identical** conditions but route to a `wouldFire()` that returns true **without** setting `m_skipHighPriority`/`m_backoffTriggered` and **without** disarming; it resets the cluster window (`m_de1FaultCount`/`m_windowStartMs`) so the next episode is detected cleanly. This preserves the single source of truth for the trigger conditions (no copy-paste of the cluster math) while making the consequence mode-dependent. *Alternative considered:* a second detector instance for observe — rejected: duplicates the sliding-window logic and risks the two drifting.

### D3 — Recovery is event-based, surfaced from `WeightProcessor`
Add `scaleFeedResumed(qint64 gapMs)`. `checkScaleFeedStall()` already computes the stall edge; record the wall-clock at which `m_scaleFeedStale` was set, and in `processWeight()` — at the exact point it clears `m_scaleFeedStale` — if a stall had been signalled this cycle, emit `scaleFeedResumed(now - stallStart)`. This is pure addition: the clear already happens; we only emit on the 1→0 edge. No timer (CLAUDE.md: event-based, not timed). DE1-fault-cluster "recovery" is softer — there is no discrete resume event — so it is logged as "fault cluster window elapsed without escalation" from the detector's existing window-expiry path (the `nowMs - m_windowStartMs > kWindow` re-anchor branch), observe-only. *Alternative considered:* a periodic liveness poll — rejected (timer-as-guard, forbidden).

### D4 — Observe forces HIGH by gating at the transport, before the latch is consulted
`QtScaleBleTransport::onControllerConnected()` already chooses HIGH vs skip based on the seeded latch. Insert a mode check first: if `observe`, never call `setSkipHighPriority(true)` from the manager latch, always request HIGH, and arm the detector in observe. The latch value is left intact on disk/in memory (observe overrides, does not erase it) so switching back to `enforce` restores the prior latch state honestly. *Alternative considered:* clearing the latch on entering observe — rejected: destroys evidence and conflates "I want to observe" with "I judged the latch wrong."

### D5 — Mode persistence is a sibling key, and the latch-clear is narrowed
Store the mode at `connectionPriority/policyMode` (string `"enforce"`/`"observe"`, absent ⇒ `enforce`). It is **not** build-scoped: it is a deliberate operator choice, not an auto-classification, so the build-rehydrate logic does not touch it. This forces a correctness fix: `clearConnectionPriorityLatch()` must remove only `connectionPriority/{latched,triggerKind,setTimeIso,buildCode}`, not the whole group, or a latch reset would silently wipe the mode. The four-key removal is also more correct in isolation (clear means "clear the latch", not "clear everything under this prefix"). *Alternative considered:* a different QSettings group (`connectionPriorityPolicy/`) — rejected: the narrowed clear is needed anyway for hygiene, and one group keeps related state discoverable.

### D6 — Bounded in-memory observe-event ring on `BLEManager`, MCP-readable
A fixed-capacity (20) ring of `{ isoTime, triggerKind, kind∈{wouldBackoff,recovered}, stallSec|gapSec }`. In-memory only (matches the latch's in-memory diagnostic metadata; a production tester reads it live via MCP, and the debug log is the durable record). `devices_connection_status` gains `backoffMode` and `recentObserveEvents[]`; a new `devices_set_scale_priority_mode` mirrors `devices_reset_scale_priority`'s eventually-consistent contract — the mode persists immediately, but the HIGH-forcing applies on the next scale reconnect (the response must say so explicitly, same wording pattern).

### D7 — Wording and data conventions
Observe log lines are WARN (same salience as the real BACKOFF line, since the whole point is field-grepping) and clearly marked observe/no-action so they cannot be mistaken for a real backoff. MCP fields follow CLAUDE.md: ISO-8601 with offset, `stallSec`/`gapSec` (units in name), `"enforce"`/`"observe"` strings, `kind` as a human string.

## Risks / Trade-offs

- **[Observe path silently diverges from enforce conditions over time]** → The trigger conditions live in one place (`onDe1Fault`/`onScaleStall`); only the *consequence* branches on mode. A unit test asserts observe and enforce reach the trigger point on identical input sequences.
- **[A latch reset wipes the mode]** → Root-caused here: the whole-group `remove()` is narrowed to the four latch keys; a regression test asserts `policyMode` survives `clearConnectionPriorityLatch()`.
- **[Observe accidentally alters SAW/flow/frame decisions]** → Observe only changes the connection-priority consequence and adds logging; `WeightProcessor` SAW/flow/frame paths are untouched. `tst_weightprocessor` asserts byte-identical SAW behavior with observe on vs off, and that `scaleFeedResumed` fires only on the stall→sample edge.
- **[User leaves the build in observe and ships effectively no protection]** → Acceptable and intended: observe is an explicit, operator-set, MCP-only diagnostic state, persisted by deliberate choice; `devices_connection_status` always reports the active `backoffMode` so the state is never hidden. Default is `enforce`; a fresh install is protected.
- **[Eventually-consistent set confuses the operator]** → The MCP response states the mode persisted immediately but HIGH-forcing applies on next reconnect, identical to the existing reset tool the user already understands.

## Migration Plan

- Additive; no data migration. New QSettings key defaults to absent ⇒ `enforce` ⇒ today's behavior.
- Deploy: ship in a normal build. Operator sets `observe` via MCP, forces a scale reconnect (toggle scale / restart app), runs production, reads `devices_connection_status` / greps the debug log, then sets `enforce` (or it is the default on the next fresh install).
- Rollback: revert the change — `enforce` path is byte-identical to today, so removal is safe; any persisted `policyMode` key becomes inert (unread) and is harmless.

## Open Questions

- None blocking. (Ring capacity 20 and WARN log level are defaults chosen to match existing diagnostic conventions; trivially tunable if field use suggests otherwise.)
