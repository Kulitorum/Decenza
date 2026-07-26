## ADDED Requirements

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

### Requirement: A recipe block is written only when parameters were established

A recipe block SHALL be written to a profile only when its parameters were established — derived from frames or set by a user edit. A profile SHALL NOT be given a recipe block built from default-constructed parameters because its title identifies it as a recipe profile.

#### Scenario: An imported profile gains no fabricated block

- **WHEN** a profile with no recipe data is imported and serialized
- **THEN** no recipe block is written whose values did not come from that profile

#### Scenario: An edited profile stores what the user set

- **WHEN** a user edits a parameter and saves
- **THEN** the stored recipe block carries the edited value together with the values derived from the profile's own frames

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

Correcting extraction SHALL apply to how a profile is read and saved from now on. A stored recipe block SHALL NOT be rewritten in place on the grounds that it disagrees with its frames, because a disagreeing value may be one the user set deliberately.

#### Scenario: Stored profiles are not migrated

- **WHEN** the corrected extraction ships
- **THEN** no pass rewrites recipe blocks in already-saved profiles
- **AND** each such profile simply reads its parameters from its frames the next time it is opened
