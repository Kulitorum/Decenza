## Context

See proposal.md - Why.

Today's recompute-on-load is inconsistent across the three things `ShotHistoryStorage::loadShotRecordStatic` (`src/history/shothistorystorage.cpp:3209`) derives from a shot's raw samples:

- **Quality badges** (channeling/grind/skip-first-frame/pour-truncated): always recomputed via `ShotAnalysis::analyzeShot`, then compared against the stored `shots` columns; a mismatch triggers an `UPDATE shots SET ... WHERE id=:id` on the same connection (`shothistorystorage.cpp:3463-3490`). The comment there states the intended pattern directly: "the DB converges with detector improvements as shots are viewed without needing a separate bulk-resweep migration."
- **conductance / darcyResistance / conductanceDerivative**: only recomputed via `computeDerivedCurves()` when `record.conductance.isEmpty()` (`needsDerivedCurves` gate, `shothistorystorage.cpp:3370-3377`) — a backfill for shots that predate the `conductance` column (added migration 10), not a general trust-but-verify step.
- **resistance**: never recomputed. Loaded verbatim from the `shot_samples.data_blob` JSON (`shothistorystorage.cpp:2206-2207`). A shot recorded while the formula was briefly wrong (`44bd47b6`..`a6a58c21`) stays wrong forever.

`darcyResistance`'s per-sample formula is already duplicated inline in two places (`shotdatamodel.cpp:230-233` live path, `shothistorystorage.cpp:3125-3132` legacy-recompute path) despite the `Conductance` namespace existing specifically so "ShotDataModel (production, per-sample) and offline tools can share identical code paths" (`conductance.h:6-9`). `resistance` has no shared formula at all — it's inlined a third time in `shotdatamodel.cpp:218-221`. This is the same class of bug that caused #1822 in the first place: hand-rolled formulas drift.

**Scope note:** the affected population is not just the #1822 regression window. de1app's own `espresso_resistance` field is P/F² (`de1app/de1plus/gui.tcl`'s "laminar flow" calculation — the same formula Decenza calls Darcy resistance), and `shotfileparser.cpp` maps it verbatim into Decenza's P/F-labeled `record.resistance` for every `.shot`-file and Visualizer-recovery import. Making `resistance` recompute unconditionally therefore also corrects every imported shot's resistance curve, not only shots live-recorded during `44bd47b6`..`a6a58c21`. This is intentional given the Decisions below (the whole point is "recompute from this shot's own pressure/flow, don't trust the stored formula") — noted here so the blast radius is documented rather than a surprise at review time.

## Goals / Non-Goals

**Goals:**
- Every load of a shot's sample data recomputes all four derived curves (resistance, conductance, darcyResistance, conductanceDerivative) from that shot's own pressure/flow, unconditionally — not gated by which column happens to be empty.
- When a recomputed curve differs from what's stored, persist the correction back to `shot_samples.data_blob`, following the same compare-then-conditional-write shape already used for badges.
- Centralize the resistance and darcy-resistance formulas into the shared `Conductance` namespace so the live path and the load-time recompute path can no longer drift from each other, closing the actual defect class, not just this one instance of it.

**Non-Goals:**
- No bulk one-time migration / version bump. This is a lazy per-shot self-heal on load, matching the badge precedent, not a startup resweep.
- No change to the formulas' existing thresholds or clamps (e.g. `resistance` only guards `flow > 0.05` while `darcyResistance` also guards `pressure > 0.05` — that asymmetry already exists in the live path today and is out of scope here).
- No change to badge recompute logic — separate, already-correct mechanism.
- No change to how a shot is saved initially (`compressSampleData` / `ShotDataModel`), beyond having its resistance/darcy formulas call the newly shared helpers instead of inlining them.

## Decisions

### Recompute site: extend `computeDerivedCurves()`, drop the `needsDerivedCurves` gate

`computeDerivedCurves()` already does this recomputation for three of the four curves. Add `resistance` to it and call it unconditionally from `loadShotRecordStatic` right after `decompressSampleData()`, instead of only when `record.conductance.isEmpty()`. The function keeps its early-out for `n < 3` samples (too little data — leaves curves empty, matches current behavior).

Alternative considered: leave the legacy gate in place and add a second, narrower gate just for `resistance`. Rejected — two different gates for four curves that are all "pure function of pressure/flow" is exactly the kind of asymmetry that let this bug hide for months. One unconditional recompute is simpler and the cost is negligible (a shot is ~300-900 samples; recompute is a few O(n) passes plus one 9-point-kernel smoothing pass — low microseconds).

### Shared formulas: move resistance and darcy-resistance into `Conductance`

Add `Conductance::resistance(pressureBar, flowMlS)` and a darcy-resistance equivalent (mirroring the existing `Conductance::sample()` for plain conductance) to `src/ai/conductance.h`. Update both call sites — `shotdatamodel.cpp` (live, per-sample) and `shothistorystorage.cpp` (load-time recompute) — to call these instead of each inlining its own copy. This removes the pre-existing duplicate darcy loop and the never-shared resistance formula in the same pass, per the project's rule against hand-rolling a formula that already has a shared home.

### Persisting corrections: patch the four JSON keys in place, don't rebuild the blob from `ShotRecord`

`ShotRecord` doesn't carry every field the blob stores — e.g. `weightFlow` (raw scale reading) is written into the blob at save time from `ShotDataModel` but never read back into `ShotRecord` on load. Rebuilding the whole blob from `ShotRecord` would silently drop that field for every shot that gets corrected.

Instead: `decompressSampleData` (or a narrow sibling it calls) keeps the parsed `QJsonObject` in scope, recomputes the four curves, compares each recomputed curve to what was just parsed for that same key, and — only if something differs — overwrites just those four keys in the already-parsed object and recompresses. It reports the corrected blob back to `loadShotRecordStatic` (e.g. an out-parameter, `QByteArray` empty when no correction was needed), which issues:

```sql
UPDATE shot_samples SET data_blob = ? WHERE shot_id = ?
```

on the same `db` connection it already holds — same shape as the existing badge-persist block (`shothistorystorage.cpp:3472-3489`), including "skip the write when nothing changed."

### Comparison uses a tolerance, not exact equality

Both the freshly recomputed values and the stored values passed through a JSON parse/serialize round trip at different times (stored values when originally saved; recomputed values now, from pressure/flow that were themselves just parsed). Comparing with `==` risks spurious "different" results from floating-point representation noise on shots that are actually already correct, which would rewrite the blob on every single load instead of once. Compare with a small absolute tolerance (matching the epsilon already used elsewhere for point comparison, e.g. `1e-6`) and require array length equality first.

## Risks / Trade-offs

- **[Risk]** Every shot-detail load now takes the compare/recompute path that previously only ran for legacy shots → **Mitigation**: cost is a few thousand double operations per shot, not measurable by a user; this is also the entire point of the change.
- **[Risk]** Floating-point noise triggers spurious rewrites on shots that are already correct → **Mitigation**: tolerance-based comparison (see Decisions).
- **[Risk]** Two near-simultaneous loads of the same shot both detect drift and both issue the UPDATE → **Mitigation**: both computations are deterministic from the same stored pressure/flow, so both writes carry identical corrected content; harmless last-write-wins, same characteristic the existing badge-persist code already accepts.
- **[Risk]** A future formula change now silently rewrites every historical shot's stored curve the first time each is viewed, with no explicit "N shots backfilled" record → **Mitigation**: explicitly the intended behavior (this is what was asked for); the shared `Conductance` helpers mean a future formula change is a single reviewed edit, not an accidental drift between two inlined copies.

## Migration Plan

None. No schema change. Ships as a normal code change; each affected shot self-heals the next time it's opened.
