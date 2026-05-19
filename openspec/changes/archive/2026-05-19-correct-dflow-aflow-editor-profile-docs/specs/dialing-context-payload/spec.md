## ADDED Requirements

### Requirement: The shipped Profile Knowledge Base SHALL describe D-Flow/A-Flow as editor types, not profiles

The D-Flow and A-Flow content in `resources/ai/profile_knowledge.md` (the KB injected verbatim into both the in-app advisor system prompt and `dialing_get_context`) SHALL describe `D-Flow` and `A-Flow` as Recipe Editor *types*, with the profile being the name past the `/`. It SHALL NOT use "variant", "family", or "base D-Flow" phrasing in a way that implies D-Flow or A-Flow is itself a profile; a shared-behavior grouping SHALL be expressed as "profiles built with the D-Flow editor" (or equivalent). The lever-decline shape and the per-profile pressure-limit clamp SHALL be described as editor-level behavior, not as a profile trait.

Section headers (`## D-Flow`, `## D-Flow Q variant`, `## Damian's LRv2 / LRv3`, `## A-Flow`, `## Londinium`) and `Also matches:` alias lines SHALL remain byte-identical — only body prose and in-section profile-name references are changed.

#### Scenario: D-Flow/A-Flow sections teach the editor model without renaming headers

- **WHEN** the D-Flow/A-Flow sections of `resources/ai/profile_knowledge.md` are rendered into the advisor prompt and `dialing_get_context`
- **THEN** the prose SHALL state D-Flow/A-Flow are editor types and the profile is the name past the `/`
- **AND** it SHALL NOT contain profile-implying "D-Flow variant/family/base D-Flow" phrasing
- **AND** every `## ` heading and every `Also matches:` line SHALL be unchanged from before this change (drift-check)

### Requirement: The shipped Profile Knowledge Base SHALL reference only real built-in profile names

D-Flow/A-Flow profile names written in `resources/ai/profile_knowledge.md` SHALL correspond to actual shipped built-in profile titles in `resources/profiles/`. Specifically, the stale A-Flow names `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, `A-Flow / like D-Flow` SHALL be replaced with the real built-ins `A-Flow / default-light`, `A-Flow / default-medium`, `A-Flow / default-dark`, `A-Flow / default-very-dark`, `A-Flow / default-like-dflow`. No profile name not backed by a `resources/profiles/*.json` `title` SHALL be presented to the AI as an existing profile.

#### Scenario: Stale A-Flow names are corrected to shipped built-ins

- **WHEN** the shipped KB is parsed/rendered
- **THEN** it SHALL NOT contain `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, or `A-Flow / like D-Flow`
- **AND** it SHALL reference the actual A-Flow built-in titles as shipped in `resources/profiles/a_flow_*.json`

#### Scenario: A regression guard prevents reintroduction

- **WHEN** the test suite runs
- **THEN** a guard SHALL fail if `resources/ai/profile_knowledge.md` contains any of the stale A-Flow names
- **AND** the guard SHALL fail if a referenced D-Flow/A-Flow profile name has no corresponding `resources/profiles/*.json` title
