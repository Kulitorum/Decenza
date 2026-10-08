## ADDED Requirements

### Requirement: shots_update reaches every field the shot pages edit
`shots_update` SHALL accept every shot field the app and web shot pages can set, including a bag pick (`bagId`, copying the bag's snapshot), the equipment package, the taste axes and the storage dates. Storage dates SHALL be validated by the shared bag rules against the shot's own dates, and a refused value SHALL return its reason rather than be dropped.

#### Scenario: Correcting a shot's storage dates
- **WHEN** a client sends `defrostDate` and `openedDate` that are in order and not in the future
- **THEN** the shot stores them

#### Scenario: A thaw before the freeze is refused
- **WHEN** a client sends a `defrostDate` earlier than the shot's `frozenDate`
- **THEN** the update is refused with the reason and nothing is written

#### Scenario: A bag pick refuses fields it would overwrite
- **WHEN** a client sends `bagId` together with `beanBrand` or a storage date
- **THEN** the update is refused and names the clashing fields
