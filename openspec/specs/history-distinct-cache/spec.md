# history-distinct-cache Specification

## Purpose
TBD - created by archiving change keep-grind-step-across-cache-refresh. Update Purpose after archive.
## Requirements
### Requirement: Distinct-value getters SHALL read the live database

Every `getDistinct*()` getter SHALL run its query against the database on each call and return the
result. No value SHALL be cached between calls, and no getter SHALL return an empty list to signal
"not loaded yet".

Each is a small bounded `SELECT DISTINCT` on a discrete user action — a dialog or picker opening.
Measured on a real 18.5 MB database: 0.36–1.9 ms per column.

#### Scenario: A write is visible to the next read

- **GIVEN** a grinder with existing observed settings
- **WHEN** a shot is written with a setting not previously seen
- **AND** the distinct settings are read again
- **THEN** the new value SHALL be present, with no signal in between

#### Scenario: A composite key survives a data change

- **GIVEN** distinct settings read for a specific grinder model
- **WHEN** any shot is saved, edited or deleted
- **THEN** the next read for that model SHALL still return its settings

### Requirement: The column name in a distinct query SHALL be allow-listed

A column name is interpolated into the SQL text because SQLite cannot bind an identifier, so it
SHALL be rejected unless it appears in the allowed-column list.

#### Scenario: An unknown column is refused

- **WHEN** a distinct query is requested for a column not on the allow-list
- **THEN** it SHALL return empty and warn, without reaching the database

