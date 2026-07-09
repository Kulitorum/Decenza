# mcp-server (delta)

## ADDED Requirements

### Requirement: Recipe tool family
The MCP server SHALL register a recipe tool family (`mcptools_recipes.cpp`): `recipe_list`, `recipe_get`, `recipe_create`, `recipe_update`, `recipe_create_from_shot`, `recipe_clone`, `recipe_archive`, and `recipe_activate`. Read tools SHALL register at the read access level; mutating tools (create/update/clone/archive/activate/create_from_shot) at the write access level, matching the preset tools. `recipe_activate` SHALL invoke the same shared controller activation path as the UI. Lifecycle rules SHALL be enforced (archive-only for used recipes).

#### Scenario: AI saves a dialed-in shot
- **WHEN** an MCP client calls `recipe_create_from_shot` with a shot id and a name
- **THEN** a recipe is created prefilled from that shot's record and steam snapshot, with provenance recorded

#### Scenario: AI clones a family variant
- **WHEN** an MCP client calls `recipe_clone` and then `recipe_update` to change the milk weight and name
- **THEN** an independent recipe exists and the source recipe is unchanged

### Requirement: Recipe fields follow the data conventions
Recipe tool responses and inputs SHALL use the house conventions: unit-suffixed field names (`doseG`, `yieldG`, `milkWeightG`, `steamTemperatureC`), ISO 8601 timestamps with timezone, human-readable enum strings, and grind expressed explicitly as an object `{"mode": "inherited"|"pinned", "value": <string>}` plus the resolved effective value, so a client never guesses where grind lives.

#### Scenario: Grind representation
- **WHEN** `recipe_get` returns a recipe that inherits grind from its bean's bag
- **THEN** the response shows `"grind": {"mode": "inherited", "value": <bag's current grind>}`
