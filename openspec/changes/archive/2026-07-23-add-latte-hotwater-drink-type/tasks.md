# Tasks — Latte + Water drink type

## 1. Derivation (C++ + QML mirrors)

- [x] 1.1 `Recipe::deriveDrinkType` (`src/history/recipestorage.cpp`): before the milk-only branch, return `latte_hotwater` when BOTH milk and water are present (profile present). Keep tea/tea_hotwater/tea_portafilter checks ahead of it.
- [x] 1.2 `kValidDrinkTypes` (`src/history/recipestorage.h`): add `latte_hotwater`; update the doc comment listing the vocabulary.
- [x] 1.3 QML mirror `RecipeWizardPage.deriveDrinkType()`: same precedence (milk && water → `latte_hotwater`).
- [x] 1.4 `DrinkType.fromRecipeMap()` (`qml/components/DrinkType.qml`): milk && water → `latte_hotwater`.

## 2. Presentation (DrinkType singleton)

- [x] 2.1 `DrinkType.shortLabel()`: `latte_hotwater` → "Latte + Water" (new translation key `recipes.type.short.latteHotWater`).
- [x] 2.2 `DrinkType.longLabel()`: `latte_hotwater` → "Latte + Hot Water" (new key `recipes.wizard.type.latteHotWater`).
- [x] 2.3 `DrinkType.icon()`: `latte_hotwater` → `qrc:/icons/steam.svg` (cards/pills, reuse milk glyph).
- [x] 2.4 `DrinkType.icons()`: new list helper returning `[steam.svg, water.svg]` for `latte_hotwater`, `[icon(t)]` otherwise — for the wizard picker.

## 3. Wizard (`qml/pages/RecipeWizardPage.qml`)

- [x] 3.1 Add `latte_hotwater` to the `templates` map: `beverages: ["espresso",""]`, `bagKind: "coffee"`, `milk: true`, `water: true`, `waterOrder: "before"`, `grind: true`, `isTea: false`.
- [x] 3.2 Add `latte_hotwater` to the drink-type picker model (after `latte`).
- [x] 3.3 Summary shows both the milk and hot-water block rows for the new type (they key off `fHasMilk`/`fHasWater` — automatic; confirmed).
- [x] 3.4 Picker tile renders a row of glyphs via `drinkTypeIcons()`, so Latte + Water shows both steam and water icons.
- [x] 3.5 Hide the before/after water-order picker for `latte_hotwater` — the order is fixed to "before", not asked.

## 4. MCP (`src/mcp/mcptools_recipes.cpp`)

- [x] 4.1 Add `latte_hotwater` to both `drinkType` enum arrays (create + update).
- [x] 4.2 Add `latte_hotwater` to the two invalid-drinkType error strings.

## 5. Web (`src/network/shotserver_recipes.cpp`)

- [x] 5.1 Add `<option value="latte_hotwater">Latte + Water</option>` to the drink-type select.
- [x] 5.2 Add `latte_hotwater: 'Latte + Water'` to the JS label map.
- [x] 5.3 `isTeaType` unchanged (new type is not tea); the form's independent milk/water checkboxes already express milk + water.

## 6. Manual

- [x] 6.1 Update the GitHub wiki Recipe Wizard / drink-types section to list "Latte + Water" (latte plus added hot water, before the espresso). Edited `Decenza.wiki/Manual.md` (recipe-anatomy list, drink-type step, Milk and Hot Water section) — commit/push pending user OK.

## 7. Verify

- [x] 7.1 Extended `deriveDrinkTypeMatrix` (milk+water → `latte_hotwater`, both orders; milk-only → `latte`; tea profile still wins), `isKnownDrinkTypeVocabulary`, and added `updateRederivesLatteToLatteHotWater` (`tests/tst_recipestorage.cpp`).
- [x] 7.2 Full test suite green via `mcp__qtcreator__run_tests` (scope all) — 93 passed.
- [x] 7.3 qmllint clean on the touched QML (`-I` build dir) — no errors, only pre-existing TranslationManager/MainController unqualified-access warnings. App-launch confirmation of the picker/icons/round-trip left to the user.
