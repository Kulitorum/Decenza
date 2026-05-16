## ADDED Requirements

### Requirement: The Profile Knowledge Base SHALL assign distinct UGS positions to pressure-target-distinct profile variants

The Profile Knowledge Base (`resources/ai/profile_knowledge.md`) SHALL NOT encode a single UGS value for a group of profile variants whose pressure targets differ materially. Variants that share behavioral guidance (the family abstraction) but differ in pressure target or fill temperature SHALL be parsed into distinct `ProfileKnowledge` records with distinct `ugs` values and distinct canonical names, so that cross-profile grind transfer produces a directional grinder adjustment rather than treating them as grind-equivalent.

This requirement constrains the KB content the existing `loadProfileKnowledge()` parser consumes; it does not change the parser, the relative-grinder-setting anchor algorithm, or the canonical/inferred `source` semantics. The base D-Flow position SHALL remain the chart-authoritative canonical `0.5`. D-Flow/Q (alias "Damian's Q") SHALL resolve to a strictly coarser (numerically greater) UGS than base D-Flow and SHALL be marked inferred. Damian's LRv3 SHALL resolve to canonical UGS `0` (the chart's "Londinium / LRv3" position). The shared behavioral false-positive suppression (`AnalysisFlags: flow_trend_ok` and the "DO NOT flag declining pressure / pressurized soak" guidance) SHALL remain in effect for every D-Flow variant after the split.

#### Scenario: Base D-Flow keeps the chart-authoritative canonical UGS

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `computeProfileKbId("D-Flow / default", "dflow")` is resolved and `ugsForKbId` / `ugsInferredForKbId` are read for the result
- **THEN** `ugsForKbId(kbId)` SHALL be `0.5`
- **AND** `ugsInferredForKbId(kbId)` SHALL be `false`

#### Scenario: D-Flow/Q resolves strictly coarser than base D-Flow and is inferred

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbBase = computeProfileKbId("D-Flow / default", "dflow")` and `kbQ = computeProfileKbId("D-Flow / Q", "dflow")` are resolved
- **THEN** `ugsForKbId(kbQ)` SHALL be strictly greater than `ugsForKbId(kbBase)`
- **AND** `ugsInferredForKbId(kbQ)` SHALL be `true`
- **AND** `canonicalNameForKbId(kbQ)` SHALL NOT equal `canonicalNameForKbId(kbBase)`

#### Scenario: "Damian's Q" resolves to the same coarser inferred position as D-Flow/Q

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbQ = computeProfileKbId("D-Flow / Q", "dflow")` and `kbDamianQ = computeProfileKbId("Damian's Q", "dflow")` are resolved
- **THEN** `canonicalNameForKbId(kbDamianQ)` SHALL equal `canonicalNameForKbId(kbQ)`
- **AND** `ugsForKbId(kbDamianQ)` SHALL equal `ugsForKbId(kbQ)`

#### Scenario: Damian's LRv3 resolves to the canonical Londinium/LRv3 position

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** `kbLrv3 = computeProfileKbId("Damian's LRv3", "dflow")` is resolved
- **THEN** `ugsForKbId(kbLrv3)` SHALL be `0`
- **AND** `ugsForKbId(kbLrv3)` SHALL be strictly less than `ugsForKbId(computeProfileKbId("D-Flow / default", "dflow"))`

#### Scenario: Shared behavioral suppression is preserved for every D-Flow variant

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** analysis flags are read for the kbIds of "D-Flow / default", "D-Flow / Q", and "Damian's LRv2"
- **THEN** `getAnalysisFlags(kbId)` SHALL contain `flow_trend_ok` for each of the three variants
