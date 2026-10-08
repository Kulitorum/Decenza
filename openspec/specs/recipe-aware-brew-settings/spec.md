# recipe-aware-brew-settings Specification

## Purpose
Governs the Brew Settings dialog while a recipe is active: the recipe-mode layout and Recipe row, the single override-highlight colour rule and its baselines, how yield, ratio and temperature overrides stay per brew and reach a store only through an explicit Update button, dose and grind write-through, and how the live Shot Plan and Shot Review show recipe baselines and overrides.

## Requirements
### Requirement: Brew Settings layout branches on active-recipe state
`BrewDialog.qml` SHALL detect whether a recipe is active (`Settings.dye.activeRecipeId >= 0`) and choose its layout accordingly. The detection SHALL be reactive, so the layout updates without the dialog being reopened. With no recipe active, the dialog SHALL keep its existing Profile, Beans and Equipment rows and dial-in behaviour, apart from the value-colour cleanup below.

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
Brew Settings SHALL colour each editable numeric value by one rule: a value SHALL render in `Theme.textColor` when it holds its baseline, and in `Theme.highlightColor` when it deviates from it. A value is highlighted if and only if the Clear action would change it. The per-value-type colours (`weightColor`, `temperatureColor`, `primaryColor` on the value text) and the `targetManuallySet` blue/amber distinction SHALL be removed.

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

### Requirement: Override baselines resolve through the yield-anchor ladder
The baseline for each field SHALL be the value the Clear handler restores, resolved through the `yield-anchor` ladder: the active recipe's stored value when a recipe is active, else the active bag's, else the profile default. Temp Delta's baseline is the recipe's offset-derived temperature when a recipe with a non-zero offset is active, else the profile temperature. Dose's baseline is the bean's remembered dose, else 18, not recipe-relative.

#### Scenario: Temp Delta baseline follows the recipe
- **WHEN** a recipe with `tempOffsetC` of −3 on a 90° profile is active and the dial reads 87°
- **THEN** the Temp Delta baseline is 87°, so the field reads `0°` in the default colour

### Requirement: A recipe's own yield and temperature are not highlighted
A recipe's or bag's yield and offset-derived temperature SHALL render in the default colour when the dial sits on it. Only a per-brew deviation from that stored value SHALL be highlighted. The Dose cup field is not reset by Clear, so it SHALL always use the default text colour. The "Profile: …" sub-indicators MAY use the highlight colour when their field is overridden.

#### Scenario: Dose cup is never highlighted
- **WHEN** any dose-cup value is shown
- **THEN** it renders in the default color regardless of value

### Requirement: Stepper accent and yield/ratio baselines
The +/- stepper SHALL use a single accent across all fields: the highlight colour when the field deviates from its baseline, otherwise the app accent. For the yield/ratio pair the baseline SHALL be expressed in the stored anchor's own unit, with the other row's baseline derived through the current dose. Neither row SHALL highlight merely because the dose changed. The override tolerance for both rows SHALL be one unit, converted through the dose.

#### Scenario: Stored absolute anchor sets the baselines
- **WHEN** the stored anchor is `{36 g, absolute}`
- **THEN** the Stop-at baseline is 36 and the Ratio baseline is 36 ÷ dose

#### Scenario: Stored ratio anchor sets the baselines
- **WHEN** the stored anchor is `{2.0, ratio}`
- **THEN** the Ratio baseline is 2.0 and the Stop-at baseline is 2.0 × dose

### Requirement: Shot Review and Shot Detail show which values were overridden at shot time
The top of Shot Review and Shot Detail SHALL indicate, in `Theme.highlightColor`, which per-brew values were overridden at the time the shot was taken. This SHALL derive from the shot's frozen snapshot, never the live dial. Temperature SHALL be highlighted when the shot recorded a temperature override. Yield SHALL be highlighted when the shot's target weight deviated from its snapshot's profile default. Other values SHALL keep the default colour.

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
In recipe mode the top row SHALL be a Recipe row that replaces the Profile row. It SHALL let the user quick-switch the active recipe from the selectable (non-archived) recipes. It SHALL be a `SuggestionField` seeded with the active recipe's name, mirroring the Profile field it replaces.

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

### Requirement: Recipe row follows the accessibility affordance
With `AccessibilityManager.enabled` off, the Recipe control SHALL be an inline type-to-filter dropdown. With it on, the inline overlay SHALL be hidden and a labelled "Open suggestions" button SHALL open a modal `SelectionDialog` list of recipes.

#### Scenario: Recipe row seeded with the active recipe
- **WHEN** the dialog opens in recipe mode
- **THEN** the Recipe control displays the active recipe's name, and its suggestion list contains the selectable non-archived recipes

### Requirement: Selecting a different recipe re-activates it through the single activation path
Picking a recipe that differs from the active one SHALL re-activate it by calling `MainController.activateRecipe(id)`, with no separate activation mechanism. The dial-in fields SHALL then be re-seeded from the resulting DYE and profile state. Re-selecting the already-active recipe SHALL be a no-op.

#### Scenario: Switching recipes re-activates and re-seeds
- **WHEN** the user selects a recipe in the Recipe control that is not the active one
- **THEN** `MainController.activateRecipe(id)` is called for the chosen recipe
- **AND** the dialog's dial-in fields (Temp Delta, Dose, Ratio, Yield, Grind, RPM) are re-seeded from the newly activated recipe's applied state

#### Scenario: Re-selecting the active recipe is a no-op
- **WHEN** the user selects the recipe that is already active
- **THEN** the recipe is not re-activated and the dial-in fields are not reset

### Requirement: "Update Profile" becomes "Update Recipe" in recipe mode
Brew Settings SHALL carry two persist actions: one for Temp Delta and one for the yield/ratio pair. When a recipe is active, the Temp Delta button SHALL be labelled "Update Recipe" and SHALL persist the offset from the profile's espresso_temperature into the active recipe's `tempOffsetC` via `MainController.recipeStorage.requestUpdateRecipe(...)`, never modifying the profile. With no recipe active it SHALL remain "Update Profile", unchanged.

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

### Requirement: The yield persist button sits on the anchored row
The yield/ratio persist SHALL be a single button, placed on whichever of the Ratio or Stop-at rows is the current yield anchor, and it SHALL move when the user edits the other row. Its label and destination SHALL follow the resolution ladder: "Update Recipe" writes the recipe's yield spec when a recipe is active, "Update Bag" writes the bag's when only a bag is active, and the button is hidden with neither. A profile SHALL never be a destination.

#### Scenario: No yield button for an undesigned anchor
- **WHEN** the anchor's mode is `none` and the user has not yet edited either row
- **THEN** neither row shows a button, and the first edit anchors that row and its button appears on it

### Requirement: Persist buttons gate on the stored value
Both persist buttons SHALL be enabled only when the shown value differs from the active store's own stored value, not from the profile default. Yield/ratio SHALL compare like with like, and a mode change alone SHALL enable it. A persist SHALL take effect immediately and independently of OK. With no recipe active, the Temp Delta "Update Profile" button MAY keep gating on the profile default.

#### Scenario: Temp Delta reset to offset zero is persistable
- **WHEN** a recipe with `tempOffsetC` of −3 is active and the user dials the temperature back to the profile default
- **THEN** the Temp Delta button is enabled, and tapping it stores offset 0

### Requirement: The persist button is the only yield write path
The persist button SHALL be the sole way a yield or ratio change reaches a recipe or bag. Committing the dialog with OK SHALL NOT write them to either.

#### Scenario: OK does not persist the yield
- **WHEN** the user changes the Stop-at value and taps OK while a recipe is active
- **THEN** the recipe's yield spec is unchanged

### Requirement: Dial-in editing and OK/Cancel are unchanged in recipe mode
In recipe mode the dial-in fields SHALL remain editable. OK SHALL commit dose, yield, temperature and grind via `ProfileManager.activateBrewWithOverrides(...)` and save grind and RPM to `Settings.dye`, as today. Cancel SHALL discard the dialog's edits. Yield and temperature SHALL apply only as per-brew overrides.

#### Scenario: OK applies values in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps OK
- **THEN** the values are applied via the same `activateBrewWithOverrides(...)` path used in the no-recipe dialog
- **AND** grind setting and RPM are saved to `Settings.dye`
- **AND** the yield and temperature changes apply as overrides in `Settings.brew` and do not modify the active recipe

#### Scenario: Cancel discards edits in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps Cancel
- **THEN** the dialog's dial-in edits are discarded and the active recipe is unchanged by the cancel

### Requirement: Yield and temperature are per-brew overrides, never auto-written to the recipe
Yield (Stop-at), ratio and temperature (Temp Delta) set in Brew Settings are per-brew **overrides** and SHALL NOT modify their baseline. A change SHALL apply only as an override in `Settings.brew`, persisted per brew and cleared on recipe switch, and SHALL NOT be written into the active recipe or bag. The recipe's yield spec and `tempOffsetC`, and the bag's yield spec, SHALL change only via the explicit persist button.

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

### Requirement: Yield and temperature auto-stamp watchers are removed
The auto-stamp watchers on `SettingsBrew::brewOverridesChanged` and `SettingsBrew::temperatureOverrideChanged` SHALL be removed. So SHALL the bag write-through `persistYieldOverrideToBag`, called from `ProfileManager::activateBrewWithOverrides`. `RECIPES.md` SHALL drop yield and temperature from its description of tweaks that stamp the active recipe.

#### Scenario: Tweaks no longer stamp the recipe
- **WHEN** the user changes Stop-at or Temp Delta while a recipe is active
- **THEN** no write reaches the recipe, and no bag write-through is triggered by the yield change

### Requirement: Recipe activation starts the override from the stored values
On recipe activation the recipe's stored yield spec SHALL be applied as the starting override verbatim, mode included. The temperature SHALL be profile espresso_temperature plus `tempOffsetC`. An activated recipe therefore opens with its saved yield and temperature.

#### Scenario: Activated recipe opens with saved values
- **WHEN** a recipe holding `{36.0, absolute}` with a `tempOffsetC` of −3 is activated on a 90° profile
- **THEN** the session starts at a 36 g Stop-at and an 87° brew temperature

### Requirement: Dose and grind keep their existing dial write-through
Dose and grind/RPM are dial-in values, not overrides, and SHALL keep their existing write-through unchanged. Dose SHALL write through to the active bag and stamp the active recipe's `doseG`. Grind and RPM SHALL write through to the active bag and stamp the recipe's `grindPinned` and `rpmPinned`, with the non-tea guard. This change SHALL NOT add or remove any write-back for dose or grind.

#### Scenario: Editing dose in recipe mode still writes through
- **WHEN** a recipe is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`, writes through to the active bag, and stamps the active recipe's `doseG` — exactly as before this change
- **AND** the profile's recommended dose is NOT touched, because the recipe owns the dose

#### Scenario: Editing dose with only a bag active does not reach the profile
- **WHEN** no recipe is active, a bag is active, and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye` and written through to the active bag
- **AND** the profile's recommended dose is unchanged

#### Scenario: Editing dose with no recipe and no bag does not dirty the profile
- **WHEN** neither a recipe nor a bag is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`
- **AND** the loaded profile is NOT marked modified and its recommended dose is unchanged

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

### Requirement: The profile is never a dose write target here
The profile SHALL NOT be a write target for dose in this dialog. A dose dialed with neither a recipe nor a bag active SHALL stay in `Settings.dye` and SHALL NOT mark the loaded profile modified.

#### Scenario: Dose with a recipe active leaves the profile alone
- **WHEN** a recipe is active and the user changes the dose and taps OK
- **THEN** the profile's recommended dose is not touched, because the recipe owns the dose

### Requirement: Re-seeding a recipe switch writes only local values
The re-seed performed on a recipe switch SHALL write only the dialog's local `root.*` values, never `Settings`, so that re-seeding never stamps the newly activated recipe.

#### Scenario: Re-seed writes only local values
- **WHEN** the user switches recipes in the dialog and the fields are re-seeded
- **THEN** no `Settings` property is written by the re-seed

### Requirement: Dose capture never changes the yield mode
Dose, grind and RPM are things the user physically did, so they are remembered automatically. The yield anchor is design intent and is button-protected. A dose capture SHALL update the dose and SHALL NOT change the yield mode.

#### Scenario: Yield mode is kept on dose capture
- **WHEN** a recipe holding `{2.0, ratio}` is active and a scale dose capture lands
- **THEN** the yield anchor remains `{2.0, ratio}`

### Requirement: Ratio is stored in the yield spec
Ratio SHALL be recipe- and bag-stored as the mode of the yield spec. `Settings.brew.lastUsedRatio` SHALL survive only as preset memory. Cup tare SHALL remain DYE-only. Steam and hot-water blocks SHALL NOT be edited by this dialog.

#### Scenario: Ratio lives in the yield spec
- **WHEN** the user dials a ratio of 1:3 with a bag active and no recipe
- **THEN** the bag's yield spec is `{3.0, ratio}`, and `lastUsedRatio` is only preset memory

### Requirement: The live Shot Plan treats an active recipe's yield/temp as the baseline
When a recipe is active, the idle Shot Plan widget SHALL treat the recipe's own `yieldG` and offset-derived temperature as the baseline, not the profile default, mirroring Brew Settings. A recipe's designed values SHALL render as a plain target with no override arrow and no highlight. The override arrow and amber highlight SHALL return only for a per-brew value dialed beyond the recipe's saved value.

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

### Requirement: The Shot Plan temperature shows the recipe's own temperatures
The temperature SHALL show the recipe's own temperatures: the profile's frame temperatures shifted by the recipe's `tempOffsetC`, with no profile-relative tag. A signed delta tag SHALL appear only for a per-brew value dialed beyond the recipe. The Shot Plan and the Brew Settings Temp Delta SHALL agree, via a `baselineShiftC` parameter on the shared temperature formatter.

#### Scenario: Zero offset shows the profile temperatures
- **WHEN** a recipe with `tempOffsetC` of 0 is active on a profile whose frames are 84 · 94°C
- **THEN** the Shot Plan temperature reads "84 · 94°C" with no tag

### Requirement: Recipe cards and Shot Review resolve their own baseline
Recipe cards (the management list and the wizard's summary preview) SHALL resolve the same baseline decomposition against their own recipe's profile, never the currently loaded one. The live widget and its layout-editor preview SHALL use the loaded profile's frames. Shot Review and Shot Detail plan lines SHALL keep their explicit shot-relative highlighting.

#### Scenario: Recipe card shows the relationship
- **WHEN** a recipe with `tempOffsetC` of −3 on an 84 · 94°C profile is listed on its card
- **THEN** the card reads "84 · 94°C −3°", with the source and the highlighted delta

