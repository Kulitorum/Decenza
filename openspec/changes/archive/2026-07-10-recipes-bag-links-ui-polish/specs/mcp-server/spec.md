# mcp-server Delta

## MODIFIED Requirements

### Requirement: Recipe fields follow the data conventions
Recipe tool responses and inputs SHALL use the house conventions: unit-suffixed field names (`doseG`, `yieldG`, `milkWeightG`, `temperatureC`), ISO 8601 timestamps with timezone, human-readable enum strings, and grind expressed explicitly as an object `{"mode": "inherited"|"pinned", "value": <string>}` plus the resolved effective value, so a client never guesses where grind lives. Inherited grind SHALL resolve from the recipe's linked bag. Recipe responses SHALL expose the linked bag (`bagId` plus its display identity) and a human-readable staleness indication when the linked bag is no longer in inventory; `recipe_create` and `recipe_update` SHALL accept `bagId`. The optional hot-water block SHALL be accepted on create/update and returned on read via a tool schema that mirrors the steam block's pass-through handling — the same block the recipe stores, with each field's unit documented in its schema description (as the steam block does for `flow`), and with an `order` of `before` (long black) or `after` (Americano).

#### Scenario: Grind representation
- **WHEN** `recipe_get` returns a recipe that inherits grind from its linked bag
- **THEN** the response shows `"grind": {"mode": "inherited", "value": <linked bag's current grind>}`

#### Scenario: Bag link over MCP
- **WHEN** an MCP client calls `recipe_get` on a recipe whose linked bag was finished
- **THEN** the response carries the `bagId`, the bag's display identity, and a human-readable stale indication

#### Scenario: Hot-water block round-trips over MCP
- **WHEN** an MCP client calls `recipe_create` (or `recipe_update`) with a hot-water block and later calls `recipe_get`
- **THEN** the block is accepted against the tool schema, persisted, and returned unchanged (including its `order`)
