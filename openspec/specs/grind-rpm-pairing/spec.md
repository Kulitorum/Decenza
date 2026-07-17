# grind-rpm-pairing Specification

## Purpose
TBD - created by archiving change grind-widget-observed-step. Update Purpose after archive.
## Requirements
### Requirement: Grinder RPM SHALL travel with the grind setting across all shot surfaces

For variable-RPM grinders the dial-in is two values — the burr grind setting and the motor RPM. Wherever the app serializes, projects, reads, accepts, or displays a shot's grind setting, it SHALL carry the sibling RPM when one is recorded (`rpm > 0`), emitted/shown sparsely so non-RPM shots are unchanged. Storage structs and DB columns already pair the two; this requirement closes the read/projection/display/input gaps.

#### Scenario: ShotProjection JSON round-trip preserves RPM

- **GIVEN** a shot with grind `2.4` and RPM `1400`
- **WHEN** it is serialized via `ShotProjection::toVariantMap`/`toJsonObject` and read back via `fromVariantMap`
- **THEN** the serialized form SHALL include `rpm: 1400` alongside `grinderSetting`
- **AND** the round-tripped projection SHALL retain `rpm: 1400` (not reset to 0)

#### Scenario: MCP shot reads carry RPM

- **GIVEN** a shot with a recorded RPM
- **WHEN** it is returned by `shots_get_detail`, `shots_compare`, or `shots_list`
- **THEN** the response SHALL include `rpm` next to `grinderSetting`

#### Scenario: MCP shot/bag/settings inputs accept RPM

- **WHEN** `shots_update`, `bag_create`, `bag_update`, or `settings_set` accepts a grind field
- **THEN** it SHALL also accept the sibling RPM field, persisted through the existing storage path

#### Scenario: Start-a-shot accepts an independent RPM override

- **WHEN** `machine_start_espresso` (and `ProfileManager::activateBrewWithOverrides`) accepts a grind override
- **THEN** it SHALL also accept an optional RPM override, applied independently of grind and left untouched when absent

#### Scenario: ShotServer edit forms accept RPM

- **WHEN** the ShotServer shot, bag, or recipe HTML edit form is shown
- **THEN** it SHALL include an RPM input alongside the grind field (loaded and saved), and the backend SHALL persist it

#### Scenario: Shot comparison exposes an RPM row

- **GIVEN** two compared shots pulled at different RPMs on the same grind setting
- **WHEN** the comparison table is shown
- **THEN** an RPM row SHALL be present distinguishing them (shown when any compared shot has an RPM)

#### Scenario: Visualizer metadata edit does not drop RPM

- **GIVEN** a shot uploaded to the Visualizer whose `grinder_setting` was sent as "2.4 1400rpm"
- **WHEN** the shot's metadata is later edited and PATCHed
- **THEN** the PATCH SHALL send the RPM-bearing form (not bare "2.4"), preserving the RPM on the Visualizer

#### Scenario: Non-RPM shot shows no RPM

- **GIVEN** a shot with no recorded RPM
- **WHEN** any of the above surfaces render or serialize it
- **THEN** no RPM value SHALL be added

