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
2. **Batched median commits** — accumulate 5 shots' worth of pending entries before changing the model. The median is robust to single-shot outliers (channeling, scale glitches, cup interaction). 5 was chosen to match flow cal.
3. **Batch-level dispersion check** — if the 5 lags within a batch are too spread out (IQR > 1.0 s, or any single lag > 1.5 s from the batch median), the whole batch is dropped. Dispersion that high indicates the user changed conditions mid-batch (different beans, different grinder, manual stop).
4. **Global bootstrap** — the median lag across all graduated `(profile, scale)` pairs on the same scale is published as `saw/globalBootstrapLag/<scaleType>`. New pairs use this as their first-shot default instead of the scale's hardware-only `sensorLag()`.
5. **Read-path fallback chain** — `perProfile` (≥ 3 committed batches) → `globalBootstrap` → `globalPool` (legacy entries) → `scaleDefault`. New users / new pairs degrade gracefully.

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
| `saw/perProfileHistory` | array of committed batch-median entries `{drip, flow, overshoot, scale, profile, ts, batchSize}` | 10 medians (~50 shots-worth) | Source of truth for `sawLearnedLagFor` / `getExpectedDripFor` once the pair has graduated (≥ 3 medians). |
| `saw/perProfileBatch` | array of pending raw entries `{drip, flow, overshoot, scale, profile, ts}` (target size 5) | 5 (commit point) | Pending accumulator; flushed on commit or rejection. |
| `saw/globalBootstrapLag/<scaleType>` | scalar `double` (seconds) | n/a | IQR-fenced median of last committed median lag from each graduated pair on this scale. Used as first-shot default for new pairs. |

The legacy `saw/learningHistory` key is preserved as a **global pool**: every committed batch-median is mirrored into it (trim 50). This keeps `isSawConverged()` and the legacy convergence-divergence detection working without changes, and provides a final read-path fallback for users with pre-update data.

The per-entry shape gains one optional field, `profile`. Old entries without it are still readable.

## Algorithm Details

### Read path

```
sawLearnedLagFor(profile, scale):
  if perProfileSawHistory(profile, scale).size ≥ 3:
    return mean(drip / flow over last 5 medians)        ← perProfile
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
  if pending.size < 5:
    log "[SAW] accumulated drip=… flow=… (n/5) lag=…"
    return

  compute median drip, flow, overshoot, lag, IQR(lags)
  if IQR > 1.0 s or any |lag - median_lag| > 1.5 s:
    log "[SAW] batch rejected — high dispersion …"
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

Batching to N=5 holds the model constant for 5 shots, so the 5 ideals are pulled under identical conditions and are directly comparable. Taking the **median** of those 5 ideals is a built-in outlier filter: a single bad shot (channeling, scale glitch, manual stop, runaway) cannot move the model. This same logic, with the same numerical constants, has held up well in flow calibration since #739 closed.

### Why dispersion guards on top of the median

Median rejects single outliers but does not detect the case where the user changed conditions mid-batch (new beans, new grind setting). In that case all 5 ideals have shifted, and committing the median would lock in a half-changed model. The IQR-and-deviation gate (`IQR > 1 s` or `|lag - median| > 1.5 s`) drops the batch entirely in that case, forcing the user to start a fresh batch under stable conditions.

### Why a global bootstrap median instead of just the scale default

A new profile starting from `sensorLag(scaleType) + 0.1 s` will be off until 5 shots populate its history. With `globalSawBootstrapLag`, it starts from "what we have learned about this user's machine on this scale across their other profiles" — a much closer prior. Flow cal uses the same idea (median of espresso per-profile multipliers) and it noticeably reduces the cold-start error.

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
| Entry accepted into batch | `[SAW] accumulated drip=… flow=… for <profile>::<scale> (n/5) lag=…` |
| Batch rejected (dispersion) | `[SAW] batch rejected — high dispersion median_lag=… iqr=… for <pair> — dropping batch` |
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
- [qml/pages/settings/SettingsCalibrationTab.qml](../../qml/pages/settings/SettingsCalibrationTab.qml) — Calibration tab UI changes (source suffix, per-profile reset).
- [src/mcp/mcptools_control.cpp](../../src/mcp/mcptools_control.cpp) — `reset_saw_learning_for_profile` tool.
- [tests/tst_saw_settings.cpp](../../tests/tst_saw_settings.cpp) — per-pair isolation, batch commit at N=5, dispersion-rejection, bootstrap recompute, fallback chain, reset behaviour, legacy-path preservation.

## Related

- [AUTO_FLOW_CALIBRATION.md](AUTO_FLOW_CALIBRATION.md) — the architectural template this design copies from.
- [BLE_PROTOCOL.md](BLE_PROTOCOL.md) — BLE round-trip lag context for `Settings::sensorLag()`.
- Issue [#847](https://github.com/Kulitorum/Decenza/issues/847) — the bug report this addresses.
- Issue [#739](https://github.com/Kulitorum/Decenza/issues/739) — auto flow calibration evolution thread; the data and reasoning there directly informed this design.
