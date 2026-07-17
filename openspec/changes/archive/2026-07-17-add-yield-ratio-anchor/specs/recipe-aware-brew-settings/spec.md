# recipe-aware-brew-settings Delta

## MODIFIED Requirements

### Requirement: "Update Profile" becomes "Update Recipe" in recipe mode

Brew Settings SHALL carry **two** persist actions: one for Temp Delta, and **one** for the yield/ratio pair.

**Temp Delta** keeps its existing button. When a recipe is active it SHALL be labeled "Update Recipe" and persist the shown value into the active recipe's `tempOffsetC` as the delta between the dialed temperature and the profile's espresso_temperature, via `MainController.recipeStorage.requestUpdateRecipe(...)`, and SHALL NOT modify the profile. When no recipe is active it SHALL remain "Update Profile" with its existing behavior unchanged (a profile can hold a temperature).

**Yield/ratio** SHALL have a **single** persist button, not one per row. It SHALL sit on whichever of the Ratio / Stop-at rows is currently anchored (`yield-anchor`: the last written of the two), and SHALL move to the other row when the user edits that row. Its location is therefore the anchor indicator — no separate mode chip, toggle, or setting is required, and the override-highlight color channel stays free for its existing meaning.

The button's **destination follows the resolution ladder**, and its label states it:

| State | Label | Writes |
|---|---|---|
| Recipe active | "Update Recipe" | the active recipe's yield spec |
| No recipe, bag active | "Update Bag" | the active bag's yield spec |
| No recipe and no bag | *(hidden)* | — (nothing to persist; the session anchor still applies to the brew) |

A profile SHALL never be a destination for the yield button: `target_weight` is absolute and profiles are shared and exported, so a ratio has nowhere to live there. Setting a profile's default target weight remains available in the Profile Editor and Simple Profile Editor.

When the anchor's mode is `none` — no recipe/bag yield designed and the user has not yet edited either row — **neither row SHALL show a button**. The first edit anchors that row and the button appears on it.

The persist button SHALL be the **sole** way a yield/ratio change reaches a recipe or bag: yield and ratio are per-brew overrides (see the overrides requirement below), so committing the dialog with OK SHALL NOT write them to either.

Both buttons' enabled state SHALL gate on the shown value differing from **the active store's own stored value**, NOT on differing from the profile default:

- **Temp Delta** → the shown dialed-minus-profile delta vs the stored `tempOffsetC` (unchanged by this change).
- **Yield/ratio** → the shown anchor vs the stored spec, comparing like with like: a ratio anchor against a stored ratio, an absolute against a stored absolute. **A mode change alone SHALL enable it**, since persisting it genuinely changes behaviour on the next dose change even when the gram value is identical.

This makes the stored baseline movable to any value, including back to the profile default: resetting a dial to the profile default while the store holds a different value SHALL leave the button enabled so the user can persist the reset (for temperature, persisting the reset stores offset 0). When no recipe is active, the Temp Delta "Update Profile" button MAY continue to gate on the value differing from the profile default. The action persists immediately (like "Update Profile" does today) and is independent of OK.

#### Scenario: The button sits on the anchored row

- **WHEN** a recipe holding `{2.0, ratio}` is active and Brew Settings opens
- **THEN** the "Update Recipe" button sits beside the Ratio row
- **AND** the Stop-at row shows the derived gram target with no button

#### Scenario: Editing the other row moves the button

- **WHEN** that same dialog is open and the user edits the Stop-at value
- **THEN** the anchor becomes `absolute`, the button moves to the Stop-at row
- **AND** tapping it writes `{<shown grams>, absolute}` to the recipe, which is no longer ratio-anchored

#### Scenario: Ratio persists to the recipe, not the profile

- **WHEN** a recipe is active, the user dials the Ratio to 1:2.5 and taps "Update Recipe"
- **THEN** `{2.5, ratio}` is written to the active recipe via `requestUpdateRecipe`
- **AND** the profile's `target_weight` is not modified and the profile is not re-saved

#### Scenario: Stop-at persists to the recipe, not the profile

- **WHEN** a recipe is active and the user changes Stop-at and taps "Update Recipe"
- **THEN** `{<value>, absolute}` is written to the active recipe via `requestUpdateRecipe`
- **AND** the profile's `target_weight` is not modified and the profile is not re-saved

#### Scenario: Temp Delta persists to the recipe as an offset, not to the profile

- **WHEN** a recipe is active on a 90° profile, the user dials the temperature to 87 and taps the (now "Update Recipe") button
- **THEN** −3 is written to the active recipe's `tempOffsetC` via `requestUpdateRecipe`
- **AND** `applyTemperatureToProfile` is not called and the profile is not modified

#### Scenario: No recipe active — the yield button targets the bag

- **WHEN** no recipe is active, a bag is active, and the user dials a ratio of 1:3
- **THEN** the button beside the Ratio row reads "Update Bag"
- **AND** tapping it writes `{3.0, ratio}` to the active bag; no profile is modified

#### Scenario: No recipe and no bag — no yield button

- **WHEN** neither a recipe nor a bag is active
- **THEN** no persist button is shown beside either the Ratio or the Stop-at row
- **AND** the dialed anchor still applies to the next brew as a session override

#### Scenario: No recipe active — Temp Delta still updates the profile

- **WHEN** no recipe is active
- **THEN** the Temp Delta button reads "Update Profile" and bakes the value into the profile exactly as before this change

#### Scenario: Resetting the stored baseline to the profile default is persistable

- **WHEN** a recipe holding `{36.0, absolute}` is active, the user sets Stop-at to the profile target weight (e.g. 42), and the recipe's stored value (36) still differs from the shown value
- **THEN** the button is enabled
- **AND** tapping it writes `{42.0, absolute}` so the recipe no longer deviates from the profile

#### Scenario: Update disabled once the store already holds the shown value

- **WHEN** a recipe is active and the shown anchor equals the recipe's stored spec in both value and mode (or the shown temperature delta equals the stored `tempOffsetC`)
- **THEN** that button is disabled (nothing to persist)

#### Scenario: A mode change alone enables the button

- **WHEN** a recipe holds `{2.0, ratio}` with an 18 g dose (deriving 36 g) and the user edits Stop-at to exactly 36 g
- **THEN** the anchor becomes `{36.0, absolute}` — the same gram target but a different mode
- **AND** the button is enabled, because persisting it genuinely changes the recipe's behaviour on the next dose change

### Requirement: Yield and temperature are per-brew overrides, never auto-written to the recipe

Yield (Stop-at), ratio, and temperature (Temp Delta) set in Brew Settings are per-brew **overrides**: editing them adjusts the next brew relative to the baseline and SHALL NOT modify that baseline. A yield/ratio/temp change SHALL apply only as an override in `Settings.brew` (persisted per-brew; cleared on recipe switch, and on a profile switch per the mode asymmetry in `brew-overrides`) and SHALL NOT be written into the active recipe **or the active bag**. The recipe's yield spec / `tempOffsetC`, and the bag's yield spec, SHALL change only via the explicit persist button.

To honor this, the existing auto-stamps SHALL be removed — the `MainController` write-through watchers on `SettingsBrew::brewOverridesChanged` (→ recipe `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ the recipe temperature), **and** the bag write-through `persistYieldOverrideToBag` called from `ProfileManager::activateBrewWithOverrides` (see `coffee-bag-model`). `RECIPES.md` SHALL be updated to drop yield/temp from the "tweaks stamp the active recipe" description.

On recipe activation, the recipe's stored values SHALL still be applied as the starting overrides — its yield spec verbatim (mode included), and the temperature as `profile espresso_temperature + tempOffsetC` (offset 0 arms no override; see `recipe-activation`) — so an activated recipe still opens with its saved yield/temperature.

#### Scenario: One-off yield tweak does not change the recipe
- **WHEN** a recipe holding `{36.0, absolute}` is active, the user sets Stop-at to 40 and taps OK
- **THEN** the brew uses `{40.0, absolute}` as a session anchor
- **AND** the active recipe still holds `{36.0, absolute}`
- **AND** re-activating the recipe restores 36

#### Scenario: One-off ratio tweak does not change the recipe
- **WHEN** a recipe holding `{2.0, ratio}` is active, the user sets the Ratio to 1:2.5 and taps OK
- **THEN** the brew uses `{2.5, ratio}` as a session anchor
- **AND** the active recipe still holds `{2.0, ratio}`

#### Scenario: One-off yield tweak does not change the bag
- **WHEN** no recipe is active, a bag holding `{40.0, absolute}` is active, the user sets Stop-at to 44 and taps OK
- **THEN** the brew uses `{44.0, absolute}` as a session anchor
- **AND** the active bag still holds `{40.0, absolute}`

#### Scenario: One-off temperature tweak does not change the recipe
- **WHEN** a recipe is active, the user changes Temp Delta and taps OK
- **THEN** the temperature applies as a `Settings.brew` override for the brew
- **AND** the active recipe's `tempOffsetC` is unchanged

#### Scenario: The persist button is the only path yield/ratio/temp reach a store
- **WHEN** the user wants the shown yield, ratio, or temperature to persist
- **THEN** they tap the persist button, which writes the recipe's or bag's spec (or `tempOffsetC`); no other Brew Settings action writes those fields

### Requirement: Dose and grind keep their existing dial write-through

Dose and grind/RPM are dial-in values, not overrides: they have no per-brew "override vs. baseline" split and no "Update" button. This change SHALL leave their existing write-through untouched — dose continues to write through to the active bag and stamp the active recipe's `doseG`; grind/RPM continue to write through to the active bag and stamp the recipe's `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`, with the non-tea guard). This change SHALL NOT add or remove any write-back for dose or grind.

This split is the measurement/intent line of `yield-anchor`: dose, grind, and RPM are things the user physically did, so they are remembered automatically; the yield anchor is design intent, so it is button-protected. A dose capture therefore always updates the dose and never changes the yield mode.

The re-seed performed on a recipe switch SHALL write only the dialog's local QML values (`root.*`), never `Settings`, so that re-seeding never triggers a dose/grind stamp into the newly activated recipe.

Cup tare is NOT recipe-stored (it lives in DYE only). **Ratio is now recipe- and bag-stored** as the mode of the yield spec (`yield-anchor`) — superseding the previous rule that ratio lived only in `Settings.brew`; `Settings.brew.lastUsedRatio` survives only as preset memory. Steam and hot-water blocks are recipe-stored but are not edited by this dialog.

#### Scenario: Editing dose in recipe mode still writes through
- **WHEN** a recipe is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`, writes through to the active bag, and stamps the active recipe's `doseG` — exactly as before this change

#### Scenario: Grind edit still mirrors to the bag and stamps the recipe
- **WHEN** a non-tea recipe is active and the user changes the grind setting (or RPM) and taps OK
- **THEN** the active recipe's `grindPinned` (`rpmPinned`) is stamped and the setter's unconditional bag write-through mirrors the value onto the linked bag — unchanged from `fix-recipe-grind-integrity`

#### Scenario: A dose capture while the dialog is open does not flip the anchor
- **WHEN** Brew Settings is open with an `{36.0, absolute}` anchor and a scale dose capture lands
- **THEN** the dialog's dose updates to the captured value
- **AND** the anchor stays `{36.0, absolute}`, the Stop-at value stays 36 g, and the persist button does not move

#### Scenario: Re-seed after a switch does not stamp the new recipe
- **WHEN** the user switches recipes in the dialog and the dial-in fields are re-seeded
- **THEN** only local `root.*` values are written
- **AND** no `Settings` mutation occurs from the re-seed, so the newly activated recipe is not stamped with re-seeded dose/grind values

### Requirement: Brew Settings values use a single override-highlight color scheme

Brew Settings SHALL color each editable numeric value by a single rule instead of the current mix of per-value-type semantic colors (weight amber-brown, temperature red, ratio blue) and manual-vs-calculated state: a value SHALL render in the default text color (`Theme.textColor`) when it holds its baseline, and in the override-highlight color (`Theme.highlightColor` — the same highlight the Shot Plan uses for an active override, and the amber of the Clear button) when it deviates from that baseline. The invariant SHALL be: **a value is highlighted if and only if the Clear action would change it.** The per-value-type colors (`weightColor`, `temperatureColor`, `primaryColor` on the value text) and the `targetManuallySet` blue/amber distinction SHALL be removed.

The baseline for each field is the value the Clear handler restores, resolved through the `yield-anchor` ladder — the active recipe's own stored value when a recipe is active, else the active bag's, else the profile default:

- **Temp Delta** → when a recipe is active, the recipe's offset-derived temperature (the profile's espresso_temperature + the recipe's `tempOffsetC`, i.e. the delta reads `0°` at the recipe's design temperature); when no recipe is active — or the active recipe carries offset 0 — the profile temperature (delta `0°`). Unchanged by this change; the bag holds no temperature.
- **Dose** → the dose Clear restores (the bean's remembered dose `Settings.dye.dyeBeanWeight`, else 18) — unchanged, not recipe-relative.
- **Stop-at (yield)** and **Ratio** → see the anchor table below. This supersedes the previous rule, in which Stop-at's baseline was the recipe's `yieldG` and Ratio's was always that baseline ÷ the dose.

Because a recipe's or bag's yield and temperature are its design, not deviations from it, a recipe's own anchor / offset-derived temperature SHALL render in the default color (no highlight) when the dial sits on it; only a per-brew deviation *from that stored value* SHALL be highlighted. The Dose cup field is NOT reset by Clear, so it SHALL always use the default text color. The "Profile: …" sub-indicators MAY use the same highlight color when their field is overridden for visual consistency.

The +/- stepper accent SHALL be unified to a single accent across all fields rather than per-value-type colors (recommended: the override-highlight color when the field deviates from its baseline, otherwise the app accent), so a field reads as a whole when it is holding a deviation.

For the yield/ratio pair the baseline SHALL be expressed in the **stored anchor's own unit**, with the other row's baseline derived from it through the current dose:

| Stored mode | Ratio row baseline | Stop-at row baseline |
|---|---|---|
| `absolute` (36 g) | `36 ÷ dose` (derived) | `36` (stored) |
| `ratio` (1:2) | `2.0` (stored) | `2.0 × dose` (derived) |

Because the derived row's baseline moves with the dose exactly as its value does, **neither row SHALL highlight merely because the dose changed** — in either mode.

The override tolerance for the two rows SHALL be expressed in a single unit and converted through the dose, so the rows can never disagree about whether the user has deviated. (Today the ratio row uses `> 0.05` and the Stop-at row `> 0.1 g`; at an 18 g dose a 0.05 ratio nudge is 0.9 g — under one threshold and nine times over the other.)

#### Scenario: Values at their baseline render in the default color
- **WHEN** no recipe is active, and Temp Delta is 0°, Dose equals the bean's remembered dose (or 18), Ratio equals the profile ratio, and Stop-at equals the profile target
- **THEN** all four values render in `Theme.textColor` (no highlight)

#### Scenario: A recipe's own yield and temperature are not highlighted
- **WHEN** a recipe holding `{36.0, absolute}` and a `tempOffsetC` of −3 is activated on a 42 g / 90° profile
- **THEN** the Stop-at row shows 36 g and the Temp Delta reads `0°`, both in the default color, not highlighted

#### Scenario: A ratio recipe's own ratio is not highlighted
- **WHEN** a recipe holding `{2.0, ratio}` is activated with an 18 g dose
- **THEN** the Ratio row shows 1:2 and the Stop-at row shows 36 g, both in the default color

#### Scenario: A dose change highlights neither row
- **WHEN** a `{2.0, ratio}` recipe is active and the dose moves from 18 g to 17.5 g
- **THEN** the Stop-at row shows 35 g in the default color and the Ratio row shows 1:2 in the default color

#### Scenario: A dose change under an absolute anchor highlights neither row
- **WHEN** a `{36.0, absolute}` recipe is active and the dose moves from 18 g to 17.5 g
- **THEN** the Stop-at row shows 36 g and the Ratio row shows 1:2.06, both in the default color

#### Scenario: A deviation from the recipe is highlighted
- **WHEN** a `{2.0, ratio}` recipe is active and the user dials the Ratio to 1:2.5
- **THEN** the Ratio row renders in the override-highlight color

#### Scenario: Dose cup is never highlighted
- **WHEN** any dose-cup value is shown
- **THEN** it renders in the default color regardless of value

#### Scenario: Highlight tracks exactly what Clear reverts
- **WHEN** any value renders highlighted
- **THEN** tapping Clear returns exactly that value to its baseline, and the highlight clears
