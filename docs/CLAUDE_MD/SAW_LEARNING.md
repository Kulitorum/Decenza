# SAW (Stop-At-Weight) Learning

## Problem

The DE1's stop-at-weight feature has to send the stop command *before* the target weight is reached, because there is unavoidable lag between the command and pump shutdown:

```
total drip ≈ flow_at_stop × (BLE_round_trip_lag + DE1_machine_lag) + post_shutoff_drip
```

The two hardware lags are scale-specific (`Settings::sensorLag()` documents them; e.g., Decent Scale 0.38 s, Acaia 0.69 s, Bookoo 0.50 s) and the post-shutoff drip is profile- and portafilter-specific. Together they yield a total "drip after stop trigger" the controller has to predict in advance, then trigger the stop when `current_weight + predicted_drip ≥ target`.

For years the predictor learned a single drip-vs-flow curve per scale type, stored as a rolling history of `{drip, flow, scale, overshoot}` records in QSettings (`saw/learningHistory`). That model worked for users on a single profile but gradually accumulated systematic error for users who alternate between profiles with different drip dynamics.

[Issue #847](https://github.com/Kulitorum/Decenza/issues/847) reported the failure mode in unambiguous terms: a user alternating between an open-spout portafilter with one profile and a double-spout portafilter with another saw the model overshoot for the dominant pairing and undershoot for the secondary pairing. The learning was being pulled by whichever profile-portafilter combo the user pulled most often, contaminating the prediction for the rarer one.

## Empirical evidence (this device, Apr 2026)

Pulling SAW debug logs from real shots on a Decent Scale showed the contamination directly:

| Shot | Profile | Trigger flow | expectedDrip | Settled drip | Result |
|------|---------|--------------|--------------|--------------|--------|
| 1056 | 80's Espresso | 1.99 g/s | 1.04 g | ~0.9 g | close |
| 1059 | 80's Espresso | 2.04 g/s | 1.13 g | 1.1 g | accurate |
| 1054 | 80's Espresso (short shot, fast end-flow) | 5.20 g/s | 1.27 g | ~2.5 g | **2× under-predicted** |
| 1047 | **D-Flow / Q** | 2.30 g/s | 1.24 g | ~0.8 g | **0.4 g over-predicted** |

Shot 1047 is the smoking gun. D-Flow / Q's actual drip is consistently ~0.8 g but the global model predicted 1.24 g — a systematic 0.4 g undershoot per shot, exactly the issue #847 pattern. The model was being pulled toward 80's Espresso's drip dynamics because that is the user's more frequent profile. D-Flow's lower drip rate never got a chance to teach the model its own signature.

Shot 1054 is a separate point: even within a single profile, end-of-shot flow can vary 2.5×. The drip-vs-flow regression already in place handles that, so we kept it.

## Solution: per-(profile, scale) batched learning

The same architecture that fixed the analogous problem in flow calibration ([AUTO_FLOW_CALIBRATION.md](AUTO_FLOW_CALIBRATION.md), issue [#739](https://github.com/Kulitorum/Decenza/issues/739)) applies cleanly to SAW:

1. **Per-(profile, scale) history** — each pair learns its own drip dynamics. Switching profiles or scales does not contaminate the active pair's data.
2. **Batched median commits** — accumulate 3 shots' worth of pending entries before changing the model. The median is robust to single-shot outliers (channeling, scale glitches, cup interaction). N=3 is chosen because SAW has no feedback loop (a stop command does not alter pump dynamics for the next shot), so a single confirmed 3-shot batch is sufficient signal — the conservative N=5 from flow calibration is not needed here.
3. **Batch-level dispersion check** — if the 3 lags within a batch are too spread out (any single lag > 1.5 s from the batch median), the whole batch is dropped. Dispersion that high indicates the user changed conditions mid-batch (different beans, different grinder, manual stop). IQR gating requires ≥ 4 values; with N=3 the per-element deviation check handles all outlier cases.
4. **Global bootstrap** — the median lag across all graduated `(profile, scale)` pairs on the same scale is published as `saw/globalBootstrapLag/<scaleType>`. New pairs use this as their first-shot default instead of the scale's hardware-only `sensorLag()`.
5. **Read-path fallback chain** — `perProfile` (≥ `kSawMinMediansForGraduation` committed batches, currently 1 = 3 SAW shots minimum) → `globalBootstrap` → `globalPool` (legacy entries) → `scaleDefault`. New users / new pairs degrade gracefully.

### Why this is the right shape

The two systems (flow cal and SAW) are learning the same kind of correction: a per-shot scalar that depends on (profile dynamics × hardware quirks), where the hardware quirk has a known prior (sensor lag for SAW, density correction for flow cal) and the profile contribution is what we have to learn from data. Both benefit from:

- **Isolation by profile** — to eliminate cross-profile bias.
- **Median over mean** — to absorb single-shot anomalies without explicit outlier detection.
- **Batched updates** — to break the feedback loop where each model change alters the conditions for the next shot.
- **Global fallback** — to cold-start new profiles from the system's collective experience instead of from a constant.

Reusing flow cal's design for SAW reduces risk because the patterns are already in production and have a year of operational evidence behind them.

## Storage schema

Three QSettings keys, each a JSON object keyed by `"<profileFilename>::<scaleType>"`:

| Key | Shape | Trim | Purpose |
|-----|-------|------|---------|
| `saw/perProfileHistory` | array of committed batch-median entries `{drip, flow, overshoot, scale, profile, ts, batchSize}` | 10 medians (~30 shots-worth) | Source of truth for `sawLearnedLagFor` / `getExpectedDripFor` once the pair has graduated (≥ `kSawMinMediansForGraduation` medians, currently 1). |
| `saw/perProfileBatch` | array of pending raw entries `{drip, flow, overshoot, scale, profile, ts}` (target size 3) | 3 (commit point) | Pending accumulator; flushed on commit or rejection. |
| `saw/globalBootstrapLag/<scaleType>` | scalar `double` (seconds) | n/a | IQR-fenced median of last committed median lag from each pair on this scale with at least one committed batch-median. Used as first-shot default for new pairs. (Graduation for the per-profile *read* path is a stricter `kSawMinMediansForGraduation` medians; the bootstrap is a cold-start prior, so it accepts pairs with any committed history — IQR fencing handles the rest.) |

The legacy `saw/learningHistory` key is preserved as a **global pool**: every committed batch-median is mirrored into it (trim 50). This keeps `isSawConverged()` and the legacy convergence-divergence detection working without changes, and provides a final read-path fallback for users with pre-update data.

The per-entry shape gains one optional field, `profile`. Old entries without it are still readable.

## Algorithm Details

### Read path

```
sawLearnedLagFor(profile, scale):
  if perProfileSawHistory(profile, scale).size ≥ kSawMinMediansForGraduation:
    return mean(drip / flow over last 3 medians)        ← perProfile
  if globalSawBootstrapLag(scale) > 0:
    return globalSawBootstrapLag(scale)                 ← globalBootstrap
  return sawLearnedLag()                                ← globalPool / scaleDefault

sawModelSource(profile, scale):
  → "perProfile" | "globalBootstrap" | "globalPool" | "scaleDefault"
```

`getExpectedDripFor` mirrors `getExpectedDrip`'s recency- and flow-similarity-weighted regression, but on the per-pair history when available. The flow-similarity weighting is what handles within-profile flow variation (e.g. shot 1054's 2.5× end-flow): the predictor weights past shots whose flow rate is close to the current shot's flow rate.

### Write path

```
addSawLearningPoint(drip, flow, scale, overshoot, profile):
  apply existing entry-level guards:                ← unchanged
    drip < 0 or flow < 0       → reject
    drip / flow > 4 s          → reject (implausible BLE lag)
    converged + |drip - expected| > max(3, expected)  → reject

  if profile is empty:                              ← legacy path preserved
    append to global pool, trim 50, save
    return

  append entry to perProfileBatch[profile::scale]
  if pending.size < 3:
    log "[SAW] accumulated drip=… flow=… (n/3) lag=…"
    return

  compute median drip, flow, overshoot, lag
  if any |lag - median_lag| > 1.5 s:
    log "[SAW] batch rejected — outlier lag=… deviates …s > …s from median …"
    drop pending, return

  if median_overshoot < -6 g and last committed median for pair was also < -6 g:
    log "[SAW] 2nd consecutive overshoot<-6g … clearing committed history"
    perProfileSawHistory[pair] := []                ← per-pair auto-reset

  append median entry to perProfileSawHistory[pair], trim 10
  append same median to global pool, trim 50
  clear pending
  recomputeGlobalSawBootstrap(scale)
  emit sawLearnedLagChanged()
```

### Why batched, with median

Per-shot updates create a feedback loop: each update changes the predicted-drip threshold, which changes when the stop command fires, which changes the observed drip. The model partially chases its own tail.

Batching to N=3 holds the model constant for 3 shots, so the 3 ideals are pulled under identical conditions and are directly comparable. Taking the **median** of those 3 ideals is a built-in outlier filter: a single bad shot (channeling, scale glitch, manual stop, runaway) cannot move the model.

Flow calibration uses N=5 for the same reason, but SAW has no feedback loop: a stop command does not alter pump pressure or flow dynamics for the next shot. Because the model update cannot shift the conditions it is measuring, N=3 converges 2× faster with equal or better accuracy. A post-hoc simulation over real shot data (Apr 2026) confirmed this: N=3 graduated D-Flow/Q at shot 6 vs never within 9 shots for N=5 (outlier in shot 3 was cleanly isolated rather than absorbed), and graduated 80's Espresso at shot 3 vs shot 10.

### Why dispersion guards on top of the median

Median rejects single outliers but does not detect the case where the user changed conditions mid-batch (new beans, new grind setting). In that case all 3 ideals have shifted, and committing the median would lock in a half-changed model. The deviation gate (`|lag - median| > 1.5 s`) drops the batch entirely in that case, forcing the user to start a fresh batch under stable conditions. (IQR gating requires ≥ 4 values; N=3 relies exclusively on the per-element deviation check.)

### Why a global bootstrap median instead of just the scale default

A new profile starting from `sensorLag(scaleType) + 0.1 s` will be off until 3 shots populate its history (one batch). With `globalSawBootstrapLag`, it starts from "what we have learned about this user's machine on this scale across their other profiles" — a much closer prior. Flow cal uses the same idea (median of espresso per-profile multipliers) and it noticeably reduces the cold-start error.

## Settling and final-weight capture

After SAW fires the stop command, [`ShotTimingController`](../../src/controllers/shottimingcontroller.cpp) runs a settling state machine that watches the scale stabilize. The cup gains a bit more weight from post-stop drip (typically 0.5–1.5 g) before reaching its true final weight, and learning needs both `m_weightAtStop` (the trigger weight) and the settled value (`m_weight`) to compute drip and update the model.

### Stability gate and clean-avg capture

Each settling sample is added to a 6-sample rolling window. The "stable enough to commit" gate fires when all three hold:

- `avgDrift < SETTLING_AVG_THRESHOLD` (0.3 g) — window mean has stopped shifting
- `weight ≤ avg + SETTLING_ABOVE_AVG_MARGIN` (0.2 g) — current reading has caught up to the mean (drip has effectively stopped)
- `avg ≥ m_weightAtStop − 0.5` — the average isn't still recovering from pump-vibration artifacts

Once the gate has held for `SETTLING_STABLE_MS = 1000 ms`, `onSettlingComplete` writes `m_weight = avg` and the learning point is committed.

After the gate has held *continuously* for `SETTLING_CLEAN_CAPTURE_MS = 250 ms` (≈ 3 consecutive samples at typical scale rates), each subsequent gate-firing sample also writes the avg into `m_lastCleanSettlingAvg` — the last-known-good settled value held in reserve for the cup-lift case below. The 250 ms minimum filters out single-sample transients where the rolling avg fortuitously satisfies the gate amid noisy settling (a corpus scan of 953 shots found 2/953 such false-positive transients without this guard).

### Final-weight on cup removal

If the user lifts the cup before settling completes, the cup-removal detector fires on a >20 g drop (single-step OR cumulative-from-peak). The handler **skips learning** (a corrupted weight stream can't teach the predictor) but the shot still needs a `finalWeightG` to persist.

Before issue [#1280](https://github.com/Kulitorum/Decenza/issues/1280), `m_weight` was left at whatever value the last accepted sample had. In practice that was often a *cup-lift spike artifact* that squeaked past the 20 g cup-removal threshold — e.g. shot 5470 persisted `38.5 g` even though the cup had actually settled at `42.3 g` for ~700 ms and SAW had correctly stopped at `41.2 g`. The AI advisor then reasoned from `yield=38.5, target=42` and invented a "you stopped manually" narrative.

The cup-removed branch now applies this fallback chain to restore `m_weight`:

1. **Last clean settling avg, if plausible** — the value captured by the stability gate above, **provided its overshoot above `m_weightAtStop` does not exceed `MAX_PLAUSIBLE_POST_STOP_DRIP_G = 5 g`**. Real drip is 0.5–3 g; a "stable" reading 10+ g above the trigger weight is almost certainly a scale fault (freeze, drift, sensor glitch), not a settled cup weight.
2. **Scale-fault snap-to-trigger** — if the clean avg was captured but failed the plausibility check, treat the entire post-stop weight stream as corrupted and set `m_weight = m_weightAtStop`. The corpus scan found one such shot where the scale froze at ~75 g during settling on a ~40 g target; without this guard the fix would amplify the glitch from "recorded yield wrong" to "recorded yield very wrong".
3. **`m_weightAtStop` floor** — if no clean avg was ever observed (cup lifted before the window filled), raise `m_weight` to at least the SAW trigger weight. Post-stop drip can only add weight; persisting a value below the trigger weight is physically impossible.
4. **Unchanged** — if the fallback can't help (no SAW trigger captured, or `m_weight` already above stop weight with no implausible clean-avg signal), leave it as today's value.

Learning is still skipped on the cup-removed path — corruption of the post-stop drip measurement is the only reason this branch exists. The fallback only affects the persisted `finalWeightG` (and therefore the AI advisor's `yieldG`, the visualizer export, and `DyeSettings::dyeDrinkWeight`).

### Offline replay

`./shot_eval --settling ~/shot_corpus/*.json` parses the `app.data.debug_log` embedded in each saved shot and replays the settling sample stream offline, printing `recorded` (today's `finalWeightG`) vs `postFix` (what the new fallback chain would persist). Useful for scanning a personal shot corpus for affected shots — most rows will print equal values; cup-lift-mid-settle shots surface with a `***` flag.

## User Experience

- **Default ON, automatic** — there is no setting; SAW learning is always on.
- **Calibration tab** ([qml/pages/settings/SettingsCalibrationTab.qml](../../qml/pages/settings/SettingsCalibrationTab.qml)) shows the current effective lag for the active `(profile, scale)` pair and a source suffix: `(per-profile)`, `(global bootstrap)`, `(global)`, or `(default)`.
- Two reset actions in the same card:
  - **Reset this profile** — visible only when the source is `(per-profile)`. Clears just the active pair's history and pending batch.
  - **Reset all** — clears every SAW key (global pool, all per-pair history + batch, all bootstrap values, hot-water SAW offset).
- MCP tools: `reset_saw_learning` (existing, now clears the new keys too) and `reset_saw_learning_for_profile` (new, takes optional `profileFilename` and `scaleType` arguments).

## Logging

Two audiences. **System log** (qDebug/qWarning) for live debugging. **Per-shot debug log** ([src/history/shotdebuglogger.cpp](../../src/history/shotdebuglogger.cpp)), which captures qDebug output via Qt's message handler hook so any single shot's debug log is self-contained for accuracy analysis.

System log lines:

| Event | Format |
|-------|--------|
| Entry accepted into batch | `[SAW] accumulated drip=… flow=… for <profile>::<scale> (n/3) lag=…` |
| Batch rejected (dispersion) | `[SAW] batch rejected — outlier lag=… deviates …s > …s from median median_lag=… for <pair> — dropping batch` |
| Batch committed | `[SAW] committed median lag=… (drip=… flow=…) for <pair> — n_medians=k` |
| Auto-reset | `[SAW] 2nd consecutive overshoot<-6g for <pair> — clearing committed history` |
| Bootstrap recompute | `[SAW] global bootstrap lag for <scale> updated to … (median of n graduated pairs)` |
| Reset (per-pair / all) | `[SAW] reset perProfileHistory for <pair>` / `[SAW] reset all SAW learning` |

Per-shot debug log adds two key lines (and the system log lines above are also captured here, since the shot debug logger hooks the global message handler):

- `[SAW] model: source=<…> lag=… profile=<…> scale=<…> historyN=k` — emitted at extraction start when WeightProcessor's snapshot is taken. Records which model is driving the prediction for *this* shot.
- `[SAW] accuracy: predictedDrip=… actualDrip=… delta=… overshoot=… flow=… scale=… profile=…` — emitted at settling completion. Headline number for "did SAW work for this shot."

Together, opening any saved shot via the Shot Detail page or the `shots_get_debug_log` MCP tool is enough to reconstruct what model the controller used, how accurate it was, and whether this shot fed the model.

## Storage Migration

No explicit migration. The new keys are written lazily as users pull shots; the legacy `saw/learningHistory` continues to be read as the fallback global pool. Existing users do not lose any data and do not need to re-learn — they just gradually accumulate per-pair history alongside the global pool.

A user who hits "Reset all" gets a clean slate (legacy + new keys both wiped) and starts fresh with the new architecture.

## Files

- [src/core/settings.h](../../src/core/settings.h) / [src/core/settings.cpp](../../src/core/settings.cpp) — schema, batch accumulator, read-path fallback chain, bootstrap recompute. The new public API mirrors flow cal: `sawLearnedLagFor`, `getExpectedDripFor`, `sawLearningEntriesFor`, `sawModelSource`, `resetSawLearningForProfile`, `globalSawBootstrapLag`, `addSawLearningPoint(…, profileFilename)`.
- [src/main.cpp](../../src/main.cpp) — wires `ProfileManager::baseProfileName()` into the WeightProcessor snapshot path and the `sawLearningComplete` handler, and emits the per-shot `model:` / `accuracy:` log lines.
- [src/controllers/shottimingcontroller.cpp](../../src/controllers/shottimingcontroller.cpp) — settling state machine, stability gate, `m_lastCleanSettlingAvg` capture, cup-removed fallback chain (#1280).
- [qml/pages/settings/SettingsCalibrationTab.qml](../../qml/pages/settings/SettingsCalibrationTab.qml) — Calibration tab UI changes (source suffix, per-profile reset).
- [src/mcp/mcptools_control.cpp](../../src/mcp/mcptools_control.cpp) — `reset_saw_learning_for_profile` tool.
- [tests/tst_saw_settings.cpp](../../tests/tst_saw_settings.cpp) — per-pair isolation, batch commit at N=3, dispersion-rejection, bootstrap recompute, fallback chain, reset behaviour, legacy-path preservation.
- [tests/tst_settling.cpp](../../tests/tst_settling.cpp) — settling-state-machine tests including the #1280 cup-lift-mid-settle regression replays.
- [tools/shot_eval/main.cpp](../../tools/shot_eval/main.cpp) — `--settling` mode for offline replay against a saved shot corpus.

## Related

- [AUTO_FLOW_CALIBRATION.md](AUTO_FLOW_CALIBRATION.md) — the architectural template this design copies from.
- [BLE_PROTOCOL.md](BLE_PROTOCOL.md) — BLE round-trip lag context for `Settings::sensorLag()`.
- Issue [#847](https://github.com/Kulitorum/Decenza/issues/847) — the bug report this addresses.
- Issue [#739](https://github.com/Kulitorum/Decenza/issues/739) — auto flow calibration evolution thread; the data and reasoning there directly informed this design.
