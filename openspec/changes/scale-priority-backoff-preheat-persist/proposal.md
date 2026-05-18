## Why

The merged scale connection-priority backoff (#1185) is field-confirmed working on the #1176 reporter's weak Tab A8: a stalled scale feed now triggers a skip-HIGH backoff + reconnect at BALANCED instead of a ruined shot. But the build-3388 debug log shows two residual costs: (1) detection is gated to *active extraction*, so on the triggering shot the user sees a ~6 s "no scale — estimating" window mid-shot (the feed actually died ~6 s before extraction, during preheating, but we don't look until the pour); and (2) the skip-HIGH latch is in-memory only, so a known-incapable device re-pays that ~6 s disruption on the first shot of *every* app run. Both are avoidable.

## What Changes

**In scope (this change — all three together):**

- **Extend scale-feed-liveness detection to the pre-shot EspressoPreheating phase**, not just active extraction. When the espresso cycle has started (machine preheating, scale connected and tare complete, weight expected to stream), the existing stall detector evaluates there too. This is the **guaranteed safety net** — it does not depend on the probe working.
- **Startup connection-priority probe (the "early win")**: shortly after a scale connects (DE1 also connected, machine idle), generate a bounded burst of DE1 BLE traffic to provoke the dual-HIGH contention *while we're watching for it*, so detect → backoff → reconnect-at-BALANCED completes during idle, before the user pulls any shot — no warm-up blip at all. **Strictly additive and strictly safe**: if it provokes nothing it is a complete no-op; it never misconfigures the DE1; capable hardware is unaffected; correctness never depends on it (preheat detection remains the net). The probe is an opportunistic accelerator of the *existing* detector, not a new mechanism.
- **MCP read + reset of the (in-memory, app-run) skip-HIGH latch.** A diagnostic read — is the device currently latched to BALANCED, trigger kind, how long after app start — plus a reset that clears the in-memory `BLEManager` latch so the next scale (re)connect re-enters detection at HIGH, without an app restart. Also lets us confirm whether the probe latched. No UI (deliberately deferred).

**Deferred (explicitly NOT in this change):**

- **Persisting the latch across app restarts** — parked. With preheat detection + the startup probe, the residual per-run cost on a weak device is minimal (often the probe catches it during idle, before any shot). Persistence would reverse the prior change's deliberate D5 ("no disk persistence") and remove the non-sticky safety valve (a mis-latched capable device would degrade permanently with no end-user recovery). Revisit only if field data shows it is still painful.

## Capabilities

### New Capabilities
<!-- none — this evolves existing capabilities -->

### Modified Capabilities
- `ble-connection-priority`: the in-shot liveness gate extends to the EspressoPreheating phase. The latch remains in-memory app-run-scoped (no persistence change in this scope). (Capability defined by the merged `scale-ble-priority-backoff` change; not yet archived to `openspec/specs/`.)
- `mcp-server`: add the ability to read the (in-memory, app-run) scale connection-priority state — latched-to-BALANCED or not, trigger kind, time since app start — and to reset it (clears the in-memory latch; re-detects on next connect).

## Impact

- **Code (in scope)**:
  - `src/machine/weightprocessor.{h,cpp}` — extend the "weight should be flowing" gate so `setCurrentFrame()` evaluates the stall during EspressoPreheating, not only `m_active` extraction; needs a shot-cycle-active input from the machine phase.
  - Wiring (`src/main.cpp` / `MachineState`) — feed the EspressoPreheating phase into `WeightProcessor` (cross-thread → QueuedConnection); clears on Idle/Sleep/extraction-end.
  - `src/ble/blemanager.{h,cpp}` — add a `clearScaleSkipHighPriority()` and expose the in-memory latch state + trigger kind for MCP read; no persistence.
  - `src/mcp/` — MCP read + reset of the in-memory latch (new tool or extension of an existing devices/settings tool), following MCP data conventions; update `docs/CLAUDE_MD/MCP_SERVER.md`.
- **Behavior**: on the first triggering run for a weak device, detection now fires during preheat so the visible mid-shot "estimating" gap shrinks or disappears (margin-dependent — preheat ~6.8 s vs reconnect ~6.3 s); thereafter the in-memory app-run latch holds (unchanged from #1185). Capable hardware unchanged (never trips). MCP gains a diagnostic read + an in-session reset escape hatch.
- **Risk**: preheat shorter than the ~6 s reconnect leaves a brief early blip on the triggering run (acceptable; not persisted, self-corrects framing unchanged). Probe (Phase 2) deliberately stresses a weak radio — designed but staged behind on-device validation; not implemented here. Cross-reference #1093 / #1176; relates to merged #1185.
