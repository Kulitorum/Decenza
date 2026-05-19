## Why

The merged scale connection-priority backoff (#1185) is field-confirmed working on the #1176 reporter's weak Tab A8: a stalled scale feed now triggers a skip-HIGH backoff + reconnect at BALANCED instead of a ruined shot. But the build-3388 debug log shows two residual costs: (1) detection is gated to *active extraction*, so on the triggering shot the user sees a ~6 s "no scale — estimating" window mid-shot (the feed actually died ~6 s before extraction, during preheating, but we don't look until the pour); and (2) the skip-HIGH latch is in-memory only, so a known-incapable device re-pays that ~6 s disruption on the first shot of *every* app run. Both are avoidable.

## What Changes

**In scope (this change — all three together):**

- **Extend scale-feed-liveness detection to the pre-shot EspressoPreheating phase**, not just active extraction. When the espresso cycle has started (machine preheating, scale connected and tare complete, weight expected to stream), the existing stall detector evaluates there too. This is the **guaranteed safety net** — it does not depend on the probe working.
- ~~**Startup connection-priority probe (the "early win")**~~ — **REMOVED (D10).** Shipped in #1202, then false-positived the SM-X210 capable control device on the first real-device test (its synthetic read burst starved a scale that real shots never break). With persistence (below) the probe's only benefit — pre-empting the *first* triggering shot — is now a once-per-build cost caught by the preheat gate, which keys off real shot traffic and so cannot false-positive a capable device. Net benefit retained without the regression vector.
- **MCP read + reset of the skip-HIGH latch.** A diagnostic read — latched to BALANCED?, trigger kind, set-time — plus a reset that clears the latch (in-memory + persisted) so the next (re)connect re-detects at HIGH, without an app restart. Also confirms whether the probe latched. No UI (deliberately, by design).
- **DE1 link also drops to BALANCED when latched (D8).** Reverses #1185's "DE1 always HIGH" *for the latched case only*: a proven dual-HIGH-incapable device runs **both** links at BALANCED (the known-good config — what de1app does, what Android docs recommend). Field evidence: a lone HIGH DE1 starves even a BALANCED scale (shot-2 log). Eventually-consistent on the DE1's next connect; capable HW never latches → keeps HIGH (no regression). On-device SAW-overshoot validation is the acceptance gate.
- **Persist the classification, build-scoped (D9).** Stored as an internal `SettingsHardware` record (no QML/Settings-UI binding), stamped with `versionCode()`. A known-weak device starts both-BALANCED on the first connect of every run — no detection window, no ruined first shot. Auto-cleared when the build number changes (every new build re-detects — the safety valve), plus MCP reset.

**Why persistence is now in scope (it was parked):** the #1176 reporter's build-3388 log overturned the parking premise — the first shot after *every* restart loses ~8 s of weight and over-infuses to 23.8 g vs a 2.3 g target (a ruined shot every run, not a "brief blip"), which is exactly D2's stated revisit-trigger. The original "no recovery for a mis-latched capable device" objection is resolved by this change's own MCP reset + build-scoped auto-clear + the fact that the false-positive cost is "runs at BALANCED" = de1app's normal, functional config.

**Still NOT in this change (by design):** any QML/Settings-UI surface for the latch (MCP is the only operator path); live renegotiation of an already-connected link (D8 is eventually-consistent).

## Capabilities

### New Capabilities
<!-- none — this evolves existing capabilities -->

### Modified Capabilities
- `ble-connection-priority`: the in-shot liveness gate extends to the EspressoPreheating phase (preheat + extraction only — the idle probe is removed, D10); **the DE1 link also skips HIGH when latched (D8, reverses "DE1 Link Always Requests High Priority"); the classification is now persisted, build-scoped (D9, reverses "App-Run Scoped, Not Persisted")**. (Capability defined by the merged `scale-ble-priority-backoff` change; not yet archived to `openspec/specs/`.)
- `mcp-server`: add the ability to read the (in-memory, app-run) scale connection-priority state — latched-to-BALANCED or not, trigger kind, time since app start — and to reset it (clears the in-memory latch; re-detects on next connect).

## Impact

- **Code (in scope)**:
  - `src/machine/weightprocessor.{h,cpp}` — extend the "weight should be flowing" gate so `setCurrentFrame()` evaluates the stall during EspressoPreheating, not only `m_active` extraction; needs a shot-cycle-active input from the machine phase.
  - Wiring (`src/main.cpp` / `MachineState`) — feed the EspressoPreheating phase into `WeightProcessor` (cross-thread → QueuedConnection); clears on Idle/Sleep/extraction-end.
  - `src/ble/blemanager.{h,cpp}` — `clearScaleSkipHighPriority()`, latch state + trigger kind for MCP read, **`setSettings(SettingsHardware*)` to load/write-through the persisted record + the build-scoped auto-clear (D9)**.
  - `src/ble/bletransport.cpp` — **DE1 `onControllerConnected()` skips the HIGH request when latched + logs the decision either way (D8)**.
  - `src/core/settings_hardware.{h,cpp}` — **internal (no Q_PROPERTY/QML) persisted `connectionPriority/` record: latched, triggerKind, setTimeIso, buildCode (D9)**.
  - Wiring (`src/main.cpp`) — `bleManager.setSettings(settings.hardware())` before BLE connect; `#include "version.h"` for `versionCode()`.
  - `src/mcp/` — MCP read + reset (now also clears the persisted record), MCP data conventions; update `docs/CLAUDE_MD/MCP_SERVER.md`.
- **Behavior**: first triggering shot of a build for a weak device — the preheat gate (D1) catches the stall during warm-up, backs off both links; the classification persists (D9). Every subsequent run of that build — both links start at BALANCED on first connect, no detection window, no ruined first shot. New build → re-detect (safety valve). Capable hardware unchanged (never trips — no idle probe to false-trip; only real-traffic detection — nothing persisted, both keep HIGH). MCP gains a diagnostic read + an in-session reset escape hatch.
- **Risk**: the first triggering shot per build still pays the ~6 s warm-up catch (deliberate safety-valve cost; the probe that previously pre-empted it is removed for false-positiving capable HW — D10). D8 trades DE1 HIGH→BALANCED latency (≲~35 ms on the stop write, <0.1 g) — acceptance-gated by on-device SAW validation on the weak Tab A8. Cross-reference #1093 / #1176; relates to merged #1185 (reverses its "DE1 always HIGH" + "App-Run Scoped, Not Persisted" for the latched case).
