# yield-anchor Specification

## Purpose
Defines the `YieldSpec` — a yield target as a value plus a `none` | `absolute` | `ratio` mode — and the rules that govern it: the last-written quantity is the anchor, the effective spec resolves through a recipe → bag → profile ladder, a measurement never changes the mode, a ratio survives a profile change while an absolute does not, a ratio is always resolved to grams before it reaches the machine, the resolved target is latched for the duration of a shot, and shots record the anchor that produced their target so promotion can copy it verbatim.

## Requirements

### Requirement: Yield is a spec, not a number
A yield target SHALL be a `YieldSpec`: a `value` (double) and a `mode` of `none`, `absolute` or `ratio`. `none` means no yield of its own; `absolute` makes `value` a gram target; `ratio` makes it a dose multiplier. The spec SHALL be one value column plus one mode column, so an absolute and a ratio cannot coexist. `mode = ratio` SHALL be the only definition of ratio-anchored; no code SHALL infer it from grams.

#### Scenario: A ratio and an absolute cannot coexist
- **WHEN** a recipe holding `{2.0, ratio}` is updated with an absolute yield of 36 g
- **THEN** it holds `{36.0, absolute}` and no ratio remains anywhere in the row

#### Scenario: A partial update cannot leave a stale sibling
- **WHEN** an MCP or web client sends only `yieldRatio` to a recipe that previously held an absolute yield
- **THEN** the recipe holds only the ratio; the previous absolute is gone with no explicit clear required from the client

#### Scenario: A derived yield equal to the profile target stays ratio-anchored
- **WHEN** a `{2.0, ratio}` anchor resolves against an 18 g dose to exactly 36 g
- **AND** the active profile's `target_weight` is also 36 g
- **THEN** the anchor SHALL still report as ratio-anchored, and a later dose change SHALL still re-derive the target

#### Scenario: A ratio renders as a bare ratio

- **WHEN** a yield is stored as `value` 2.0 with mode `ratio`
- **THEN** it is displayed as `1:2`

### Requirement: The anchor is whichever quantity was last written
For any surface presenting both a ratio and a yield, the system SHALL treat the last written of the two as the anchor, derive the other through the dose, and store only the anchor as the spec. Writing a ratio SHALL set `mode = ratio`, and writing an absolute yield SHALL set `mode = absolute`. The mode SHALL NOT be a user-selected control or setting. Deriving the non-anchored quantity SHALL NOT mark it as an override when the dose moves.

#### Scenario: Editing the ratio anchors the ratio
- **WHEN** the user edits the ratio control
- **THEN** the anchor becomes `ratio` and the yield is shown derived as `ratio × dose`

#### Scenario: Editing the yield anchors the yield
- **WHEN** the user edits the yield (stop-at) control
- **THEN** the anchor becomes `absolute` and the ratio is shown derived as `yield ÷ dose`

#### Scenario: Switching anchors loses nothing
- **WHEN** the user enters a yield of 36 g, then picks a ratio of 1:2, then returns to the yield control
- **THEN** the yield control shows 36 g (derived from `2.0 × 18`), not an empty field
- **AND** only one of the two is stored at any point

#### Scenario: A dose change does not read as an override
- **WHEN** a `{2.0, ratio}` anchor is active and the dose moves from 18 g to 17.5 g
- **THEN** the derived yield moves from 36 g to 35 g
- **AND** neither the ratio nor the yield renders as deviating from its baseline

### Requirement: Yield resolves through a recipe → bag → profile ladder
The system SHALL resolve the effective yield spec from, in order: the active recipe's spec when its mode is not `none`; the active bag's spec when its mode is not `none`; then the active profile's `target_weight`, always `absolute`. The ladder SHALL be enforced explicitly, not by signal ordering. A profile SHALL never store a ratio.

#### Scenario: Recipe outranks bag
- **WHEN** a `{2.0, ratio}` recipe is activated and activation selects that recipe's own linked bag, which holds `{40.0, absolute}`
- **THEN** the session anchor is the recipe's `{2.0, ratio}`
- **AND** the bag's spec is not applied while that recipe stays active
- **AND** this is enforced by the ladder, not by the order in which the activation and bag-selection signals arrive

#### Scenario: Bag answers when no recipe is active
- **WHEN** no recipe is active and the user selects a bag holding `{3.0, ratio}`
- **THEN** the session anchor becomes `{3.0, ratio}` and the target derives from the current dose

#### Scenario: Profile answers when neither has a spec
- **WHEN** no recipe is active and the active bag's mode is `none`
- **THEN** the effective yield is the profile's `target_weight`, absolute

#### Scenario: Bean switch leaves the recipe anchor alone

- **WHEN** a bag's spec is applied on a bean switch while a recipe is active
- **THEN** the active recipe's anchor is unchanged

### Requirement: Persist writes back to the resolved store
A persist action SHALL write back to the same store the ladder resolved from: Update Recipe when the recipe supplied the spec, and Update Bag otherwise. The store being edited SHALL always be the store being shown.

#### Scenario: Recipe with mode none persists to the bag

- **WHEN** a recipe is active with mode `none` and the bag supplies the spec
- **THEN** the persist action reads Update Bag and writes the bag

### Requirement: A measurement never changes the anchor

A dose reading — from the scale's stable-weight capture, a manual dose edit, MCP, or settings import — SHALL always update the dose, and SHALL NEVER change the yield mode.

A recipe's or bag's stored `doseG` SHALL be a seed, not a pin: it seeds the live dose on activation, after which a measured dose supersedes it.

#### Scenario: Dose capture with a ratio anchor re-derives the yield
- **WHEN** the anchor is `{2.0, ratio}` and a dose capture reads 17.5 g
- **THEN** the dose becomes 17.5 g and the target becomes 35 g
- **AND** the anchor is still `{2.0, ratio}`

#### Scenario: Dose capture with an absolute anchor leaves the yield alone
- **WHEN** the anchor is `{36.0, absolute}` and a dose capture reads 17.5 g
- **THEN** the dose becomes 17.5 g and the target stays 36 g
- **AND** the displayed ratio reads 1:2.06, un-highlighted, because the ratio is not the anchor

#### Scenario: Dose capture never consults a global ratio preset
- **WHEN** a dose capture lands
- **THEN** no yield SHALL be computed from `Settings.brew.lastUsedRatio`; only the active anchor's own ratio can re-derive a target

### Requirement: A ratio anchor survives a profile change within its beverage group; an absolute one does not
When a profile is loaded, the system SHALL clear a session anchor whose mode is `absolute`. It SHALL keep a `ratio` anchor when the new profile is in the same beverage group as the previous one, and clear it when the group changes. Groups are espresso (including an empty or unrecognised `beverage_type`), filter (`filter`, `pourover`) and tea (`tea`, `tea_portafilter`), compared trimmed and case-insensitively.

#### Scenario: Ratio persists across a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user loads a different espresso profile
- **THEN** the anchor remains `{2.0, ratio}` and the target re-derives against the current dose

#### Scenario: Absolute clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}`, neither an active recipe nor the active bag saves a yield, and the user loads a different profile
- **THEN** the anchor clears and the new profile's `target_weight` applies

#### Scenario: Ratio clears when the beverage group changes
- **WHEN** the session anchor is `{2.5, ratio}`, neither an active recipe nor the active bag saves a yield, and the user loads a profile whose `beverage_type` is `tea_portafilter`
- **THEN** the anchor clears and the tea profile's own `target_weight` applies (0 = no weight stop)

#### Scenario: Espresso to filter drops the espresso ratio
- **WHEN** the session anchor is `{2.0, ratio}` from an espresso profile and the user loads a `pourover` profile
- **THEN** the dialed ratio is cleared

#### Scenario: The bean's saved yield applies after the switch
- **WHEN** the active bag saves `{300.0, absolute}`, no recipe is active, and the user switches from an espresso profile to a filter profile
- **THEN** the anchor is `{300.0, absolute}`

#### Scenario: A recipe's saved yield outranks the bean's
- **WHEN** the active recipe saves `{2.2, ratio}`, its bag saves `{2.0, ratio}`, and a load leaves no anchor
- **THEN** the anchor is `{2.2, ratio}`

#### Scenario: A ratio dialed after the load applies
- **WHEN** a tea profile is loaded, the dose is 18 g, and the user then sets the anchor `{2.5, ratio}`
- **THEN** the target resolves to 45 g

#### Scenario: A cleaning run neither uses nor clears the ratio
- **WHEN** the session anchor is `{3.0, ratio}` on a filter profile and the user loads a cleaning profile, then a filter profile
- **THEN** the cleaning run stops on its own `target_weight`, and the filter profile's target resolves from `{3.0, ratio}` again

#### Scenario: Replaying a shot with no yield override
- **WHEN** a shot pulled at its profile's own target is loaded from history while the active bag saves a ratio
- **THEN** the session has no yield anchor and the profile's `target_weight` applies

### Requirement: Maintenance profiles neither use nor clear overrides
A maintenance profile (`cleaning`, `descale`, `calibrate`) SHALL neither use nor clear any brew override, and SHALL NOT count as a group change. Reloading the drink profile loaded before a maintenance run SHALL keep every brew override.

#### Scenario: Maintenance run does not clear the ratio

- **WHEN** a maintenance profile is loaded between two drink shots with a ratio set
- **THEN** the ratio is still in effect when the drink profile is reloaded

### Requirement: Saved yield arms when no anchor remains
When a load leaves no anchor, the system SHALL arm the active recipe's saved yield, else the active bag's, except on a maintenance profile. A recipe's gram yield equal to the profile's own `target_weight` SHALL NOT be armed. A ratio set after the load SHALL apply normally.

#### Scenario: Recipe yield equal to the profile target is not armed

- **WHEN** the active recipe's gram yield equals the loaded profile's `target_weight`
- **THEN** no yield is armed from the recipe

### Requirement: A ratio never reaches the machine

The system SHALL resolve the effective yield spec to grams **before** `MachineState::setTargetWeight`. `MachineState`, `WeightProcessor`, the MQTT `target_weight` entity, and the shot's `yield_override` column SHALL only ever see resolved grams.

#### Scenario: Machine and MQTT see grams
- **WHEN** a `{2.0, ratio}` anchor is active with an 18 g dose
- **THEN** `MachineState::targetWeight()` returns 36.0
- **AND** the MQTT `target_weight` topic publishes `36.0` with `unit_of_measurement: "g"`

#### Scenario: Quality detectors are unaffected
- **WHEN** a shot pulled under a ratio anchor is analysed
- **THEN** the yield-overshoot and yield-shortfall arms compute against the resolved gram target exactly as for an absolute anchor, with no ratio-specific branch

### Requirement: The resolved target is latched for the duration of a shot
The system SHALL latch the resolved gram target, not merely the dose, when the espresso cycle starts. While latched, a write to any input of the ladder (dose, session anchor, anchor clear, bag switch, recipe activation or profile load) SHALL NOT move the live stop-at-weight target. The latched target SHALL be pushed to the machine at latch time. The latch SHALL be event-driven, never a timer.

#### Scenario: A mid-shot dose write does not move the target
- **WHEN** a shot is running under a `{2.0, ratio}` anchor latched at an 18 g dose
- **AND** any surface writes a dose of 20 g while the shot is in progress
- **THEN** the stop target remains 36 g for the remainder of that shot

#### Scenario: A mid-shot anchor clear does not move the target
- **WHEN** a shot is running at a latched 45 g target
- **AND** a bean switch clears the session anchor mid-pour (or any surface writes a new anchor)
- **THEN** the stop target remains 45 g for the remainder of that shot

#### Scenario: A cycle aborted before flow releases the latch
- **WHEN** an espresso cycle is started and stopped during preheat, without ever flowing
- **THEN** the latch SHALL be released, and a subsequent target change SHALL move the machine's target normally
- **AND** the next shot SHALL resolve its own target from live state rather than re-latching the aborted shot's

#### Scenario: The next shot picks up the new dose
- **WHEN** that shot ends and a new dose of 20 g stands
- **THEN** the next shot's target derives as 40 g

#### Scenario: Bag switch during a pour does not move the target

- **WHEN** a bag switch during a pour would re-resolve a 45 g target to 36 g
- **THEN** the live stop-at-weight target stays at 45 g, because latching only the dose is not sufficient

### Requirement: The latch releases when the espresso cycle exits
The latch SHALL be released when the espresso cycle exits, not when extraction ends, and also when the connection is lost mid-cycle, which likewise leaves the cycle.

#### Scenario: Cycle left without flowing releases the latch

- **WHEN** the user stops during preheat, so the cycle exits without any flow
- **THEN** the latch is released and the next shot resolves a fresh target

### Requirement: Latching resolves against live state
Latching SHALL resolve the target against live state, never through a still-armed latch.

#### Scenario: Re-arming does not reuse the previous latch

- **WHEN** a new cycle latches while a previous latch is still armed
- **THEN** the new target is resolved from live inputs, not from the old latched value

### Requirement: Shots record the anchor that produced their target
The system SHALL record, on every saved shot, the resolved gram target and the anchor that produced it: the anchor's mode and value. The anchor value SHALL be stored, never derived at read time. These values SHALL be read from the shot's start-of-shot snapshot, not from live session state at save time, so the snapshot outlives the latch it was taken with.

#### Scenario: A mid-shot dose write does not reach the shot record
- **WHEN** a shot runs at a latched 45 g target and a dose capture lands while the cup is still filling
- **THEN** the saved shot records the 45 g target the shot ran at, not a target re-derived from the new dose

#### Scenario: A ratio shot records its ratio
- **WHEN** a shot is pulled at a `{2.0, ratio}` anchor with an 18 g dose
- **THEN** the shot records a resolved target of 36 g, mode `ratio`, and anchor value 2.0

#### Scenario: A post-shot dose correction does not rewrite the anchor
- **WHEN** the user corrects that shot's dose to 19 g on the review page
- **THEN** the shot still records mode `ratio` and anchor value 2.0 — not the 1.89 implied by 36 ÷ 19

#### Scenario: Legacy shots read as absolute
- **WHEN** a shot saved before this change is read
- **THEN** its mode is `absolute` with an anchor value equal to its recorded target when that target is greater than zero, and `none` otherwise

### Requirement: Promotion copies the shot's anchor

When a recipe is created from a shot, the recipe SHALL adopt the shot's recorded anchor — mode and value — unchanged. The system SHALL NOT reconstruct a ratio from the shot's target and dose.

#### Scenario: Promoting a ratio shot yields a ratio recipe
- **WHEN** the user promotes a shot recorded with mode `ratio`, anchor value 2.0
- **THEN** the new recipe holds `{2.0, ratio}`

#### Scenario: Promoting an absolute shot yields an absolute recipe
- **WHEN** the user promotes a shot recorded with mode `absolute`, anchor value 36.0
- **THEN** the new recipe holds `{36.0, absolute}`

#### Scenario: A corrected dose does not distort a promoted ratio
- **WHEN** a shot pulled at 18 g → 36 g under a 1:2 anchor has its dose corrected to 19 g and is then promoted
- **THEN** the recipe holds `{2.0, ratio}` and a `doseG` of 19 g — no implicit 1:1.89 is minted

### Requirement: A ratio is bounded for every drink kind

The system SHALL clamp a ratio to 0.5–100 at every write boundary: Brew Settings, the ratio presets, recipes, bags, MCP and the web pages. The bound SHALL be defined once in C++ and read by QML rather than repeated.

#### Scenario: A filter ratio is stored as written
- **WHEN** a bag's yield is saved as `{16.0, ratio}`
- **THEN** the bag holds `{16.0, ratio}`

#### Scenario: A ratio above the bound clamps
- **WHEN** a bag's yield is saved as `{150.0, ratio}`
- **THEN** the bag holds `{100.0, ratio}`

#### Scenario: A filter ratio resolves against the dose
- **WHEN** the session anchor is `{16.0, ratio}` and the dose is 18 g
- **THEN** the resolved target is 288 g
