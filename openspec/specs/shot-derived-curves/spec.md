# shot-derived-curves Specification

## Purpose

Defines resistance, conductance, Darcy resistance, and their derivative as values always derived from a shot's own pressure/flow samples, computed fresh on every load rather than trusted from storage, so a formula fix applies to every existing shot automatically instead of only to shots recorded after the fix.

## Requirements

### Requirement: Derived curves are recomputed from pressure/flow on every load

Whenever a shot's sample data (pressure and flow) is loaded, the system SHALL recompute resistance, conductance, Darcy resistance, and the conductance derivative from that shot's own pressure/flow samples, using the current formulas, rather than trusting whatever values are already stored for those curves.

#### Scenario: A shot recorded with a since-fixed formula displays correctly

- **GIVEN** a stored shot whose stored resistance curve was computed by a formula that has since been corrected
- **WHEN** the shot is loaded for display
- **THEN** the resistance curve shown SHALL match what the current formula produces from that shot's stored pressure/flow samples
- **AND** it SHALL NOT match the stale formula's output

#### Scenario: A shot with too few samples skips recompute without error

- **GIVEN** a stored shot whose pressure and flow sample arrays have fewer than 3 usable points
- **WHEN** the shot is loaded
- **THEN** the recompute step SHALL be skipped, leaving whatever derived-curve values were already parsed from storage (empty for a shot whose storage never carried them)
- **AND** no error SHALL be raised

### Requirement: Recomputed curves are persisted back only when they differ from storage

After recomputing a shot's derived curves, the system SHALL compare the recomputed values to what is currently stored for that shot. When any value differs beyond floating-point noise, the system SHALL overwrite the shot's stored derived curves with the recomputed values. When every recomputed value matches storage, the system SHALL NOT write to storage.

#### Scenario: A stale shot is corrected once and stays corrected

- **GIVEN** a stored shot whose stored resistance curve does not match what the current formula produces
- **WHEN** the shot is loaded
- **THEN** the shot's stored resistance curve SHALL be overwritten with the recomputed values
- **AND** loading the same shot again afterward SHALL find the recomputed values already matching storage and SHALL NOT write again

#### Scenario: An already-correct shot is never rewritten

- **GIVEN** a stored shot whose stored derived curves already match what the current formula produces
- **WHEN** the shot is loaded
- **THEN** the system SHALL NOT write to the shot's storage

### Requirement: Recompute-and-heal applies uniformly, not only to legacy shots

The recompute-and-persist behavior SHALL apply to every shot with sample data on load, regardless of when the shot was recorded or which storage schema version produced it. There SHALL NOT be a separate code path that only recomputes derived curves for shots missing a particular stored field.

#### Scenario: A recently recorded shot is covered the same way as an old one

- **GIVEN** a shot recorded with the current app version, with all derived curves already correctly stored
- **WHEN** the shot is loaded
- **THEN** it SHALL go through the same recompute-and-compare path as an old shot
- **AND** the outcome SHALL be no write, since the stored values already match
