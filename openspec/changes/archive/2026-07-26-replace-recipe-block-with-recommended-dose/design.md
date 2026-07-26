## Context

The `recipe` block was once the storage model: the editor's numbers were authoritative and frames
were generated from them. #1646 inverted that — the frames became the truth and
`getOrConvertRecipeParams` was changed to ignore any stored block for D-Flow and A-Flow, calling
`prepDFlow` / `prepAFlow` on the frames instead. That change neutralised the block but left it on
disk.

**Decenza is the only producer.** de1app has no such key in any of its 88 profiles, reaprime
models ten fields and drops the rest, and Visualizer normalises it away in both its JSON and TCL
renderings. So the population of files carrying a block is closed and finite: it is exactly the
profiles Decenza itself wrote. That is what makes the transitional machinery below a sunset
rather than a permanent tax.

Measured state of a shipped D-Flow profile's block — 34 keys on disk, of which
`RecipeParams::toJson()` still emits 30:

| Group | Count | Fate |
|---|---|---|
| Re-derived from frames by `prep` before any read | 8 (D-Flow) / 12 (A-Flow) | overwritten |
| Shadowed by top-level `target_weight` / `target_volume` | 2 | overwritten |
| Inert for that editor type (simple-editor scalars, per-step temps, A-Flow toggles on a D-Flow profile) | 19 / 15 | never read |
| Unmodelled legacy (`fillPressure`, `fillFlow`, `fillTimeout`, `infuseEnabled`) | 4 | already dropped on any save since #1646 |
| Genuinely carried | 1 (`dose`) | promoted, see below |

The frame builders read exactly the fields `prep` derives — `createFillFrame` /
`createInfuseFrame` / `createPourFrame` touch 8 fields, `generateAFlowFrames` touches 12, and
`prep` overwrites all of them. That overlap is why the five stale A-Flow blocks are harmless
today, and why the parity audit is structurally blind to them.

`RecipeParams::dose` appears only in `recipeparams.{h,cpp}` (toJson / fromJson / toVariantMap /
fromVariantMap / clamp / validate). It is excluded from `frameAffectingFieldsEqual` as "metadata
only", read by neither generator, and has no QML binding. Its only surface is MCP. The live
per-profile dose is the top-level `recommended_dose` / `has_recommended_dose` pair, bound in
`ProfileEditorPage.qml:524-539` and consumed by `dialing_get_context`, `profiles_get_detail` and
the AI advisor.

## Goals / Non-Goals

**Goals:**

- Stop emitting a stored cache of frame-derived values.
- Leave **no profile carrying a block** — drain the stores at upgrade, strip anything that
  arrives later, and persist the strip.
- Preserve the one value in it a user can set (`dose`) in a key that is actually consumed.
- Keep frames, BLE output and every other key byte-identical.

**Non-Goals:**

- Changing how editor parameters are derived. `prepDFlow` / `prepAFlow` are untouched.
- Rewriting frames, or "correcting" any value that disagrees with them.
- Removing `RecipeParams` itself — it remains the in-memory editor DTO, just not a persisted one.
- ~~Adding a recommended-dose control to the Recipe Editor. D-Flow/A-Flow profiles have no dose
  UI today and deliberately do not gain one.~~
  **WRONG, corrected during review.** Both `RecipeEditorPage` and `SimpleProfileEditorPage`
  already had a Dose control bound to `recipe.dose`, and the Recipe Editor's round-tripped
  correctly on `main`. Removing the field silently broke both. They were repaired to read and
  write `recommended_dose` — no new surface was added; an existing one was kept working. The
  scoping decision this Non-Goal recorded was taken on my false claim that no such control
  existed, so it never had the meaning it appears to have. Kept struck through rather than
  deleted: the promoted spec now carries the rule (`One dose field, whichever surface sets it`),
  and a Non-Goal that quietly vanished would leave the next reader wondering why the change
  touched QML at all.
- Reading de1app's `profile_grinder_dose_weight` on import. Covered by
  `fix-tcl-import-metadata-loss`.

## Decisions

**Keep `recipe` in `kKnownProfileKeys` while neither reading nor writing it — permanently.** That
list is the *exclusion* list for the unknown-key passthrough (`profile.cpp:1104` captures every
key **not** in it; `profile.cpp:430` starts the output from those captured keys). A listed key is
dropped; an unlisted one is preserved verbatim. Removing the entry as "tidying up" would make
every stale block immortal, and would resurrect one on every save even during the migration.
Listed-but-unhandled *is* the removal. The comment above the set documents only the forward rule
("add a key here when Profile starts modelling it") and must gain the inverse.

**Excuse `recipe` in `jsonParityErrors`.** `isInertValue` returns false for any object
(`profile.cpp:687`), so a dropped block always reads as `recipe: KEY LOST`. Four consumers act on
that verdict: `upgradeStoredEncoding` (`profilemanager.cpp:1318`), the `espresso_temperature`
repair persist (`:1504`), `migrateProfileFormat` (`:3288`) and `tools/profile_sync --rewrite-format`.
Without an excusal, every profile still carrying a block is permanently refused canonicalisation
and repair — and the strip-on-load write-back below is refused too, which would defeat the goal
outright. `nonZeroDefaultKeys()` is the existing precedent for a key-specific rule in that checker.

**Strip on sight and persist, not just on save.** Dropping the block in memory means it survives
on disk on any profile the user never re-saves. The persist follows the existing
`m_espressoTemperatureHealed` pattern in `loadProfile` — a flag set during `fromJson`, a single
write-back, no rewrite on subsequent loads. A profile that cannot be written (bundled resource,
read-only store) still loads with no block in memory; the failure is reported, not raised.

**Scope the no-op-save guard change to D-Flow and A-Flow.** `uploadRecipeProfile` branches on
`isSimpleProfile` (`profilemanager.cpp:2509`), so the `else` holds dflow, aflow **and advanced**.
For advanced, `extractRecipeParams` falls past the dflow/aflow dispatch into the heuristic pattern
detector, so frame-derived params would never compare equal to the editor's defaults →
`needFrameRegen` true → `regenerateFromRecipe()` early-returns for advanced → **and the `else`
that applies target weight and volume is skipped**, silently dropping a target edit. So the new
comparison basis applies only where the editor was populated by `prep`; advanced keeps comparing
against `m_currentProfile.recipeParams()`, which for it is a default-constructed struct on both
sides and therefore still equal.

This matters because a regeneration is not a no-op: D-Flow derives `exit_pressure_over` from the
soak pressure (`soak < 2.8 ? soak : soak/2 + 0.6`, floored 1.2), and an observed regeneration
moved frame 0 byte 5 from `0x30` (3.00) to `0x3a` (3.6).

**Remove the dead Visualizer safety net.** `parseVisualizerProfile` calls `regenerateFromRecipe()`
when a payload has no steps (`visualizerimporter.cpp:654`). That call is guarded by
`!m_hasRecipeParams`, which `fromJson` will no longer set, so it becomes unreachable and its
comment ("the net still fires for its actual case") becomes false. A stepless payload is already
rejected by the `isValid()` / `steps().isEmpty()` checks in both callers, so deleting the call
loses nothing.

**Replace `migrateRecipeFrames` rather than adding a second migration, and place it before
`migrateProfileFormat`.** Its purpose was regenerating frames from stored blocks — and note it is
*not* a no-op today: it genuinely rewrites frames, because `fromJson` currently sets the flag.
Retiring it is a real behaviour change for an install that never ran it, and the right one: those
profiles keep their frames instead of having them rebuilt from a block that #1646 established is
untrustworthy. The constructor order is `migrateProfileFormat` → `migrateRecipeFrames` →
`migrateReadOnlyProfiles` (`:292/295/298`), and `migrateProfileFormat` carries a parity gate
whose comment warns it must not reach the legacy population unaudited. Leaving the strip pass in
slot 295 would have `migrateProfileFormat` refuse every legacy profile carrying a block (per the
parity finding) and then let the ungated strip pass rewrite those same files anyway. Running the
strip first, with the excusal in place, keeps the gate meaningful.

**Give the MCP `dose` parameter real plumbing, and report its flag.** `doseG` is a read-only
output alias today (`mcptools_profiles.cpp:344`); only keys present in `currentParams` are
accepted on write (`:450-454`). Once `RecipeParams::dose` is gone, an unmodified `dose` would land
in `ignoredKeys` — the exact outcome the redirect exists to avoid. So the handler must run before
that loop and write `Profile::setRecommendedDose` / `setHasRecommendedDose`. On read,
`recommendedDoseG` must be accompanied by `hasRecommendedDose`: every profile holds 18 g whether
set or not, and a bare figure tells an AI there is a recommendation when there is not.

**Promote only a non-default dose, and never over an explicit recommendation.** Every shipped
block has `dose: 18` — the struct default — with `has_recommended_dose: false`. Promoting
unconditionally would switch on a recommendation the user never made, on every profile that had a
block.

## Sunset

The strip machinery is transitional by construction, because the producing population is closed.
Once a release has shipped with this change and the stores have drained, the following can be
deleted, in this order:

1. The one-time upgrade pass and its settings flag.
2. The strip-on-load persist and its flag.
3. The `recipe.dose` promotion in `fromJson`.
4. The `jsonParityErrors` excusal.

**The `kKnownProfileKeys` entry stays permanently.** It is one string, and it is what makes a
straggler — an old share code, a restored backup, a device that skipped a release — simply drop
its block on the next save instead of having it captured and re-emitted forever. Removing it is
the one step that converts "harmless leftover" back into "immortal leftover", so it is not part
of the sunset. The code comment should say so, or someone will delete the dangling entry as dead
code.

## Risks / Trade-offs

**The no-op-save guard is the one place a bug would be silent** → Getting it wrong either
regenerates on every save (visible churn, and an `exit_pressure_over` rewrite) or short-circuits a
real edit so the user's change vanishes. `recipeparams.h:52` warns about exactly this class.
Mitigation: a test that edits one parameter and asserts the frames change, paired with a test that
saves without editing and asserts the frames do not — both against a D-Flow profile whose stored
`exit_pressure_over` is off-formula, so a spurious regeneration is detectable. Plus a test that an
advanced profile's target-weight edit still applies.

**The migration and the strip-on-load both rewrite user files** → A bug corrupts profiles.
Mitigation: both remove one key and may add two, re-serializing through the canonical serializer,
so any other difference is a serializer bug that would already affect every save. Both are gated
on the parity check, and a failed write leaves the original intact and is reported by name.

**Loss of `RecipeParams::dose`'s `[0, 100]` clamp** → `setRecommendedDose` is a bare assignment
(`profile.h:112`), so the MCP redirect drops a bound an agent previously could not bypass. Low
impact (display only), but it is a silent loss rather than the pure gain the proposal implies.
Mitigation: clamp in the new handler.

**A straggler after the sunset** → Its block is dropped on next save and never re-emitted, because
the `kKnownProfileKeys` entry is permanent. Accepted.

**The block stops being available as an extension point** → Deliberate. Anything frame-derived
should not be stored; anything independent belongs in a flat top-level key, which is what both
de1app and the rest of Decenza's format already do.

## Migration Plan

1. Ship the serializer, reader and parity-excusal changes together. A build that writes no block
   but still reads one is harmless; the reverse would resurrect blocks on every save, and the
   excusal must land with them or the write-backs are refused.
2. The strip pass runs at `ProfileManager` construction **before** `migrateProfileFormat`, gated
   on a new settings flag, over the user, downloaded and SAF stores. `_current.json` is skipped,
   as the pass it replaces did.
3. Anything arriving later is stripped and persisted on load.
4. Shipped built-ins are updated at source, not by the migration — they are resource files, and
   all eight carry `dose: 18`, so nothing is promoted.
5. Rollback: an older build reading a stripped profile derives its parameters from the frames,
   which is what it already does. The only regression on rollback is a promoted dose showing as a
   recommendation rather than a block value — a display difference, not data loss.

## Open Questions

None outstanding. Two were raised and resolved:

- **A Recipe Editor dose control** — decided against. Dose belongs to recipes going forward, so
  D-Flow/A-Flow profiles keep having no dose UI and keep assuming the 18 g default.
- **`preserve-recipe-visualizer-roundtrip`** — being withdrawn under the separate
  `fix-tcl-import-metadata-loss` change, on the evidence this one rests on.
