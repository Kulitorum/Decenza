# Tasks — Add Basket Equipment

## 1. Storage model (equipment-package-model)
- [x] 1.1 Extend create/update in `equipmentstorage.{h,cpp}` to write an optional `kind="basket"` item (`brand`, `model`, empty `attrs`)
- [x] 1.2 Widen `findPackageByGrinderIdentityStatic` (and rename/generalize) + `supersedeOrEditGrinderStatic` dedup/fork key to include the basket identity; treat "no basket" as a distinct value
- [x] 1.3 Resolve the basket item in `EquipmentPackageView` / `toVariantMap()`; derive specs via `BasketAliases::findEntry()` at read time (do not store specs)
- [x] 1.4 Carry basket items through `importEquipmentStatic` (device transfer)
- [x] 1.5 Add `SettingsDye` bridges: `knownBasketBrands()`, `knownBasketModels(brand)`, active-basket resolution + display properties; thread basket identity through switch/create/update
- [x] 1.6 Unit tests: optional basket, identity widening (fork/dedup/edit-in-place), derive-at-read — `basketOptionalAndDerive` + `basketIdentityWidening` in `tst_equipment.cpp` (23 pass). Import path is the same kind-agnostic copy loop + widened dedup, exercised indirectly.

## 2. Registry helper (basketaliases.h)
- [x] 2.1 Add a differentiator/`summary()`-style helper producing the model-row subtitle string (lead with the within-brand differentiating axis where feasible) — used existing `BasketAliases::summary()` as the baseline (design-endorsed); exposed via `SettingsDye::basketModelSummary()`. Lead-ordering polish deferred.

## 3. Picker UI (switch-equipment-dialog)
- [x] 3.1 Extend `SuggestionField` with an optional `descriptions` (value→subtitle) map rendered as a second row + folded into accessibility (lower-risk than a full `{value,description}` refactor; string filter/select logic untouched)
- [x] 3.2 Add the optional vendor-first two-level basket section to `SwitchEquipmentDialog` (brand typeahead, model rows with differentiator subtitle; clearing brand+model = no basket)
- [ ] 3.3 (Optional) group a brand's models by sub-family (Decent: Ridgeless/Ridged/Waisted) — deferred (design-marked optional)
- [x] 3.4 Translation keys + accessibility attributes for the basket fields (inline `TranslationManager.translate` with fallbacks; accessibleName set)

## 4. Inventory view (equipment-inventory-view)
- [x] 4.1 `EquipmentCard`: basket sub-line (omit when none), grinder stays the title
- [x] 4.2 `EquipmentInfoDialog`: basket `InfoRow` + details row (omit when none/custom)

## 5. Shot resolution + AI/MCP (dialing-context-payload, mcp-server)
- [x] 5.1 Resolve basket identity via the `equipment_id` JOIN — `ShotRecord` + `ShotProjection` gain basketBrand/Model; `loadShotRecordStatic` LEFT JOINs `equipment_items` kind='basket'; `convertShotRecord` copies them (no new shot column)
- [x] 5.2 `dialing_blocks`: `currentBean` gains the `basket` sub-object (brand/model/wallProfile/relativeFlow/precision/doseRangeG), specs derived from `BasketAliases`; omitted when absent, identity-only for custom
- [x] 5.3 Expose `doseRangeG` ({min,max}) for the dose-outside-range sanity signal
- [x] 5.4 MCP `equipment_*` tools: `equipment_list`/`equipment_update`/`equipment_select` read + accept the optional basket identity; dialing surfaces the basket sub-object per unit/string conventions
- [x] 5.5 `relativeFlow`/`wallProfile` emit as human-readable strings (from `BasketAliases::flowRateName`/`wallProfileName`)

## 6. Verification
- [x] 6.1 Quick compile check via Qt Creator MCP — app + all test targets build clean (0 errors, 0 warnings); equipment/coffeebags/dialing_blocks/mcptools_write/shotsummarizer/dbmigration suites all pass
- [ ] 6.2 Manual: create grinder+basket package, switch basket (fork), switch back (dedup), grinder-only package; confirm card/info display — needs the running app (covered logically by unit tests)
- [ ] 6.3 Confirm dialing context JSON carries the basket sub-object (and omits it when no basket / custom) — needs the running app + a shot with a basketed package

## 7. Housekeeping
- [x] 7.1 Archive the stale `add-basket-settings` proposal (dose-centric; superseded) — moved to `archive/2026-06-19-add-basket-settings`, marked OBSOLETE
