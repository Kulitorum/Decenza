# profile-usage-history Specification

## Purpose
Derives per-profile usage — last shot time and shot count — from shot history so the picker and favorites ordering can rank profiles by real use without a new stamp on every load.

## Requirements

### Requirement: Usage derived from shot history
The app SHALL compute, per profile title, the most recent shot timestamp and the shot count from the shot history, off the main thread, at startup and after every shot save. The result SHALL be keyed by profile title, the same string shot history records. Profiles with no shots SHALL be absent from the result and treated as never used.

#### Scenario: Refresh after save
- **WHEN** a shot is saved for profile "Blooming Espresso"
- **THEN** the usage for "Blooming Espresso" reflects that shot's time and an incremented count before the next picker open

#### Scenario: Renamed profile
- **WHEN** a profile is renamed and no shot has been pulled under the new title
- **THEN** the picker treats it as never used until the next shot

### Requirement: Off the main thread
The usage query SHALL run on a worker thread and deliver its result by queued callback. The main thread SHALL NOT block on it; consumers SHALL render with the last delivered result until a new one arrives.

#### Scenario: Picker opens during query
- **WHEN** the picker opens before the first result arrives
- **THEN** it renders every card as never used and re-renders when the result lands
