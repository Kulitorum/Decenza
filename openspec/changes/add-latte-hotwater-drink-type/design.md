# Design — Latte + Water drink type

## Context

`drink_type` is a presentation-only tag on a recipe (see `recipe-model`,
`RECIPES.md`). The recipe already supports independent milk and hot-water
blocks, so no schema change is needed — this is a new *label* + *template* for
the milk-plus-water combination, plus a change to the one place derivation
collapses that combination.

## Key decisions

### Type id: `latte_hotwater`
Follows the established `tea_hotwater` naming (base drink + `_hotwater`
suffix for the added-water variant). No new column, no migration — `drink_type`
is free-form TEXT and derivation backfills legacy/empty rows on next save.

### Derivation precedence: milk + water → `latte_hotwater`
Today `deriveDrinkType` returns `latte` as soon as `hasMilk` is true, before it
ever inspects the water block ("milk wins over added water"). The new rule
inserts a check *before* the milk-only branch: if BOTH `hasMilk` and `hasWater`
(and a profile is present, so it isn't a profile-less tea), return
`latte_hotwater`. Order matters — the profile-less+water → `tea_hotwater` and
`tea_portafilter` → `tea` checks still run first, so a tea recipe never becomes
`latte_hotwater`. This is the only behavioral change; it affects derived labels
only, never machine behavior, and only a recipe that genuinely carries both
blocks (none exist today from the latte template, whose water flag is false).

Both the C++ `Recipe::deriveDrinkType` and its QML mirrors
(`RecipeWizardPage.deriveDrinkType`, `DrinkType.fromRecipeMap`) apply the same
precedence.

### Icon: milk/steam on cards, both glyphs in the picker
`DrinkType.icon()` returns one SVG for the compact surfaces (cards, pills,
summary hero): `latte_hotwater` reuses `steam.svg` there, exactly as americano
and long black share `water.svg` and rely on their short text label to
disambiguate. The short label "Latte + Water" carries the distinction. The
wizard's drink-type picker has room for more, so a new `DrinkType.icons()`
returns a LIST — `[steam.svg, water.svg]` for `latte_hotwater`, a single-element
list for every other type — and the picker tile renders it as a row of glyphs so
the combination reads at a glance.

### Template: pre-seed both blocks, order fixed to "before"
The wizard template for `latte_hotwater` sets `milk: true`, `water: true`,
`waterOrder: "before"`, `bagKind: "coffee"`, espresso beverage filter, `grind:
true`, `isTea: false` — the union of the latte and long-black templates. The
water goes in BEFORE the espresso and the wizard does NOT offer the before/after
order choice for this type (the water window hides the order combobox when
`fDrinkType === "latte_hotwater"`). The details walk already shows a window per
active block, so both the steam and water windows appear; the summary's
add/remove affordances keep every combination reachable regardless.

### Grind & other behaviors are automatic
`DrinkTypes::hasGrind` keys off the `tea` prefix, so `latte_hotwater` grinds
like any coffee drink with no extra code. Heater hold keys off `hasMilk` in the
steam block (not the drink type), so an active Latte + Water recipe holds the
steam heater exactly like a latte, and its hot-water block takes no heater hold —
both already correct via the blocks.

## Risks

- **Re-derivation of an existing latte that has both blocks.** In practice none
  exist (the latte template never set water), and re-derivation only fires when
  a block/profile field changes without an explicit `drinkType`. An explicit
  stored `latte` is preserved. Acceptable and correct — a latte the user gave
  water to is a Latte + Water.
- **Surfaces that switch on the type string.** The web recipe form's `isTeaType`
  helper and label map, and the MCP enum, must all learn the new value or they
  silently mislabel it. Covered by tasks below.
