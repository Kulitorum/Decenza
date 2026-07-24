## ADDED Requirements

### Requirement: Common built-in profiles are content-equivalent across apps

The bundled profiles Decenza and reaprime share SHALL, after reconciliation, produce the same extraction — importing a shared profile into either app yields functionally-identical frames. Divergences SHALL be resolved case-by-case using de1app settings and Visualizer canonical JSON as references (never de1app's stale `advanced_shot` frames), with fixes flowing to whichever app is less faithful.

> STUB: this requirement is a placeholder capturing the equivalence goal. Full requirements (audit method, 3-way tooling, migration, dedup rules) are to be authored once `align-profile-json-with-reaprime` lands and the case-by-case audit begins.

#### Scenario: A reconciled shared profile makes the same coffee in either app

- **WHEN** a built-in common to Decenza and reaprime has been reconciled
- **THEN** the two apps' copies parse to functionally-equal frames
- **AND** the divergence resolution is recorded in the audit with its chosen reference source

#### Scenario: A settings_2a profile carrying stale advanced_shot frames is caught

- **WHEN** a `settings_2a` profile is found carrying explicit frames that contradict its settings intent
- **THEN** it is flagged for review and reconciled from the settings fields (or Visualizer canonical JSON), not from the stale `advanced_shot`
