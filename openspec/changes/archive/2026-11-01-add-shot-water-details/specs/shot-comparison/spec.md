# Spec Delta

## MODIFIED Requirements

### Requirement: "What you changed" lists only inputs that differ from the base

"What you changed" SHALL list inputs differing between the base and any visible shot: profile, brew temperature, dose, target yield, grind, RPM, advisor shot-identity fields (grinder, burrs, basket, puck prep, bean, roast and storage dates), barista and available saved added-water temperature. Differing cells SHALL be marked; numeric inputs SHALL show Δ. Water temperature SHALL be labeled Water temp (set).

#### Scenario: Only the grind moved

- **GIVEN** two shots identical except grind setting 10 on the base and 9 on the other
- **WHEN** the comparison is shown
- **THEN** "What you changed" has one row, Grind, showing 10 and 9 with a Δ of −1
- **AND** no other input appears as a row

#### Scenario: Water temperature setting moved
- **WHEN** two drinks have saved water temperature settings of 85 °C and 90 °C
- **THEN** What you changed shows Water temp (set) as 85 °C and 90 °C with a +5 °C difference
- **AND** an identical water temperature setting follows the existing Unchanged presentation

### Requirement: "What happened" shows key metrics first, the rest on request

"What happened" SHALL show by default: duration, yield (with its target when set), ratio, time
to first drop, peak pressure and mean flow, each non-base value carrying its Δ against the base. It SHALL also show added-water amount rows by default when at least one visible shot has a recorded water outcome.
A "Show more" control SHALL reveal peak flow, average g/s, mean group temperature, temperature
sag, resistance, preinfusion and pour times, and TDS/EY when recorded, with Δs.

#### Scenario: Two dial-in shots

- **GIVEN** a base shot of 37.2 s and 53.3 g and another shot of 11.0 s and 13.3 g
- **WHEN** the comparison is shown
- **THEN** the other shot's duration shows a Δ of −26.2 s and its yield a Δ of −40.0 g

#### Scenario: Details are collapsed until asked for

- **WHEN** the comparison opens
- **THEN** resistance and temperature sag are not shown
- **AND** tapping "Show more" reveals them with their Δs

#### Scenario: A metric missing on one shot

- **WHEN** a shot has no weight data, so yield cannot be measured
- **THEN** its yield shows "—" with no Δ

#### Scenario: Different dispensed water amounts
- **WHEN** the base contains 100.0 g of measured added water and another completed drink contains 120.0 g
- **THEN** a default Added water amount row shows 100.0 g and 120.0 g with +20.0 g against the base
- **AND** the espresso Yield row retains its espresso-only values

## ADDED Requirements

### Requirement: Water comparison preserves measurement meaning
Water amount cells SHALL identify scale-measured grams or estimated millilitres. Numeric differences SHALL be shown only between complete, available amounts with matching units and measurement source. Interrupted amounts SHALL be labeled partial, and unknown amounts SHALL remain unavailable. Comparison SHALL NOT convert flow volume into measured weight or substitute a configured target.

#### Scenario: Comparing scale and flow results
- **WHEN** one drink has a scale result of 100 g and another a flow estimate of 120 mL
- **THEN** both amounts and their measurement sources are visible in their respective unit rows
- **AND** neither row shows a numeric difference between grams and millilitres

#### Scenario: Old snapshot versus measured result
- **WHEN** an old shot has a configured 100 g vessel snapshot without an actual result and another shot has 105 g measured water
- **THEN** the old shot's water amount is unavailable and the other shows 105 g
- **AND** no water-amount difference is calculated

#### Scenario: Interrupted result
- **WHEN** an interrupted drink records a partial 60 g beside a completed 100 g drink
- **THEN** the 60 g cell is marked partial with no numeric difference suggesting a completed total

#### Scenario: Step never started
- **WHEN** a shot records that its water step was skipped or refused
- **THEN** its water result states Not dispensed
- **AND** an unrecorded historical result remains distinguishable from that known absence

### Requirement: Water comparison data follows the selected shots on every current surface
Native and Wasm comparison SHALL use the same saved water results, temperature settings and difference rules. Existing shot-detail and MCP comparison responses SHALL expose additive water data with explicit units and source. Paging and rebasing SHALL preserve shot identity and recompute comparable differences. Missing legacy data SHALL NOT be manufactured.

#### Scenario: Paging and rebasing
- **WHEN** the user pages to another drink or changes the base
- **THEN** water values remain attached to their own shot and valid differences are recomputed against the selected base

#### Scenario: Native and browser agree
- **WHEN** the same drinks and base are selected in the native app and its Wasm browser client
- **THEN** both show the same water amount, source, set temperature and differences

#### Scenario: Programmatic comparison
- **WHEN** an MCP client compares drinks with captured added-water results
- **THEN** it receives the same water measurements, sources, completion states and set temperatures with unit-bearing fields
- **AND** water-amount differences follow the same availability and comparability rules as the shared UI
