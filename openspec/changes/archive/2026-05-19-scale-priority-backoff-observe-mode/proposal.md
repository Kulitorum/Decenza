## Why

The scale BLE connection-priority dual-HIGH backoff acts on a single observation: one >2 s scale-feed stall (or a ≥2/20 s DE1-fault cluster) latches the scale link to BALANCED app-run-wide, then disconnects and reconnects. Field triage (2026-05-18) found the only firing on the shipping build was a genuine 15 s stall that recovered correctly — but it is impossible to tell from a production run *how often the detector would fire if it took no action*, or *whether the scale recovers on its own at HIGH*, because firing changes the very behavior under evaluation. We need a way to run a production build with detection live but inert, so the aggressiveness of the backoff can be judged on real evidence instead of inference.

## What Changes

- Add a persistent, MCP-controlled **backoff policy mode** with two values: `enforce` (default — today's behavior, unchanged) and `observe` (detect-and-log-only).
- In `observe`: the detector still arms and evaluates the identical DE1-fault-cluster and scale-feed-stall conditions, but instead of latching/disconnecting it logs a WARN "would back off" event and **takes no action** — the link stays HIGH. It is **not** fire-once: it keeps detecting subsequent episodes for the duration of a long production test.
- In `observe`: the link is **forced to HIGH** — entering observe overrides any pre-existing persisted BALANCED latch and never skips HIGH, so detection runs against the real at-risk path.
- Add **recovery logging**: a new `scaleFeedResumed` signal lets the system log when a stalled feed comes back on its own ("feed RESUMED after X.X s — would-have-been-backoff recovered at HIGH"), plus a log when a DE1-fault cluster window elapses without escalating. This is the core evidence the evaluation needs.
- The mode **persists until explicitly changed** — it survives app restarts and build upgrades (unlike the build-scoped latch). Only an explicit MCP call (or switching back to `enforce`) changes it.
- Surface the mode and a bounded ring of recent observe events (would-back-off + recovered, each timestamped) via the MCP read tool, and add an MCP tool to set the mode.
- **Correctness fix**: `clearConnectionPriorityLatch()` currently removes the entire `connectionPriority/` QSettings group; it must remove only the four latch sub-keys so the new mode key survives a latch reset.
- Not breaking: additive. `enforce`-mode behavior (the detector, the latch, the disconnect/reconnect, the existing structural confirmation) is byte-identical to today; observe is opt-in via MCP.

## Capabilities

### New Capabilities
<!-- none — this extends existing in-flight capabilities -->

### Modified Capabilities
- `ble-connection-priority`: add the `enforce`/`observe` policy-mode dimension to the dual-HIGH backoff, the observe (no-action, non-latching, forced-HIGH, repeating) detection path, the `scaleFeedResumed` recovery signal + cluster-subsided logging, and the non-build-scoped mode persistence (plus the latch-clear key-scoping fix). Layers on the two active, unarchived changes `scale-ble-priority-backoff` and `scale-priority-backoff-preheat-persist`; does not archive them.
- `mcp-server`: add `devices_set_scale_priority_mode` (eventually-consistent, same contract as `devices_reset_scale_priority`) and extend `devices_connection_status` to report `backoffMode` and the recent observe-event list.

## Impact

- **Code**: `src/ble/transport/bleprioritydetector.h` (observe-aware non-latching path), `src/ble/transport/qtscalebletransport.cpp` (mode gate: `triggerScaleBackoff` vs new `logWouldBackoff`; observe forces HIGH; wire resume), `src/ble/blemanager.{h,cpp}` (`BackoffMode` enum + getter/setter + observe-event ring + connect-path wiring), `src/machine/weightprocessor.{h,cpp}` (`scaleFeedResumed(gapMs)` + stall-start tracking), `src/main.cpp` (cross-thread wiring of the new signal, mirroring `scaleFeedStalled`), `src/core/settings_hardware.{h,cpp}` (`cpMode()/setCpMode()` non-build-scoped persistence; scope the latch-clear to the four latch keys), `src/mcp/mcptools_devices.cpp` (new tool + extended status).
- **MCP / data conventions**: ISO-8601-with-offset timestamps, units in field names (`stallSec`, `gapSec`), human-readable enum strings (`"enforce"`/`"observe"`) per CLAUDE.md.
- **Persistence**: one new QSettings key `connectionPriority/policyMode`; latch keys unchanged in meaning but the clear path is narrowed to not delete siblings.
- **Tests**: `bleprioritydetector` observe path (repeating would-fire, never latches, stays armed, recovery re-arms); `tst_weightprocessor` (`scaleFeedResumed` gap correctness; observe never alters SAW/flow/frame decisions); MCP tool round-trip if a harness exists.
- **No** DB migration, schema change, UI surface, or Visualizer payload change. `enforce`-path rollback is a single revert; observe is inert by construction.
