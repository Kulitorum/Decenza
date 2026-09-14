## ADDED Requirements

### Requirement: settings_set applies Brew Settings values as brew overrides

`settings_set` SHALL apply the Brew Settings fields the way Brew Settings OK does, without starting a shot and without editing the profile:

- `targetWeight` (grams) SHALL arm an absolute yield override; `yieldRatio` SHALL arm a ratio yield override. Sending both SHALL be rejected. `0` for either SHALL clear the yield override. An absolute equal to the profile's own target SHALL NOT count as an override.
- `espressoTemperature` SHALL set the temperature override, or clear it when the value equals the profile's temperature, and re-upload the profile. Values outside 70-100 °C SHALL be rejected.
- A `targetWeight`, `yieldRatio` or `espressoTemperature` that is not a non-negative number SHALL be rejected, with nothing written.
- `clearBrewOverrides: true` SHALL restore the baseline: the yield anchor of the active recipe or bag when one designs a yield (otherwise no yield override), and the recipe's temperature (profile temperature plus its offset) or the profile's.
- `dyeBeanWeight`, `dyeGrinderSetting` and `dyeGrinderRpm` keep their meaning; a dose written in the same call SHALL apply before a ratio resolves.
- `ratioPreset1`-`ratioPreset3`, `doseCupTareWeight` and `doseCaptureSoundEnabled` SHALL write those settings. Non-numeric values and a non-boolean `clearBrewOverrides` SHALL be rejected; `clearBrewOverrides` SHALL be refused while the active recipe is still loading.
- The reply SHALL carry a `brew` object read after the change applied — `targetWeightG`, `brewYieldMode`, `brewYieldValue`, `espressoTemperatureC` — with a `note` when the result differs from the request.

#### Scenario: Dialing a ratio over MCP
- **WHEN** the dose is 18 g and a client calls `settings_set` with `yieldRatio: 2.5`
- **THEN** the session yield anchor is `{2.5, ratio}`, the reply's `brew.targetWeightG` is 45, and the profile's `target_weight` is unchanged

#### Scenario: Both yield keys are rejected
- **WHEN** a client calls `settings_set` with `targetWeight` and `yieldRatio`
- **THEN** the call returns an error and nothing is written

#### Scenario: A value that is not a number is rejected
- **WHEN** a client calls `settings_set` with `targetWeight: "heavy"`
- **THEN** the call returns an error and the yield override is unchanged

#### Scenario: Temperature is an override, not a profile edit
- **WHEN** a client calls `settings_set` with `espressoTemperature: 91` on a 93 °C profile
- **THEN** the temperature override is 91 °C, the profile is uploaded, and the profile is not marked modified

#### Scenario: Clear restores the bean's ratio
- **WHEN** the active bag saves `{2.0, ratio}`, the session anchor is `{40, absolute}`, and a client calls `settings_set` with `clearBrewOverrides: true`
- **THEN** the session anchor is `{2.0, ratio}`

### Requirement: settings_get reports Brew Settings state

`settings_get` category `espresso` SHALL report: `targetWeightG` as the target the machine will stop at and `espressoTemperatureC` as the temperature it will brew at; `brewYieldMode` and `brewYieldValue` (the session anchor); `yieldRatio` (the effective ratio, or 0); `hasTemperatureOverride` and `temperatureOverrideC`; `baselineYieldMode`, `baselineYieldValue`, `baselineYieldSource` (`recipe`, `bag` or `profile`), `baselineTemperatureC`; `yieldIsRealOverride` and `temperatureIsRealOverride`; `yieldPersistTarget` (`recipe`, `bag` or empty — where Update Recipe/Bag would write); `lastUsedRatio`, `ratioPreset1`-`ratioPreset3`, `doseCupTareWeightG` and `doseCaptureSoundEnabled`.

#### Scenario: A ratio-anchored session reads back
- **WHEN** the session anchor is `{2.5, ratio}` and the dose is 18 g
- **THEN** `settings_get` category `espresso` returns `brewYieldMode: "ratio"`, `brewYieldValue: 2.5`, `targetWeightG: 45`

### Requirement: profiles_edit_params saves a profile temperature like Update Profile

`profiles_edit_params` SHALL accept `espressoTemperature` (70-100 °C) on every editor type, sent without other parameters, and apply it through the same path as Brew Settings' Update Profile: shift every frame to the new temperature, clear a temperature override, upload, and save the profile when it has a file. The reply SHALL report `saved` and say why a profile was not saved (read-only built-in, no file); a failed write SHALL be reported as an error.

#### Scenario: Saving a temperature to the profile
- **WHEN** a client calls `profiles_edit_params` with only `espressoTemperature: 92` on a saved, writable 93 °C profile
- **THEN** the profile's `espresso_temperature` is 92 °C, every frame shifts by -1 °C, no temperature override remains, and `saved` is true

#### Scenario: Mixed with other parameters
- **WHEN** a client sends `espressoTemperature` together with `pourFlow`
- **THEN** the call returns an error and the profile is unchanged

### Requirement: equipment creates packages

The `equipment` tool SHALL accept `action=create` with a grinder and/or basket identity, an optional `name` and optional `puckPrep` flags, using the same storage rule as the Switch Equipment dialog: an identical package already in inventory SHALL be returned instead of duplicated, with `created: false`, and a name already used by another package SHALL be rejected.

#### Scenario: Creating a package
- **WHEN** a client calls `equipment` with `action: create`, `grinderBrand: "Niche"`, `grinderModel: "Zero"`
- **THEN** the response carries the new package with `created: true`, and `action=list` includes it

#### Scenario: Creating gear that already exists
- **WHEN** a client creates a package whose full identity matches one in inventory
- **THEN** the response carries that package with `created: false`

### Requirement: machine_start does not take brew overrides

`machine_start action=espresso` SHALL NOT accept dose, yield, temperature, grind or RPM arguments; a call carrying any of them SHALL be refused before any confirmation is requested, without starting a shot. Its description SHALL direct clients to set brew values with `settings_set` first.

#### Scenario: A stale client sends a yield
- **WHEN** a client calls `machine_start` with `action: espresso` and `yield: 40`
- **THEN** the call returns an error naming `settings_set`, and no shot starts

### Requirement: An unanswered machine confirmation is reported as a timeout

When the on-machine confirmation dialog for `machine_start` closes without an answer, the reply and the log SHALL say the call was not confirmed before the dialog timed out, distinct from a Deny tap, and the tool SHALL NOT run.

#### Scenario: Nobody answers the dialog
- **WHEN** a client calls `machine_start` at a confirmation level that raises the dialog and nobody taps it
- **THEN** after the dialog times out the reply's `error` says it was not confirmed before the dialog timed out, and nothing starts
