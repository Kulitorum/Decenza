# flow_calibration

The per-profile flow calibration multiplier scales the DE1's flow-sensor reading. Above 1.0 means
the machine was under-reporting flow.

## Which value is actually in effect

There are two values — a per-profile one and a global fallback — and a switch that decides which
the machine uses:

| Auto calibration | Per-profile value stored | In effect |
|---|---|---|
| on | yes | the per-profile value |
| on | no | the global multiplier |
| **off** | yes | **the global multiplier — the stored per-profile value is ignored** |
| off | no | the global multiplier |

That third row is the one that gets misread. `action=get` reports it explicitly in `state`, and
`action=set` warns when it stores a value that is not currently in effect.

`get` also returns `pendingAutoCalShots` and `autoCalBatchSize`: auto calibration only commits a
new value on a FULL batch, which is why a stored multiplier can sit unchanged for several shots.

## Freezing an exact number

`action=set` writes the same value auto calibration learns, so with auto calibration ON the
number takes effect immediately and later shots keep adjusting it. To freeze an exact number:
turn auto calibration off (`settings_set autoFlowCalibration=false`) AND set that number as the
global multiplier (`settings_set flowCalibrationMultiplier`).

## Orphans

`action=get` with `allProfiles: true` is the only way to answer "which profiles are calibrated?",
because an unknown profile name is rejected rather than probed. It reports `profileExists` per
entry and an `orphanCount`: entries naming a profile that no longer exists, written before the
name check was added. They are never read. `action=clear` accepts a missing profile precisely so
an orphan can be removed; `hadCalibration` says whether anything was actually stored.

Accepted range for `set` is 0.5-2.7. Values outside it are refused, not clamped.
