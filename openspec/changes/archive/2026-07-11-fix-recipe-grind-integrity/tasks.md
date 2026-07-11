## 1. Bug A — real, clearable override flags

- [x] 1.1 Make `hasTemperatureOverride`/`hasBrewYieldOverride` only go true when the incoming value genuinely differs from the profile default by more than the 0.1 epsilon `ShotPlanText.qml` already uses — not unconditionally on every `setTemperatureOverride`/`setBrewYieldOverride` call. Note: `SettingsBrew` has no profile knowledge, so per design.md Decision 6 either add a reference-value parameter to the setters or make the flag decision at the `ProfileManager`/`MainController` call sites — the setters as-is (`settings_brew.cpp` ~792-844) cannot do the comparison alone.
- [x] 1.2 Wire a real clear (`clearTemperatureOverride()`/equivalent for yield) into every path that currently calls `ProfileManager::clearBrewOverrides()` (`profilemanager.cpp:453-460`) — profile switch, recipe activation, recipe deactivation — so the flags actually go false, not just the numeric value resyncing.
- [x] 1.3 In `MainController::applyLoadedShotMetadata` (`maincontroller.cpp:661-748`, ~724-727), only mark the override active when the shot's frozen `temperatureOverride`/yield genuinely differs from the freshly-loaded profile's own default.
- [x] 1.4 Decide the fate of dead code `SettingsBrew::clearAllBrewOverrides()` (`settings_brew.h`/`.cpp:851-878`) — wire it in as the shared clear point, or remove it if unused after 1.2.
- [x] 1.5 Reconcile `qml/components/layout/items/CustomItem.qml:43-51`'s duplicate highlight rule with `ShotPlanText.qml`'s (`_tempOverride`, ~lines 75-80) — same override-only definition, explicit decision on whether brew-by-ratio also highlights.
- [x] 1.6 Manually verify: switch profiles/recipes repeatedly with no deliberate override set — Shot Plan never highlights; set a deliberate override — it highlights and clears correctly via Clear, profile switch, and recipe switch.

## 2. Recipe-owned grind — schema semantics and migration

- [x] 2.1 Update `src/history/recipestorage.h`'s header comment (~lines 23-27) and any code comments referencing "inherit" to describe grind as always recipe-owned.
- [x] 2.2 Add migration 30 in `src/history/shothistorystorage.cpp` (next after migration 29, same gated/idempotent pattern as migrations 26-29): for every `recipes` row with empty `grind_pinned` and a non-null `bag_id`, copy that bag's current `grinderSetting`/`rpm` into `grind_pinned`/`rpm_pinned`. Skip rows whose bag has an empty `grinderSetting` (tea bags, never-dialed bags — don't write empty strings). Rows with no linked bag are left untouched.
- [x] 2.3 Add/update unit test coverage for migration 30 (mirror the existing migration chain tests referenced in `docs/CLAUDE_MD/TESTING.md`), including the empty-source-grind skip and a tea recipe left grind-less.
- [x] 2.4 In `RecipeStorage::requestCreateRecipe`, add the save-time grind default for non-interactive surfaces (design.md Decision 4): a create map that links a bag but **omits** grind adopts the bag's current `grinderSetting`/`rpm` as the recipe's own value (mirroring the existing bag-link normalization at `recipestorage.cpp:399-405`); a map with an **explicitly empty** grind stores it empty. Unit-test both cases.
- [x] 2.5 Apply the migration-30 backfill rule inside `RecipeStorage::importRecipesStatic` (`recipestorage.cpp:1154`) for imported recipes with empty `grind_pinned` and a (remapped) bag link — a device-transfer/backup source DB can predate migration 30, and imported rows arrive after this device's migration already ran. Add import-path test coverage.

## 3. Make bag grind write-through unconditional (Bug B1 root cause)

- [x] 3.1 In `src/core/settings_dye.{h,cpp}`, remove `m_grindBagWriteThroughSuspended` and its setter; make the `writeThroughToBag("grinderSetting"/"rpm", ...)` calls inside `setDyeGrinderSetting`/`setDyeGrinderRpm` (`settings_dye.cpp:288-320`) unconditional, matching how every other bag-backed dye field already writes through. Keep the `writeThroughToActivePackage("lastGrindSetting"/"lastRpm", ...)` calls unchanged.
- [x] 3.2 In `src/controllers/maincontroller.cpp`, drop the grind-routing suspend/bag-fallback branch in `applyActivatedRecipe` (~lines 1151-1164) — the live dye cache's grind/rpm always come from `recipe.grindPinned`/`rpmPinned` when a recipe is active; the (now-unconditional) bag write-through and the recipe stamp fire independently off the same edit event.
- [x] 3.3 Remove the `if (!m_activeRecipe.value("grindPinned")...)` guard on the live-edit-to-recipe stamp connections (`maincontroller.cpp:941-949`) — always stamp the active recipe on grind/rpm change, in parallel with the bag write-through.
- [x] 3.4 Confirm `MainController::onShotEnded`'s existing post-shot bag stamp (~lines 2942-2960, dose/yield/lastUsed) needs no grind/rpm changes — grind/rpm are already kept current live, so nothing further to add there. Optional hygiene (design.md Decision 2 note): route the stamp through a `SettingsDye`-counted path to skip the now-redundant post-shot bag re-read; not load-bearing for correctness.
- [x] 3.5 Implement the bean-less activation bag-clear (design.md Decision 2, `recipe-activation` spec): `applyActivatedRecipe` clears the active bag when the recipe has no bean link, and the ingredient-swap deactivation watcher treats "no bag" as matching a bean-less recipe (the clear must not self-deactivate the recipe). Verify the other corollary too: activating a bag-linked recipe updates that bag's grind to the recipe's value (`coffee-bag-model` scenario).
- [x] 3.6 Manual verification: activate a recipe, edit its grind mid-session, pull a shot — grind is unchanged after the shot; the linked bag's stored grind already matched the live value well before the shot ended.
- [x] 3.7 Manual verification: edit grind (with or without a recipe active) and force-quit/restart the app before pulling a shot — the edited value is unaffected on restart (the bag write-through already landed live).

## 4. Recipe re-activation reconciliation (Bug B2)

- [x] 4.1 In `MainController::activateRecipe`/`applyActivatedRecipe`, detect same-id re-activation (`id == dye->activeRecipeId()`) and, per design.md Decision 3, short-circuit to re-pushing the current in-memory `m_activeRecipe` cache through the apply stages instead of re-reading from `RecipeStorage`.
- [x] 4.2 Confirm first activation (from a fresh session, or of a *different* recipe) still does the full fresh DB read — only same-id re-activation is short-circuited.
- [x] 4.3 Add/update `tst_settings` or equivalent coverage (per `docs/CLAUDE_MD/TESTING.md`) for: (a) re-tapping the active recipe while a grind write is in flight preserves the edit; (b) an external edit to the active recipe (simulated via direct storage write) is still picked up on next genuine activation.
- [x] 4.4 Manual verification: edit grind on the active recipe, immediately re-tap its pill (RecipesPage/RecipesItem) before the write could plausibly land — live grind is the edited value, not reverted.

## 5. Recipe wizard — remove inherit toggle, add default-fill, reorder equipment before grind

- [x] 5.1 In `qml/pages/RecipeWizardPage.qml`, remove the `fGrindOverride` toggle and "Follows the bag" (`trInherited`) UI — grind/rpm fields are always plain editable fields.
- [x] 5.2 Repurpose the existing bag-read logic (`refreshInheritedGrind`, ~lines 456-580, and bag-selection handlers ~1114-1123, 2559-2565) into a one-time default-fill on bag (re)selection during creation, gated on `fEquipmentRpmCapable` for the rpm portion — not a live mode.
- [x] 5.3 Confirm editing an existing recipe does not re-trigger the default-fill (only shows the recipe's own stored grind/rpm).
- [x] 5.4 Reorder the "details" step layout so the Equipment section (~lines 2245+) renders above the numbers/grind card (`numbersCard`/`grindCard`, ~lines 1947-2170).
- [x] 5.5 Update the grind-hint/history prefill logic per `recipe-wizard` spec's updated "Details step prefills from history, then bag data, then profile defaults" requirement — bag default-fill applies only when there's no matching shot history for the bean+profile pair.
- [x] 5.6 Change the promote-from-shot prefill (`RecipeWizardPage.qml:427-428`): default `grindPinned`/`rpmPinned` from the shot's recorded `grinderSetting`/`rpm` regardless of `hasBeanData` (today's `hasBeanData ? "" : ...` empty/inherit encoding is retired), editable on the summary before saving.
- [x] 5.7 Manual verification: create a new recipe for a bean+profile with no shot history — grind/rpm prefill from the bag's current dial, rpm only shown/prefilled when the chosen equipment is rpm-capable, and this is correct on first view (no equipment-not-yet-chosen flicker). Promote a shot whose grind differs from the bag's current dial — the recipe defaults to the shot's grind.

## 6. Recipe-bag-lifecycle simplification

- [x] 6.1 Update relink code paths (`RecipeStorage`'s roll-on-finish / wake-on-restock / manual re-point handlers) to confirm they already never touch `grindPinned`/`rpmPinned` — remove any remaining inherit-branch logic/comments if present.
- [x] 6.2 Confirm stale-recipe activation (`recipe-bag-lifecycle`'s "Stale is a display state, never a lock") applies the recipe's own grind regardless of the linked bag's finished state — should already follow from Section 3's changes; add a regression test if not already covered.

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/RECIPES.md`'s "Grind: inherit by default, pin by exception" section to describe the new always-owned model, the one-time creation default-fill, and the post-shot bag write-back.
- [x] 7.2 Sweep `src/history/recipestorage.h`/`.cpp` and `src/controllers/maincontroller.cpp` for other stale "inherit"/"pin"/"suspend" comments introduced by `add-recipes` (commit `77dffb72`) and update them.
- [x] 7.3 Update the user manual in the GitHub wiki (`Kulitorum/Decenza.wiki`, Manual pages): recipe grind now always lives on the recipe (no "Follows the bag" option — describe the creation-time default from the bag), the wizard details step's equipment-before-grind order, and what the Shot Plan highlight color means (deliberate temperature override only). Per the standing convention, hold the wiki edits uncommitted in the local wiki working tree and push only when the release ships.

## 8. MCP / Web surface update (contract changes, verified required)

- [x] 8.1 Update `src/mcp/mcptools_recipes.cpp` to the recipe-owned model: the read-side `grind.mode: "inherited"|"pinned"` object (~lines 125-155) collapses (drop `mode` or make grind a plain value), `recipe_create`'s empty-to-inherit recommendation (~lines 398, 426-429), `recipe_update`'s "set '' to return to inheriting" (~lines 493-494), and `recipe_create_from_shot`'s inherit rule (~lines 584-585, now: shot's grind becomes the recipe's own). Update `tst_mcpserver_session`/protocol stubs per `docs/CLAUDE_MD/MCP_SERVER.md`.
- [x] 8.2 Update `src/network/shotserver_recipes.cpp`: `effectiveGrind` bag-resolution when `grindPinned` is empty (~line 99) and the "(pinned)" label (~line 659) go away — the recipe's own grind is the value; check the edit form's empty-field semantics against the omitted-vs-explicit-empty rule (task 2.4).
- [x] 8.3 Update `docs/CLAUDE_MD/MCP_SERVER.md`'s recipe-tool documentation to the recipe-owned grind semantics (empty `grindPinned` no longer means inherit). No client-compatibility callout needed — MCP clients reload tool schemas/descriptions every connection.

## 9. Final verification

- [x] 9.1 Run the full test suite (`docs/CLAUDE_MD/TESTING.md`) including the migration chain tests and any new/updated `tst_settings` coverage.
- [x] 9.2 Walk through GitHub issue #1468's original repro (Videos 1 and 2): activate a recipe with a pinned/owned grind, edit grind while Shot Plan may show an override highlight from an unrelated deliberate temperature change, pull a shot — confirm grind is retained and the highlight only reflects the deliberate temperature override, not the grind edit.
- [x] 9.3 Update `docs/CLAUDE_MD/RECIPES.md` cross-references (`RECIPE_PROFILES.md` table row, `MCP_SERVER.md` if grind contract changed) are consistent.
