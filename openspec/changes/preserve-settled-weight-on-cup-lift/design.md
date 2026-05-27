## Context

`ShotTimingController` runs a settling state machine after SAW fires the stop command, sampling the scale at ~4–10 Hz and tracking when the cup's weight stabilizes. Two stable-completion paths exist: sample-driven (`onWeightSample` once the rolling window flattens for `SETTLING_STABLE_MS = 1000 ms`) and timer-driven (`onDisplayTimerTick` BLE-silence override after 2 s of no samples).

When the cup is lifted before settling completes, a cup-removal heuristic at [`shottimingcontroller.cpp:210–211`](../../src/controllers/shottimingcontroller.cpp) catches the final big drop and aborts learning. But it does NOT undo the damage already done by transient cup-lift artifacts. `ShotDataModel` has spike rejection (>10 g/s); `ShotTimingController` does not — every settling sample is written to `m_weight`. Then `MainController::onShotEnded` reads `m_timingController->currentWeight()` as `finalWeightG` for the saved record.

For shot 5470 the sequence was:

```
clean settling (~700 ms stable at 42.3 g)
  → m_weight = 42.5, m_lastSettlingAvg = 42.3
cup lift starts (within ~150 ms of the stable plateau, before SETTLING_STABLE_MS)
  → samples 44, 48.4, 51 arrive (TC accepts all; m_weight = 51, peak = 51)
  → sample 38.5 arrives (drop is under 20 g threshold, accepted; m_weight = 38.5)
  → sample −28 arrives (drop > 20 g from m_weight = 38.5; cup-removed fires)
  → cup-removed handler early-returns with m_weight = 38.5
```

`m_weight = 38.5` then propagates into `finalWeightG`, the saved shot record, `DyeSettings::dyeDrinkWeight`, the visualizer export, and the AI advisor's `yieldG` field.

## Goals / Non-Goals

**Goals:**

- Persist a `finalWeightG` that reflects what was actually in the cup at the moment the user lifted it.
- Make the standalone "analyze this shot" prompt carry `stoppedBy` so LLMs do not invent stop-reason narratives when `yieldG` looks short.
- Add regression coverage for the cup-lift-mid-settle path so this bug cannot silently return.

**Non-Goals:**

- Backfilling existing under-reported shots in shot history (the recorded value is what it is; future shots get the correct value).
- Restoring SAW learning on cup-lifted shots (cup removal still corrupts the drip measurement; we keep `m_sawTriggeredThisShot = false` as today).
- Changing the cup-removal detection thresholds.
- Mirroring `ShotDataModel`'s 10 g/s spike rejection in `ShotTimingController` — a much larger blast radius that could reject legitimate "delayed splat" samples.

## Decisions

### Decision: Capture the last clean rolling-window avg, not a spike-rejected stream

**Rationale:** The settling code already computes a rolling-window average and a "stable enough" gate (`avgDrift < SETTLING_AVG_THRESHOLD && !avgBelowStop && !weightAboveAvg`). When that gate is satisfied, the avg by definition reflects a settled cup. Tracking that value separately as `m_lastCleanSettlingAvg` and restoring it on cup-removed gives us the truth for free.

**Alternative considered:** Apply `ShotDataModel`'s `> 10 g/s` spike rejection to `ShotTimingController`'s settling path so cup-lift spikes never update `m_weight` in the first place. Rejected — the rate threshold was tuned for SDM's role and the surface area of the change is much larger. A legitimate splat or scale recovery sample could be wrongly rejected. The bug only manifests on the cup-removed path, so the surgical fix lives there.

**Alternative considered:** Just floor at `m_weightAtStop` (always raise `m_weight` to at least `m_weightAtStop` on cup removed). Rejected as primary path — simpler but loses ~1 g of post-stop drip that actually landed. Kept as the secondary fallback when no clean avg was ever observed.

### Decision: Capture in BOTH `onWeightSample` and `onDisplayTimerTick` stable branches

**Rationale:** Settling has two completion paths (sample-driven and BLE-silence). The cup-removed handler can only fire from `onWeightSample` (a new sample is required to detect removal), so technically capturing in `onWeightSample` is sufficient for the bug at hand. But the stable branch in `onDisplayTimerTick` ALSO writes `m_weight = avg` at line 437 when it finalizes — for symmetry and to avoid a class of "wrong source for `m_lastCleanSettlingAvg` if the BLE-silence path ever drives cup-removed in a future change", capture in both. Costs one line.

### Decision: Reset `m_lastCleanSettlingAvg` in BOTH `startShot()` and `startSettlingTimer()`

**Rationale:** `startSettlingTimer()` resets all other settling-related members, but it's only called on SAW shots. A non-SAW shot that runs immediately after a SAW shot bypasses `startSettlingTimer()` entirely. The new field is gated by `if (m_sawSettling)` so it can't leak into a non-SAW shot's recorded weight, but defensive reset at every shot boundary keeps the state machine clean and predictable. Belt-and-suspenders.

### Decision: Live and saved summarize paths must produce identical `stoppedBy`

**Rationale:** `MainController` already classifies `stoppedBy` from authoritative C++ state ([line 2050](../../src/controllers/maincontroller.cpp)) and persists it on the shot record. The live summarize path (called from the post-shot review page) needs the same value. Re-deriving it inside `summarize` would risk drift between the two derivations. Cleanest: have `MainController` pass the already-resolved string into `summarize()`. The saved path reads `ShotProjection.stoppedBy` directly from the DB — same value either way.

### Decision: `stoppedBy` allowlist matches `dialing_blocks.cpp`

The standalone block emits `stoppedBy` only when the value is `"manual"`, `"weight"`, or `"volume"` — same gating as [`src/ai/dialing_blocks.cpp:121-124`](../../src/ai/dialing_blocks.cpp). Omitting `"profileEnd"` and `""` matches the rubric at [`shotsummarizer.cpp:1405`](../../src/ai/shotsummarizer.cpp) which already documents how the model should treat the field's absence (profile-end vs DE1 hardware button — the BLE protocol cannot distinguish).

## Risks

- **`weightAboveAvg` gate could be bypassed by a very slow cup lift.** The gate requires `weight ≤ avg + SETTLING_ABOVE_AVG_MARGIN (0.2 g)`. At lift rates above ~0.8 g/sample (3–8 g/s at scale rates) the gate fires reliably. Hand lifts are physically faster than that, and the 20 g cup-removal threshold catches anything slower within ~10–20 samples regardless. No realistic scenario where `m_lastCleanSettlingAvg` gets polluted by a slow lift.
- **A shot that lifts the cup before `SETTLING_WINDOW_SIZE = 6` samples are accumulated** has no clean avg to fall back on. The `m_weightAtStop` floor catches it (drip can only add, so finalWeight ≥ SAW trigger weight). For shot 5470 this isn't the case (user had ~700 ms of stable readings first), but the fallback chain handles it.
- **`stoppedBy` field becomes load-bearing for the standalone block.** If `MainController` ever forgets to pass it, the field silently disappears. Mitigated by: defaulting to empty (matches today's behavior), and the test for this requirement will assert the field is present for known-SAW shots.

## Migration

None. No schema changes. No setting added. Existing recorded shots keep their under-reported `finalWeightG`. Shots pulled after the fix ships record the correct value.

## Open Questions

- Should the offline `settling_replay` tool be added to `tests/data/shots/manifest.json` validation so shot 5470 lives in the regression corpus as a fixture? **Defer** — manifest format expects detector verdicts, not finalWeight assertions; adding a new manifest schema for this one case feels heavy. The unit test covers the same invariant.
