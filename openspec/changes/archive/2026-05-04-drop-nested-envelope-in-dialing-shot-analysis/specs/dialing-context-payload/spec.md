# dialing-context-payload — Delta

## ADDED Requirements

### Requirement: dialing_get_context.shotAnalysis SHALL be prose, not a JSON envelope

The `dialing_get_context` response field `result.shotAnalysis` SHALL be a prose markdown string carrying the `## Shot Summary` block, `## Phase Data` block, and `## Detector Observations` block — the same content the in-app advisor's user-prompt envelope carries under its own `shotAnalysis` key. The field SHALL NOT carry a JSON-encoded object; specifically, it SHALL NOT carry an embedded copy of `currentBean`, `profile`, `tastingFeedback`, or any other structured field that the response already exposes at the top level.

The structured fields (`currentBean`, `profile`, `tastingFeedback`, `dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) continue to live exactly once at the top level of the response. The `shotAnalysis` field carries only the prose body the system prompt teaches the AI to read.

`ShotSummarizer::buildShotAnalysisProse(summary)` SHALL be the single source for the prose body. Both `dialing_get_context.shotAnalysis` and the in-app advisor's user-prompt envelope's `shotAnalysis` key SHALL produce byte-identical prose for identical input — the prose comes from the same private renderer in both code paths.

#### Scenario: dialing_get_context.shotAnalysis is a prose string

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL be a string starting with `## Shot Summary` (or the equivalent prose body header the renderer emits)
- **AND** parsing `result.shotAnalysis` as JSON via `QJsonDocument::fromJson(...)` SHALL fail (the value is prose, not an object)

#### Scenario: dialing_get_context.shotAnalysis carries no structured-field block names

- **GIVEN** any successful `dialing_get_context` call against a shot with populated DYE state
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL NOT contain the substring `"currentBean"` (the JSON key name that the previous nested envelope embedded)
- **AND** `result.shotAnalysis` SHALL NOT contain the substring `"tastingFeedback"`
- **AND** `result.shotAnalysis` SHALL NOT contain `\"profile\":` or any other structured-field block-name token in JSON form

#### Scenario: dialing_get_context.shotAnalysis matches the in-app advisor's shotAnalysis field byte-for-byte

- **GIVEN** a fixed resolved shot
- **WHEN** `dialing_get_context.shotAnalysis` is captured AND the in-app advisor's user-prompt envelope's `shotAnalysis` field is captured for the same shot
- **THEN** the two strings SHALL be `==` (byte-for-byte identical)
- **AND** both SHALL come from `ShotSummarizer::buildShotAnalysisProse(summary)` — no other prose builder may produce either value

### Requirement: dialing_get_context response SHALL NOT double-ship currentBean / profile / tastingFeedback

The `dialing_get_context` response SHALL contain `currentBean`, `profile`, and `tastingFeedback` exactly once each, at the top level of the response. The values previously embedded inside the `shotAnalysis` field's JSON envelope SHALL NOT appear at any nesting level.

#### Scenario: top-level structured blocks remain unchanged

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.currentBean`, `result.profile`, `result.tastingFeedback` SHALL be emitted at the top level exactly as the existing requirements specify

#### Scenario: no nested copies inside shotAnalysis

- **GIVEN** any successful `dialing_get_context` call
- **WHEN** the response is assembled
- **THEN** `result.shotAnalysis` SHALL NOT contain a JSON-encoded copy of `currentBean`, `profile`, or `tastingFeedback`
- **AND** the LLM SHALL see each of those three blocks exactly once
