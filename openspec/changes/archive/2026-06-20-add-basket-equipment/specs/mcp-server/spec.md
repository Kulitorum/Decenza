## ADDED Requirements

### Requirement: Equipment MCP tools SHALL read and write the basket identity
The MCP `equipment_*` tools SHALL include the package's basket identity
(`brand`, `model`) in equipment reads, and SHALL accept an optional basket identity
when creating or updating a package, alongside the grinder identity. Omitting the
basket SHALL leave a package grinder-only. Basket edits SHALL flow through the same
package-identity (dedup/fork) rules as grinder edits.

#### Scenario: Equipment read includes the basket
- **WHEN** an MCP equipment read resolves a package that has a basket
- **THEN** the response SHALL include the basket brand and model

#### Scenario: Equipment write sets a basket
- **WHEN** an MCP equipment write supplies a basket identity
- **THEN** the package SHALL gain a `kind="basket"` item subject to the identity rules

### Requirement: MCP basket fields SHALL follow the data conventions
Basket fields exposed over MCP SHALL follow the project MCP conventions: dose range
named with its unit (`doseRangeG`), and `wallProfile` / `relativeFlow` / `precision`
expressed as human-readable strings/booleans rather than numeric codes. Derived
specs SHALL be omitted when unknown (custom basket) rather than zero-filled.

#### Scenario: Basket specs are LLM-legible
- **WHEN** the MCP dialing context emits the basket sub-object
- **THEN** `wallProfile` and `relativeFlow` SHALL be readable strings and the dose range SHALL be unit-suffixed (`doseRangeG`)
