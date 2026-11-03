# shot-water-results Specification

## Purpose
Records the added water belonging to an espresso-based drink sequence so saved drink history retains the dispensed amount and the water temperature setting independently of recipe intent.

## Requirements

### Requirement: Added water belongs to its drink's espresso shot
The system SHALL associate the water step of a sequenced Americano or long black with that sequence's espresso shot, independent of step order and save timing. Another operation, recipe selection or shot SHALL NOT receive its result. A sequence with no saved espresso SHALL NOT create a shot solely to store water.

#### Scenario: Americano water finishes after the shot saves
- **WHEN** an Americano's espresso has saved and its water step then completes
- **THEN** the water result is added to that espresso's saved shot

#### Scenario: Long black water comes first
- **WHEN** a long black dispenses water and then pulls its espresso
- **THEN** the espresso's saved shot includes that preceding water result

#### Scenario: Save completion arrives after the water result
- **WHEN** a sequence's water result becomes available before its espresso's asynchronous save completes
- **THEN** the result is retained and attached when that same shot's save succeeds

#### Scenario: A later operation starts
- **WHEN** another shot or standalone water pour starts while an earlier result is awaiting persistence
- **THEN** each result stays associated with its original sequence and cannot overwrite the later shot

#### Scenario: Long black espresso is skipped
- **WHEN** a long black's water finishes but its espresso is skipped, refused or discarded
- **THEN** no shot record is created for the water alone

### Requirement: Water amount records the pour result
The system SHALL save net added-water weight in grams from valid physical-scale readings when available. Otherwise it SHALL save a flow-derived volume in millilitres as an estimate, or mark the amount unavailable if neither is valid. It SHALL exclude cup and existing espresso weight and SHALL NOT substitute the configured amount or a frozen stop-trigger display value.

#### Scenario: Actual weight differs from the preset
- **WHEN** a vessel is configured for 100 g and its completed pour measures 103.2 g
- **THEN** the result records 103.2 g as scale-measured water
- **AND** the configured 100 g is not reported as the dispensed amount

#### Scenario: Water is added to espresso already in the cup
- **WHEN** an Americano dispenses into a cup containing espresso, including when the scale's tare acknowledgement is delayed
- **THEN** the result includes only the added water using the pour's net-weight reference

#### Scenario: A physical scale is unavailable
- **WHEN** the water step has usable flow data but no valid physical-scale measurement
- **THEN** the result records the integrated dispensed volume in millilitres with its flow-estimate source
- **AND** a virtual flow scale is not presented as a physical weighing

#### Scenario: Cup lift during completion
- **WHEN** the cup is lifted after water flow stops
- **THEN** the result retains the last valid water measurement rather than saving the lifted-cup reading

### Requirement: Water temperature records the setting used
The system SHALL snapshot the configured hot-water temperature at the start of the associated water pour and store it in Celsius. Presentation SHALL identify it as a setting, apply the user's temperature unit, and SHALL NOT describe it as measured outlet temperature or final drink temperature. Later recipe or vessel edits SHALL NOT change it.

#### Scenario: Temperature setting changes after the drink
- **WHEN** water starts with a configured temperature of 85 °C and the vessel is later edited to 90 °C
- **THEN** the saved result continues to report a water temperature setting of 85 °C

#### Scenario: Temperature setting is unavailable
- **WHEN** no valid configured temperature was captured for the water step
- **THEN** the result leaves temperature unavailable rather than borrowing a later setting or espresso temperature

### Requirement: Omitted and interrupted water are explicit
A water step known not to have started SHALL be recorded as not dispensed. A started pour interrupted by disconnect or another operation SHALL be marked interrupted and retain only valid captured measurements. An absent result SHALL mean unrecorded, not zero water. A normal user stop SHALL retain the amount poured without inventing the remainder of the target.

#### Scenario: Americano water is skipped or refused
- **WHEN** an Americano's water step is skipped or refused before flow starts
- **THEN** its espresso shot reports water not dispensed
- **AND** it does not report the configured amount as poured

#### Scenario: Water is stopped early
- **WHEN** the user stops a water pour at 60 g despite a configured amount of 100 g
- **THEN** its recorded dispensed amount is the measured 60 g

#### Scenario: Connection is lost during the water step
- **WHEN** the DE1 disconnects after some water has poured
- **THEN** the saved result is marked interrupted
- **AND** any retained amount is identified as partial rather than a completed total

### Requirement: Water results survive history transport without rewriting intent
Saved water results SHALL survive shot reload, metadata edits, backup and device transfer. Legacy shots SHALL keep an unavailable result without inferring actual water from their vessel snapshot. Recipe promotion SHALL continue to use the saved water specification, independently of the dispensed result. Espresso yield, ratio, TDS/EY and quality analysis SHALL remain espresso-only.

#### Scenario: Legacy shot has only a vessel snapshot
- **WHEN** a historical shot contains a configured 100 g water snapshot and no captured pour result
- **THEN** the dispensed amount is unavailable rather than 100 g or zero

#### Scenario: Result survives edits and transfer
- **WHEN** a water result is saved and the shot's rating is edited, then the shot is transferred to another device
- **THEN** its water amount, source, temperature setting and completion state remain intact

#### Scenario: Promotion preserves the intended recipe
- **WHEN** a shot measured 103.2 g of added water against a saved 100 g vessel specification and is promoted to a recipe
- **THEN** the promoted recipe retains the 100 g specification
- **AND** the source shot retains its 103.2 g result

#### Scenario: Espresso and water remain distinct
- **WHEN** a shot has 36 g espresso from an 18 g dose and 100 g added water
- **THEN** its espresso yield remains 36 g and its brew ratio remains 1:2
- **AND** the added water does not alter the extraction detectors or espresso TDS/EY calculation
