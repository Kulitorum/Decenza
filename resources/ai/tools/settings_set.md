# settings_set

Updates any app setting on the device, across every settings tab. API keys and passwords are
excluded.

## Brew Settings

These keys do what Brew Settings does when the user presses OK: they change the next shot without
starting it and without editing the profile.

- `dyeBeanWeight` (dose), `dyeGrinderSetting`, `dyeGrinderRpm`.
- `targetWeight`: stop-at weight in grams. `yieldRatio`: yield as a multiple of the dose
  (2.5 = 1:2.5). Send one, not both. `0` clears the yield override.
- `espressoTemperature`: brew temperature override, 70-100 °C. The profile's own temperature
  clears it.
- `clearBrewOverrides: true`: Brew Settings Clear. The yield goes back to the active recipe's, else
  the bean's, else the profile's; the temperature to the recipe's, else the profile's.
- `ratioPreset1`-`3`, `doseCupTareWeight`, `doseCaptureSoundEnabled`.

Values must be numbers; anything else is refused. The reply's `brew` object is what took effect
(`targetWeightG`, `brewYieldMode`, `brewYieldValue`, `espressoTemperatureC`), with a `note` when it
differs from the request: a clamp, a gram target equal to the profile's, or a ratio with no dose.

A profile switch clears the temperature override and a gram target; a ratio carries to another
profile of the same kind (espresso, filter or tea) and is cleared when the kind changes. When a
switch leaves no yield, the recipe's, else the bean's, saved yield applies. A cleaning, descale or
calibrate profile neither uses nor clears any of these.

`settings_get` category `espresso` reads the state back, including the baseline and
`yieldPersistTarget`. To save a value instead: `recipe_update` (`yieldG`/`yieldRatio`,
`tempOffsetC`) or `bag` action=update (`yieldG`/`yieldRatio`). The profile's own target and
temperature are `profiles_edit_params` (`targetWeight`; `espressoTemperature` sent on its own).

Dose from the scale: `scale_tare` with the empty cup, add beans, `scale_get_weight`, then send the
reading as `dyeBeanWeight`.

**Only call this when the user explicitly asks to change something on the machine.** For
discussion and recommendations, answer in chat instead.
