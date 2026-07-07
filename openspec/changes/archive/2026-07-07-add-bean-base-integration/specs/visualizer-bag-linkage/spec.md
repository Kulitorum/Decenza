## ADDED Requirements

### Requirement: Coffee bag upload includes canonical Bean Base identifiers when available

When a Visualizer coffee_bag is created or updated for a preset that carries a non-empty `beanBaseId`, the upload payload SHALL include `coffee_bag[canonical_coffee_bag_id]` and `coffee_bag[canonical_roaster_id]` set to the Bean Base bean and roaster ids respectively.

#### Scenario: Bag upload for a Bean-Base-linked preset
- **WHEN** the active preset has `beanBaseId = "a1b2c3d4-…"` (a Visualizer canonical UUID) and a canonical roaster id
- **AND** a coffee_bag create / update is sent to Visualizer
- **THEN** the payload includes `coffee_bag[canonical_coffee_bag_id]` = that UUID
- **AND** the payload includes `coffee_bag[canonical_roaster_id]` = the roaster id

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
