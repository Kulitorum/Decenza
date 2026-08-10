## MODIFIED Requirements

### Requirement: Steam block with pitcher snapshot

A recipe's steam block SHALL contain: `hasMilk` (bool), milk weight (g), a pitcher snapshot (name and volume copied by value — never a reference into the global pitcher preset list), and steam temperature, flow, and timeout values.

The block MAY instead carry an **off marker** in place of a pitcher snapshot, meaning the recipe wants the steam heater off. The marker SHALL be a stable field, never the built-in entry's displayed name, because that name is translated. A block carrying the off marker SHALL NOT carry a pitcher name, and SHALL NOT be resolved by name lookup against the preset list.

A block that carries neither a pitcher snapshot nor the off marker SHALL be treated as wanting the heater off, the same as the marker.

When a block names a pitcher that no longer exists, the recipe SHALL still activate. Re-creating the named pitcher from the block's own values is permitted only for a real pitcher; a block carrying the off marker SHALL never cause a preset to be created.

#### Scenario: Pitcher preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global steam pitcher presets after a recipe was saved
- **THEN** the recipe's steam behavior is unchanged, because the pitcher was snapshotted by value

#### Scenario: The off marker survives a language change
- **WHEN** a recipe carrying the off marker is activated in a different app language from the one it was saved in
- **THEN** it resolves to the built-in "Heater off" entry, and no pitcher preset is created

#### Scenario: A block with no pitcher wants the heater off
- **WHEN** a recipe's steam block carries neither a pitcher snapshot nor the off marker
- **THEN** activating it with "Let the recipe decide" on turns the steam heater off

#### Scenario: An unresolvable pitcher name does not manufacture an off preset
- **WHEN** a shot's steam snapshot names a pitcher that no longer exists and the shot is promoted to a recipe
- **THEN** the promotion succeeds and creates no preset
