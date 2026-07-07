# coffee-bag-model Delta

## MODIFIED Requirements

### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield override: `yieldOverrideG` (double, 0 = none) — the bean's override of the active profile's target weight, NOT a standalone target. 0 means the bag follows the profile default.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string), `visualizerSyncPending` (bool, default false — a bag edit failed to push and awaits retry)

The `beanBaseData` blob SHALL be valid **without** a canonical `id`: a manual bag may carry user-entered detail keys (`origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, `link`, `degree`) while remaining unlinked (`isLinked` stays defined solely by a non-empty `id`). A linked blob additionally carries a `canonical` sub-object — the pristine entry snapshot for revert — which consumers of the flat working keys ignore and shot snapshots carry along unchanged.

#### Scenario: Bag creation with full canonical data
- **WHEN** a user creates a bag from a Bean Base canonical result
- **THEN** the bag SHALL store `beanBaseId`, `beanBaseData` (origin, variety, process, harvest, tasting, producer, elevation, canonical_roaster_id), `roasterName`, `coffeeName`, and the user-entered `roastDate`

#### Scenario: Bag creation without canonical data
- **WHEN** a user creates a bag via manual entry
- **THEN** the bag SHALL store only user-entered fields; `beanBaseId` SHALL be null
- **AND** `beanBaseData` SHALL be null unless the user entered bean details, in which case it SHALL carry those keys with no `id`

#### Scenario: Unlinked blob does not read as linked
- **WHEN** a bag's `beanBaseData` carries detail keys but no `id`
- **THEN** `isLinked` SHALL be false and no canonical id SHALL be sent on shot PATCH for it

## ADDED Requirements

### Requirement: visualizer_sync_pending column
A schema migration SHALL add a `visualizer_sync_pending INTEGER NOT NULL DEFAULT 0` column to `coffee_bags`, set when an edit-time Visualizer push fails retryably and cleared on successful push (see `visualizer-coffee-management`). The column SHALL survive backup restore and device transfer like every other bag column.

#### Scenario: Migration adds the column
- **WHEN** the schema migration runs on an existing database
- **THEN** every existing bag SHALL have `visualizer_sync_pending = 0`
