## MODIFIED Requirements

### Requirement: Brew Settings values use a single override-highlight color scheme

Brew Settings SHALL color each editable numeric value by a single rule instead of the current mix of per-value-type semantic colors (weight amber-brown, temperature red, ratio blue) and manual-vs-calculated state: a value SHALL render in the default text color (`Theme.textColor`) when it holds its baseline, and in the override-highlight color (`Theme.highlightColor` — the same highlight the Shot Plan uses for an active override, and the amber of the Clear button) when it deviates from that baseline. The invariant SHALL be: **a value is highlighted if and only if the Clear action would change it.** The per-value-type colors (`weightColor`, `temperatureColor`, `primaryColor` on the value text) and the `targetManuallySet` blue/amber distinction SHALL be removed.

The baseline for each field is the value the Clear handler restores, and it SHALL depend on whether a recipe is active:

- **Temp Delta** → when a recipe is active, the recipe's own temperature (`MainController.activeRecipe.tempOverrideC`, i.e. the delta reads `0°` at the recipe's design temperature); when no recipe is active — or the active recipe carries no temperature of its own — the profile temperature (delta `0°`).
- **Stop-at (yield)** → when a recipe is active, the recipe's own yield (`MainController.activeRecipe.yieldG`); when no recipe is active — or the active recipe carries no yield of its own — the profile target weight.
- **Dose** → the dose Clear restores (the bean's remembered dose `Settings.dye.dyeBeanWeight`, else 18) — unchanged, not recipe-relative.
- **Ratio** → the profile default ratio (`profileTargetWeight / dose`) — unchanged, not recipe-relative.

Because a recipe's yield and temperature are the recipe's design, not deviations from it, a recipe's own `yieldG` / `tempOverrideC` SHALL render in the default color (no highlight) when the dial sits on them; only a per-brew deviation *from the recipe's* value SHALL be highlighted. The Dose cup field is NOT reset by Clear, so it SHALL always use the default text color. The "Profile: …" sub-indicators MAY use the same highlight color when their field is overridden for visual consistency.

The +/- stepper accent SHALL be unified to a single accent across all fields rather than per-value-type colors (recommended: the override-highlight color when the field deviates from its baseline, otherwise the app accent), so a field reads as a whole when it is holding a deviation.

#### Scenario: Values at their baseline render in the default color

- **WHEN** no recipe is active, and Temp Delta is 0°, Dose equals the bean's remembered dose (or 18), Ratio equals the profile ratio, and Stop-at equals the profile target
- **THEN** all four values render in `Theme.textColor` (no highlight)

#### Scenario: A recipe's own yield and temperature are not highlighted

- **WHEN** a recipe with `yieldG` = 36 and `tempOverrideC` 4° below the profile default is active and the dial sits on the recipe's applied values
- **THEN** the Stop-at value shows 36 g and the Temp Delta reads `0°`, both in `Theme.textColor` (no highlight), because they are the recipe's baseline rather than a deviation from it

#### Scenario: A deviation from the recipe is highlighted

- **WHEN** a recipe with `yieldG` = 36 is active and the user sets Stop-at to 40
- **THEN** the Stop-at value renders in `Theme.highlightColor`
- **AND** tapping Clear returns it to 36 (the recipe's yield) and its color returns to `Theme.textColor`

#### Scenario: Dose cup is never highlighted

- **WHEN** the Dose cup holds any value (0 or non-zero)
- **THEN** it renders in the default text color, because Clear does not reset it

#### Scenario: Highlight tracks exactly what Clear reverts

- **WHEN** any editable numeric value differs from the value the Clear handler would restore (the recipe's value in recipe mode for Temp Delta / Stop-at, the profile default otherwise)
- **THEN** it is highlighted; and when it equals that value, it is not — with no dependence on value type or on whether the yield was set manually vs. calculated

### Requirement: "Update Profile" becomes "Update Recipe" in recipe mode

Today the Temp Delta and Stop-at rows each carry an "Update Profile" button that bakes the shown value into the underlying profile (Temp Delta → `ProfileManager.applyTemperatureToProfile(...)`; Stop-at → `uploadProfile` + `saveProfile` with the new `target_weight`). In recipe mode this would leak a recipe-specific tweak into the shared profile, changing every recipe and use that points at that profile. Therefore, when a recipe is active, each of these two buttons SHALL be labeled "Update Recipe" and SHALL persist the shown value into the **active recipe record** — Temp Delta → the recipe's `tempOverrideC`, Stop-at → the recipe's `yieldG` — via `MainController.recipeStorage.requestUpdateRecipe(Settings.dye.activeRecipeId, {...})`, and SHALL NOT modify the profile. When no recipe is active, both buttons SHALL remain "Update Profile" with their existing behavior unchanged.

The "Update Recipe" button SHALL be the **sole** way a yield/temp change reaches the recipe: yield and temperature are per-brew overrides (see the overrides requirement below), so committing the dialog with OK SHALL NOT write them to the recipe.

In recipe mode, the button's enabled state SHALL gate on the shown value differing from **the active recipe's own stored value** (`yieldG` / `tempOverrideC`) — NOT on differing from the profile default. This makes the recipe's baseline movable to any value, including back to the profile default: resetting the dial to the profile default while the recipe holds a different value SHALL leave "Update Recipe" enabled so the user can persist the reset. When no recipe is active, the "Update Profile" button's enabled state MAY continue to gate on the value differing from the profile default. The action persists immediately (like "Update Profile" does today) and is independent of OK.

#### Scenario: Stop-at persists to the recipe, not the profile

- **WHEN** a recipe is active and the user changes Stop-at and taps the (now "Update Recipe") button
- **THEN** the value is written to the active recipe's `yieldG` via `requestUpdateRecipe`
- **AND** the profile's `target_weight` is not modified and the profile is not re-saved

#### Scenario: Temp Delta persists to the recipe, not the profile

- **WHEN** a recipe is active and the user changes Temp Delta and taps the (now "Update Recipe") button
- **THEN** the resulting temperature is written to the active recipe's `tempOverrideC` via `requestUpdateRecipe`
- **AND** `applyTemperatureToProfile` is not called and the profile is not modified

#### Scenario: Resetting the recipe baseline to the profile default is persistable

- **WHEN** a recipe with `yieldG` = 36 is active, the user sets Stop-at to the profile target weight (e.g. 42), and the recipe's stored `yieldG` (36) still differs from the shown value
- **THEN** the "Update Recipe" button is enabled
- **AND** tapping it writes 42 to the recipe's `yieldG` so the recipe no longer deviates from the profile

#### Scenario: Update Recipe disabled once the recipe already holds the shown value

- **WHEN** a recipe is active and the shown Stop-at (or Temp Delta) equals the recipe's stored `yieldG` (`tempOverrideC`)
- **THEN** the "Update Recipe" button is disabled (nothing to persist)

#### Scenario: No recipe active — buttons still update the profile

- **WHEN** no recipe is active
- **THEN** both buttons read "Update Profile" and bake the value into the profile exactly as before this change
