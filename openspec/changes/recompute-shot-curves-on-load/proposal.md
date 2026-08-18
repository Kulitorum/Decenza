## Why

`resistance`, `conductance`, `darcyResistance`, and `conductanceDerivative` are pure functions of a shot's `pressure`/`flow` samples, but only `conductance`/`darcyResistance`/`conductanceDerivative` are recomputed on load, and only for shots that predate the `conductance` column (`ShotHistoryStorage::computeDerivedCurves`, gated by `needsDerivedCurves`). `resistance` is always read verbatim from storage. A stored value computed by a since-fixed formula therefore stays wrong forever, with no way for a user to get a correct graph short of deleting and re-recording the shot (#1822).

## What Changes

- Recompute all four derived curves (`resistance`, `conductance`, `darcyResistance`, `conductanceDerivative`) from `pressure`/`flow` every time a shot is loaded with sample data, not only when the stored value is missing.
- Compare the recomputed values against what was stored; when they differ, persist the corrected values back to `shot_samples.data_blob` so the fix is a one-time write per affected shot, not a repeated recompute-and-discard.
- Drop the `needsDerivedCurves` legacy-only gate — recompute is now unconditional, so the backfill-for-missing-column special case is no longer needed.

## Capabilities

### New Capabilities
- `shot-derived-curves`: covers derivation of resistance/conductance/darcyResistance/conductanceDerivative from raw pressure/flow on every shot load, and self-healing persistence back to the shot's stored sample blob when the recomputed values differ from what was stored.

### Modified Capabilities
(none — no existing spec currently documents this behavior)

## Impact

- `src/history/shothistorystorage.cpp`: `computeDerivedCurves()` gains resistance recomputation and always runs; the shot-load path (`loadShotRecordStatic` and its callers) gains a compare-and-persist step for `shot_samples.data_blob`.
- Runs on the existing background-thread load path already used for shot history reads — no new threading concerns.
- No schema change and no migration — this is a load-time self-heal, not a one-time batch fix.
