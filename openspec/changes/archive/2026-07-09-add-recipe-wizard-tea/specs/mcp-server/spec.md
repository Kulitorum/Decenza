# mcp-server Specification (delta)

## ADDED Requirements

### Requirement: Recipe tools carry drink type and accept profile-less hot-water recipes
The recipe tool family SHALL expose `drinkType` (human-readable string per the data conventions) on `recipe_list` and `recipe_get`, and accept it on `recipe_create`/`recipe_update` (derived from blocks when omitted; re-derived on update only when blocks change and the caller did not set it). Derivation SHALL resolve an installed profile's `beverage_type` from the profile catalog — recipes referencing installed profiles embed no profile JSON, and without the catalog a tea profile would derive as espresso. `recipe_create` and `recipe_update` SHALL accept a recipe with no profile when the payload carries a hot-water block with `hasWater` true, and SHALL reject a profile-less payload without one. `recipe_activate` on a profile-less recipe SHALL follow the shared profile-less activation path.

#### Scenario: Create hot-water tea recipe via MCP
- **WHEN** an MCP client calls `recipe_create` with a name, a tea bean link, and a hot-water block but no profile
- **THEN** the recipe is created and `recipe_get` returns it with `drinkType` reflecting hot-water tea

#### Scenario: Profile-less without hot water rejected
- **WHEN** an MCP client calls `recipe_create` with no profile and no hot-water block
- **THEN** the tool returns a validation error naming the rule

### Requirement: Bag tools expose kind
`bag_list` and bag detail payloads SHALL include the bag's `kind` (`"coffee"` or `"tea"`); `bag_update` SHALL NOT accept changing it (kind is set at creation). Tea bags' structured brewing fields (teaType, brewTempC, leafGramsPer100Ml, steepTime) SHALL appear in bag payloads following the data conventions (units in field names), and `bag_update` SHALL reject tea-vocabulary writes on a coffee bag with an error naming the rule (never a silent drop).

#### Scenario: Tea bag in bag_list
- **WHEN** an MCP client calls `bag_list` with a tea bag in inventory
- **THEN** the bag carries kind "tea" and its stated brewing fields

#### Scenario: Kind is immutable via MCP
- **WHEN** an MCP client calls `bag_update` attempting to change kind
- **THEN** the update is rejected or the field ignored with the response noting kind is creation-time only

### Requirement: Bags are creatable via MCP with kind stamped at creation
The MCP server SHALL provide `bag_create` (write access level): kind `coffee` (default) or `tea`, at least one of roasterName/coffeeName required, kind-gated vocabularies in both directions (tea fields rejected on coffee creates; roastLevel/grinderSetting rejected on tea creates), details landing in the bag blob via the shared merge helper. The created bag SHALL enter the inventory but SHALL NOT become the active bag (a remote client must not silently switch what the next shot is pulled with; `bag_select` activates).

#### Scenario: Create a tea bag with brewing data
- **WHEN** an MCP client calls `bag_create` with kind "tea", a brand/name, teaType, and brewTempC
- **THEN** the bag appears in `bag_list` with kind "tea" and its brewing fields, and the active bag is unchanged

#### Scenario: Kind-gated create
- **WHEN** an MCP client calls `bag_create` with kind "coffee" and a teaType
- **THEN** the create is rejected with an error naming the tea-only fields

### Requirement: Page extraction is drivable and diagnosable via MCP
The MCP server SHALL provide `bag_extract_details` (control tier): runs the exact in-app two-stage extraction for a bag's product URL (kind selects the coffee/tea vocabulary) and returns the extracted fields WITHOUT writing them, plus diagnostics — which stage ran, the stage-1 failure when the fallback fired, the provider and model, and the fetched text size. Applying fields is the caller's explicit `bag_update`.

#### Scenario: Stage-2 diagnosis on a JS-rendered shop
- **WHEN** an MCP client calls `bag_extract_details` for a bag whose page is a JS-rendered SPA
- **THEN** the response shows stage 2, the stage-1 emptyPage error, and the extracted fields including imageUrl when present
