# mcp-server Delta

## ADDED Requirements

### Requirement: Bag tools carry bean-detail fields

The `bag_update` tool SHALL accept the bean-detail parameters `origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, and `link` (product URL), merging them into the bag's `beanBaseData` blob with the same merge semantics as the bag editor (preserve link identity keys; clearing a value removes the key). A `bag_update` carrying detail fields SHALL trigger the same Visualizer edit-push as an editor save. `bag_list` and the `bag_update` response SHALL emit the stored detail fields as human-readable strings.

#### Scenario: Agent adds a URL and tasting notes
- **WHEN** an agent calls `bag_update` with `link` and `tastingNotes` for a bag
- **THEN** the blob SHALL carry both values, the response SHALL echo them
- **AND** the Visualizer push SHALL fire if the bag has a `visualizerBagId`

#### Scenario: Detail fields visible in bag_list
- **WHEN** an agent calls `bag_list`
- **THEN** each bag with stored details SHALL include them (origin, variety, process, tasting notes, link, etc.)

#### Scenario: Clearing a detail field
- **WHEN** an agent calls `bag_update` with `region` set to an empty string
- **THEN** the `region` key SHALL be removed from the blob and absent from subsequent reads
