# settings_set

Updates any app setting on the device, across every settings tab. API keys and passwords are
excluded.

## Brew Settings

These keys do what Brew Settings does when the user presses OK. They change the next shot
without starting it and without editing the profile, and they stay in effect until changed,
cleared, or replaced by a profile, bean or recipe switch.

- `dyeBeanWeight` (dose), `dyeGrinderSetting`, `dyeGrinderRpm`.
- `targetWeight`: stop-at weight in grams. `yieldRatio`: yield as a multiple of the dose
  (2.5 = 1:2.5). Send one, not both. `0` clears the yield override.
- `espressoTemperature`: brew temperature override in °C. The profile's own temperature clears it.
- `clearBrewOverrides: true`: Brew Settings Clear. The yield and temperature go back to the active
  recipe's, else the bean's, else the profile's.
- `ratioPreset1`-`3`, `doseCupTareWeight`, `doseCaptureSoundEnabled`.

`settings_get` category `espresso` reads the result back: `targetWeightG` (what the shot stops
at), the yield anchor, the temperature override, the baseline and its source, and
`yieldPersistTarget`.

To save a value instead of overriding it, write the store `yieldPersistTarget` names:
`recipe_update` or `bag` action=update (`yieldG`/`yieldRatio`, `tempOffsetC`). The profile's own
target weight and temperature are `profiles_edit_params` (`targetWeight`, `espressoTemperature`).

Dose from the scale: `scale_tare` with the empty cup, add beans, `scale_get_weight`, then send the
reading as `dyeBeanWeight`.

**Only call this when the user explicitly asks to change something on the machine.** For
discussion and recommendations, answer in chat instead.
