# Tasks: recipe-relative-temp-offset

## 1. Storage & migration

- [x] 1.1 Add `temp_offset_c` to the recipes schema: kCols registration + CREATE TABLE + migration step in `src/history/recipestorage.{h,cpp}`; replace `tempOverrideC` with `tempOffsetC` in the record model; normal operation stops reading/writing `temp_override_c` (a legacy-source import stages the source absolute into the dest dead column and marks the row unconverted for the deferred pass — the only writer)
- [x] 1.2 Implement the reusable legacy-conversion pass (D2): `offset = temp_override_c − profile.espresso_temperature`, profile by title (user → downloaded → built-in) with embedded profile JSON fallback, unresolvable → 0, |offset| < 0.05 → 0; run it in the migration chain, and at the tail of transfer/backup import **only when the source schema lacks `temp_offset_c`** (PRAGMA table_info check — a current-version source imports its offset verbatim and its dead legacy column is ignored)
- [x] 1.3 Extend `tests/tst_recipestorage.cpp`: migration converts a legacy absolute against its profile; unresolvable profile drops the pin; legacy-source import converts; current-version-source import does NOT reconvert (offset reset to 0 survives even with a stale dead column); offset round-trips through save/load

## 2. Activation & baselines

- [x] 2.1 `applyActivatedRecipe` (maincontroller.cpp): apply `profileTemp + tempOffsetC` as the temperature override iff offset ≠ 0; delete the Bug-A ≈-comparison for temperature; rename the `m_activeRecipe` map key to `tempOffsetC`
- [x] 2.2 `activeBaselineTemperatureC()`: return `profileTargetTemperature() + activeRecipe.tempOffsetC` (yield accessor unchanged)
- [x] 2.3 Update QML consumers of the recipe map key: `BrewDialog.qml` (incl. Update Recipe persisting `dialed − profileTemp`), `ShotPlanItem.qml`, `ScreensaverEditorPopup.qml`

## 3. Per-recipe frames & card baseline presentation

- [x] 3.1 `ProfileManager`: add `Q_INVOKABLE temperatureDisplayForSteps(QVariantList stepTempsC, anchorTemp, hasOverride, overrideTemp, baselineShiftC)` reusing `TemperatureDisplay::format`; cover in `tests/tst_temperaturedisplay.cpp`
- [x] 3.2 `ShotPlanText.qml`: add `profileStepTemps` property; non-empty routes `_tempStr` through the new invokable, empty keeps current live behavior
- [x] 3.3 `RecipesPage.refreshProfileNumbers()`: extract frame temperatures from the already-loaded profile map and hand them to the card
- [x] 3.4 `RecipeDrinkCard.qml`: pass `profileStepTemps` (own profile) and the stored offset; temperature renders profile temps plain + highlighted signed tag (tag-only tint, °F via `Theme.cDeltaToDisplay`); yield keeps the "profile → recipe" arrow with tint on the arrow expression only; unresolvable profile → omit the temperature segment (never the loaded profile's frames)
- [x] 3.5 `RecipeWizardPage.qml` summary preview: same source + delta treatment as the card

## 4. Wizard & promotion

- [x] 4.1 Wizard numbers step (coffee): `fTempDeltaC` loads `r.tempOffsetC` verbatim and saves back verbatim (delete the open-time subtraction and save-time addition); "Reset to profile" still zeroes the offset
- [x] 4.2 Wizard tea flow: `fTeaTempC` loads `profileTemp + offset` (offset 0 → profile temp) and saves `entered − profileTemp` (equal → 0); verify the bag `brewTempC` seeding path still produces the right offset; no change to activation
- [x] 4.3 Promote-from-shot: wizard path and `recipepromotion.cpp` convert the shot's absolute override to an offset against the shot's profile (title → embedded shot profile JSON fallback → 0); update `tests/tst_recipepromotion.cpp`

## 5. External surfaces & docs

- [x] 5.1 `mcptools_recipes.cpp`: expose/accept only `tempOffsetC` (read: present when non-zero); update MCP register-stub externs in tests if signatures move; build `--target all`
- [x] 5.2 `shotserver_recipes.cpp` + web recipe editor payload/form: rename to `tempOffsetC`
- [x] 5.3 Docs: `RECIPES.md` (offset semantics + migration note), `MCP_SERVER.md` (field rename), `DATA_MIGRATION.md` (legacy column on import), agent file if it names the field

## 6. Verification

- [x] 6.1 Build all targets + run unit tests (tst_recipestorage, tst_recipepromotion, tst_temperaturedisplay, MCP suites)
- [ ] 6.2 Drive the running app: cards keep their own temps across activations; a recipe with −3° reads "84 · 94°C −3°" on its card (tag-only tint) and "81 · 91°C" untagged on the live Shot Plan; wizard shows the stored offset unchanged after a profile temp edit; Update Recipe → offset stored; MCP recipe_get shows `tempOffsetC`
