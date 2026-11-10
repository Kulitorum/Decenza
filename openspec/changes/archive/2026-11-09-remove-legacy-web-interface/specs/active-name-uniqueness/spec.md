# Spec Delta

## MODIFIED Requirements

### Requirement: Enforcement covers non-form creation paths

The system SHALL enforce the active-name-uniqueness rule at the storage layer for each
entity, so that programmatic creation and rename paths (MCP tools) cannot introduce a duplicate active name. On a collision the storage
operation SHALL fail and report the failure to its caller; it SHALL NOT auto-rename
the item and SHALL NOT silently succeed.

#### Scenario: MCP or REST create with a duplicate active name
- **WHEN** a non-form caller requests creation of an equipment package or recipe with
  a name that duplicates an active item of the same kind
- **THEN** the storage operation is rejected and the caller receives an explicit
  failure identifying the name conflict, with no item created


### Requirement: The app, MCP, and ShotServer behave identically

The in-app forms and the MCP tools SHALL apply the
same active-name-uniqueness rule with the same scope and the same normalization, and
SHALL each report a name collision as an explicit, identifiable name conflict rather
than a generic failure. This SHALL hold for renames as well as creations.

#### Scenario: The same collision is reported the same way on every surface
- **WHEN** the same duplicate name is submitted through the in-app form or an MCP tool,
  whether creating or renaming
- **THEN** both reject the write, neither creates or renames anything, and each
  reports a conflict that identifies the name as the cause

