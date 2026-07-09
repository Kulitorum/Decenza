# mcp-server Specification (delta)

## ADDED Requirements

### Requirement: Recipe tools carry drink type and accept profile-less hot-water recipes
The recipe tool family SHALL expose `drinkType` (human-readable string per the data conventions) on `recipe_list` and `recipe_get`, and accept it on `recipe_create`/`recipe_update` (derived from blocks when omitted; re-derived on update only when blocks change and the caller did not set it). `recipe_create` and `recipe_update` SHALL accept a recipe with no profile when the payload carries a hot-water block with `hasWater` true, and SHALL reject a profile-less payload without one. `recipe_activate` on a profile-less recipe SHALL follow the shared profile-less activation path.

#### Scenario: Create hot-water tea recipe via MCP
- **WHEN** an MCP client calls `recipe_create` with a name, a tea bean link, and a hot-water block but no profile
- **THEN** the recipe is created and `recipe_get` returns it with `drinkType` reflecting hot-water tea

#### Scenario: Profile-less without hot water rejected
- **WHEN** an MCP client calls `recipe_create` with no profile and no hot-water block
- **THEN** the tool returns a validation error naming the rule

### Requirement: Bag tools expose kind
`bag_list` and bag detail payloads SHALL include the bag's `kind` (`"coffee"` or `"tea"`); `bag_update` SHALL NOT accept changing it (kind is set at creation). Tea bags' structured brewing fields (teaType, brewTempC, leafGramsPer100Ml, steepTime) SHALL appear in bag payloads following the data conventions (units in field names).

#### Scenario: Tea bag in bag_list
- **WHEN** an MCP client calls `bag_list` with a tea bag in inventory
- **THEN** the bag carries kind "tea" and its stated brewing fields

#### Scenario: Kind is immutable via MCP
- **WHEN** an MCP client calls `bag_update` attempting to change kind
- **THEN** the update is rejected or the field ignored with the response noting kind is creation-time only
