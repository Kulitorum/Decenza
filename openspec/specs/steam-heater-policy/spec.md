# steam-heater-policy Specification

## Purpose
Governs the steam heater's standing state: the two Settings toggles (Keep warm when idle and Let the recipe decide), the permission and veto model that decides whether the heater is warm, the built-in "Heater off" pitcher entry, the single derivation of the steam target, the steam readout and wake behaviour, and the migration off user-created heater-off presets.
## Requirements
### Requirement: Two independent steam heater settings

The system SHALL expose two independent boolean settings in Settings → Machine → Steam, replacing the single `keepSteamHeaterOn` boolean:

- **Keep warm when idle** — whether the steam heater is warm when nothing else has an opinion.
- **Let the recipe decide** — whether an active recipe's pitcher overrides the standing pitcher.

They SHALL be independently settable. Both SHALL default to on, for a fresh install and for an installation that already has settings.

#### Scenario: Both on
- **WHEN** both settings are on, no recipe is active
- **THEN** the steam heater is warm while the machine is awake

#### Scenario: An existing user gets the recipe behaviour on upgrade
- **WHEN** an installation with existing settings is migrated
- **THEN** Let the recipe decide is on, so a parked recipe with no pitcher turns the steam heater off

#### Scenario: Baseline warm, recipe ignored
- **WHEN** Keep warm when idle is on, Let the recipe decide is off, and a recipe with no pitcher is active
- **THEN** the steam heater stays warm — the recipe has no effect on the heater

#### Scenario: Baseline warm, recipe allowed to decide
- **WHEN** both settings are on and a recipe with no pitcher is active
- **THEN** the steam heater is off

#### Scenario: Baseline cold, recipe allowed to decide
- **WHEN** Keep warm when idle is off, Let the recipe decide is on, and a milk recipe is active but no shot has started
- **THEN** the steam heater is off

#### Scenario: Both off
- **WHEN** both settings are off
- **THEN** the steam heater is off until the user starts a steam operation, whatever recipe is active

### Requirement: Permission and veto model

The steam heater SHALL be warm if and only if permission is granted AND no veto applies. A veto SHALL be either the effective pitcher being the "Heater off" entry, or the transient steam-off toggle. Permission granted by a state SHALL persist while that state holds. Permission granted by an event SHALL be revoked when the machine returns to Idle.

#### Scenario: A veto beats permission
- **WHEN** Keep warm when idle is on and the standing pitcher is "Heater off"
- **THEN** the steam heater is off

#### Scenario: Selecting a recipe lifts a veto
- **WHEN** Keep warm when idle is on, Let the recipe decide is on, the standing pitcher is "Heater off", and a recipe carrying a real pitcher is activated
- **THEN** the steam heater becomes warm, and returns to off when that recipe is deactivated

#### Scenario: Selecting a real pitcher does not warm a cold machine
- **WHEN** Keep warm when idle is off, no recipe is active, the "Heater off" entry is selected, and the user selects a real pitcher
- **THEN** the veto is removed but the heater stays off, because nothing granted permission

#### Scenario: Selecting a real pitcher resumes a permitted heater
- **WHEN** Keep warm when idle is on, the "Heater off" entry is selected, and the user selects a real pitcher
- **THEN** the heater warms, to that pitcher's temperature

#### Scenario: Event permission is revoked at Idle
- **WHEN** Keep warm when idle is off, Let the recipe decide is on, a milk recipe's shot has warmed the heater, and the machine returns to Idle
- **THEN** the steam heater is turned off again

#### Scenario: State permission survives Idle
- **WHEN** Keep warm when idle is on and the machine returns to Idle after a shot
- **THEN** the steam heater stays warm

#### Scenario: Starting a steam operation always heats
- **WHEN** both settings are off and the user starts a steam operation
- **THEN** the steam heater is commanded on

#### Scenario: Recipe selection that lifts no veto changes nothing

- **WHEN** a recipe is activated while no veto applies and permission is absent
- **THEN** the steam heater SHALL stay off

### Requirement: Permission SHALL come from exactly three sources

Permission SHALL come from exactly three sources: Keep warm when idle while the machine is awake; Let the recipe decide, when a shot starts while a steam-using recipe is active; and the user starting a steam operation. Selecting a recipe or a pitcher SHALL NOT grant permission.

#### Scenario: Recipe activation alone grants nothing

- **WHEN** both settings are off and a recipe carrying a real pitcher is activated
- **THEN** the steam heater SHALL stay off

### Requirement: Recipe selection SHALL re-resolve the effective pitcher and veto

Selecting a recipe SHALL re-resolve the effective pitcher immediately, so a veto MAY be applied or lifted. The pitcher row expresses what the user would steam with, not whether the heater is warm: selecting "Heater off" SHALL apply the veto, and selecting a real pitcher SHALL only remove it.

#### Scenario: A real pitcher may stay selected while the heater is off

- **WHEN** Keep warm when idle is off and a real pitcher is selected with no permission granted
- **THEN** the pitcher SHALL remain selected and the heater SHALL be off

### Requirement: Steaming with Heater off selected SHALL use the live steam settings

Steaming while the "Heater off" entry is selected SHALL use the live steam settings (duration, flow, temperature), which hold the values of the last real pitcher selected. The "Heater off" entry SHALL NOT carry steam values of its own.

#### Scenario: Steam start with Heater off selected uses the last real pitcher's values

- **GIVEN** the "Heater off" entry is selected
- **WHEN** the user starts a steam operation
- **THEN** the machine SHALL be commanded with the live steam duration, flow and temperature

### Requirement: The steam readout says when the heater is off

Every surface that displays the steam temperature SHALL show that the heater is off, rather than a temperature, whenever the resolved target is off. This SHALL be driven by the resolved state, never by the measured boiler temperature, which keeps reporting heat while the heater cools.

#### Scenario: The widget reads Off, not a stale temperature
- **WHEN** the resolved target is off and the boiler is still at 130 °C on its way down
- **THEN** the steam temperature widget shows that the heater is off, not 130 °C

#### Scenario: A selected pitcher does not imply a warm heater
- **WHEN** Keep warm when idle is off, a real pitcher is selected, and no recipe or steam action has granted permission
- **THEN** the pitcher stays selected in the pitcher row AND the steam readout shows the heater is off

#### Scenario: The readout follows the veto
- **WHEN** the user selects the "Heater off" entry
- **THEN** the steam readout shows the heater is off

### Requirement: Waking restores the pre-sleep heater state

Going to sleep SHALL NOT change the persistent heater inputs (settings, selected pitcher, active recipe). On waking, the system SHALL re-assert the resolved target to the machine. When an auto-load recipe is configured, the recipe it activates SHALL decide the heater. Permission granted by an event SHALL NOT survive sleep.

#### Scenario: A cold machine stays cold
- **WHEN** the machine slept with the heater off and no auto-load recipe is configured
- **THEN** it wakes with the heater off

#### Scenario: A warm machine comes back warm
- **WHEN** the machine slept with Keep warm when idle on
- **THEN** it wakes with the heater warming again

#### Scenario: A cleared transient flag is re-asserted
- **WHEN** the transient steam-off toggle was set, the machine slept (which clears it), and the machine wakes
- **THEN** the resolved target is re-sent, so the heater returns to what the settings say rather than staying at the value the machine happened to retain

#### Scenario: An auto-load recipe decides instead
- **WHEN** an auto-load recipe is configured and the machine wakes
- **THEN** that recipe is activated and the heater follows it

### Requirement: A single derivation of the steam target

Exactly one function SHALL compute the steam heater state and target temperature from the settings, effective pitcher, transient flag and active recipe. Every path that writes shot settings to the machine or reports heater state SHALL call it, and no second derivation SHALL exist. The target SHALL be the effective pitcher's temperature, falling back to the global steam temperature when it has none.

#### Scenario: A profile upload cannot contradict the heater state
- **WHEN** a recipe activation warms the heater and a deferred profile upload completes afterwards
- **THEN** the uploaded shot settings carry the same steam target, not a separately derived one

#### Scenario: The pitcher's temperature is used
- **WHEN** the effective pitcher carries a temperature different from the global steam temperature and the heater is permitted to be warm
- **THEN** the machine is commanded to the pitcher's temperature

### Requirement: A single built-in "Heater off" pitcher entry

The pitcher row SHALL contain exactly one built-in "Heater off" entry, present for every user, whose displayed name is translated. Users SHALL NOT be able to create, rename, remove or reorder it, and SHALL NOT be able to create additional entries that turn the heater off. Selecting it SHALL veto the heater.

Its identity SHALL be an off marker, never its displayed name, so that a recipe referencing it resolves correctly regardless of the app's language.

#### Scenario: Everyone has it
- **WHEN** a user opens the pitcher row on a fresh install
- **THEN** a "Heater off" entry is present alongside the default pitchers

#### Scenario: It cannot be duplicated
- **WHEN** a user adds a new pitcher preset
- **THEN** no option to create a heater-off preset is offered, on any surface including MCP

#### Scenario: Language change does not break a recipe
- **WHEN** a recipe carrying the off marker is activated after the app's language has changed
- **THEN** it resolves to the built-in entry and the heater is turned off — no preset is created

#### Scenario: It cannot be edited away
- **WHEN** a user attempts to rename, delete, reorder or edit the built-in entry
- **THEN** the operation is refused and the entry is unchanged

### Requirement: Migration off user-created heater-off presets

On first run, the system SHALL remove every disabled steam pitcher preset, remap `selectedSteamPitcher` so surviving presets keep their identity, and rewrite any recipe whose steam block names a removed preset to carry the off marker. A selection that was a removed preset SHALL end on the built-in "Heater off" entry. The migration SHALL be idempotent and SHALL NOT rewrite shot history.

#### Scenario: The heater does not silently turn on at upgrade
- **WHEN** a user's selected pitcher was their own Off preset
- **THEN** after migration the selection is the built-in "Heater off" entry and the heater is still off

#### Scenario: Surviving presets keep their identity
- **WHEN** a disabled preset sat between two real presets and the user was selecting the later one
- **THEN** after migration the same real preset is selected

#### Scenario: Recipes are rewritten, not orphaned
- **WHEN** a recipe's steam block named a removed disabled preset
- **THEN** the recipe carries the off marker and activating it turns the heater off, creating no preset

#### Scenario: History is preserved
- **WHEN** a stored shot's steam snapshot names a removed preset
- **THEN** the shot is unchanged, and promoting it to a recipe does not create a preset

#### Scenario: A stored snapshot naming a removed preset is tolerated

- **WHEN** a stored shot's steam snapshot names a preset that no longer exists
- **THEN** the shot SHALL display without error and no preset SHALL be created

