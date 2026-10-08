# profile-import-beverage-inference Specification

## Purpose
Gives imported profiles a `beverage_type` when the source carried none, so the picker's beverage filters and the beverage-group rules see them correctly.

## Requirements

### Requirement: Infer a missing beverage type on import
When a profile imported from Visualizer, a tablet, or a file has an empty or absent `beverage_type`, the app SHALL set one before saving, using the first matching rule: (1) title keywords; (2) shape; (3) otherwise `espresso`. A non-empty source `beverage_type` SHALL never change, and profiles already on disk SHALL NOT be re-tagged.

#### Scenario: Keyword wins over shape
- **WHEN** an imported profile titled "Cold Brew Tea" has no beverage type
- **THEN** it is saved as `tea_portafilter`

#### Scenario: Low pressure without keyword
- **WHEN** an imported profile titled "Slow Long" has no beverage type and no step exceeds 2 bar
- **THEN** it is saved as `pourover`

#### Scenario: Explicit tag untouched
- **WHEN** an imported profile carries `beverage_type: espresso` but every step is under 1 bar
- **THEN** it is saved as `espresso`

#### Scenario: Existing files untouched
- **WHEN** the app starts with previously imported untagged profiles on disk
- **THEN** their files are not modified

### Requirement: Title keywords infer the beverage type
Title keywords SHALL be checked in the order listed: clean, flush, backflush or descale → `cleaning`; calibrat → `calibrate`; tea, steep, chai or matcha → `tea_portafilter`; pour over, pourover, filter, v60, aeropress, chemex, cold brew, drip or immersion → `pourover`.

#### Scenario: Keyword order decides between matches
- **WHEN** an imported profile titled "Pour Over Tea" has no beverage type
- **THEN** it is saved as `tea_portafilter`, because the tea keyword is checked first

### Requirement: Shape infers pourover when no keyword matches
With no keyword match, a profile SHALL be `pourover` when its highest pressure across steps (setpoint for pressure steps, pressure limit for flow steps) is under 3 bar, or when any step temperature is at or below 40 °C. Otherwise it SHALL be `espresso`.

#### Scenario: Cool step without keyword
- **WHEN** an imported profile with no keyword match has a step at 40 °C
- **THEN** it is saved as `pourover`
