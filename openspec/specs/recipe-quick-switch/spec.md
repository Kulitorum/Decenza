# recipe-quick-switch Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipes idle widget mirrors the Beans button
The system SHALL provide a Recipes layout widget structurally mirroring the Beans widget: tap toggles a pill row of the five most-recently-used non-archived recipes (MRU-ordered, no favorite flag), tapping a pill activates that recipe, and double-tap or long-press opens the Recipes management page. The active recipe's pill SHALL be highlighted. When no recipes exist, a plain tap SHALL go directly to the Recipes management page. The widget SHALL meet the accessibility rules that the Beans widget follows (focus trap in the pill popup, announcements, `AccessibleTapHandler`).

#### Scenario: Quick switch
- **WHEN** the user taps the Recipes widget and selects a pill
- **THEN** that recipe activates (full bundle incl. steam) and the pill row closes with the selection highlighted on next open

#### Scenario: Empty state
- **WHEN** the user taps the widget with zero recipes
- **THEN** the Recipes management page opens directly

#### Scenario: MRU ordering
- **WHEN** a recipe is activated
- **THEN** it moves to the front of the pill list, and only the five most recent appear

### Requirement: Bean button coherence
Activating a recipe SHALL set the active bag (the recipe's linked bag), so the Beans widget's pill selection reflects the recipe's bag without additional wiring. Deactivation by ingredient swap SHALL deselect the recipe pill while leaving bag selection as the user set it.

#### Scenario: Bag pill follows recipe
- **WHEN** a recipe linked to bag X is activated
- **THEN** the Beans widget shows bag X as selected

### Requirement: Management page
The Recipes management page SHALL list all non-archived recipes with create, edit, clone, and archive/delete actions (delete only for recipes with no shots), and provide access to archived recipes for reference. Each recipe card SHALL present, in order: the recipe name as the top-line anchor (with the Active badge); a drink line with the drink-type icon, its short label, the profile title (always shown, including for same-bean twins), and the milk weight when stored (the bare word "milk" SHALL NOT appear without the weight); a bean line with the bag name and shot count; and the shot-plan line. Card text lines SHALL wrap rather than elide so the profile is never truncated away — added card height is acceptable. Profile-less hot-water tea cards SHALL show "Tea · Hot water" on the drink line and the vessel snapshot (amount and temperature) in place of the shot-plan line. When zero recipes exist the page SHALL show two large starter tiles — one opening shot history (promote a good shot) and one opening the wizard — in place of a text-only hint.

The shot-plan line SHALL be computed entirely from the card's own recipe, presenting **source + delta** — where the values come from and how this recipe modifies them. The temperature segment SHALL render **that recipe's profile's** frame temperatures (resolved by the recipe's profile title, embedded JSON fallback) — never the currently loaded profile's frames — unshifted and in the default text color, followed by the stored `tempOffsetC` as a signed tag (e.g. "84 · 94°C −3°") when the offset is non-zero. The yield SHALL render as "profile target → recipe yield" (e.g. "36.0 → 40.0g") when the recipe's yield differs from its profile's target, else as the plain value. The override-highlight tint SHALL wrap only the delta markers — the offset tag and the yield arrow expression — never the profile's base temperatures, so the highlight uniformly means "this recipe modifies its profile". The offset tag SHALL follow the user's temperature unit (a °C delta converts to °F as ×9/5 with no +32, as the wizard's stepper already does). When the recipe's profile resolves by neither title nor embedded JSON, the card SHALL omit the temperature segment entirely — it SHALL NOT fall back to the currently loaded profile's frames. The wizard's summary preview card SHALL render by the same rule. Activating, loading, or editing a *different* profile or recipe SHALL NOT change what any other recipe's card displays.

#### Scenario: Archive from management page
- **WHEN** the user archives a used recipe
- **THEN** it disappears from the list default view and the MRU pills, and remains visible in shot history provenance

#### Scenario: Same-bean twins are distinguishable
- **WHEN** two recipes share a bean but differ by profile
- **THEN** each card shows its profile on the drink line without truncation

#### Scenario: Latte card carries its milk
- **WHEN** a latte recipe stores 200g of milk
- **THEN** its card's drink line includes "200g milk" and no bare "milk" token

#### Scenario: Hot-water tea card degrades deliberately
- **WHEN** a profile-less hot-water tea recipe is listed
- **THEN** its card shows "Tea · Hot water" and the vessel's amount and temperature instead of an empty shot plan

#### Scenario: First-run empty state teaches both paths
- **WHEN** the management page opens with zero recipes
- **THEN** the user sees a "start from a good shot" tile opening shot history and a "build from scratch" tile opening the wizard

#### Scenario: Cards are immune to the loaded profile
- **WHEN** the recipes list is open and a recipe on profile A (frames 84 · 94°C) is displayed while the machine currently holds profile B (frames 90 · 88°C)
- **THEN** that recipe's card shows 84 · 94°C, and activating a different recipe changes no other card's temperatures

#### Scenario: A card shows its temperature as source + delta
- **WHEN** a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C is listed
- **THEN** its card's temperature reads "84 · 94°C −3°" with the base temps in the default text color and only the "−3°" tag in the highlight color
- **AND** the live Shot Plan for that recipe (when active) reads "81 · 91°C" untagged — the card shows the relationship, the live widget shows the result

#### Scenario: A card shows its yield as a difference from the profile
- **WHEN** a recipe stores yield 40 on a profile whose target weight is 36
- **THEN** its card's yield reads "36.0 → 40.0g" with the arrow expression in the highlight color

#### Scenario: An unmodified value carries no highlight
- **WHEN** a recipe stores offset 0 and a yield equal to its profile's target
- **THEN** its card shows the profile's temps and yield in the default text color with no tag and no arrow

### Requirement: Recipe pills show a drink-type icon
Recipe pills in the idle widget and recipe lists SHALL show a small icon for the recipe's drink type (stored value, derived from blocks when absent), rendered as an SVG image (never a Unicode glyph per QML conventions). Wherever the drink type appears as text (cards, wizard summary, auto-names), surfaces SHALL use short labels — "Latte", "Tea", "Americano", "Long black" — reserving the long picker labels ("Latte / Cappuccino", "Tea (hot water)") for the wizard's drink-type step.

#### Scenario: Mixed pill row is scannable
- **WHEN** the idle widget shows an espresso, a latte, and a tea recipe
- **THEN** each pill carries its distinct drink-type icon

#### Scenario: Americano and long black stay distinct
- **WHEN** an americano recipe and a long-black recipe appear on cards or the summary
- **THEN** each pairs the shared water icon with its own short text label

### Requirement: Stale recipes are visibly indicated
Surfaces listing recipes SHALL indicate a stale recipe (linked bag finished): the management card SHALL show a "bag finished" state with the one-tap re-point affordance (see `recipe-bag-lifecycle`), and the idle pill SHALL be dimmed or badged. Indication SHALL NOT block activation.

#### Scenario: Stale pill still works
- **WHEN** the idle pill row contains a stale recipe
- **THEN** the pill is visually distinct, and tapping it still activates the recipe

