## ADDED Requirements

### Requirement: Coffee bag upload includes canonical Bean Base identifiers when available

When a Visualizer coffee_bag is created or updated for a preset that carries a non-empty `beanBaseId`, the upload payload SHALL include `coffee_bag[canonical_coffee_bag_id]` and `coffee_bag[canonical_roaster_id]` set to the Bean Base bean and roaster ids respectively.

#### Scenario: Bag upload for a Bean-Base-linked preset
- **WHEN** the active preset has `beanBaseId = "5188"` and `beanBaseRoasterId = "42"`
- **AND** a coffee_bag create / update is sent to Visualizer
- **THEN** the payload includes `coffee_bag[canonical_coffee_bag_id] = "5188"`
- **AND** the payload includes `coffee_bag[canonical_roaster_id] = "42"`

#### Scenario: Bag upload for an unlinked preset
- **WHEN** the active preset has empty `beanBaseId`
- **AND** a coffee_bag create / update is sent to Visualizer
- **THEN** the payload does NOT include `coffee_bag[canonical_coffee_bag_id]` or `coffee_bag[canonical_roaster_id]`
- **AND** the existing free-text `roaster` and `name` fields behave exactly as before this change

### Requirement: Coffee bag upload includes cached attribute fields

When the active preset carries cached Bean Base attributes (from a prior match), the upload payload SHALL include them in their corresponding Visualizer coffee_bag fields.

#### Scenario: Cached attributes are forwarded
- **WHEN** the preset has cached `origin = "Colombia"`, `region = "Huila"`, `variety = "Caturra"`, `process = "Washed"`, `producer = "Finca Buenos Aires"`, `productUrl = "https://getprodigal.com/..."`, `tastingNotes = "..."`
- **AND** a coffee_bag create / update is sent
- **THEN** the payload includes `coffee_bag[country] = "Colombia"`, `coffee_bag[region] = "Huila"`, `coffee_bag[variety] = "Caturra"`, `coffee_bag[process] = "Washed"`, `coffee_bag[producer] = "Finca Buenos Aires"`, `coffee_bag[url] = "https://getprodigal.com/..."`, `coffee_bag[tasting] = "..."`
- **AND** missing cached fields are omitted (not sent as empty strings)

### Requirement: First sync after Bean Base configuration deduplicates existing bags

The first coffee_bag creation attempt after a user configures their Bean Base API key SHALL be preceded by a single `GET /api/coffee_bags` to fetch the user's existing bag list, and SHALL reuse a matching bag's UUID instead of creating a duplicate.

#### Scenario: A matching bag already exists on Visualizer with the same canonical id
- **WHEN** the user picks a Bean Base entry for a preset
- **AND** the user already has a Visualizer coffee_bag with `canonical_coffee_bag_id` equal to the picked Bean Base bean id
- **THEN** the existing bag's UUID is reused on the next shot upload
- **AND** no `POST /api/coffee_bags` is sent for this preset
- **AND** the existing bag's fields are updated via PATCH only if their values differ from the cached Bean Base attributes

#### Scenario: A matching bag exists by roaster+name but not by canonical id
- **WHEN** the user picks a Bean Base entry for a preset
- **AND** the user has a Visualizer coffee_bag matching by `roaster` and `name` strings but no `canonical_coffee_bag_id`
- **THEN** the existing bag's UUID is reused
- **AND** a PATCH is sent setting `canonical_coffee_bag_id` and `canonical_roaster_id` on the existing bag
- **AND** a log warning records that the match was by string, not by canonical id

#### Scenario: No matching bag exists
- **WHEN** the user picks a Bean Base entry for a preset
- **AND** no existing Visualizer coffee_bag matches by canonical id or by roaster+name
- **THEN** a single `POST /api/coffee_bags` is sent with all canonical + cached fields
- **AND** the returned bag UUID is persisted on the preset

#### Scenario: Backfill is not triggered for unlinked presets
- **WHEN** the user configures a Bean Base API key for the first time
- **AND** has existing presets without a `beanBaseId`
- **THEN** no `GET /api/coffee_bags` call is made until the user actually matches one of those presets to a Bean Base entry
