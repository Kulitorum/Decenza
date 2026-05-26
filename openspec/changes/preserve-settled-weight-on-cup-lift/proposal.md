## Why

Issue [#1280](https://github.com/Kulitorum/Decenza/issues/1280): the AI advisor gave Mark a self-contradictory dial-in recommendation, claiming "you stopped manually at 38.5g when the target was 42g" and suggesting a coarser grind despite a fast 17 s duration. Mark did not stop the shot — SAW correctly stopped at 41.2 g and the cup settled at 42.3 g for ~700 ms before he lifted it. But the shot record persisted `finalWeightG = 38.5`, so the advisor reasoned from bad data.

Root cause is in `ShotTimingController::onWeightSample()` settling branch: cup-lift spike artifacts (44, 48.4, 51, then a 38.5 down-step) pollute `m_weight` before the cup-removed detector fires on the final ≥20 g drop. The cup-removed branch then preserves `m_weight` as documented, but that value is now a poisoned post-spike artifact rather than the real settled weight.

A secondary issue compounds it: the standalone "analyze this shot" JSON block sent to LLMs omits `stoppedBy`, so when `yieldG` looks short the model is free to invent "you stopped manually" framing. The field already exists on dial-in surfaces (`bestRecentShot`, `dialInSessions`) via `dialing_blocks.cpp` but not on the standalone shot block.

## What Changes

- **`ShotTimingController`**: track the last *clean* rolling-window average during settling (only updated when the existing stability conditions hold). On cup-removed, restore `m_weight` to that value if present; else floor at `m_weightAtStop` (drip can only add); else leave as-is. Reset the new state on both `startShot()` and `startSettlingTimer()`.
- **`ShotSummarizer`**: add `stoppedBy` to `ShotSummary`, populate from the same source `MainController` uses (live) and from `ShotProjection.stoppedBy` (saved), emit in the standalone shot block with the same `{manual, weight, volume}` allowlist `dialing_blocks.cpp` uses.
- **Documentation**: add a "Settling and final-weight capture" section to `docs/CLAUDE_MD/SAW_LEARNING.md`.
- **Tests**: add a unit test in `tests/tst_settling.cpp` that replays shot 5470's settling sample stream and asserts `currentWeight()` ≈ 42.3 g (fails today, returns 38.5 g).
- **Tools**: add `tools/settling_replay/` — a small CLI that parses `app.data.debug_log` from any saved shot JSON, replays the settling samples through the controller, and prints before/after `finalWeight` so the fix can be validated against a personal shot corpus.

## Capabilities

### Modified Capabilities

- `stop-at-weight-learning`: new requirement defining how `finalWeightG` is captured at shot end — clean settle uses the settled rolling-window average; cup-lifted-mid-settle falls back to the last clean average, then to `m_weightAtStop` as a physical floor.
- `advisor-user-prompt`: new requirement that the standalone shot JSON block carries `stoppedBy` when the saved value is in the same `{manual, weight, volume}` allowlist used on dialing-context surfaces.

## Impact

- **`src/controllers/shottimingcontroller.{h,cpp}`** — new `m_lastCleanSettlingAvg` member, capture in two stable-branch sites (`onWeightSample`, `onDisplayTimerTick`), restore in cup-removed handler, reset in `startShot()` + `startSettlingTimer()`.
- **`src/ai/shotsummarizer.{h,cpp}`** — new `stoppedBy` field on `ShotSummary`; populated in `summarize` (live path) and `summarizeFromHistory` (saved path); emitted in `buildShotBlock`.
- **`src/controllers/maincontroller.cpp`** — pass the already-resolved `stoppedBy` string into `m_summarizer->summarize()` so the live path and saved path produce identical values for the same shot.
- **`tests/tst_settling.cpp`** — new test `cupLiftMidSettlePreservesLastStableAvg`.
- **`tools/settling_replay/`** — new offline CLI, wired into the desktop-only `tools/` block in root `CMakeLists.txt`.
- **`docs/CLAUDE_MD/SAW_LEARNING.md`** — new section.
- No BLE protocol changes, no setting added, no schema migration, no shot-data backfill (existing under-reported shots remain as-recorded; the fix applies only to shots pulled after deploy).
