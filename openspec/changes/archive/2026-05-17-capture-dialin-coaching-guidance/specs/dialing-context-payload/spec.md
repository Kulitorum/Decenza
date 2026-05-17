## ADDED Requirements

### Requirement: ProfileExpectation SHALL express a two-sided grind diagnostic

This change introduces the entire `ProfileExpectation` seam (struct + parser on `ProfileKnowledge`, mirroring the existing `ugs`/`ugsInferred` precedent). `ProfileExpectation` SHALL support an optional centred envelope (`ExpectedPressureBar:` / `ExpectedFlowMlS:` / `ExpectedTimeSec:`) AND optional directive lines expressing the diagnostic arms and curve shape for a profile:

- `GrindTooCoarse:` — the observable signature of an under-extracting (too coarse) grind for this profile (e.g. `peak < 7 bar; pressure crash; sharp acidity`).
- `GrindTooFine:` — the observable signature of an over-extracting (too fine) grind (e.g. `pressure > 9 bar; reduced flow; much longer time`).
- `PressureShape:` — the intended pressure-curve qualifier, one of `declining` / `flat` / `rising-bad` / `crash` (or a profile-specific pattern token).
- `PreinfusionDripG:` — the expected preinfusion-dripping grams with optional too-coarse/too-fine bounds (e.g. `~4` or `2-8; >17 = too coarse`).
- `FlowStability:` — an optional flow-stability qualifier (e.g. `flow does not dip during extraction`).

Every directive remains optional. A value MUST be a range/pattern token and MUST NOT be coerced to a degenerate `min == max` range. Each seeded value MUST be traceable to an existing source-tagged "Official dial-in target" / "Grind diagnostics" line in `docs/PROFILE_KNOWLEDGE_BASE.md`; no value is invented.

#### Scenario: A seeded profile exposes both diagnostic arms and a shape qualifier

- **WHEN** `loadProfileKnowledge()` parses a section seeded with `GrindTooCoarse:`, `GrindTooFine:`, and `PressureShape:` lines
- **THEN** the resulting `ProfileExpectation` SHALL expose the parsed too-coarse signature, too-fine signature, and shape qualifier as distinct populated fields
- **AND** a profile that intentionally varies SHALL retain its pattern value (e.g. `6-9 declining`) without collapse to `min == max`

#### Scenario: Partially-seeded and un-seeded sections degrade, never throw

- **WHEN** a section carries only `ExpectedPressureBar:` (no diagnostic arms), or a malformed `GrindTooFine:` value, or no `Expected*:` lines at all
- **THEN** the present fields SHALL be populated, the malformed/absent fields SHALL be marked absent, the section parse SHALL continue, and no exception SHALL be thrown
- **AND** an un-seeded section SHALL be byte-identical in behavior to today

### Requirement: Per-profile suppression catalogue SHALL travel with the expectation

Each seeded section SHALL be able to carry its "this is GOOD / do not flag" facts as one or more machine-adjacent `Suppress:` directive lines (e.g. `Suppress: declining pressure; slow early soak flow; channeling-normal`), in addition to any free-text "DO NOT flag" prose. The parsed suppression set SHALL be exposed on `ProfileExpectation` so the active-coaching teaching and any future deterministic consumer reference a concrete per-profile suppression set rather than re-mining prose.

#### Scenario: Suppression facts are parsed as a structured set

- **WHEN** a section carries a `Suppress:` line enumerating intentional behaviors
- **THEN** the parsed `ProfileExpectation` SHALL expose those behaviors as a structured suppression set
- **AND** a section with no `Suppress:` line SHALL expose an empty suppression set and parse unchanged

### Requirement: Coverage SHALL be two-track and the diagnostic arms SHALL be evidence-graded

Coaching coverage SHALL be delivered on two decoupled tracks. **Prose track:** broad per-profile coaching prose for every espresso profile with first-party backing in `docs/PROFILE_KNOWLEDGE_BASE.md`. **Diagnostic-arm track:** the structured `GrindTooCoarse:` / `GrindTooFine:` arms SHALL be seeded ONLY where a citation supports that specific arm. Every seeded arm SHALL carry a per-arm `[SRC:...]` provenance tag and a confidence marker, and SHALL be marked `synthesis` when the symptom is stated only in first-party prose rather than as a pre-structured "Official dial-in target" / "Grind diagnostics" line.

A profile SHALL NOT have a missing arm fabricated to complete a two-sided pair: a one-sided profile SHALL expose only the cited arm with the other absent. A profile with no citation for any arm SHALL stay arm-absent rather than receive an invented value (prose for it MAY still exist on the prose track).

The provenance tag set SHALL include a distinct `[SRC:maintainer]` tier for first-hand operational knowledge, separate from published first-party (`decent-guide`, `*-video`), creator, and EAF/community tiers. A `[SRC:maintainer]` arm SHALL be seeded like any other cited arm, SHALL carry the explicit `[SRC:maintainer]` tag plus a confidence marker, and SHALL be a concrete observable. The no-fabrication rule is unchanged — `[SRC:maintainer]` widens *who counts as a citable source* (it does not permit uncited values).

#### Scenario: Two-sided arms only where cited, with provenance and confidence

- **WHEN** `loadProfileKnowledge()` parses Adaptive v2, Londinium/LRv2-LRv3, or Blooming Espresso
- **THEN** each SHALL expose both `GrindTooCoarse:` and `GrindTooFine:` arms, each with a `[SRC:...]` tag and confidence marker
- **AND** the Blooming arms SHALL be marked `synthesis` (first-party prose, not a pre-structured line)
- **AND** the Adaptive v2 record SHALL also carry a grind-tolerant-by-design suppression/leniency note

#### Scenario: A one-sided profile is not completed into a fabricated pair

- **WHEN** `loadProfileKnowledge()` parses E61 (cited too-coarse only) or Allongé (cited too-fine only)
- **THEN** only the cited arm SHALL be populated and the opposite arm SHALL be absent
- **AND** no symptom SHALL be invented for the absent arm

### Requirement: D-Flow / A-Flow guidance SHALL follow the editor-vs-profile model

`D-Flow` and `A-Flow` are Recipe Editor *types* (identified by the `D-Flow/` / `A-Flow/` title prefix), not profiles. The profile is the name past the `/`. Diagnostic arms SHALL attach to the specific profile, keyed by `profile_kb_id` to that profile's own KB section, never to "D-Flow" or "A-Flow" as such. The D-Flow editor SHALL contribute only an editor-level structural prior (`PressureShape: declining`) and an editor-level suppression rule (pressure pegging flat at the resolved profile's own D-Flow pressure limit is by-design clamp behavior, not a grind fault), valued per profile from de1app stock parameters (`[SRC:de1app-dflow]`: default 8.5, Q 10.0, La Pavoni 9.0 bar). There SHALL be no "capped Damian-advanced variant" concept — every D-Flow profile is clamped at its own limit.

#### Scenario: D-Flow / Q carries a first-party-stated two-sided peak-pressure bound

- **WHEN** `loadProfileKnowledge()` parses the `D-Flow / Q` profile's KB section
- **THEN** it SHALL expose a two-sided arm tagged `[SRC:profile-notes]` with a high confidence marker, traceable to the profile's shipped `notes` ("grind for a pressure peak between 6 and 9 bar")
- **AND** the `GrindTooCoarse:` arm SHALL encode "peak pressure never reaches ~6 bar"
- **AND** the `GrindTooFine:` arm SHALL encode "peak pressure exceeds ~9 bar" (unconfounded — this profile's editor pressure limit is 10.0 bar, above the target)
- **AND** the arm SHALL be keyed to the D-Flow/Q profile section only and SHALL NOT be applied to any other `D-Flow/…` profile

#### Scenario: D-Flow / La Pavoni carries a clean one-sided (too-coarse) cited arm; its fine direction is clamp-masked

- **WHEN** `loadProfileKnowledge()` parses the `D-Flow / La Pavoni` profile's KB section
- **THEN** it SHALL expose a `GrindTooCoarse:` arm tagged `[SRC:profile-notes]` with a high confidence marker, encoding "peak pressure never reaches ~6 bar", traceable to the profile's shipped `notes` ("grind for a pressure peak between 6 and 9 bar")
- **AND** it SHALL NOT expose a `GrindTooFine:` arm — its editor pressure limit is 9.0 bar (the top of the 6–9 target band), so a too-fine grind pegs flat at the clamp and is handled by the editor-level clamp suppression rule, NOT a fabricated too-fine arm
- **AND** the arm SHALL be keyed to the D-Flow/La Pavoni profile section only and SHALL NOT be applied to any other `D-Flow/…` profile

#### Scenario: The D-Flow editor contributes a structural prior, not a profile arm

- **WHEN** `loadProfileKnowledge()` resolves any `D-Flow/…` profile that has no cited per-profile numeric target
- **THEN** it SHALL expose the editor structural prior `PressureShape: declining` tagged `[SRC:maintainer]`
- **AND** it SHALL NOT expose a `GrindTooCoarse:`/`GrindTooFine:` numeric arm (absence, not fabrication)

#### Scenario: Editor pressure-limit clamp is suppressed per profile, not as a special variant

- **WHEN** a `D-Flow/…` shot's pressure pegs flat at that profile's own D-Flow editor pressure limit (e.g. 8.5 bar for `D-Flow / default`, 9.0 for `D-Flow / La Pavoni`)
- **THEN** that railing SHALL be carried as an editor-level `Suppress:` rule keyed to the resolved profile's limit (`[SRC:de1app-dflow]`)
- **AND** a `GrindTooFine:` call SHALL NOT fire on the strength of pressure sitting at/above the profile's own limit (the editor clamps there by design)
- **AND** no scenario SHALL reference a "capped Damian-advanced variant"

#### Scenario: An EAF-only arm is conservatively tagged

- **WHEN** `loadProfileKnowledge()` parses the Turbo Shot arm
- **THEN** the arm SHALL be tagged `[SRC:eaf-profiling]` with a non-high confidence marker

#### Scenario: An uncited profile stays arm-absent rather than invented

- **WHEN** a profile has no citation supporting either grind arm
- **THEN** its `ProfileExpectation` SHALL carry no diagnostic arm
- **AND** no numeric target or symptom SHALL be fabricated for it

### Requirement: shotAnalysisSystemPrompt SHALL carry an active-coaching, taste-deferring, suppression-gated diagnostic clause

The `shotAnalysisSystemPrompt` "How to read structured fields" section SHALL include an active-coaching clause: when a shot's observed pressure/flow/time/dripping matches a profile's `GrindTooCoarse:` or `GrindTooFine:` signature, the advisor SHALL name the matched signature and recommend the corresponding grind direction. The clause SHALL require that any such diagnostic statement is suppressed when the profile's `Suppress:` catalogue, a fired channeling detector, unknown/very-fresh beans, or a grind-tolerant-by-design profile would explain the observation, and SHALL state that taste (too sour → finer, too bitter → coarser) outranks any expectation-vs-actual or curve-shape verdict. Prescriptive "start here" guidance SHALL be permitted to be generous; diagnostic statements SHALL be symptom-specific.

This guidance SHALL reach both the in-app advisor system prompt and the `dialing_get_context` MCP surface via the existing KB-injection path, identically on both — no separate copy is authored.

#### Scenario: Matched signature produces a gated, taste-deferring coaching statement

- **WHEN** the espresso `shotAnalysisSystemPrompt` is produced
- **THEN** it SHALL contain the active-coaching clause directing the advisor to name a matched `GrindTooCoarse:`/`GrindTooFine:` signature and recommend a grind direction
- **AND** the clause SHALL require AND-gating against the profile suppression set, channeling, unknown/very-fresh beans, and grind-tolerant-by-design
- **AND** the clause SHALL state that taste outranks the curve verdict

#### Scenario: Both AI surfaces present identical coaching guidance

- **WHEN** the same seeded profile is rendered into the in-app advisor prompt and into `dialing_get_context`
- **THEN** the per-profile coaching guidance and the teaching clause SHALL be identical on both surfaces

### Requirement: Coaching guidance SHALL be advisor-prose-only and never synthesized, persisted, or exported

The enriched dial-in coaching SHALL be delivered solely as read-time prose on the AI surfaces. No computed confidence, quality, or trust score SHALL be fed to the advisor as a "treat as a hint" signal. No coaching verdict SHALL be written to the shot record, any database column, or visualizer.coffee. No deterministic detector consuming the structured `ProfileExpectation` SHALL be added by this capability; that consumer remains a separate deferred change.

#### Scenario: No synthesized signal, no persisted or exported verdict

- **WHEN** the advisor produces a dial-in coaching statement for a shot
- **THEN** the statement SHALL be derived only from the KB prose plus the shot's observed values
- **AND** no synthesized confidence/quality/trust score SHALL be injected into the advisor input
- **AND** no coaching verdict SHALL be written to the shot record, the database, or visualizer.coffee
