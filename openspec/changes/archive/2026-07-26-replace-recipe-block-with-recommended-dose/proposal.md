## Why

Decenza's profile JSON carries a `recipe` block that nothing reads. Its contents are recomputed
from the frames on every load (`prepDFlow` / `prepAFlow`), and `getOrConvertRecipeParams` ignores
a stored block outright for D-Flow and A-Flow. It is the only nested object in a format that is
otherwise flat scalars, the only key whose value is a cache of data that also lives elsewhere, and
neither de1app nor reaprime has any equivalent — de1app ships no `recipe` key in any of its 88
profiles, and reaprime models ten fields and discards the rest.

A cache that no reader trusts still drifts. Five shipped A-Flow built-ins carry byte-identical
boilerplate blocks (88 °C / 20 s / 9 bar) against frames that say 93–95 °C / 6–60 s / 9–10 bar —
REC-1 residue that #1646 neutralised but did not delete. The audit cannot see it, because making
the block irrelevant *was* the fix.

The block's only field that is not reconstructed from somewhere else is `dose`, and it is
consumed by nothing: it appears solely in `recipeparams.{h,cpp}` serialization and bounds checks,
is excluded from `frameAffectingFieldsEqual`, is read by neither frame generator, and has no QML
binding. The live per-profile dose is the top-level `recommended_dose` / `has_recommended_dose`
pair, which has a UI in the advanced editor and feeds `dialing_get_context` and the AI advisor.

Size note: `RecipeParams::toJson()` emits **30** keys today, but all 8 shipped built-ins carry
**34** on disk — they still hold `fillPressure`, `fillFlow`, `fillTimeout` and `infuseEnabled`,
which #1646 removed from the struct and which every save has silently dropped since. Both numbers
appear below and mean different things.

## What Changes

- **BREAKING (file format, additive-key removal):** `Profile::toJsonObject()` no longer emits
  `recipe`. The key stays listed in `kKnownProfileKeys` so the unknown-key passthrough does not
  echo it back — that list is the passthrough's *exclusion* list, so removing the entry would
  make every stale block immortal instead of dropping it.
- `Profile::jsonParityErrors()` gains a deliberately-dropped-key excusal for `recipe`. Without
  it, a dropped block reads as `recipe: KEY LOST` — objects are never "inert" — which disables
  `upgradeStoredEncoding`, the `espresso_temperature` repair persist in `loadProfile`,
  `migrateProfileFormat`, and the `profile_sync --rewrite-format` audit, for any profile that
  still carries one.
- `Profile::fromJson()` no longer reconstructs a `RecipeParams` from a stored block. It reads one
  field: a `recipe.dose` that differs from the struct default is promoted to `recommended_dose`
  (setting `has_recommended_dose`) when the profile does not already carry an explicit
  recommendation.
- A one-time upgrade rewrites existing profiles in the user, downloaded and SAF stores: strip
  `recipe`, promote a genuinely-set `dose`. Replaces the now-dead `migrateRecipeFrames` pass,
  whose entire purpose was regenerating frames from blocks.
- **A block is removed on sight, and the removal is persisted.** The upgrade covers what is
  already stored; a profile arriving afterwards — an import, a share code, a SAF sync, a restored
  backup — is stripped when it is loaded and written back once, the same shape as the existing
  `espresso_temperature` repair. Dropping the block only in memory would leave it on disk on any
  profile the user never re-saves. This is what makes the parity excusal above load-bearing
  rather than merely tidy: the persist path is gated on `jsonParityErrors`, so without the
  excusal the write-back is refused and the block survives.
- The eight shipped built-ins that carry a block (5 A-Flow, 3 D-Flow) ship without one.
- `RecipeParams::dose` is removed. MCP `profiles_edit_params` gains a real `dose` handler writing
  `recommended_dose` + `has_recommended_dose`; `profiles_get_params` reports `recommendedDoseG`
  **with** `hasRecommendedDose`, so a caller cannot read the 18 g default as a recommendation.
- `ProfileManager::uploadRecipeProfile`'s no-op-save check compares against frame-derived
  parameters **for D-Flow and A-Flow only** — advanced profiles share that branch and must keep
  their current comparison.
- The dead `regenerateFromRecipe()` safety net in `parseVisualizerProfile` is removed, since
  `m_hasRecipeParams` will never again be set from JSON.

## Capabilities

### New Capabilities

- `recipe-block-retirement`: the profile serializer stops emitting the `recipe` block; a
  genuinely-set dose survives as `recommended_dose`; a one-time upgrade brings already-saved
  profiles to the new shape; the parity checker excuses the removal.

### Modified Capabilities

- `recipe-editor-parity`: "A recipe block is written only when parameters were established"
  becomes "no recipe block is written." The no-retro-rewrite requirement is narrowed — it
  forbids *rewriting a disagreeing value*, and must not be read as forbidding removal of a field
  no reader consults, given that the one user-settable value in it is preserved. Its scenario
  "Stored profiles are not migrated" is deliberately dropped, because a pass now does exactly
  that.
- `profile-json-interchange`: the canonical serializer's emitted key set no longer includes
  `recipe`.
- `de1app-profile-parity`: "User-saved profiles are not rewritten" is narrowed so that removing a
  key no reader consults is not the retroactive rewrite it forbids.

## Impact

- `src/profile/profile.{h,cpp}` — serializer, `fromJson`, `recipeJson()`, `jsonParityErrors`,
  the `kKnownProfileKeys` comment
- `src/profile/recipeparams.{h,cpp}` — `dose` field and its serialization
- `src/controllers/profilemanager.{h,cpp}` — the one-time upgrade, the no-op-save check
- `src/network/visualizerimporter.cpp` — the dead safety net
- `src/mcp/mcptools_profiles.cpp` — `dose` handler, `get_params` output
- `tools/profile_sync.cpp` — parity audit behaviour (no code change expected; verify)
- `resources/profiles/*.json` — 8 built-ins
- `docs/CLAUDE_MD/RECIPE_PROFILES.md`, `docs/CLAUDE_MD/MCP_SERVER.md`
- Tests pinning block presence or content across ~10 files
- No change to frames, BLE output, or brewing behaviour. No change to `read_only`, `hidden`,
  `mode`, `temperature_presets`, or any de1app-derived key.
