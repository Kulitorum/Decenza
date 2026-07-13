# Design: recipe-relative-temp-offset

## Context

Recipes store an absolute brew temperature (`temp_override_c`) but every editor presents and every user reasons about a **delta against the profile** ("Temp offset", "this bean 3° cooler"). The absolute is re-diffed against the profile's current `espresso_temperature` at each display, so profile temperature edits (`applyTemperatureToProfile` from Brew Settings' "Save temperature to profile") retroactively change what the user sees — phantom ±5° offsets on recipes never touched (PR #1492 video report, reproduced live).

Independently, recipe cards call `ProfileManager::temperatureDisplay()`, which reads `m_currentProfile.steps()` — the globally loaded profile — so every card shows the active profile's frame temps (all cards flipped 90·88 → 84·94 °C on activation of one recipe), and cards paint recipe-pinned values amber while the post-#1492 Shot Plan correctly shows them white.

Current flow of the absolute value: `recipestorage` (column + record) → `applyActivatedRecipe` / `activeBaselineTemperatureC` (maincontroller.cpp:1334, 1487) → `Settings.brew` override → BrewDialog / ShotPlanText baselines; parallel writers are the wizard (RecipeWizardPage.qml:276 open-time subtraction), promote-from-shot (recipepromotion.cpp:50), MCP (`temperatureOverrideC`), and ShotServer/web editor.

## Goals / Non-Goals

**Goals:**
- One stored truth with delta semantics: `temp_offset_c`; the effective brew temperature is always `profile.espresso_temperature + offset`, computed at use time.
- Recipe cards (and the wizard preview) computed 100% from the card's own recipe: its own profile's frames plus its own stored deltas — source + modification, with the tint on the deltas only.
- Migration that preserves today's effective behavior for every existing recipe on an unchanged profile.

**Non-Goals:**
- Yield stays an absolute target (`yieldG`) — a yield is a cup weight, not a relationship to the profile.
- Shot records are untouched: a shot's `temperatureOverrideC` is a frozen absolute snapshot of what was brewed — correct as-is; Shot Review lines keep shot-relative highlighting.
- No stepper/ValueInput rework (verified correct), no Brew Settings layout changes.

## Decisions

**D1 — New column, legacy column left dead.** Add `temp_offset_c REAL` via the standard kCols migration; stop reading/writing `temp_override_c` (dropped from the record model and kCols registration). SQLite `DROP COLUMN` is avoided — a dead column is harmless and keeps the migration a pure additive step. *Alternative rejected:* reinterpreting `temp_override_c` in place — absolute values (85–95) and offsets (±10) overlap numerically with no way to tell a migrated row from an unmigrated one.

**D2 — Conversion is a reusable pass, run at migration AND after legacy imports only.** `offset = temp_override_c − profile.espresso_temperature`, profile resolved by the recipe's title (user → downloaded → built-in), falling back to the recipe's embedded profile JSON; unresolvable → offset 0 (a delta against an unknown baseline is meaningless; logged). |offset| < 0.05 stores 0. Device-to-device transfer / backup import runs the pass **only when the source schema lacks `temp_offset_c`** (detected via the existing `PRAGMA table_info` read): an old-version peer still sends `temp_override_c` and needs conversion, but a new-version source carries a correct offset *and* the stale absolute in the dead column — reconverting from the dead column would resurrect exactly the phantom offsets this change abolishes (e.g. user cleared the offset after migrating; the dead column still says 87). A legacy-source import stages the source's absolute into the destination's dead `temp_override_c` and marks the row unconverted (`temp_offset_c = NULL`); the same deferred conversion pass that migration 31 uses then normalizes it (re-triggered when an import completes). Normal operation never reads or writes the dead column.

**D3 — Activation math replaces the Bug-A guard.** `applyActivatedRecipe`: `if (offset != 0) setTemperatureOverride(profileTemp + offset)`. The old "value ≈ profile default is not an override" comparison disappears — offset 0 is unambiguous. `activeBaselineTemperatureC()` returns `profileTargetTemperature() + activeRecipe.tempOffsetC`. The `m_activeRecipe` map key becomes `tempOffsetC` (QML consumers: BrewDialog, ShotPlanItem, ScreensaverEditorPopup follow).

**D4 — Cards get per-recipe frames via an explicit-steps formatter.** `ProfileManager` gains `Q_INVOKABLE QString temperatureDisplayForSteps(const QVariantList& stepTempsC, double anchorTemp, bool hasOverride, double overrideTemp, double baselineShiftC)` — same `TemperatureDisplay::format()` core, frames supplied by the caller. `ShotPlanText` gains `property var profileStepTemps: []`; non-empty routes `_tempStr` through the new invokable, empty keeps the live-widget read of the current profile. `RecipesPage.refreshProfileNumbers()` already loads the recipe's profile map (`getProfileByFilename` returns `steps`) — it additionally extracts the frame temperatures. *Alternative rejected:* a filename-keyed formatter doing its own disk read per card per repaint — the page already has the profile map in hand.

**D5 — Cards show source + delta; the tint marks only the delta.** A recipe card deliberately answers "what does this recipe start from, and how does it modify it" (Jeff's ruling, 2026-07-13): temperature renders the recipe's own profile's frames **unshifted** with the stored offset as a signed tag ("84 · 94°C −3°"), and yield renders "profileYield → recipeYield g" when they differ (the existing arrow). The highlight color wraps only the delta markers — the offset tag and the yield arrow expression — never the profile's base temps, so amber uniformly means "this recipe modifies its profile" on every surface. ShotPlanText composes the temperature as base string (explicit-steps formatter, no tag) + a QML-built tag span; the tag converts the offset with `Theme.cDeltaToDisplay` so °F users see the delta in °F (×9/5, no +32 — same as the wizard stepper). The yield arrow logic is unchanged. A card whose profile resolves neither by title nor embedded JSON omits the temperature segment entirely — it never falls back to the loaded profile's frames (that bleed-through is the bug this fixes). Live surfaces (Shot Plan, Brew Settings) keep the #1492 recipe-as-baseline presentation: recipe values only, no profile reference. The wizard's summary preview follows the card rule.

**D6 — Wizard edits the stored value directly; tea converts at the boundary.** For coffee drinks, `fTempDeltaC` loads `r.tempOffsetC` verbatim and saves back verbatim (no `fProfileTempC` subtraction at open, no addition at save). Promote-from-shot keeps a conversion (shot absolute − shot profile temp) because the *shot* side is a frozen absolute by design.

**Tea recipes share the same stored field with an absolute-temperature UI** (`fTeaTempC`, "Temp (°C)") — the user thinks "80°", not "profile −8°". The UI stays absolute and converts at the load/save boundary exactly like promote-from-shot: load `fTeaTempC = profileTemp + offset` (offset 0 → show the profile temp), save `offset = teaTemp − profileTemp` (equal → 0). Activation needs no tea special-case — `profile temp + offset` reproduces the absolute the user typed. Missing this would write an absolute (~80) into the delta column and, on the load side, seed `fTeaTempC = 0` and silently discard the stored temperature on re-save (the exact hazard the existing load-path comment warns about). MCP/web tea recipes go through the same `tempOffsetC` field — the surfaces are delta-only (D7); a tea client sends the delta like everyone else.

**D7 — Surfaces rename without aliases.** MCP + ShotServer/web expose and accept only `tempOffsetC`. A silent alias for `temperatureOverrideC` would let an old client write an absolute (≈90) into a delta column — a 90° offset — so the field is removed outright and documented (`MCP_SERVER.md`, `RECIPES.md`, agent file). Update Recipe (BrewDialog temp button) persists `dialed − profileTemp`.

## Risks / Trade-offs

- [Migration misresolves a renamed profile] → title match falls back to the recipe's embedded profile JSON (stored at save time); only recipes with neither lose their pin, logged with recipe name for triage.
- [Old-device transfer after this ships] → import keeps accepting `temp_override_c` and the conversion pass normalizes post-import (D2); covered by a storage test.
- [External MCP/web clients using `temperatureOverrideC`] → breaking and intentional (D7); surfaced in docs and the MCP agent file. In-repo consumers are all updated in this change.
- [`extract-recipe-wiring-controller` (proposed) moves `applyActivatedRecipe`] → whichever lands second rebases; contracts here are spec-level, not file-level.
- [MCP register-stub duplication] → `tests/tst_mcpserver_*` / `tst_mcptools_*` stubs recompile against any signature change; build `--target all` before pushing (known project gotcha).

## Migration Plan

Single forward step inside the existing recipes-table migration chain (background thread): add column → convert rows (D2) → conversion pass also invoked at the tail of transfer/backup import. No rollback path needed beyond "dead column still holds the old absolutes" — a downgraded build reads `temp_override_c` untouched.

## Open Questions

None — storage semantics (relative), presentation (cards = source + delta with tint on the delta only; all other surfaces = recipe values as baseline), and the no-alias surface rename were decided with Jeff on 2026-07-13.
