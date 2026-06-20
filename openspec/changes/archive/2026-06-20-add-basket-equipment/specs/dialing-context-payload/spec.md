## ADDED Requirements

### Requirement: Dialing context SHALL expose the basket via the equipment package
The dialing context SHALL include a `basket` sub-object in the `currentBean` block
(and in the shot snapshot it resolves), populated through the package's basket item
via `equipment_id` — there SHALL be no separate shot-level basket column. The
sub-object SHALL carry `brand`, `model`, and the registry-derived `wallProfile`,
`relativeFlow`, `precision`, and `doseRangeG`. When the resolved package has no
basket, or the basket is custom with unknown specs, the absent fields SHALL be
omitted rather than fabricated.

#### Scenario: Basket sub-object present
- **WHEN** the resolved shot's package has a registry basket
- **THEN** `currentBean.basket` SHALL include brand, model, wallProfile, relativeFlow, precision, and doseRangeG

#### Scenario: No basket on the package
- **WHEN** the resolved shot's package has no basket item
- **THEN** the `basket` sub-object SHALL be omitted

#### Scenario: Custom basket with unknown specs
- **WHEN** the resolved basket does not match the registry
- **THEN** `currentBean.basket` SHALL include brand/model and omit the unknown derived spec fields

### Requirement: Relative flow class SHALL be expressed as a directional string
The basket's `relativeFlow` SHALL be a human-readable string
(`"restrictive"` / `"standard"` / `"open"`), conveying the direction of a
cross-basket grind change, not a magnitude. The payload SHALL NOT present it as an
ordered numeric scale.

#### Scenario: Flow class string
- **WHEN** the basket sub-object is emitted
- **THEN** `relativeFlow` SHALL be one of the directional strings, not a numeric code

### Requirement: Dose range SHALL be available as an advisory sanity signal
The basket sub-object SHALL expose the basket's recommended dose range
(`doseRangeG: { min, max }`) so the advisor can flag a dose that falls outside the
basket's rated range. This is advisory only and SHALL NOT change dose ownership
(dose remains bean/recipe-scoped).

#### Scenario: Dose outside the rated range is detectable
- **WHEN** the shot's dose is above the basket's `doseRangeG.max`
- **THEN** the payload SHALL carry the range such that the advisor can detect the mismatch
