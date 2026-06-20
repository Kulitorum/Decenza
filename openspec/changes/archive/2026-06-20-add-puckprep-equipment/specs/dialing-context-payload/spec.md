## ADDED Requirements

### Requirement: Dialing context SHALL expose puck prep via the equipment package
The dialing context SHALL include a `puckPrep` sub-object in the `currentBean` block
(and in the shot snapshot it resolves), populated through the package's puck-prep
item via `equipment_id` — there SHALL be no separate shot-level puck-prep column.
The sub-object SHALL carry the set boolean flags and the derived `distribution`
rollup. When the resolved package has no puck-prep item, the `puckPrep` sub-object
SHALL be omitted rather than fabricated.

#### Scenario: Puck-prep sub-object present
- **WHEN** the resolved shot's package has a puck-prep item
- **THEN** `currentBean.puckPrep` SHALL include the set flags and `distribution`

#### Scenario: No puck prep on the package
- **WHEN** the resolved shot's package has no puck-prep item
- **THEN** the `puckPrep` sub-object SHALL be omitted

### Requirement: The distribution rollup SHALL be a human-readable directional string
The `distribution` rollup SHALL be one of the human-readable strings
`"none"` / `"light"` / `"thorough"`, conveying the amount of distribution effort so
the advisor can branch its channeling guidance. It SHALL NOT be a numeric code.

#### Scenario: Distribution string emitted
- **WHEN** the puck-prep sub-object is emitted
- **THEN** `distribution` SHALL be one of `"none"`, `"light"`, or `"thorough"`
