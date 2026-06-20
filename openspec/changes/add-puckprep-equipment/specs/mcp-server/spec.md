## ADDED Requirements

### Requirement: Equipment MCP tools SHALL read and write the puck-prep flags
The MCP `equipment_*` tools SHALL include the package's puck-prep flags (and the
derived `distribution`) in equipment reads, and SHALL accept the optional puck-prep
flags when creating or updating a package, alongside the grinder and basket
identity. Omitting them SHALL leave the package without puck prep. Puck-prep edits
SHALL flow through the same package-identity (dedup/fork) rules as grinder/basket edits.

#### Scenario: Equipment read includes puck prep
- **WHEN** an MCP equipment read resolves a package that has puck prep
- **THEN** the response SHALL include the set flags and the derived `distribution`

#### Scenario: Equipment write sets puck prep
- **WHEN** an MCP equipment write supplies one or more puck-prep flags
- **THEN** the package SHALL gain a `kind="puckprep"` item subject to the identity rules

### Requirement: MCP puck-prep fields SHALL follow the data conventions
Puck-prep fields exposed over MCP SHALL follow the project MCP conventions: boolean
flags with self-describing names (`wdt`, `shaker`, `puckScreen`, `paperFilter`,
`rdt`) and a human-readable `distribution` string rather than a numeric code.

#### Scenario: Puck-prep fields are LLM-legible
- **WHEN** the MCP dialing context emits the puck-prep sub-object
- **THEN** the flags SHALL be booleans and `distribution` SHALL be a readable string
