## ADDED Requirements

### Requirement: MCP grinder reads SHALL resolve via the equipment package
MCP surfaces that report grinder identity — the `de1://dialing` resource (`mcpresources.cpp`), `dialing_get_context`, `dialing_get_grinder_calibration`, and `ai_advisor_invoke` — SHALL resolve grinder brand/model/burrs through the equipment package (the active bag's package for live snapshots; the resolved shot's `equipment_id` for shot-derived blocks). They SHALL additionally expose the package identity (`id`, display `name`, `rpmAdjustable`) and the `rpm` dial-in. All fields SHALL follow MCP data conventions (units in field names, `kind` as a string enum, ISO-8601 timestamps).

#### Scenario: Dialing resource reports the package
- **WHEN** the `de1://dialing` resource is read
- **THEN** the grinder block SHALL include the resolved brand/model/setting plus `rpm` and `rpmAdjustable`, sourced from the active bag's equipment package

### Requirement: equipment_list tool
The MCP server SHALL provide an `equipment_list` tool (modeled on `bag_list`) returning equipment packages: `id`, display `name`, grinder `brand`/`model`/`burrs`, `rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

#### Scenario: Listing packages
- **WHEN** an agent calls `equipment_list`
- **THEN** it SHALL receive the inventory of equipment packages with the fields above

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment_select` tool (modeled on `bag_select`) that sets the active bag's equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the active bag per the dual-memory rule.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment_select` with a valid package id
- **THEN** the active bag's `equipment_id` SHALL be set to that package
- **AND** the active bag's grind setting and rpm SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment_update` tool (modeled on `bag_update`) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing bags/shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment_update` changing a package's grinder model
- **THEN** the package SHALL be updated, `rpmAdjustable` re-derived, and all referencing bags/shots SHALL resolve to the new identity
