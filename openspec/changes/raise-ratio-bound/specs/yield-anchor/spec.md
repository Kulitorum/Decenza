## ADDED Requirements

### Requirement: A ratio is bounded for every drink kind

The system SHALL clamp a ratio to 0.5–100 at every write boundary: Brew Settings, the ratio presets, recipes, bags, MCP and the web pages. The bound SHALL be defined once in C++ and read by QML rather than repeated. A ratio's resolved gram target SHALL be held to the absolute bound of 1–500 g.

#### Scenario: A filter ratio is stored as written
- **WHEN** a bag's yield is saved as `{16.0, ratio}`
- **THEN** the bag holds `{16.0, ratio}`

#### Scenario: A ratio above the bound clamps
- **WHEN** a bag's yield is saved as `{150.0, ratio}`
- **THEN** the bag holds `{100.0, ratio}`

#### Scenario: A large ratio on an espresso dose stops at 500 g
- **WHEN** the session anchor is `{100.0, ratio}` and the dose is 18 g
- **THEN** the resolved target is 500 g
