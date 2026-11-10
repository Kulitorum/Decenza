# Spec Delta

## MODIFIED Requirements

### Requirement: Water amount specifications round-trip across recipe surfaces
Recipe storage, native/browser editing and MCP SHALL preserve the fixed-or-ratio discriminator and the ratio value. Existing blocks without an amount discriminator SHALL retain fixed semantics. A name-only legacy block SHALL continue resolving its fixed settings from the named preset. Saving an unrelated field SHALL NOT flatten or discard a water ratio.

#### Scenario: Existing fixed-volume recipe
- **WHEN** a legacy hot-water block stores 150 ml in volume mode and no amount discriminator
- **THEN** it loads and pours as fixed 150 ml until explicitly edited

#### Scenario: MCP ratio round trip
- **WHEN** a client creates or updates a recipe with a 2.7× water ratio and reads it back
- **THEN** the returned hot-water block retains ratio mode and 2.7, with no fixed amount treated as authoritative

#### Scenario: Edit an unrelated field through an older form
- **WHEN** the shared editor changes only the name of a recipe carrying ratio water
- **THEN** the saved recipe retains its ratio amount specification


### Requirement: Recipe surfaces expose the offset
The MCP recipe tools and shared native/browser editor SHALL expose the temperature as `tempOffsetC`, a signed °C delta present only when non-zero on read, and SHALL accept it as the only temperature field. The legacy `temperatureOverrideC` SHALL NOT appear in responses. A create or update carrying it SHALL be rejected with an error naming `tempOffsetC` and its delta semantics, never silently dropped.

#### Scenario: MCP reads a recipe with an offset
- **WHEN** an MCP client fetches a recipe holding a −3° offset
- **THEN** the response contains `tempOffsetC: -3` and no `temperatureOverrideC` key

#### Scenario: Web editor round-trips the offset
- **WHEN** the web recipe editor saves a recipe with `tempOffsetC` = 2
- **THEN** the stored recipe holds offset 2 and re-reading it returns `tempOffsetC: 2`

#### Scenario: Legacy field is rejected loudly
- **WHEN** an MCP client sends `temperatureOverrideC` on recipe create or update
- **THEN** the request fails with an error naming `tempOffsetC` and its signed-delta semantics, and no field is written

