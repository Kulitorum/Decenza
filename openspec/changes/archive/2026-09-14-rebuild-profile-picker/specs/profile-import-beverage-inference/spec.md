## Purpose

Gives imported profiles a `beverage_type` when the source carried none, so the picker's beverage filters and the beverage-group rules see them correctly.

## ADDED Requirements

### Requirement: Infer a missing beverage type on import
When a profile imported from Visualizer, from a tablet, or from a file has an empty or absent `beverage_type`, the app SHALL set one before saving, by these rules in order, first match wins: (1) title keywords — clean/flush/backflush/descale → `cleaning`; calibrat → `calibrate`; tea/steep/chai/matcha → `tea_portafilter`; pour over/pourover/filter/v60/aeropress/chemex/cold brew/drip/immersion → `pourover`; (2) shape — highest pressure across steps (setpoint for pressure steps, pressure limit for flow steps) under 3 bar, or any step temperature at or below 40 °C → `pourover`; (3) otherwise `espresso`. A non-empty `beverage_type` in the source SHALL never be changed. Profiles already on disk SHALL NOT be re-tagged.

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
