# yield-anchor Specification

## Purpose
Defines the `YieldSpec` — a yield target as a value plus a `none` | `absolute` | `ratio` mode — and the rules that govern it: the last-written quantity is the anchor, the effective spec resolves through a recipe → bag → profile ladder, a measurement never changes the mode, a ratio survives a profile change while an absolute does not, a ratio is always resolved to grams before it reaches the machine, the resolved target is latched for the duration of a shot, and shots record the anchor that produced their target so promotion can copy it verbatim.
## Requirements
### Requirement: Yield is a spec, not a number

The system SHALL represent a yield target as a `YieldSpec`: a `value` (double) plus a `mode` enum of `none` | `absolute` | `ratio`.

- `none` — no yield of its own; the next rung of the ladder answers.
- `absolute` — `value` is a gram target.
- `ratio` — `value` is a multiplier of the dose (2.0 renders as `1:2`).

A `YieldSpec` SHALL be stored as one value column plus an explicit mode column, never as two parallel value columns. Holding both an absolute and a ratio at once SHALL be **unrepresentable** rather than an invariant enforced by convention — writing a ratio *is* setting the mode, so no stale sibling value can survive a partial write from any surface.

`mode = ratio` SHALL be the only definition of "this yield is ratio-anchored". No code SHALL infer it by comparing a resolved gram value against the profile's target weight.

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

### Requirement: The anchor is whichever quantity was last written

For any surface presenting both a ratio and a yield, the system SHALL treat the **last written** of the two as the anchor. The other SHALL be derived through the dose and presented as a consequence; only the anchor SHALL be stored as the spec. (A saved shot additionally records the gram target that actually ran — that is outcome, not spec; see "Shots record the anchor that produced their target".)

Writing a ratio SHALL set `mode = ratio`; writing an absolute yield SHALL set `mode = absolute`. The mode SHALL NOT be a separate control the user selects, and SHALL NOT be a user setting.

Deriving the non-anchored quantity SHALL NOT mark it as deviating from its baseline: when the dose moves, the derived value and its derived baseline move together, so neither the ratio nor the yield reads as an override purely because the dose changed.

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

The system SHALL resolve the effective yield spec in this order:

1. The active recipe's spec, when a recipe is active and its mode is not `none`.
2. The active bag's spec, when a bag is active and its mode is not `none`.
3. The active profile's `target_weight`, always `absolute`.

A persist action SHALL write back to the **same store the ladder resolved from** — *Update Recipe* when the recipe supplied the spec, *Update Bag* when the bag did. When a recipe is active but its own mode is `none`, the ladder has fallen through to the bag, so the button SHALL read *Update Bag* and write the bag: the store being edited is always the store being shown. A profile SHALL never store a ratio — `target_weight` is absolute and profiles are shared and exported.

The ladder SHALL be enforced explicitly and SHALL NOT emerge from the ordering of signal delivery. In particular, applying a bag's spec on a bean switch SHALL NOT overwrite an active recipe's anchor.

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

### Requirement: A ratio anchor survives a profile change; an absolute one does not

When a profile is loaded, the system SHALL clear a session anchor whose mode is `absolute` (a gram target describes the profile it was set against) and SHALL preserve one whose mode is `ratio` (a ratio is profile-independent).

#### Scenario: Ratio persists across a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user loads a different profile
- **THEN** the anchor remains `{2.0, ratio}` and the target re-derives against the current dose

#### Scenario: Absolute clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}` and the user loads a different profile
- **THEN** the anchor clears and the new profile's `target_weight` applies

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

The system SHALL latch the **resolved gram target** — not merely the dose — when the espresso cycle starts, and SHALL release it when the espresso cycle **ends**. While latched, a write to ANY input of the resolution ladder (the dose, the session anchor, an anchor clear, a bag switch, a recipe activation, a profile load) SHALL NOT move the live stop-at-weight target. The latch SHALL be event-driven, never a timer.

Latching only the dose is insufficient and SHALL NOT be treated as satisfying this requirement: every *other* ladder input stays live during a shot, and each re-resolves and pushes a new value at the machine. This is not hypothetical — a bag switch during a pour dropped a 45 g target to 36 g and cut the shot short.

The release SHALL be driven by the espresso cycle's **exit**, not by extraction ending. The two are different events: a cycle can be entered and left without ever flowing (the user stops during preheat, the machine aborts, the connection drops), and a release gated on flow having started never fires for such a cycle — leaving the latch armed and every subsequent shot pinned to a stale target. The release SHALL therefore also fire when the connection is lost mid-cycle, which likewise leaves the cycle.

Latching SHALL resolve the target against live state, never through a still-armed latch: an implementation that reads its own latched value while re-arming turns a one-shot leak into a permanent one.

The latched target SHALL be pushed to the machine at latch time, so the value the machine stops at and the value the shot records cannot disagree.

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

### Requirement: Shots record the anchor that produced their target

The system SHALL record, on every saved shot, both the resolved gram target (existing behaviour) and the anchor that produced it: the anchor's mode and its value. The anchor value SHALL be stored, never derived at read time from the recorded target and dose.

These values SHALL be read from the shot's **start-of-shot snapshot**, not from live session state at save time. The save path runs after stop-at-weight settling — i.e. after the latch has released — so a live read would record whatever the dial drifted to during the pour rather than what the shot actually ran at. The snapshot SHALL therefore outlive the latch it was taken with.

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
