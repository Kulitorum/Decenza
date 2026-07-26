## MODIFIED Requirements

### Requirement: Repairs do not rewrite values a user set

Correcting extraction SHALL apply to how a profile is read and saved from now on. A stored
recipe block's values SHALL NOT be rewritten in place on the grounds that they disagree with the
frames, because a disagreeing value may be one the user set deliberately.

Removing the block entirely is not such a rewrite, and is permitted: every field it holds is
either re-derived from the frames on read, duplicated by a top-level key, or unread by any code
path — except `dose`, which is preserved as `recommended_dose` rather than discarded. No value a
user set is lost.

#### Scenario: Disagreeing values are not corrected in place

- **WHEN** a profile carries a recipe block whose values contradict its frames
- **THEN** no pass rewrites those values to match the frames
- **AND** the profile reads its parameters from its frames the next time it is opened

#### Scenario: Removal preserves the one user-settable value

- **WHEN** the upgrade removes a recipe block that carried a dose the user set
- **THEN** that dose is preserved as the profile's recommended dose

## REMOVED Requirements

### Requirement: A recipe block is written only when parameters were established

**Reason**: Superseded by "No recipe block is written" in the `recipe-block-retirement`
capability. The requirement existed to stop fabricated blocks being written from
default-constructed parameters (REC-1). Writing no block at all satisfies that concern outright,
and removes the stale-cache class of defect the narrower rule left open — five shipped A-Flow
built-ins carried boilerplate blocks contradicting their own frames while fully complying with
this requirement, because their parameters had been "established" at some point in the past.

**Migration**: No caller action. Editor parameters were already derived from frames on every read
(`getOrConvertRecipeParams`), so nothing that reads a profile changes behaviour. Existing stored
blocks are removed by the one-time upgrade, with a set `dose` promoted to `recommended_dose`.
