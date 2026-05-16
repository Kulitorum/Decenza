## ADDED Requirements

### Requirement: ProfileKnowledge SHALL expose an optional structured per-profile expected dial-in signature

`ShotSummarizer::ProfileKnowledge` SHALL carry an optional structured `ProfileExpectation` value (a present/absent flag plus parsed pressure, flow, and time expectation fields). `loadProfileKnowledge()` SHALL populate it, per section, from optional directive lines `ExpectedPressureBar:`, `ExpectedFlowMlS:`, and `ExpectedTimeSec:`, using the same prefixed-directive parsing approach as the existing `UGS:` / `AnalysisFlags:` lines.

The field SHALL be profile-agnostic — available to any `## ` section, never specific to one profile family. The capability exists for every profile that documents a dial-in signature; the well-documented profiles (Adaptive v2, Londinium, Blooming Espresso, E61, Allongé, Gentle & Sweet, and the D-Flow family) are its primary beneficiaries, not D-Flow alone. When a section carries none of the `Expected*:` lines, the parsed expectation SHALL be marked absent and the section SHALL parse and behave exactly as it does today (no behavioral change, no consumer obligation). When an `Expected*:` value cannot be parsed, that individual field SHALL degrade to absent rather than aborting the section parse or throwing. Expectation values that describe intentionally-variable behavior (e.g. a declining-pressure lever profile, or a grind-adaptive profile) SHALL be representable as a pattern/range token and SHALL NOT be coerced into a degenerate equal-bound range.

This requirement covers parsing and exposure only. No production code in this change consumes the `ProfileExpectation` struct; the deviation detector that would read it is out of scope and deferred to a separate `shot-analysis-pipeline` change. The existing `UGS:` parsing, the RGS anchor algorithm, and canonical/inferred `source` semantics are unchanged.

#### Scenario: A well-documented profile exposes a populated expectation

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** the `ProfileKnowledge` for the `Adaptive v2` section is read
- **THEN** its `ProfileExpectation` SHALL be marked present
- **AND** its parsed pressure, flow, and time fields SHALL reflect the section's `Expected*:` directive-line values

#### Scenario: Section without Expected lines exposes an absent expectation

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** the `ProfileKnowledge` for a section that carries no `Expected*:` lines is read
- **THEN** its `ProfileExpectation` SHALL be marked absent
- **AND** every other parsed field of that section (content, `ugs`, `AnalysisFlags`, aliases) SHALL be identical to the pre-change parse

#### Scenario: Malformed Expected value degrades to absent

- **GIVEN** a section containing an `ExpectedPressureBar:` line whose value cannot be parsed
- **WHEN** `loadProfileKnowledge()` parses the section
- **THEN** the pressure expectation field SHALL be absent
- **AND** the section parse SHALL complete without error and other fields SHALL be unaffected

#### Scenario: Variable-by-design value is preserved as a pattern, not a degenerate range

- **GIVEN** a section containing `ExpectedPressureBar: 6-9 declining`
- **WHEN** `loadProfileKnowledge()` parses the section
- **THEN** the parsed pressure expectation SHALL retain the range and the variability qualifier
- **AND** it SHALL NOT be reduced to an equal-bound (min == max) range

### Requirement: An archetype-diverse initial set of profiles SHALL carry a seeded expected dial-in signature

This change seeds `Expected*:` lines for an **initial set of five archetype-distinct profiles**, chosen to validate the schema across the full range of extraction shapes before bulk population: `Adaptive v2` (grind-adaptive), `Damian's LRv2/LRv3` (declining-pressure lever — also the #1160 section), `Blooming Espresso` (bloom with a hard never-max bound), `E61` / Flat 9 Bar (flat constant pressure), and `Allongé` / Rao Allongé (high-flow turbo). Each seeded value SHALL be traceable to that profile's existing source-tagged "Official dial-in target" / "Grind diagnostics" line(s) in `docs/PROFILE_KNOWLEDGE_BASE.md` (itself a source-tagged synthesis of Decent's blogs/videos, the official Decent guide, EAF, Scott Rao, and the community index); no value is invented.

The remaining well-documented profiles (base D-Flow, D-Flow/Q, standalone `Londinium`, `Gentle & Sweet`) and the long tail SHALL remain valid with an **absent** expectation and SHALL be populated by a separate follow-up change, gated on confirming the capability is a valuable addition. The D-Flow split, UGS correction, cross-profile tables, prose, and the parser/consumability/teaching surfaces are general and fully in scope here regardless — only the *count of seeded `Expected*:` data* is reduced to the validation set.

Intentionally-variable profiles SHALL be seeded with pattern/range tokens that encode the variability (e.g. a declining-pressure lever profile as `6-9 declining`, a grind-adaptive profile as a wide band), never a tight band that would misrepresent by-design behavior.

#### Scenario: Each of the five initial profiles resolves to a present expectation

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** the `ProfileKnowledge` is read for each of: `Adaptive v2`, `Damian's LRv2` (`computeProfileKbId("Damian's LRv2","dflow")`), `Blooming Espresso`, `E61`, `Allongé`
- **THEN** each resolved section's `ProfileExpectation` SHALL be marked present
- **AND** each SHALL carry at least one of pressure / flow / time consistent with that profile's documented dial-in target

#### Scenario: A deferred well-documented profile remains valid with an absent expectation

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** the `ProfileKnowledge` for base D-Flow (`computeProfileKbId("D-Flow / default","dflow")`) — split and UGS-corrected here but NOT in the initial seed set — is read
- **THEN** its `ProfileExpectation` SHALL be marked absent
- **AND** its split, `ugs`, `AnalysisFlags`, aliases, and prose SHALL otherwise be exactly as this change's other requirements specify

#### Scenario: An un-seeded profile remains valid with an absent expectation

- **GIVEN** the shipped `profile_knowledge.md` parsed by `loadProfileKnowledge()`
- **WHEN** the `ProfileKnowledge` for a profile NOT in the seed set (e.g. a niche community profile) is read
- **THEN** its `ProfileExpectation` SHALL be marked absent
- **AND** the section SHALL otherwise parse exactly as before this change

### Requirement: Per-profile expected dial-in guidance SHALL be consumable on both AI surfaces, read directionally

Every profile section that carries an expectation SHALL also carry that pressure / flow / time and grind-direction guidance as **qualitative prose** (not solely as terse `Expected*:` directive tokens), so the guidance is consumable by an LLM. This applies to every profile with a documented dial-in signature, not only the D-Flow family. Because the KB section content is shipped verbatim into both the in-app advisor system prompt and the `dialing_get_context` MCP response, the guidance for a given profile SHALL be the same content on both surfaces — the two AI paths SHALL NOT diverge in the expectation guidance they present.

The `shotAnalysisSystemPrompt` "How to read structured fields" section SHALL additionally teach that per-profile expectations are **directional, qualitative context — not a hard pass/fail rule**: intentional in-profile variability (e.g. declining pressure on lever/D-Flow profiles, a grind-adaptive profile that shifts peak pressure with grind) is normal and SHALL NOT be flagged as a fault, and a taste-based judgement (too sour → finer, too bitter → coarser) outranks any expectation-vs-actual curve comparison. This mirrors the existing directional gating taught for `tastingFeedback` and `beanFreshness`. The structured `ProfileExpectation` struct itself remains unconsumed by code in this change (the deterministic detector is deferred); this requirement governs only the prose surface and the teaching clause.

#### Scenario: A well-documented profile's guidance is identical across the MCP and in-app surfaces

- **GIVEN** the `Adaptive v2` section in the shipped KB
- **WHEN** the in-app advisor system prompt is rendered AND a `dialing_get_context` response is built for an Adaptive v2 shot
- **THEN** the Adaptive v2 expected dial-in guidance text SHALL appear on both surfaces
- **AND** it SHALL be the same content (the KB section is the single source for both)

#### Scenario: System prompt teaches directional, taste-deferring reading of expectations

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** rendered
- **THEN** the "How to read structured fields" section SHALL contain guidance that per-profile expectations are directional context, not a hard pass/fail rule
- **AND** SHALL state that intentional in-profile variability (e.g. declining pressure, grind-adaptive pressure) is normal and not a fault
- **AND** SHALL state that a taste-based judgement outranks an expectation-vs-actual curve comparison

### Requirement: The Profile Knowledge Base SHALL assign distinct UGS positions to pressure-target-distinct profile variants

This is the #1160 instance of the general principle that the KB must be per-profile accurate, applied to the D-Flow family. The Profile Knowledge Base (`resources/ai/profile_knowledge.md`) SHALL NOT encode a single UGS value for a group of profile variants whose pressure targets differ materially. Variants that share behavioral guidance (the family abstraction) but differ in pressure target or fill temperature SHALL be parsed into distinct `ProfileKnowledge` records with distinct `ugs` values and distinct canonical names, so that cross-profile grind transfer produces a directional grinder adjustment rather than treating them as grind-equivalent.

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
