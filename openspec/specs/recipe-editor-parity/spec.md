# recipe-editor-parity Specification

## Purpose
TBD - created by archiving change verify-recipe-editor-parity. Update Purpose after archive.
## Requirements
### Requirement: The upstream plugins are the parity oracle

Decenza's D-Flow and A-Flow implementations SHALL be verified against the upstream de1app plugins — `Damian-AU/D_Flow_Espresso_Profile` and `Jan3kJ/A_Flow` — as the source of truth. Every expected value in the parity suite SHALL trace to a named proc in the plugin source or to a profile the plugin itself ships. No expected value SHALL be derived from Decenza's own code or from Decenza's built-in profile JSONs.

Fixtures SHALL come from the plugin's own profile directory. The copies under de1app's `de1plus/profiles/` SHALL NOT be used as the reference, because four A-Flow profiles there are a stale 6-frame snapshot that shadows the plugin's 9-frame originals (de1app issue #350).

#### Scenario: Expected values are traceable

- **WHEN** the parity suite asserts an expected frame field or parameter value
- **THEN** that expectation cites the plugin proc it was transcribed from
- **AND** where the rule is also exercised by a shipped stock profile, the suite verifies it against that profile as well

#### Scenario: Stale snapshot is not used as reference

- **WHEN** an A-Flow fixture is loaded
- **THEN** it comes from the plugin's `profiles/` directory
- **AND** a fixture carrying the stale 6-frame layout is treated as the legacy-layout case, never as the expected output of the current editor

### Requirement: Frame generation matches the plugin

For a given set of editor parameters, Decenza SHALL generate the frames the corresponding plugin's `update_*` proc generates, field for field.

#### Scenario: D-Flow derives fill pressure from soak pressure

- **WHEN** frames are generated for a D-Flow profile
- **THEN** the fill frame's pressure equals the soak pressure
- **AND** the fill frame's pressure-over exit equals the soak pressure when that is below 2.8, otherwise `soak / 2 + 0.6`, and is never below 1.2

#### Scenario: D-Flow and A-Flow take the soak temperature from different parameters

- **WHEN** frames are generated for a D-Flow profile
- **THEN** the soak frame's temperature comes from the pour temperature
- **WHEN** frames are generated for an A-Flow profile
- **THEN** the soak frame's temperature comes from the fill temperature

#### Scenario: A-Flow toggle matrix

- **WHEN** A-Flow frames are generated for each combination of ramp-down, flow-up, and second-fill
- **THEN** every generated frame field matches what the plugin produces for that combination
- **AND** the ramp time splits between the ramp-up and ramp-down frames the way the plugin splits it, including when the value is odd

#### Scenario: Fields the plugin leaves alone are not overwritten

- **WHEN** Decenza generates frames for a profile the plugin would round-trip untouched
- **THEN** no frame field that the plugin's `update_*` proc never writes is changed from its value in the source profile

### Requirement: Parameter extraction matches the plugin

Decenza SHALL recover from a profile's frames the same editor parameters the plugin's `prep` proc recovers, without relying on any stored recipe data.

#### Scenario: Parameters come from the frames

- **WHEN** a D-Flow or A-Flow profile is loaded
- **THEN** its editor parameters are derived from its frames
- **AND** the derived values equal what the plugin's `prep` proc would set

#### Scenario: A-Flow structural toggles are derived from frame structure

- **WHEN** an A-Flow profile is loaded
- **THEN** ramp-down is enabled exactly when the ramp-down frame has a non-zero duration
- **AND** flow-up is enabled exactly when the extraction frame's flow exceeds the pour flow
- **AND** second-fill is enabled exactly when the layout carries the pause frame and that frame has a non-zero duration

#### Scenario: Pour pressure comes from the limiter, not the pressure field

- **WHEN** a D-Flow profile is loaded
- **THEN** the pour pressure parameter is taken from the pour frame's maximum-flow-or-pressure limiter, not from its pressure setpoint

### Requirement: Loading and saving a profile unchanged preserves it

Loading a stock D-Flow or A-Flow profile and saving it without editing SHALL leave every frame field unchanged.

#### Scenario: Every stock profile is a round-trip fixed point

- **WHEN** any profile shipped by either plugin is loaded and re-serialized with no edit
- **THEN** every frame field is identical to the source
- **AND** this holds for all five stock A-Flow profiles and all stock D-Flow profiles

#### Scenario: A profile is not rewritten from defaults

- **WHEN** a profile is loaded whose editor parameters cannot be established
- **THEN** its frames are preserved as-is rather than regenerated from default parameters

### Requirement: Both A-Flow frame layouts are handled

Decenza SHALL handle the A-Flow 9-frame layout and the legacy 6-frame layout, resolving frame roles by the same rule the plugin's `set_profile_index` uses.

#### Scenario: Legacy layout resolves the correct frame roles

- **WHEN** a 6-frame A-Flow profile is loaded
- **THEN** each frame role resolves to the index the plugin assigns for that layout
- **AND** the parameters extracted match those the plugin would extract

#### Scenario: Legacy layout is upgraded the way the plugin upgrades it

- **WHEN** a 6-frame A-Flow profile is edited and saved
- **THEN** the result carries the frames the plugin's upgrade path inserts, with the plugin's values

### Requirement: Parameters Decenza exposes beyond the plugin are declared

Where Decenza's editor exposes a parameter its plugin does not, that difference SHALL be recorded with its effect on a profile the plugin would round-trip untouched. An undeclared divergence SHALL be treated as a defect.

#### Scenario: Each extra parameter has a verdict

- **WHEN** the parity suite runs
- **THEN** every editor parameter with no counterpart in the plugin is listed with a verdict of deliberate extension or defect
- **AND** for a deliberate extension, the frame fields it writes that the plugin leaves untouched are named

### Requirement: The editor surfaces the plugin's parameters

Each editor SHALL surface the parameters its plugin exposes, and changing one SHALL move the frame fields the plugin moves.

#### Scenario: Editor coverage

- **WHEN** the D-Flow or A-Flow editor is opened
- **THEN** every parameter the plugin exposes is editable
- **AND** changing one alters the same frame fields the plugin's `update_*` proc alters, and no others

### Requirement: Identical frames reach the machine as identical bytes

Given the same frames, Decenza's BLE encoders SHALL produce the bytes de1app's `de1_packed_shot` produces. The comparison SHALL be against de1app's real packer executed as an oracle, not against transcribed expectations, and SHALL cover the quantisation boundaries no stock profile reaches.

#### Scenario: Stock profiles pack identically

- **WHEN** any stock D-Flow or A-Flow profile is packed for upload
- **THEN** the header and every frame is byte-identical to de1app's output
- **AND** any byte that differs is recorded as a finding rather than accepted

#### Scenario: Quantisation boundaries agree

- **WHEN** values are packed that sit on an encoder's rounding boundary — the fixed-point ties, the split-format switchover, and the 10-bit wrap
- **THEN** the encoded bytes match de1app's for every such value

### Requirement: One edit produces the plugin's frames

For every parameter a plugin exposes and every profile that plugin ships, changing that one parameter through the app's own save path SHALL produce the frames the plugin's `prep` + `update_*` produce from the same starting profile and the same edit.

This is verified through the interface the editor and MCP both use, not through the generator in isolation — a generator that is correct in isolation can still be fed wrong parameters by the layer above it.

#### Scenario: Every parameter × every stock profile

- **WHEN** a single editable parameter is changed on a stock profile and the profile is saved
- **THEN** every frame field matches the plugin's output for that same edit
- **AND** a divergence is reported for every field that differs, not only the first

#### Scenario: An unedited parameter is not silently reset

- **WHEN** one parameter is edited
- **THEN** no frame field belonging to a parameter that was not edited takes a value absent from the source profile

### Requirement: A confirmed defect is recorded, not hidden

A parity failure the change does not repair SHALL remain expressed as a failing or expected-to-fail assertion identifying the finding. An assertion SHALL NOT be weakened to pass over a known defect.

#### Scenario: Known defects stay visible

- **WHEN** the suite finds a divergence that is not repaired in this change
- **THEN** the assertion remains in the suite marked as an expected failure carrying the finding's identifier
- **AND** the finding is described in the change's findings document

### Requirement: Editor parameters are derived from the profile's frames

For a D-Flow or A-Flow profile, the parameters shown in the editor and returned to callers SHALL be derived from the profile's frames by the rule the corresponding plugin's `prep` proc uses. A stored recipe block SHALL NOT override a value the frames determine.

Frame roles SHALL be resolved positionally, by the plugin's `set_profile_index` rule, for both the 9-frame and legacy 6-frame layouts. Roles SHALL NOT be inferred from frame names or from pattern-matching the frame sequence.

#### Scenario: A profile with no recipe block yields its own parameters

- **WHEN** a profile arriving without any recipe data — a `.tcl` import, a Visualizer download, a profile shared from another app — is opened in the editor
- **THEN** every parameter shown equals what the plugin's `prep` recovers from those frames

#### Scenario: Frames win over a disagreeing stored block

- **WHEN** a profile carries a recipe block whose values contradict its frames
- **THEN** the parameters used are those derived from the frames

#### Scenario: A-Flow parameters use A-Flow's rules

- **WHEN** an A-Flow profile is opened
- **THEN** pour flow comes from the `Flow Start` frame
- **AND** ramp time is the sum of the ramp-up and ramp-down frame durations
- **AND** ramp-down, flow-up and second-fill are each derived from frame structure

### Requirement: A single edit changes only what the plugin changes

Changing one editor parameter and saving SHALL produce the frames the plugin's `prep` + `update_*` produce from the same starting profile and the same edit, for every parameter either plugin exposes and every profile either plugin ships.

#### Scenario: Editing one parameter does not reset another

- **WHEN** a parameter is edited on a profile whose fill temperature differs from the editor's default
- **THEN** the fill temperature in the saved frames is the profile's own value

#### Scenario: The edit matrix is clear

- **WHEN** the edit matrix is run against the pinned plugin commits
- **THEN** every case either matches the plugin's frames exactly or is a recorded, justified difference
- **AND** no golden has been adjusted to match Decenza's output

### Requirement: Only fields the plugin writes are written

Generating frames SHALL NOT set a frame field that the corresponding plugin's `update_*` proc never writes. Such a field SHALL retain the value it had in the source profile.

#### Scenario: Stop caps are preserved

- **WHEN** a profile whose pour frame carries a volume cap is loaded and saved
- **THEN** that cap is unchanged, and is not replaced by a value that the firmware reads as "no limit"

#### Scenario: Fill frame fields are left alone

- **WHEN** frames are generated for a D-Flow or A-Flow profile
- **THEN** the fill frame's volume, weight and duration are those of the source profile, not generator constants

### Requirement: The editor exposes no parameter its plugin lacks

Every parameter the recipe editors expose SHALL correspond to a parameter its plugin exposes. A parameter with no counterpart, no user-facing surface, and no recorded justification SHALL be removed rather than retained.

#### Scenario: No vestigial parameters remain

- **WHEN** the editor's parameter set is compared against the plugins'
- **THEN** each parameter maps to a plugin parameter or carries a recorded verdict of deliberate extension
- **AND** no parameter writes a frame field the plugin leaves untouched

### Requirement: Packed frames are byte-identical to de1app

Frames sent to the machine SHALL encode to the bytes de1app's `de1_packed_shot` produces for the same frames, with no differing byte.

#### Scenario: No residual byte difference

- **WHEN** any stock profile, or any profile from the quantisation-boundary corpus, is packed
- **THEN** every byte of the header and of every frame matches de1app's output

### Requirement: Repairs do not rewrite values a user set

Correcting extraction SHALL apply to how a profile is read and saved from now on. A stored
recipe block's values SHALL NOT be rewritten in place on the grounds that they disagree with the
frames, because a disagreeing value may be one the user set deliberately.

Removing the block entirely is not such a rewrite, and is permitted: every field it holds is
either re-derived from the frames on read, duplicated by a top-level key, or unread by any code
path — except `dose`, which is preserved as `recommended_dose` rather than discarded. No value a
user set is lost.

#### Scenario: Stored profiles are not migrated

- **WHEN** the corrected extraction ships
- **THEN** no pass rewrites the VALUES held in an already-saved profile
- **AND** each such profile reads its parameters from its frames the next time it is opened
- **AND** removing the recipe block is not such a rewrite: it drops a key no reader consults,
  preserving the one value in it a user could set

#### Scenario: Disagreeing values are not corrected in place

- **WHEN** a profile carries a recipe block whose values contradict its frames
- **THEN** no pass rewrites those values to match the frames
- **AND** the profile reads its parameters from its frames the next time it is opened

#### Scenario: Removal preserves the one user-settable value

- **WHEN** the upgrade removes a recipe block that carried a dose the user set
- **THEN** that dose is preserved as the profile's recommended dose

