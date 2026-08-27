## Context

The flow calibration multiplier is resolved by `SettingsCalibration::effectiveFlowCalibration()`
(`src/core/settings_calibration.cpp:213`): the per-profile value from `calibration/perProfileFlow`
when auto flow calibration is enabled and one exists, otherwise the global
`calibration/flowMultiplier`. `ProfileManager::applyFlowCalibration()`
(`src/controllers/profilemanager.cpp:3565`) writes it to the DE1 as MMR `FLOW_CALIBRATION`, and it
is re-applied on every change to either key.

A shot's stored curves are downstream of that value — `shots.flow` is the machine's calibrated
report — but no shot column records it. The only place it appears today is the debug log, in
`Auto flow cal: steady window found … currentFactor=`, which is written only when auto-cal runs to
that point. Shots with no physical scale, no qualifying window, or a stream-force rejection log
nothing.

Precedent for this exact shape already exists on the shot record: `yield_mode` /
`yield_anchor_value` (add-yield-ratio-anchor) store the intent behind a shot alongside its outcome,
latched at shot start by `ProfileManager::latchForShot()` and carried through `ShotMetadata` to the
row. This change follows it deliberately rather than inventing a second pattern.

## Goals / Non-Goals

**Goals:**
- Make a shot record self-describing: given the row alone, a reader knows what multiplier produced
  its flow curve.
- Record the value the shot was ACTUALLY pulled at, proof against the auto-cal update that runs
  between shot end and save.
- Distinguish "not recorded" from "recorded as 1.0" for every consumer, forever.
- Reach the surfaces that already consume shot provenance (MCP, AI payloads, export, transfer)
  without adding a new surface of its own.

**Non-Goals:**
- Not backfilling historical shots. The multiplier is unrecoverable from stored data.
- Not changing how the multiplier is computed, applied, or updated — `computeAutoFlowCalibration()`
  is untouched by this change.
- Not adding it to the Visualizer upload payload. DYE has no such field, and the local-history-only
  precedent (`storageHint`, `recipeId`, `yieldMode`) is established.
- Not surfacing it in the UI. Nobody asked to read it on a screen; the consumers are diagnostic.
- Not recording the global and per-profile values separately (see below).

## Decisions

### Latch at shot start, in `ProfileManager::latchForShot()`

`MainController::onShotEnded()` calls `computeAutoFlowCalibration()` at
`maincontroller.cpp:4079` — before it builds `ShotMetadata` (`:4100`) and before it calls
`saveShot()` (`:4267`). When a 5-shot batch completes on that shot, auto-cal writes a NEW
per-profile multiplier at that point. A save-time call to `effectiveFlowCalibration()` would
therefore read the post-update value on exactly the shots where the value changed, and record a
multiplier the shot was never pulled at — silently, and most often on the shot a reader most wants
to interpret.

So the value is snapshotted in `latchForShot()`, which already exists for precisely this hazard
(its comment: a target once moved mid-pour and cut a shot short) and already holds `m_settings` and
`m_baseProfileName`. `latchedFlowCalibration()` joins `latchedTargetG()` as a shot-start fact the
save path reads.

**Alternatives considered:**
- *Read at save time, before `computeAutoFlowCalibration()`.* Would work today by ordering alone.
  Rejected: it makes a correctness property depend on the relative position of two calls in one
  function, with nothing to fail if a later edit moves either. The latch states the intent.
- *Echo back what `DE1Device` last wrote.* `setFlowCalibrationMultiplier()`
  (`src/ble/de1device.cpp:1852`) keeps no copy, so this means adding a member there and reading
  device state at save time — a second source of truth for a settings value, and still readable at
  the wrong moment.

### NULL means "not recorded", never 1.0

The column is nullable with no default and no backfill. A shot from before this change, or from an
importer whose source lacks the column, stores NULL. 1.0 is a legitimate multiplier, so a default
of 1.0 would make "we don't know" indistinguishable from "we measured 1.0" — in the one column
whose purpose is knowing which. The struct-side sentinel is `0.0` (never a valid multiplier;
persistence bounds are `[0.5, 2.7]`, `kProfileFlowCalMin`/`kProfileFlowCalMax`), bound to the row as
`value > 0 ? QVariant(value) : QVariant()`, exactly as `yield_anchor_value` binds.

Backfilling is not merely unavailable, it would be wrong: the per-profile multiplier stored in
settings today is the CURRENT value, and auto-cal has been moving it. Stamping it onto historical
rows would assert that every past shot ran at today's number, which is false for any user whose
calibration ever converged.

### One number: the effective multiplier

Stored is what `effectiveFlowCalibration()` returned — the value written to the machine — not the
global and per-profile values separately. That is the only one with physical meaning for the shot's
curves; a reader asking "which source won" is asking a settings question, and preserving a
two-field split on every row to answer it costs a column and an invariant nobody consumes. If that
question ever needs answering per shot, `flow_calibration` plus the profile name still narrows it.

### Rides on `ShotMetadata`, not a new `saveShot()` argument

`ShotHistoryStorage` has no access to `Settings` or `ProfileManager`, so the value has to be handed
in. `ShotMetadata` is already the channel for shot-start context that only `MainController` can
supply (`recipeId`, `steamJson`, `bagId`, `yieldMode`, `yieldAnchorValue`), several of which are
explicitly documented there as local-history-only and not part of the upload. Adding a tenth
argument to a ten-argument `saveShot()` instead would be the worse of two conventions.

### Sparse emit in the projection

`ShotProjection::toVariantMap()` omits `yieldMode`/`stoppedBy` when they carry nothing meaningful,
so consumers read absence as "unknown" rather than parsing a sentinel. `flowCalibration` follows:
emitted only when `> 0`. This matters for the AI payloads specifically — a `0` or `1.0` reaching a
model as a recorded fact is a fabricated measurement, which is the failure mode the whole column
exists to prevent.

## Risks / Trade-offs

- **The latch must be released and re-armed correctly.** A stale latched value would mis-stamp a
  later shot. Mitigated by living inside the existing latch, which `releaseShotLatch()` already
  governs; a shot that never latched (imported, simulated) reads 0 and stores NULL, which is the
  honest answer.
- **Positional load indices.** `loadShotRecordStatic()` reads by numeric index (`query.value(55)`
  today). The new column is appended at the END of the SELECT list (index 56), the same rule the
  taste-axis and yield-anchor columns followed, so no existing index shifts.
- **Migration count.** One more additive column on a table that has taken many. The migration is a
  single `ALTER TABLE … ADD COLUMN`, gated `>= 38 && < 39`, with the version stamp transacted
  (`DELETE` + `INSERT` in one transaction) per the rule in `CLAUDE.md`.

## Migration Plan

Migration 39, following the shape of migrations 25-27:

1. `ALTER TABLE shots ADD COLUMN flow_calibration REAL` when `hasColumn()` says it is absent.
2. Stamp 39 only if the column is present afterwards — it is a schema fact, so the bump is gated on
   it; an incomplete run retries next launch.
3. No data pass, no settings touched, nothing to roll back. Downgrade-safe: an older binary ignores
   an unknown column.
