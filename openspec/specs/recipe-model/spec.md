# recipe-model Specification

## Purpose
Drink-recipe data model: the `recipes` table, yield specs, steam and hot-water blocks snapshotted by value, drink type, bag links, recipe-owned grind and temperature offsets. Also covers the legacy migrations and how the MCP and ShotServer surfaces expose these fields.

## Requirements

### Requirement: Recipe entity
Recipes SHALL be stored in the `recipes` table, with I/O on a background thread. A recipe SHALL have a name, drink type, dose (g), yield spec (`yield-anchor`) and steam block, and a profile reference (with embedded JSON fallback) unless its hot-water block has `hasWater` true. It MAY have a bean link, equipment reference, temperature offset (`temp_offset_c`, a signed °C delta from its profile), pinned grind and hot-water block.

#### Scenario: Minimal recipe is valid
- **WHEN** a recipe is created with only a name and a profile
- **THEN** it saves successfully and can be activated, with no bean, equipment, dose, steam, or hot-water fields required
- **AND** its yield mode is `none`

#### Scenario: Profile-less hot-water recipe is valid
- **WHEN** a recipe is created with a name and a hot-water block but no profile
- **THEN** it saves successfully and can be activated

#### Scenario: Profile-less recipe without hot water is rejected
- **WHEN** a save is attempted with no profile and no hot-water block
- **THEN** validation fails on every surface (wizard, MCP, web)

#### Scenario: Storage runs off the main thread
- **WHEN** any recipe read or write is requested
- **THEN** the database work runs on a background thread and results are delivered to the main thread via a queued signal

#### Scenario: Profile temperature edit does not change the stored offset
- **WHEN** a recipe stores a −3° offset on a 90° profile and the profile's temperature is later saved as 88°
- **THEN** the recipe still stores (and its editor still shows) −3°, and its effective brew temperature becomes 85°

#### Scenario: Dose change does not change the stored ratio
- **WHEN** a recipe stores `{2.0, ratio}` with a `doseG` of 18 and the live dose becomes 17.5
- **THEN** the recipe still stores (and its editor still shows) `1:2`, and its effective target becomes 35 g

#### Scenario: A recipe cannot hold both an absolute yield and a ratio
- **WHEN** a recipe holding `{36.0, absolute}` is given a ratio of 1:2 from any surface (wizard, Brew Settings, MCP, web)
- **THEN** it holds `{2.0, ratio}` and retains no absolute yield

#### Scenario: A ratio recipe with no dose renders as a bare ratio
- **WHEN** a recipe holds `{2.0, ratio}` and its `doseG` is unset
- **THEN** surfaces that display its yield show `1:2` — there is no dose to derive a gram target from, and no fallback to the profile's target weight

#### Scenario: Structured sub-fields persist as JSON
- **WHEN** a recipe with a steam block, a hot-water block or a pinned grind is saved
- **THEN** each structured sub-field is written to its JSON text column and read back unchanged

### Requirement: Recipe dose is a seed
A recipe's `doseG` SHALL be a seed, not a pin: it seeds the live dose on activation, after which a measured dose supersedes it. A ratio-moded recipe SHALL resolve against its own `doseG` on browsing surfaces such as recipe cards, and against the live dose once activated.

#### Scenario: Browsing a ratio recipe uses its seed dose
- **WHEN** a recipe holding `{2.0, ratio}` with `doseG` 18 is shown on a recipe card
- **THEN** its gram target is 36 g

#### Scenario: Activated ratio recipe uses the live dose
- **WHEN** that recipe is activated and the measured dose is 17.5
- **THEN** its gram target becomes 35 g

### Requirement: Yield spec storage
The yield spec SHALL be stored as `yield_value` (double) plus `yield_mode` (`none`, `absolute` or `ratio`), so a recipe can never hold both an absolute yield and a ratio. Mode `none` means the recipe designs no yield, and the ladder falls through to the bag, then the profile. The legacy `yield_g` column SHALL be left dead in place.

#### Scenario: Mode none falls through the ladder
- **WHEN** a recipe with `yield_mode` = `none` is resolved for its yield target
- **THEN** the bag's yield is used, and the profile's if the bag has none

### Requirement: Offset and ratio are stored as set
The temperature offset and the yield SHALL be stored as set, never recomputed into absolutes at display time. A profile or dose edit therefore moves the effective target while the stored value stays as the user set it.

#### Scenario: Stored ratio is derived at use time
- **WHEN** a recipe stores `{2.0, ratio}` with a `doseG` of 18 and the live dose becomes 17.5
- **THEN** the recipe still stores `1:2`, and its effective target becomes 35 g

### Requirement: Recipe surfaces expose the yield spec
Every recipe surface (wizard, MCP, web editor) SHALL present yield as none, an absolute yield, or a ratio. The last written of ratio and yield is the anchor; the other is shown derived, never blank. JSON surfaces SHALL expose `yieldG` and `yieldRatio` as sparse, mutually exclusive keys, reject a request carrying both with an error naming the conflict, and clear the other when one is written. Tool descriptions SHALL say so.

#### Scenario: MCP rejects both yield keys at once
- **WHEN** a `recipe_create` or `recipe_update` call carries both `yieldG` and `yieldRatio`
- **THEN** the call fails with an error naming the conflict and no partial write occurs

#### Scenario: MCP partial update swaps the anchor cleanly
- **WHEN** a `recipe_update` carries only `yieldRatio` for a recipe currently holding an absolute yield
- **THEN** the recipe holds only the ratio afterwards, with no explicit clear required from the caller

#### Scenario: MCP reads a recipe's yield sparsely
- **WHEN** `recipe_get` returns a recipe holding `{2.0, ratio}`
- **THEN** the response carries `yieldRatio` = 2.0 and omits `yieldG` entirely
- **AND** for a recipe whose mode is `none`, neither key appears

#### Scenario: Web editor's blank yield field does not silently clear a ratio
- **WHEN** the web recipe editor saves a recipe whose anchor is a ratio
- **THEN** it posts `yieldRatio` and omits `yieldG` — it SHALL NOT coerce a blank yield input to `0` and clear the anchor

### Requirement: Steam block with pitcher snapshot
A recipe's steam block SHALL contain `hasMilk`, milk weight (g), steam temperature, flow and timeout, and either a pitcher snapshot (name and volume, by value) or an off marker. A missing pitcher SHALL NOT block activation, and a real one MAY be re-created.

An espresso recipe SHALL NOT inherit a shot's or the currently selected pitcher's steam settings when milk was not included.

#### Scenario: Promote an espresso shot with a previously selected pitcher
- **WHEN** a shot carrying an ambient pitcher snapshot is promoted without milk
- **THEN** the new recipe does not contain that pitcher or the last milk weight

#### Scenario: Pitcher preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global steam pitcher presets after a recipe was saved
- **THEN** the recipe's steam behavior is unchanged, because the pitcher was snapshotted by value

### Requirement: Steam off marker
The off marker SHALL be a stable field, and SHALL NOT carry a pitcher name. A block with neither a pitcher snapshot nor the off marker SHALL be treated as the off marker. An off-marker block SHALL NOT create a preset.

#### Scenario: The off marker survives a language change
- **WHEN** a recipe carrying the off marker is activated in a different app language from the one it was saved in
- **THEN** it resolves to the built-in "Heater off" entry, and no pitcher preset is created

#### Scenario: A block with no pitcher wants the heater off
- **WHEN** a recipe's steam block carries neither a pitcher snapshot nor the off marker
- **THEN** activating it with "Let the recipe decide" on turns the steam heater off

#### Scenario: An unresolvable pitcher name does not manufacture an off preset
- **WHEN** a shot's steam snapshot names a pitcher that no longer exists and the shot is promoted to a recipe
- **THEN** the promotion succeeds and creates no preset

#### Scenario: Off marker is never looked up by name
- **WHEN** a block carrying the off marker is activated
- **THEN** the preset list is not searched and no pitcher preset is created

### Requirement: Recipe lifecycle mirrors bags
A recipe with zero shots SHALL be hard-deletable. A recipe that any shot references SHALL only be archivable: archived recipes disappear from pickers and quick-select but remain readable so shot history provenance never dangles.

#### Scenario: Deleting an unused recipe
- **WHEN** the user deletes a recipe no shot references
- **THEN** it is removed permanently

#### Scenario: Archiving a used recipe
- **WHEN** the user archives a recipe that has shots
- **THEN** it leaves all pickers and the MRU pills, and its shots still display its name

### Requirement: Shots record recipe provenance and steam snapshot
The `shots` table SHALL gain a nullable `recipe_id` recording which recipe (if any) was active at shot start, and SHALL record a snapshot of the steam spec and the hot-water spec in effect, so that promoting any shot to a recipe round-trips the whole drink. Existing rows SHALL be unaffected (nullable columns, single forward migration).

#### Scenario: Shot pulled with a recipe active
- **WHEN** a shot is pulled while a recipe is active
- **THEN** the shot row stores that recipe's id, the steam spec used, and the hot-water spec used

#### Scenario: Legacy shots
- **WHEN** shots recorded before this change are read
- **THEN** they load normally with no recipe provenance and no steam or hot-water snapshot

### Requirement: Hot-water block is an opt-in water-vessel snapshot
A recipe MAY carry a hot-water block; `hasWater` enables it and requires a vessel. The enabled block SHALL require a nonblank vessel name and snapshot its name, temperature, flow and either fixed amount with weight/volume mode or a water-to-espresso-target-yield ratio. Ratio amounts SHALL be recipe-specific and resolve to grams. The block SHALL carry `order`, before or after espresso, defaulting to after. Steam and hot-water blocks SHALL remain independent.

#### Scenario: Enabling hot water requires a vessel
- **WHEN** the user turns on added hot water for a recipe
- **THEN** the recipe references a water vessel and stores its selected amount specification, temperature and flow as a by-value snapshot

#### Scenario: Fixed and ratio water both require a vessel
- **WHEN** a recipe enables water without a nonblank vessel name, including a fixed or older unnamed block
- **THEN** saving or applying its water fails with an instruction to choose a vessel
- **AND** the current standalone selection is not automatically assigned to the recipe

#### Scenario: Water order distinguishes long black from Americano
- **WHEN** the user sets the hot-water order to `before` (long black) or `after` (Americano)
- **THEN** the recipe's hot-water block records that order and returns it unchanged when the recipe is loaded

#### Scenario: Water vessel preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global water-vessel presets after a recipe was saved
- **THEN** the recipe's hot-water behavior is unchanged, because its fixed amount or ratio, temperature and flow were snapshotted by value

#### Scenario: Steam and hot water coexist on one recipe
- **WHEN** a recipe carries both a milk steam block and a hot-water block
- **THEN** both are stored and neither overrides the other

#### Scenario: Ratio is stored rather than flattened
- **WHEN** a recipe with a 40 g espresso target saves a 2.5× water ratio
- **THEN** reopening it shows Ratio and 2.5×, rather than a fixed 100 g amount

#### Scenario: Shared vessel serves independent recipes
- **WHEN** two recipes use the same vessel name with water ratios 2 and 3
- **THEN** each retains its own ratio and editing either does not change the other or the shared preset's fixed amount automatically

### Requirement: Recipes carry a drink type
The `recipes` table SHALL gain a `drink_type` TEXT column (`espresso`, `filter`, `americano`, `long_black`, `latte`, `latte_hotwater`, `tea`, `tea_hotwater`) by migration, riding transfer and backup import. The value records user intent and SHALL NOT drive activation, which applies only the blocks and profile. Recipes without a stored value, and promote-from-shot, SHALL derive it as in "Drink type derivation".

#### Scenario: Legacy recipe derives its type
- **WHEN** a pre-migration americano recipe (hot-water block, order "after", no milk) is opened for edit
- **THEN** the summary shows drink type Americano, and saving stores `americano`

#### Scenario: Latte with added hot water derives its own type
- **GIVEN** a recipe with a profile carrying BOTH a milk block (`hasMilk`) and a hot-water block (`hasWater`)
- **WHEN** its drink type is derived (legacy row or promote-from-shot)
- **THEN** the derived type SHALL be `latte_hotwater`, superseding the prior "milk wins" collapse to `latte`
- **AND** a recipe with a milk block but no hot-water block SHALL still derive `latte`

#### Scenario: Drink type never gates activation
- **WHEN** a recipe's blocks contradict its stored drink type
- **THEN** activation applies the blocks exactly as stored

### Requirement: Drink type derivation
Recipes without a stored drink type, and promote-from-shot, SHALL derive it in this order: hot-water block without profile gives `tea_hotwater`; `tea_portafilter` profile gives `tea`; milk and hot water give `latte_hotwater`; milk alone gives `latte`; hot water alone gives `americano` (order `after`) or `long_black` (`before`); `filter` or `pourover` profile gives `filter`; otherwise `espresso`. The derived value SHALL be stored on the next save.

#### Scenario: Tea profile derives tea
- **WHEN** a recipe with no stored drink type and a `tea_portafilter` profile is saved
- **THEN** its drink type is stored as `tea`

### Requirement: Recipes link a specific bag
A recipe SHALL link a specific bag via a `bag_id` column, riding transfer and backup import with id remapping like `equipment_id`. Its bean identity (Bean Base canonical id, roaster, coffee) SHALL be kept as a display fallback and as the key for automatic relinking. Activation SHALL use the linked bag directly, never most-recently-used resolution. A recipe MAY have no bag.

#### Scenario: Two open bags of the same bean
- **WHEN** two recipes link two different open bags of the same bean and each is activated in turn
- **THEN** each activation selects exactly its own linked bag and applies its own grind

#### Scenario: Bean-less recipe unaffected
- **WHEN** a recipe with no bag link is activated
- **THEN** the active bag is unchanged and recipe-local grind applies, exactly as before

### Requirement: Promote-from-shot grind comes from the shot
Promote-from-shot, in the wizard and in MCP `recipe_create_from_shot`, SHALL default grind and rpm from the shot's own recorded values, the dial that produced the shot, and SHALL NOT use the bag's current dial.

#### Scenario: Wizard promote offers the shot's grind
- **WHEN** the user opens the wizard to promote a shot recorded at grind "17"
- **THEN** the grind field offers "17" as an editable default

### Requirement: Absolute temperature overrides migrate to offsets
A one-time migration SHALL set `temp_offset_c` to legacy `temp_override_c` minus the profile's `espresso_temperature`, resolving the profile by title, then embedded JSON. A legacy 0 or unresolvable profile SHALL give offset 0; magnitudes below 0.05 °C SHALL store as 0. The migration runs off the main thread, and `temp_override_c` SHALL no longer be read or written. Import SHALL convert only unconverted rows; converted rows SHALL import verbatim.

#### Scenario: Legacy absolute converts against its own profile
- **WHEN** the database migrates with a recipe storing `temp_override_c` = 87 whose profile's espresso_temperature is 90
- **THEN** the recipe's `temp_offset_c` becomes −3 and behaves identically to before the migration on an unchanged profile

#### Scenario: Unresolvable profile drops the pin
- **WHEN** the database migrates with a recipe whose profile title matches no profile file and that has no embedded profile JSON
- **THEN** the recipe migrates with offset 0 and activates at whatever temperature its profile-load stage yields

#### Scenario: Tea recipes round-trip through the migration
- **WHEN** a portafilter-tea recipe storing an absolute 80° on an 88° tea profile migrates
- **THEN** it stores offset −8, activation still targets 80°, and the wizard's tea temperature field still shows 80

#### Scenario: Import from a legacy-version device converts
- **WHEN** recipes are imported from a source database that has `temp_override_c` but no `temp_offset_c` column
- **THEN** the conversion pass runs on the imported rows, producing the same offsets the local migration would have

#### Scenario: Import from a current-version device never reconverts
- **WHEN** recipes are imported from a source that has `temp_offset_c` (its dead `temp_override_c` still holding pre-migration absolutes), including a recipe whose offset the user reset to 0 after migrating
- **THEN** the imported recipe keeps offset 0 — the dead column is ignored

#### Scenario: An unconverted row inside a current-version source still converts
- **WHEN** recipes are imported from a source that has `temp_offset_c` but whose deferred conversion never ran (a row with a NULL offset and `temp_override_c` = 87)
- **THEN** the row imports as unconverted and the destination's conversion pass produces the same offset the source's own pass would have — the pin is not flattened to 0

#### Scenario: Promote-from-shot stores an offset
- **WHEN** a shot pulled with an absolute brew temperature override of 87 on a 90° profile is promoted to a recipe
- **THEN** the new recipe stores `temp_offset_c` = −3 (converted at promotion time against the shot's profile)

### Requirement: Tea temperatures are edited absolute, stored as the same offset
Portafilter-tea recipes SHALL store temperature in `temp_offset_c` with the same delta semantics. The wizard's tea field SHALL stay absolute, loading as profile `espresso_temperature` plus offset and saving as entered minus that. If the profile cannot be resolved, the field SHALL be disabled and the offset preserved. Hot-water tea SHALL store no pin, and its migration SHALL drop a legacy absolute quietly.

#### Scenario: Editing a migrated tea recipe shows its absolute temperature
- **WHEN** the user opens the details of a tea recipe holding offset −8 on an 88° tea profile
- **THEN** the temperature field shows 80, and saving without touching it keeps offset −8 (no silent loss, no re-encoding)

#### Scenario: Tea save converts the entered absolute
- **WHEN** the user sets a tea recipe's temperature to 75 on an 88° profile and saves
- **THEN** the recipe stores offset −13 and activation targets 75°

#### Scenario: Hot-water tea has no temperature pin
- **WHEN** the user edits a hot-water tea recipe
- **THEN** no separate temperature field is offered; the summary shows the selected vessel's temperature, and the recipe stores offset 0

#### Scenario: Tea activation needs no special case
- **WHEN** a portafilter-tea recipe with offset -8 on an 88° profile is activated
- **THEN** it targets 80°, the same as the absolute the user entered, with no tea-specific branch

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

### Requirement: Water ratios are validated consistently
A water ratio SHALL be finite and positive, require an espresso yield basis, and resolve to supported whole grams. Every write SHALL validate the complete resulting recipe and reject invalid amounts, conflicting inputs and supplied non-string, empty or unknown amount modes without partial updates. Only an absent amount discriminator SHALL use legacy fixed semantics; derived targets SHALL NOT be authoritative.

#### Scenario: Conflicting amount inputs
- **WHEN** a recipe write supplies both an authoritative fixed water amount and a ratio
- **THEN** the write fails with a message identifying the conflict

#### Scenario: Reject malformed amount discriminator
- **WHEN** a recipe write supplies amountMode as a number, boolean, null, object, array, empty string or unsupported string
- **THEN** it fails before persistence rather than treating the supplied value as a legacy fixed block

#### Scenario: Derived target is not an amount input
- **WHEN** a recipe write includes a supplied resolvedTargetG alongside a valid water ratio
- **THEN** the derived value is ignored or removed from the writable specification
- **AND** the ratio and candidate's espresso target determine the validated water amount

#### Scenario: Validate a changed yield dependency
- **WHEN** an update changes only bagId or profileJson and makes an existing water ratio resolve outside the supported range
- **THEN** the complete candidate fails validation and neither the link nor the water specification is partially written

#### Scenario: Imported profile context is unavailable
- **WHEN** a static import has no installed-profile catalog and a structurally valid ratio inherits an espresso target unavailable to that import
- **THEN** it may preserve the recipe for later resolution
- **AND** it still rejects a known invalid explicit ratio or target and cannot enable water until activation resolves a supported plan

#### Scenario: Invalid ratio
- **WHEN** a recipe write supplies zero, a negative or non-finite ratio, or a ratio on profile-less hot-water tea
- **THEN** the write fails with a message identifying the invalid ratio or missing espresso basis

#### Scenario: Ratio result exceeds supported weight
- **WHEN** the configured ratio and espresso target would require more water than the supported weight target
- **THEN** the editor or write response identifies the unsupported result rather than silently changing the ratio

### Requirement: History preserves designed water and the amount used
Shots from ratio-water recipes SHALL retain both the water ratio specification and the resolved water target used for that drink. These SHALL remain distinct from actual water dispensed. Cloning, promoting a shot, exporting and importing SHALL preserve the designed ratio; legacy fixed snapshots SHALL remain fixed.

#### Scenario: Promote a ratio-water shot
- **WHEN** a shot designed at 2.5× with a 40 g espresso target is saved and promoted after the current espresso target has changed
- **THEN** the promoted recipe retains 2.5× and the shot retains its original 100 g water target
- **AND** neither is inferred from the measured espresso or water output

#### Scenario: Clone and transfer
- **WHEN** a ratio-water recipe is cloned or transferred through backup/import
- **THEN** its ratio amount specification survives unchanged

#### Scenario: Later recipe edit cannot replace a drink's water specification
- **WHEN** a drink starts with 2.5× water and its stored water specification is edited to 3× before its water step
- **THEN** that drink and its history retain 2.5× while the next setup uses 3×

#### Scenario: History uses the captured water-step target
- **WHEN** an Americano's intended espresso target changes from 40 g to 42 g before water starts at 2.5×
- **THEN** its captured target and saved history both use 105 g
- **AND** this is true whether water finishes before or after the espresso row is saved

### Requirement: Recipes own grind independently of bags
Grind SHALL always live on the recipe. Every recipe of a grind-bearing drink type, linked to a bag or not, SHALL store its own `grindPinned` (opaque text) and optional `rpmPinned`. Grind-less drink types (`tea` and `tea_hotwater`) SHALL store neither. A recipe's grind SHALL never be read from its bag at activation. Editing grind on an active recipe SHALL write to that recipe, not to its bag.

#### Scenario: Editing grind updates the active recipe, not the bag
- **WHEN** a recipe is active and the user edits its grind or rpm
- **THEN** that recipe's own `grindPinned`/`rpmPinned` change immediately
- **AND** no grind or RPM is written to the bag

#### Scenario: Sibling recipes on the same bag are independent
- **WHEN** two recipes are linked to the same bag and one recipe's grind is edited
- **THEN** the other recipe's own `grindPinned`/`rpmPinned` is unchanged, and no recipe reads grind from the bag

#### Scenario: A live grind change outside a recipe does not alter recipes
- **WHEN** the live grind is adjusted outside a recipe
- **THEN** it does not change any recipe grind or a bag grind field

### Requirement: Recipe creation never inherits a bag grind
A newly created recipe SHALL own its grind and RPM and SHALL NOT adopt them from a bag. Explicitly provided values, including an empty grind, SHALL be retained.

#### Scenario: Explicit empty grind
- **WHEN** recipe creation supplies an empty grind
- **THEN** the recipe stores no grind and does not inherit one

#### Scenario: Historical shot promotion
- **WHEN** a shot is promoted to a recipe
- **THEN** its recorded grind and RPM are used
