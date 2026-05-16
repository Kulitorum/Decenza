## ADDED Requirements

### Requirement: D-Flow / La Pavoni SHALL resolve to its own KB section, not the base D-Flow section

The Profile Knowledge Base (`resources/ai/profile_knowledge.md`) SHALL parse `D-Flow / La Pavoni` into its own `ProfileKnowledge` record with its own canonical name, distinct from `D-Flow / default`'s. `D-Flow / La Pavoni` SHALL NOT be an `Also matches:` alias of the base `## D-Flow` section. The new section's title SHALL NOT contain `" / "` (the parser splits titles on `" / "`; a `## D-Flow / La Pavoni` header would register the bare key `d-flow` and collide with the base section) — resolution SHALL be via `Also matches: "D-Flow / La Pavoni"`, the same construction the shipped `## D-Flow Q variant` section uses.

This requirement constrains KB content the existing `loadProfileKnowledge()` parser consumes; it does not change the parser, the relative-grinder-setting anchor algorithm, or the canonical/inferred `source` semantics. `D-Flow / La Pavoni` SHALL resolve to a strictly coarser (numerically greater) UGS than base `D-Flow / default` and SHALL be marked inferred (the same lower-pressure-target + 84°C-fill mechanism the shipped Q variant documents). The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok` and the "DO NOT flag declining pressure / pressurized soak / setpoint-vs-actual temperature gap" guidance) SHALL remain in effect for `D-Flow / La Pavoni` after the split.

#### Scenario: D-Flow / La Pavoni resolves to its own canonical name, distinct from default

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` are resolved
- **THEN** `canonicalNameForKbId(kbLP)` SHALL NOT equal `canonicalNameForKbId(kbBase)`

#### Scenario: D-Flow / La Pavoni resolves strictly coarser than base D-Flow and is inferred

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` are resolved
- **THEN** `ugsForKbId(kbLP)` SHALL be strictly greater than `ugsForKbId(kbBase)`
- **AND** `ugsInferredForKbId(kbLP)` SHALL be `true`

#### Scenario: Shared behavioral suppression is preserved for D-Flow / La Pavoni

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbLP = computeProfileKbId("D-Flow / La Pavoni", "dflow")` is resolved
- **THEN** `getAnalysisFlags(kbLP)` SHALL contain `flow_trend_ok`

#### Scenario: The split introduces exactly one section and no title collision

- **GIVEN** the shipped `profile_knowledge.md` before and after this change
- **WHEN** the `## ` heading count is compared and every built-in profile title is resolved
- **THEN** the heading count after SHALL be exactly the count before plus one
- **AND** every built-in profile title SHALL resolve to exactly one section (no key collides with the bare `d-flow` base key)
