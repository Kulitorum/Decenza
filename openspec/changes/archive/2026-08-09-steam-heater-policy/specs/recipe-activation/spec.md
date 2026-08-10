## REMOVED Requirements

### Requirement: Steam settings write on recipe switch with a held heater state

**Reason**: The `hasMilk` heater hold is retired. It granted the heater permission to be warm from a *parked* state, and users park a recipe as the machine's resting configuration rather than choosing one per drink — so the hold silently converted "don't keep the heater warm" into "always warm" for anyone with a milk recipe left selected. It also overrode the user's own setting, which is the behaviour this change exists to remove. Its three hold-specific scenarios go with it.

**Migration**: Replaced by "Steam settings write on recipe switch, heater follows the user's settings" below, plus the permission/veto model in the new `steam-heater-policy` capability. Users who relied on the hold get the same warm heater from **Keep warm when idle**, which is on by default and is what `keepSteamHeaterOn: true` migrates to; users who had `keepSteamHeaterOn: false` and a parked milk recipe lose a warm heater they never asked for.

## ADDED Requirements

### Requirement: Steam settings write on recipe switch, heater follows the user's settings

Activating a recipe SHALL write its steam block into the live brew settings (propagating to the DE1 as today) at activation time, not at shot start, and SHALL apply the selected pitcher's stored values — duration, flow and temperature — not merely record its index.

The recipe's pitcher SHALL be an **override** of the standing pitcher rather than a write to it: while the recipe is active and **Let the recipe decide** is on, the recipe's pitcher is the effective pitcher; deactivating SHALL restore the standing pitcher without the recipe having modified it.

Activating a recipe SHALL NOT grant the steam heater permission to be warm. A recipe that uses steam SHALL warm the heater when its **shot starts**, and only when **Let the recipe decide** is on; that permission SHALL be revoked when the machine returns to Idle. A recipe carrying the "Heater off" marker, or carrying no pitcher, SHALL veto the heater immediately on activation when **Let the recipe decide** is on.

`hasMilk` SHALL NOT cause the heater to be warmed and SHALL NOT override the user's settings. Whether a recipe uses steam SHALL be determined by `hasMilk` or by the presence of a pitcher that is not the off marker.

#### Scenario: Milk recipe selected
- **WHEN** a recipe with a milk pitcher is activated
- **THEN** its steam temperature, flow and timeout apply immediately from that pitcher, and the heater state follows the user's settings — it is not warmed by the act of selecting

#### Scenario: Milk recipe's shot warms the heater
- **WHEN** Keep warm when idle is off, Let the recipe decide is on, a milk recipe is active, and its shot starts
- **THEN** the steam heater begins warming to that recipe's pitcher temperature

#### Scenario: Warmth from a shot is released at Idle
- **WHEN** the shot and any steaming finish and the machine returns to Idle, with Keep warm when idle off
- **THEN** the steam heater is turned off

#### Scenario: A recipe without a pitcher turns the heater off
- **WHEN** Let the recipe decide is on and a recipe carrying no pitcher is activated
- **THEN** the steam heater is turned off immediately

#### Scenario: Deactivating restores the standing pitcher
- **WHEN** a recipe whose pitcher differs from the standing pitcher is deactivated
- **THEN** the standing pitcher is in effect again, unmodified by the activation

#### Scenario: The recipe is ignored when the user says so
- **WHEN** Let the recipe decide is off and the active recipe names no pitcher
- **THEN** activating it leaves the effective pitcher and the heater state unchanged

#### Scenario: An explicit pitcher choice is a selection, not an inference
- **WHEN** a recipe names a pitcher — a real one, or the built-in "Heater off"
- **THEN** activating it applies that pitcher whatever the two settings say, because the user chose it when they built the recipe
- **AND** a recipe carrying "Heater off" leaves the heater cold whatever the two settings say
- **AND** starting steam on it behaves exactly as it does with "Heater off" standing: the last real pitcher's duration, flow and temperature, with a message saying so

#### Scenario: Milk-less recipe with keep-warm user
- **WHEN** a recipe with no milk is activated, the user has Keep warm when idle on and Let the recipe decide off
- **THEN** the heater stays warm

#### Scenario: Heater state survives a profile upload
- **WHEN** a recipe activation sets the heater state and the profile upload it triggered completes afterwards
- **THEN** the heater state is unchanged by that upload
