## MODIFIED Requirements

### Requirement: Icons on Tools and Resources

Every resource record returned by `resources/list` SHALL include an `icons` array with at least one entry. Each icon entry SHALL contain `src` (a `data:image/svg+xml;base64,...` URI), `mimeType: "image/svg+xml"`, and a `sizes` field. Icon assignment SHALL be driven by resource kind. Tool records returned by `tools/list` SHALL NOT include icons: the field is optional in the protocol and cosmetic in effect, while inline base64 SVGs were 87% of the `tools/list` payload (~216 KB), 41 of 97 of them the same generic fallback.

#### Scenario: Tool list omits icons
- **WHEN** a client calls `tools/list`
- **THEN** no tool record includes an `icons` key

#### Scenario: Resource list includes icons
- **WHEN** a client calls `resources/list`
- **THEN** every resource record includes an `icons` array with at least one SVG entry

### Requirement: equipment_list tool
The MCP server SHALL provide an `equipment` tool with an `action: "list"` (modeled on the `bag` tool's `list` action) returning equipment packages: `id`, display `name`, grinder `brand`/`model`/`burrs`, `rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

#### Scenario: Listing packages
- **WHEN** an agent calls `equipment` with `action: "list"`
- **THEN** it SHALL receive the inventory of equipment packages with the fields above

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment` tool with an `action: "select"` (modeled on the `bag` tool's `select` action) that sets the active bag's equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the active bag per the dual-memory rule.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment` with `action: "select"` and a valid package id
- **THEN** the active bag's `equipment_id` SHALL be set to that package
- **AND** the active bag's grind setting and rpm SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment` tool with an `action: "update"` (modeled on the `bag` tool's `update` action) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing bags/shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment` with `action: "update"` changing a package's grinder model
- **THEN** the package SHALL be updated, `rpmAdjustable` re-derived, and all referencing bags/shots SHALL resolve to the new identity
