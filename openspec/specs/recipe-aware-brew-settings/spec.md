# recipe-aware-brew-settings Specification

## Purpose
TBD - created by archiving change recipe-aware-brew-settings. Update Purpose after archive.
## Requirements
### Requirement: Brew Settings layout branches on active-recipe state

`BrewDialog.qml` SHALL detect whether a recipe is currently active (an active recipe exists when `Settings.dye.activeRecipeId >= 0`, equivalently `MainController.activeRecipe` is non-empty) and choose its layout accordingly. The detection SHALL be reactive: if the active-recipe state changes while the dialog is open (e.g. the user switches recipes from within the dialog), the layout SHALL update without requiring the dialog to be reopened.

When no recipe is active, the dialog SHALL retain its existing layout and behavior — the Profile row, Beans row, and Equipment row are all present, and every dial-in field behaves identically — with the sole exception of the value-color cleanup below (the override-highlight scheme), which applies uniformly in both modes. This change SHALL NOT otherwise alter the behavior of the no-recipe dialog.

#### Scenario: No recipe active — dialog unchanged
- **WHEN** the Brew Settings dialog is opened with no recipe active (`Settings.dye.activeRecipeId < 0`)
- **THEN** the Profile, Beans, and Equipment rows are shown
- **AND** all dial-in fields (Temp Delta, Dose, Cup tare, Ratio, Yield, Grind, RPM) behave exactly as before this change

#### Scenario: Recipe active — dialog switches to recipe mode
- **WHEN** the Brew Settings dialog is opened with a recipe active (`Settings.dye.activeRecipeId >= 0`)
- **THEN** the dialog shows a Recipe row in place of the Profile row
- **AND** the Beans row and Equipment row are not shown

#### Scenario: State change while open is reactive
- **WHEN** the dialog is open in recipe mode and the active recipe is deactivated (from any surface)
- **THEN** the dialog returns to the no-recipe layout (Profile, Beans, Equipment rows) without being reopened

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

### Requirement: Shot Review and Shot Detail show which values were overridden at shot time

The top of the Shot Review and Shot Detail pages SHALL indicate which per-brew values (temperature, yield) were overridden **at the time the shot was taken**, using the override-highlight color (`Theme.highlightColor`), derived from the shot's frozen snapshot — never from the current live dial:

- **Temperature:** when the shot recorded a temperature override (`temperatureOverrideC > 0`), the temperature SHALL be shown in the highlight color where the header presents it (the "{profile} ({temp})" title parenthetical, which already appears only when an override was recorded).
- **Yield:** when the shot's target weight deviated from the profile default recorded in the shot's own profile snapshot (`profileJson`), the yield item in the plan snapshot line SHALL be highlighted (per the plan-widgets per-item scheme), sourced from the shot's frozen values.

Values that were not overridden, and every other item, SHALL remain the default color. Dose and grind are recorded dial-in values, not overrides, and SHALL NOT be highlighted here.

#### Scenario: Shot taken with a temperature override
- **WHEN** a shot whose snapshot has `temperatureOverrideC > 0` is opened in Shot Review or Shot Detail
- **THEN** the temperature in the header is shown in `Theme.highlightColor`

#### Scenario: Shot taken with a yield override
- **WHEN** a shot whose recorded target deviated from its profile-snapshot default yield is opened
- **THEN** the yield item in the plan snapshot line is shown in `Theme.highlightColor`

#### Scenario: Shot taken with no overrides
- **WHEN** a shot whose snapshot recorded no temperature or yield override is opened (even while a live override is currently active)
- **THEN** nothing at the top of the page is highlighted

### Requirement: Recipe row replaces the Profile row in recipe mode

In recipe mode, the top row of the dialog SHALL be a Recipe row that replaces the Profile row. The Recipe control SHALL let the user quick-switch the active recipe by choosing from the list of selectable (non-archived) recipes. The control SHALL be presented as a `SuggestionField` seeded with the active recipe's name, mirroring the Profile `SuggestionField` it replaces, so it inherits the app's existing accessibility affordance: with `AccessibilityManager.enabled` off it is an inline type-to-filter dropdown; with it on, the inline overlay is hidden and a labeled "Open suggestions" button opens a modal `SelectionDialog` list.

#### Scenario: Recipe row seeded with the active recipe
- **WHEN** the dialog opens in recipe mode
- **THEN** the Recipe control displays the active recipe's name
- **AND** its suggestion list contains the selectable non-archived recipes

#### Scenario: Accessibility off — inline dropdown
- **WHEN** the dialog is in recipe mode and `AccessibilityManager.enabled` is false
- **THEN** the Recipe control presents the inline type-to-filter dropdown affordance

#### Scenario: Accessibility on — modal selection dialog
- **WHEN** the dialog is in recipe mode and `AccessibilityManager.enabled` is true
- **THEN** the inline dropdown overlay is not shown
- **AND** a labeled "Open suggestions" button is shown that opens a modal `SelectionDialog` list of recipes

### Requirement: Selecting a different recipe re-activates it through the single activation path

When the user picks a recipe from the Recipe control that differs from the currently active recipe, `BrewDialog.qml` SHALL re-activate it by calling the existing single activation path `MainController.activateRecipe(id)` — it SHALL NOT introduce a separate activation mechanism. Because activation applies the recipe's profile, bag, equipment, dose, yield, temperature, and grind, the dialog's dial-in fields SHALL be re-seeded from the resulting DYE/profile state so the editable values reflect the newly activated recipe. Re-selecting the already-active recipe SHALL be a no-op (no redundant re-activation).

#### Scenario: Switching recipes re-activates and re-seeds
- **WHEN** the user selects a recipe in the Recipe control that is not the active one
- **THEN** `MainController.activateRecipe(id)` is called for the chosen recipe
- **AND** the dialog's dial-in fields (Temp Delta, Dose, Ratio, Yield, Grind, RPM) are re-seeded from the newly activated recipe's applied state

#### Scenario: Re-selecting the active recipe is a no-op
- **WHEN** the user selects the recipe that is already active
- **THEN** the recipe is not re-activated and the dial-in fields are not reset

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

### Requirement: Dial-in editing and OK/Cancel are unchanged in recipe mode

In recipe mode, the dial-in fields SHALL remain editable and the OK and Cancel actions SHALL behave as they do today: OK commits the dose/yield/temperature/grind values via `ProfileManager.activateBrewWithOverrides(...)` and saves grind/RPM to `Settings.dye`, and Cancel discards the dialog's edits. Removing the Profile/Beans/Equipment rows SHALL NOT change how the dial-in values are applied. Yield and temperature are applied as per-brew overrides only (see the overrides requirement below); OK SHALL NOT persist them to the recipe.

#### Scenario: OK applies values in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps OK
- **THEN** the values are applied via the same `activateBrewWithOverrides(...)` path used in the no-recipe dialog
- **AND** grind setting and RPM are saved to `Settings.dye`
- **AND** the yield and temperature changes apply as overrides in `Settings.brew` and do not modify the active recipe

#### Scenario: Cancel discards edits in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps Cancel
- **THEN** the dialog's dial-in edits are discarded and the active recipe is unchanged by the cancel

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

### Requirement: The live Shot Plan treats an active recipe's yield/temp as the baseline

The idle-screen **Shot Plan** widget SHALL, when a recipe is active, treat that recipe's own `yieldG` / offset-derived temperature as the baseline rather than the profile's default — mirroring Brew Settings — so a recipe's designed values do not render as overrides. Specifically: the yield SHALL render as a plain effective target (e.g. "40.0g") with no "profile-default → target" arrow, and neither the yield nor the temperature segment SHALL be tinted with the override-highlight color, when the live values equal the active recipe's own. The override arrow (yield) and the amber highlight (yield and temperature) SHALL return only for a per-brew value dialed **beyond** the recipe's saved value.

The temperature STRING SHALL show the recipe's OWN temperatures — the profile's frame temperatures shifted uniformly by the recipe's stored `tempOffsetC`, e.g. a recipe with offset −3 on an `84 · 94°C` profile reads **"81 · 91°C"** — with NO profile-relative offset tag. A signed delta tag SHALL appear only for a per-brew value dialed beyond the recipe (measured from the recipe temp). *A baseline is a baseline*: the Shot Plan temperature and the Brew Settings Temp Delta (which reads `0°` at the recipe) SHALL agree. This is provided by a `baselineShiftC` parameter on the shared temperature formatter; with no recipe active the shift is 0 and the profile temps + offset tag render as before.

This recipe-as-baseline rendering applies to the live widget and its layout-editor preview using the currently-loaded profile's frames (via the shared temperature formatter's live state), and to recipe cards (the management list and the wizard's summary preview) using the same baseline decomposition resolved against **their own** recipe's profile instead — never the currently loaded one — since a card must render correctly even while a different profile is loaded (see `recipe-quick-switch`). The Shot Review / Shot Detail plan lines keep their own explicit shot-relative highlighting, showing what was overridden **at shot time** relative to the shot's own profile.

#### Scenario: Active recipe's yield shows as a plain target, un-highlighted

- **WHEN** a recipe with `yieldG` = 40 is active on a profile whose target weight is 36, and no per-brew tweak has been dialed
- **THEN** the Shot Plan yield reads "40.0g" (no "36.0 → 40.0g" arrow) and is not tinted

#### Scenario: A per-brew tweak beyond the recipe re-arms the arrow and highlight

- **WHEN** that recipe is active and the user dials the next-brew stop-at to 44
- **THEN** the Shot Plan yield reads "40.0 → 44.0g" and is tinted with the override-highlight color

#### Scenario: Recipe cards resolve the same baseline against their own profile

- **WHEN** a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C is listed on its management-page card while the machine currently has a *different* profile loaded
- **THEN** the card's temperature reads "81 · 91°C" — resolved from that recipe's own profile, not the loaded one — matching what the live Shot Plan would show if that recipe were active

#### Scenario: An active recipe's temperature shows the recipe's own temps, un-tinted

- **WHEN** a recipe carrying `tempOffsetC` = −3 is active on a profile whose frames are `84 · 94°C`, and no per-brew tweak has been dialed
- **THEN** the Shot Plan temperature reads "81 · 91°C" (the profile frames shifted by −3°) with no offset tag and no tint
- **AND** the Brew Settings temperature sub-line reads "Recipe: 81 · 91°C" while its Temp Delta control reads `0°`

#### Scenario: Live surfaces show the result; cards show the relationship

- **WHEN** a recipe with `tempOffsetC` = −3 on an 84 · 94°C profile is active
- **THEN** the live Shot Plan reads "81 · 91°C" (recipe values only, untagged, untinted) while that recipe's card reads "84 · 94°C −3°" (source + highlighted delta)
- **AND** a Shot Review / Shot Detail plan line still highlights what was overridden at shot time relative to the shot's own profile

