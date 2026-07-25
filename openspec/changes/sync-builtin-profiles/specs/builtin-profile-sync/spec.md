## ADDED Requirements

### Requirement: Common built-in profiles are content-equivalent across apps

The bundled profiles Decenza and reaprime share SHALL, after reconciliation, produce the same extraction — importing a shared profile into either app yields functionally-identical frames. Divergences SHALL be resolved case-by-case using de1app settings and Visualizer canonical JSON as references (never de1app's stale `advanced_shot` frames), with fixes flowing to whichever app is less faithful.

Reconciliation is **bidirectional**: neither app is automatically authoritative. reaprime's set was pulled from Visualizer and de1app copy-exports and is documented (their issue #242) to have contained stale notes, duplicate collisions and lever-profile corruption; Decenza is the more faithful side for some profiles and the less faithful for others. A blanket "de1app wins" rule governed the de1app leg of this work and MUST NOT be carried across to reaprime.

**A-Flow and D-Flow profiles are excluded from the reaprime comparison.** reaprime is believed broken for those editor types, so a difference there is not evidence of a Decenza defect. They remain in scope against de1app, where `de1app-profile-parity` already holds them.

> STUB: this requirement is a placeholder capturing the equivalence goal. Full requirements (audit method, 3-way tooling, migration, dedup rules) are to be authored in `design.md`/`tasks.md`, which do not exist yet — `align-profile-json-with-reaprime` has now landed, so that authoring is unblocked.

#### Scenario: A reconciled shared profile makes the same coffee in either app

- **WHEN** a built-in common to Decenza and reaprime has been reconciled
- **THEN** the two apps' copies parse to functionally-equal frames
- **AND** the divergence resolution is recorded in the audit with its chosen reference source

#### Scenario: An A-Flow or D-Flow difference against reaprime is not treated as a defect

- **WHEN** the comparison reports a divergence on an A-Flow or D-Flow profile against reaprime
- **THEN** it is reported as excluded rather than queued for reconciliation
- **AND** the same profile's parity against de1app is still enforced

<!--
REMOVED from this stub: "A settings_2a profile carrying stale advanced_shot frames
is caught". That behaviour shipped in fix-de1app-profile-drift and is now specified
as "Simple profiles derive frames from their scalars" in
openspec/specs/de1app-profile-parity/spec.md. Restating it here would give one
behaviour two owners, which is how the two drift apart.
-->

