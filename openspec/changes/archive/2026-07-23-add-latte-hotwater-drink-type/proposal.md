# Add "Latte + Water" Drink Type to the Recipe Wizard

## Why

The recipe model already lets a single recipe carry **both** a milk (steam)
block and a hot-water block — the two blocks are explicitly independent
(`recipe-model`). But the drink-type vocabulary has no name for that
combination: `Recipe::deriveDrinkType` resolves "milk + water" to plain `latte`
because milk wins over added water. So a user who wants a latte with a shot of
added hot water (a milk drink plus an Americano-style water pour) can build it
on the summary via Add-milk + Add-water, but the wizard offers no first-class
drink type for it, the card/pill shows it as an ordinary latte, and the auto
name, template pre-seed, MCP, and web surfaces all treat it as a latte.

This change gives that combination its own drink type — **"Latte + Water"** — so
it is pickable in one tap, pre-seeds both blocks, and reads as its own drink
everywhere a drink type is shown.

## What Changes

- **New drink type `latte_hotwater`** across the shared vocabulary: the wizard
  picker (long label **"Latte + Hot Water"**), cards/pills/auto-names (short
  label **"Latte + Water"**), MCP `drinkType` enum, and the web recipe form.
- **`deriveDrinkType` (C++ and its QML mirror)** SHALL return `latte_hotwater`
  when a recipe carries BOTH a milk block (`hasMilk`) and a hot-water block
  (`hasWater`) with a profile present — superseding the current "milk wins"
  collapse to `latte`. Milk-only stays `latte`; water-only stays
  americano/long_black; profile-less + water stays `tea_hotwater`.
- **Wizard template** for `latte_hotwater` pre-seeds the steam block (milk) AND
  the hot-water block (order `before` — the water goes in before the espresso,
  fixed with no order choice offered), coffee bag kind, espresso profile filter,
  grind on. The details walk includes both the steam and water windows.
- **Presentation**: `DrinkType.shortLabel/longLabel/icon(s)`, the card, the
  pill, the auto-name short label, and the `hasGrind`/grind behavior (coffee —
  it grinds) all recognize the new type. Cards/pills use the milk/steam glyph
  with the disambiguating short label (as americano/long black share the water
  glyph); the wizard's drink-type picker shows BOTH the steam and water glyphs
  via a new `DrinkType.icons()` list helper, so the combination reads at a
  glance.
- **Manual**: document the new drink type in the recipe wizard section of the
  GitHub wiki manual.

Machine behavior is unchanged — activation still reads the blocks, never the
drink type. This is purely a first-class name + template for an already-valid
block combination.

## Capabilities

### Modified Capabilities

- `recipe-model` — the `drink_type` vocabulary and derivation gain
  `latte_hotwater` (milk + water + profile).
- `recipe-wizard` — the drink-type set and its template gain "Latte + Water".

## Impact

- **C++:** `Recipe::deriveDrinkType` (`recipestorage.cpp`), the valid-type list
  (`recipestorage.h` `kValidDrinkTypes`), the MCP enum + error strings
  (`mcptools_recipes.cpp`), the web form option + label map
  (`shotserver_recipes.cpp`).
- **QML:** `DrinkType.qml` (labels/icon/`fromRecipeMap`), `RecipeWizardPage.qml`
  (template, picker model, `deriveDrinkType` mirror, summary block visibility).
- **Manual:** wiki Recipe Wizard drink-type list.
- **No migration.** No new column, no data rewrite — legacy rows still derive on
  next save, and only a recipe that genuinely has both blocks changes its
  derived label.

## Open Questions

None. Label wording confirmed with the maintainer ("Latte + Water" / picker
"Latte + Hot Water").
