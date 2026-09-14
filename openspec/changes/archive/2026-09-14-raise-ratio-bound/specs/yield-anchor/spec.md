## ADDED Requirements

### Requirement: A ratio is bounded for every drink kind

The system SHALL clamp a ratio to 0.5–100 at every write boundary: Brew Settings, the ratio presets, recipes, bags, MCP and the web pages. The bound SHALL be defined once in C++ and read by QML rather than repeated.

#### Scenario: A filter ratio is stored as written
- **WHEN** a bag's yield is saved as `{16.0, ratio}`
- **THEN** the bag holds `{16.0, ratio}`

#### Scenario: A ratio above the bound clamps
- **WHEN** a bag's yield is saved as `{150.0, ratio}`
- **THEN** the bag holds `{100.0, ratio}`

#### Scenario: A filter ratio resolves against the dose
- **WHEN** the session anchor is `{16.0, ratio}` and the dose is 18 g
- **THEN** the resolved target is 288 g
