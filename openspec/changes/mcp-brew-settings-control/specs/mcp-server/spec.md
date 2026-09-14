## ADDED Requirements

### Requirement: settings_set applies Brew Settings values as brew overrides

`settings_set` SHALL apply the Brew Settings fields the way Brew Settings OK does, without starting a shot and without editing the profile:

- `targetWeight` (grams) SHALL arm an absolute yield override; `yieldRatio` SHALL arm a ratio yield override. Sending both SHALL be rejected. `0` for either SHALL clear the yield override. An absolute equal to the profile's own target SHALL NOT count as an override.
- `espressoTemperature` SHALL set the temperature override, or clear it when the value equals the profile's temperature, and re-upload the profile.
- `clearBrewOverrides: true` SHALL restore the baseline: the yield anchor of the active recipe or bag when one designs a yield (otherwise no yield override), and the recipe's temperature (profile temperature plus its offset) or the profile's.
- `dyeBeanWeight`, `dyeGrinderSetting` and `dyeGrinderRpm` keep their meaning; a dose written in the same call SHALL apply before a ratio resolves.
- `ratioPreset1`-`ratioPreset3`, `doseCupTareWeight` and `doseCaptureSoundEnabled` SHALL write those settings.

#### Scenario: Dialing a ratio over MCP
- **WHEN** the dose is 18 g and a client calls `settings_set` with `yieldRatio: 2.5`
- **THEN** the session yield anchor is `{2.5, ratio}`, the stop-at target is 45 g, and the profile's `target_weight` is unchanged

#### Scenario: Both yield keys are rejected
- **WHEN** a client calls `settings_set` with `targetWeight` and `yieldRatio`
- **THEN** the call returns an error and nothing is written

#### Scenario: Temperature is an override, not a profile edit
- **WHEN** a client calls `settings_set` with `espressoTemperature: 91` on a 93 °C profile
- **THEN** the temperature override is 91 °C, the profile is uploaded, and the profile is not marked modified

#### Scenario: Clear restores the bean's ratio
- **WHEN** the active bag saves `{2.0, ratio}`, the session anchor is `{40, absolute}`, and a client calls `settings_set` with `clearBrewOverrides: true`
- **THEN** the session anchor is `{2.0, ratio}`

### Requirement: settings_get reports Brew Settings state

`settings_get` category `espresso` SHALL report: `targetWeightG` as the target the machine will stop at; `brewYieldMode` and `brewYieldValue` (the session anchor); `yieldRatio` (the effective ratio, or 0); `hasTemperatureOverride` and `temperatureOverrideC`; `baselineYieldMode`, `baselineYieldValue`, `baselineYieldSource` (`recipe`, `bag` or `profile`), `baselineTemperatureC`; `yieldIsRealOverride` and `temperatureIsRealOverride`; `yieldPersistTarget` (`recipe`, `bag` or empty — where Update Recipe/Bag would write); `lastUsedRatio`, `ratioPreset1`-`ratioPreset3`, `doseCupTareWeightG` and `doseCaptureSoundEnabled`.

#### Scenario: A ratio-anchored session reads back
- **WHEN** the session anchor is `{2.5, ratio}` and the dose is 18 g
- **THEN** `settings_get` category `espresso` returns `brewYieldMode: "ratio"`, `brewYieldValue: 2.5`, `targetWeightG: 45`

### Requirement: profiles_edit_params saves a profile temperature like Update Profile

`profiles_edit_params` SHALL accept `espressoTemperature` on every editor type and apply it through the same path as Brew Settings' Update Profile: shift every frame to the new temperature, clear a temperature override, upload, and save the profile.

#### Scenario: Saving a temperature to the profile
- **WHEN** a client calls `profiles_edit_params` with `espressoTemperature: 92` on a saved 93 °C profile
- **THEN** the profile's `espresso_temperature` is 92 °C, every frame shifts by -1 °C, and no temperature override remains

### Requirement: equipment creates packages

The `equipment` tool SHALL accept `action=create` with a grinder identity, an optional basket identity, an optional `name` and optional `puckPrep` flags, using the same storage path as the Switch Equipment dialog: an identical package already in inventory SHALL be returned instead of duplicated, and a name already used by another package SHALL be rejected.

#### Scenario: Creating a package
- **WHEN** a client calls `equipment` with `action: create`, `grinderBrand: "Niche"`, `grinderModel: "Zero"`
- **THEN** the response carries the new package, and `action=list` includes it

### Requirement: machine_start does not take brew overrides

`machine_start action=espresso` SHALL NOT accept dose, yield, temperature, grind or RPM arguments. Its description SHALL direct clients to set brew values with `settings_set` first.

#### Scenario: Overrides are set before starting
- **WHEN** a client wants a 1:2.5 shot
- **THEN** it calls `settings_set` with `yieldRatio: 2.5`, then `machine_start` with `action: espresso`
