## ADDED Requirements

### Requirement: The store SHALL hold every grinder's derived step in memory

`ShotHistoryStorage` SHALL derive the grind step and the RPM step for **every** grinder model in a
single background pass and hold the results in memory, keyed by case- and whitespace-folded model
name. The pass SHALL run when the store becomes ready and again whenever shot history is
invalidated.

Deriving all grinders SHALL be one query, not one query per model: the cost is dominated by the
scan or index walk, so the whole set costs what a single model costs.

The map SHALL carry an explicit all-grinders entry, derived from the full cross-grinder history, so
a reader with no grinder model selected is served the same way as any other reader.

#### Scenario: All grinders derived in one pass

- **GIVEN** a shot history containing settings for several grinder models
- **WHEN** the store completes its derivation pass
- **THEN** the map SHALL contain a grind step and an RPM step for each model that has derivable history
- **AND** it SHALL contain an all-grinders entry derived from the full history

#### Scenario: Model lookup folds case and whitespace

- **GIVEN** history recorded against a grinder model stored as `" Zero "`
- **WHEN** the step is requested for `"zero"`
- **THEN** the same entry SHALL be returned
- **AND** the folding SHALL match the identity folding used by the equipment lookup

### Requirement: Step reads SHALL be synchronous and SHALL NOT depend on the distinct-value cache

`grindStepForGrinder(model)` and `grindRpmStepForGrinder(model)` SHALL answer from the in-memory
map and SHALL return without performing a database query on the calling thread. They SHALL NOT
consult the async distinct-value cache, and SHALL NOT return `0` on the grounds that a cache entry
is absent or a fetch is in flight.

They SHALL return `0` only when the grinder has no derivable history — fewer than two distinct
numeric settings, per `deriveGrindStep()` — leaving the caller's documented fallback (`1.0` for
grind, `50` for RPM) to mean that and nothing else.

#### Scenario: Value survives an invalidation

- **GIVEN** a grinder whose 28 distinct observed settings derive a step of `0.25`
- **WHEN** shot history is invalidated by a shot save, shot edit or equipment change
- **AND** the step is read again immediately, before any rebuild has completed
- **THEN** the read SHALL return `0.25`
- **AND** it SHALL NOT return `0`

#### Scenario: A reader that cannot wait is still correct

- **GIVEN** the ShotServer grind-candidates endpoint, which answers a one-shot HTTP request with no
  binding to re-evaluate
- **WHEN** it requests the step for a grinder with derivable history
- **THEN** it SHALL receive the derived step in that same request
- **AND** the response SHALL NOT be built on the `1.0` fallback

#### Scenario: Thin history still falls back

- **GIVEN** a grinder whose history holds fewer than two distinct numeric settings
- **WHEN** the step is read
- **THEN** the store SHALL return `0`
- **AND** the caller SHALL apply its documented fallback

### Requirement: The derivation SHALL be answered from a covering index

The `shots` schema SHALL carry an index over `(equipment_id, grinder_setting, rpm)` so the
derivation is satisfied without visiting row pages. The index SHALL be created for new databases
and added to existing ones by schema migration.

This bounds the derivation's cost by the number of distinct dial-in values rather than by the size
of the per-shot blobs (`debug_log`, `profile_json`, `steam_json`), which are what the table's
growth actually consists of.

#### Scenario: Query plan uses the covering index

- **WHEN** the all-grinder derivation query is planned
- **THEN** the plan SHALL report a covering-index search over `shots`
- **AND** it SHALL NOT report a full table scan of `shots`

#### Scenario: Existing database gains the index

- **GIVEN** a database created before this change
- **WHEN** the store opens it and runs migrations
- **THEN** the index SHALL exist afterwards
- **AND** re-running the migration SHALL be harmless

### Requirement: One derivation SHALL serve the widget, the web endpoint and the AI payload

There SHALL be exactly one code path deriving a grinder's step. The AI dialing payload's
`grinderContext.stepSize`, the grind picker's step, and the ShotServer grind-candidates step SHALL
all read the same derivation, so they cannot report different numbers for the same grinder.

The separate synchronous derivation previously used by the dialing payload SHALL be removed rather
than left in place agreeing by convention.

#### Scenario: Widget and AI payload agree by construction

- **GIVEN** a grinder whose history derives a step of `0.25`
- **WHEN** the grind picker's step and `grinderContext.stepSize` are both obtained
- **THEN** both SHALL be `0.25`
- **AND** both SHALL have come from the same derivation, not from two queries that happen to match

#### Scenario: Cross-thread read is deliberate

- **GIVEN** the dialing context is built on a background thread
- **WHEN** it reads the derived steps
- **THEN** the read SHALL be made safe against concurrent rebuild by an explicit mechanism
- **AND** the chosen mechanism SHALL be stated in a comment at the point of use

### Requirement: The log SHALL record which state answered a step request

The store's grind-step narration SHALL distinguish a value **derived** in this pass, a value
**held** from a previous derivation, and a grinder with **no derivable history**, alongside the
sample count and the resulting number. Repeats SHALL continue to be deduped.

A log that records only the number cannot separate a correct `1.0` from a `1.0` standing in for a
lost value — which is the confusion this capability exists to end, and which cost #1713 a
25,720-line log that could not answer the question.

#### Scenario: Fallback is distinguishable in a submitted log

- **GIVEN** a submitted debug log
- **WHEN** a reader looks for why a grinder stepped in whole numbers
- **THEN** the log SHALL state whether the step was derived, held, or absent for want of history
- **AND** the sample count behind that answer SHALL be present
