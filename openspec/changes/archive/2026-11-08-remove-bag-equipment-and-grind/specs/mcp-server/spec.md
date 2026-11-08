# Spec Delta

## MODIFIED Requirements

### Requirement: MCP grinder reads SHALL resolve via the equipment package

MCP surfaces reporting grinder identity (the `de1://dialing` resource in `mcpresources.cpp`, `dialing_get_context`, `dialing_get_grinder_calibration`, `ai_advisor_invoke`) SHALL resolve brand, model and burrs through the equipment package: the active equipment package for live snapshots, the shot's `equipment_id` for shot-derived blocks. They SHALL also expose the package `id`, `name`, `rpmAdjustable` and the `rpm` dial-in.

#### Scenario: Dialing resource reports the package
- **WHEN** the `de1://dialing` resource is read
- **THEN** the grinder block SHALL include the resolved brand/model/setting plus `rpm` and `rpmAdjustable`, sourced from the active equipment package

#### Scenario: Data conventions on grinder reads
- **WHEN** a dialing MCP surface reports grinder identity
- **THEN** units are in field names, `kind` is a string enum, and timestamps are ISO-8601

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment` tool with an `action: "select"` (modeled on the `bag` tool's `select` action) that sets the active equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the live dial.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment` with `action: "select"` and a valid package id
- **THEN** that package SHALL become active
- **AND** the live grind setting and RPM SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment` tool with an `action: "update"` (modeled on the `bag` tool's `update` action) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment` with `action: "update"` changing a package's grinder model
- **THEN** the package SHALL be updated, `rpmAdjustable` re-derived, and all referencing shots SHALL resolve to the new identity

### Requirement: Recipe fields follow the data conventions

Recipe tool responses and inputs SHALL follow the house data conventions. Grind SHALL expose the recipe's own value and RPM, never inherited from a bag. Responses SHALL expose the linked bag (`bagId` and display identity), and create and update SHALL accept `bagId`. The hot-water block SHALL round-trip as the steam block does.

#### Scenario: Grind representation
- **WHEN** `recipe_get` returns a recipe with its own grind
- **THEN** the response shows its own grind and RPM

#### Scenario: Bag link over MCP
- **WHEN** an MCP client calls `recipe_get` on a recipe whose linked bag was finished
- **THEN** the response carries the `bagId`, the bag's display identity, and a human-readable stale indication

#### Scenario: Hot-water block round-trips over MCP
- **WHEN** an MCP client calls `recipe_create` (or `recipe_update`) with a hot-water block and later calls `recipe_get`
- **THEN** the block is accepted against the tool schema, persisted, and returned unchanged (including its `order`)

#### Scenario: Temperature is the offset field
- **WHEN** a recipe holding a −3° temperature offset is returned by any recipe tool
- **THEN** the response carries `tempOffsetC: -3` (a signed delta in °C against the recipe's profile) and no absolute recipe-temperature field

#### Scenario: Hot-water order and units
- **WHEN** a client sends a hot-water block on `recipe_create`
- **THEN** each field's unit is documented in the tool schema, and `order` is `before` (long black) or `after` (Americano)

#### Scenario: House conventions applied
- **WHEN** a recipe response is read
- **THEN** field names carry unit suffixes, timestamps are ISO 8601 with timezone, and enum values are human-readable strings

## ADDED Requirements

### Requirement: MCP does not write bag equipment or grind
MCP bag writes SHALL refuse non-empty obsolete equipment, grind and RPM fields with a clear reason. Recipe creation SHALL NOT silently adopt a bag grind.

#### Scenario: Obsolete bag grind write
- **WHEN** a client submits a non-empty bag grind
- **THEN** the write is refused with a reason
