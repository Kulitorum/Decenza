## MODIFIED Requirements

### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `storageHint` (nullable string enum: `counter` / `airtight` / `vacuum-sealed` / `fridge` — the **out-of-freezer storage plan**: how the beans are kept when NOT in the freezer. It is forward-looking on a frozen bag ("when this is thawed, it goes in a vacuum jar") and descriptive on a thawed or never-frozen one, so it is orthogonal to the freezer axis and SHALL be settable and retained in every freeze state. The enum has no `"frozen"` value — frozen state is determined solely by `frozenDate` being set — but the two fields answer different questions and therefore cannot disagree; `storageHint` SHALL NOT be cleared, hidden, or otherwise suppressed on account of `frozenDate`), `openedDate` (nullable date — the non-frozen analogue of `defrostDate`: when the current portion started being actively used/exposed to room temperature), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield spec: `yieldValue` (double) + `yieldMode` (`none` | `absolute` | `ratio`) — the bean's **own** yield, a first-class anchor rather than a deviation from the active profile's target weight (`yield-anchor`). `mode = none` means the bag designs no yield and the ladder falls through to the profile. The legacy `yieldOverrideG` column is converted by migration and left dead in place.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string), `visualizerSyncPending` (bool, default false — a bag edit failed to push and awaits retry)

The yield spec SHALL be a local-only field, same as the grinder/dose fields — it SHALL NOT be included in `touchesVisualizerFields()`, so an anchor edit never triggers a bag PATCH. `storageHint` and `openedDate` are local-only for the same reason (`bean-freshness-followup`): neither is included in `touchesVisualizerFields()` and neither is pushed to a Visualizer bag.

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

#### Scenario: storageHint and openedDate are never synced to Visualizer
- **WHEN** a bag with `storageHint = "airtight"` and `openedDate` set is edited
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only those two keys
- **AND** no Visualizer PATCH SHALL be triggered by that edit alone

#### Scenario: A frozen bag retains its out-of-freezer storage plan
- **WHEN** a bag has `frozenDate` set and `storageHint = "vacuum-sealed"`
- **THEN** both values SHALL be stored and returned unchanged
- **AND** no read or write path SHALL clear `storageHint` on account of `frozenDate` being set

#### Scenario: A bag's yield spec is never synced to Visualizer
- **WHEN** a bag's yield anchor is changed
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only the yield spec keys, and no network PATCH SHALL be issued

#### Scenario: A bag cannot hold both an absolute yield and a ratio
- **WHEN** a bag holding `{40.0, absolute}` is given a ratio of 1:3 from any surface (Change Beans dialog, Brew Settings, MCP `bag_update`, web bag editor)
- **THEN** it holds `{3.0, ratio}` and retains no absolute yield
