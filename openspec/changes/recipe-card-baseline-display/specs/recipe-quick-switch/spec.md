## MODIFIED Requirements

### Requirement: Management page
The Recipes management page SHALL list all non-archived recipes with create, edit, clone, and archive/delete actions (delete only for recipes with no shots), and provide access to archived recipes for reference. Each recipe card SHALL present, in order: the recipe name as the top-line anchor (with the Active badge); a drink line with the drink-type icon, its short label, the profile title (always shown, including for same-bean twins), and the milk weight when stored (the bare word "milk" SHALL NOT appear without the weight); a bean line with the bag name and shot count; and the shot-plan line. Card text lines SHALL wrap rather than elide so the profile is never truncated away — added card height is acceptable. Profile-less hot-water tea cards SHALL show "Tea · Hot water" on the drink line and the vessel snapshot (amount and temperature) in place of the shot-plan line. When zero recipes exist the page SHALL show two large starter tiles — one opening shot history (promote a good shot) and one opening the wizard — in place of a text-only hint.

The shot-plan line SHALL render the recipe's own **resulting** temperature and yield as a plain baseline — the same presentation the live idle Shot Plan widget uses once the recipe is active — with no delta tag and no arrow. The temperature segment SHALL be resolved from **that recipe's profile's** frame temperatures (resolved by the recipe's profile title, embedded JSON fallback) — never the currently loaded profile's frames — shifted by the recipe's stored `tempOffsetC` and rendered as the resulting value only (e.g. a profile of 84 · 94°C with `tempOffsetC` = −3 renders "81 · 91°C"), in the default text color with no separate offset tag. The yield SHALL render as the plain resulting value (the recipe's stored yield when set, else the profile's target) in the default text color, with no "profile → recipe" arrow. When the recipe's profile resolves by neither title nor embedded JSON, the card SHALL omit the temperature segment entirely — it SHALL NOT fall back to the currently loaded profile's frames. The wizard's summary preview card SHALL render by the same rule. Activating, loading, or editing a *different* profile or recipe SHALL NOT change what any other recipe's card displays.

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
- **THEN** that recipe's card shows values resolved from profile A's own frames, and activating a different recipe changes no other card's temperatures

#### Scenario: A card shows its temperature as a resulting baseline
- **WHEN** a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C is listed
- **THEN** its card's temperature reads "81 · 91°C" in the default text color, with no separate offset tag
- **AND** the live Shot Plan for that recipe (when active) reads the same "81 · 91°C" — the card and the live widget agree

#### Scenario: A card shows its yield as the plain resulting value
- **WHEN** a recipe stores yield 40 on a profile whose target weight is 36
- **THEN** its card's yield reads "40.0g" in the default text color, with no arrow and no highlight

#### Scenario: An unmodified value carries no highlight
- **WHEN** a recipe stores offset 0 and a yield equal to its profile's target
- **THEN** its card shows the profile's temps and yield in the default text color with no tag and no arrow
