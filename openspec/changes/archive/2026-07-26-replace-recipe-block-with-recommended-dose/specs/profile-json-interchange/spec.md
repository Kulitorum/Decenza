## ADDED Requirements

### Requirement: The emitted key set carries no derived cache

Every key the canonical serializer emits SHALL hold state that is independent of the frames, or
be a value derived from the frames at emit time. The serializer SHALL NOT emit a stored copy of
values it recomputes on read.

This is what separates the keys Decenza adds beyond de1app's set — `read_only`,
`recommended_dose`, `has_recommended_dose`, `mode` — from the retired `recipe` block. An
independent scalar cannot be invalidated by an edit to the frames; a cache of frame-derived
values can, and did.

#### Scenario: No emitted key duplicates frame-derived state

- **WHEN** a profile is serialized
- **THEN** no emitted key holds a stored copy of parameters that are reconstructed from the
  frames on read

#### Scenario: Independent scalars are still emitted

- **WHEN** a profile with a read-only flag, a hidden flag, temperature presets and a recommended
  dose is serialized
- **THEN** each of those keys is present, because none of them is derived from the frames
