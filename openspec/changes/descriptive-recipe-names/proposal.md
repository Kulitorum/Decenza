## Why

The wizard's auto-suggested recipe name is built from only the bean and drink-type short label — `"Yirgacheffe Latte"`. Users who make one bean many ways (the explicit pain in [#1548](https://github.com/Kulitorum/Decenza/issues/1548): fredphoesh runs Ethiopian + S-American beans across DFlow + Cremina profiles) get recipes with identical names that can't be told apart at a glance and — crucially — that the newly-shipped recipe search/sort cannot disambiguate. That user's own mental handle for a recipe is *"Crem Yir"* (Cremina + Yirgacheffe), i.e. **profile + bean** — but the profile is exactly what the current name omits, so his search finds nothing. The profile is both the axis users vary most and the token they already type when hunting; putting it in the default name makes recipes distinguishable and makes the existing search actually work.

## What Changes

- The wizard's auto-suggested recipe name gains the **profile**, applied from the first recipe (not only on collision): `bean + drink-type short label + profile`, e.g. `"Yirgacheffe Latte · Cremina"`.
- The profile token is **cleaned** before use: the `D-Flow/` / `A-Flow/` editor-membership title prefix is stripped, and a profile whose trailing word repeats the drink type is de-stuttered (mirroring the existing bean stutter guard) so the name never reads `"… Espresso · Blooming Espresso"`.
- **Name collisions**: when the composed name matches an existing non-archived recipe's display name, the wizard appends a qualifier from the draft's own dial-in values — the yield (ratio/target), else the dose — trying yield first and falling back to dose, so the suggestion stays distinct. Never a numeric counter.
- The existing guards are preserved: the suggestion is applied only while the name field is empty or still holds the previous suggestion (never over a user edit), and the type word is dropped when the bean already ends with it.
- **All idle pill rows fit the longer names (Recipes, Beans, Profiles, Equipment, Flush, Hot water)**: the idle pill rows windowed the MRU list into fixed pages of five (recipes/beans), capped at five (equipment), or wrapped unbounded (profiles/flush/hot water), which with the longer descriptive names can overflow into three or more rows. All change to a **live fit** — as many pills as comfortably fit within **at most two rows** at the row's real available width, computed from measured pill widths, so the per-page count varies with name length and from page to page. This applies to **both rendering paths** of each widget: the compact-bar popup (`RecipesItem`/`BeansItem`/`EspressoItem`/`EquipmentItem`/`FlushItem`/`HotWaterItem`) and the `IdlePage` center-zone expansion. `PresetPillRow`'s width-flow, arrow gutter, and pagination arrows are reused unchanged; a shared `PillFit.packPageSizes()` helper computes the per-page counts, and the fixed windowing is replaced. (Steam pills are the one idle row left unchanged — not requested.)
- Manual (wiki) documentation of the auto-name behaviour is updated to match.

Out of scope (belongs to the broader #1548 findability work): swipe-for-more and the search/sort UI itself — untouched here. (The fixed 5-pill count is in scope now: the longer names force the idle rows to become fit-aware.)

## Capabilities

### New Capabilities
- (none)

### Modified Capabilities
- `recipe-wizard`: the "Name auto-suggestion from bean and drink type" requirement changes to include the cleaned profile token and the collision qualifier.
- `recipe-quick-switch`: the Recipes idle pill row changes from fixed pages of five to a live fit within at most two rows (both rendering paths).
- `bag-inventory-view`: the Beans idle pill row makes the same fit-based change, for consistency (both rendering paths).
- `idle-default-layout`: adds the favorite-profile, equipment, flush, and hot-water idle pills to the same two-row live fit (previously unpaginated / fixed-cap; both rendering paths).

## Impact

- **Code**:
  - `qml/pages/RecipeWizardPage.qml` — `suggestName()` and its call sites; a local `cleanProfileForName()` helper; collision detection against the set of existing non-archived recipe names cached from `MainController.recipeStorage`.
  - `qml/components/layout/items/RecipesItem.qml`, `BeansItem.qml`, `EspressoItem.qml` (compact popups) and `qml/pages/IdlePage.qml` (center-zone expansion) — replace `pillPageSize: 5` slicing / unbounded wrap with a live two-row fit computed from measured pill widths against each row's real available width. `PresetPillRow.qml` is unchanged (its width-flow and pagination arrows are reused); a shared `qml/components/layout/PillFit.js` helper covers all rows.
- **Behaviour**: the auto-suggested string changes, and the idle pill rows show a name-length-dependent (and page-dependent) number of pills capped at two rows. No storage, migration, activation, MCP, or web changes — names remain free-form and uniqueness is still not enforced.
- **Docs**: wiki Manual entry for recipe naming; `docs/CLAUDE_MD/RECIPES.md` `suggestName()` note.
- **Tests**: QML is tested manually in this project (no QML test harness); the naming logic and pill fit stay in QML and are verified by the manual walk, not an automated test.
