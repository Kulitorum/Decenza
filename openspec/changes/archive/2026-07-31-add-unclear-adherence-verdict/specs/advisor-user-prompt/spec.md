## MODIFIED Requirements

### Requirement: `recentAdvice` entries SHALL carry a computed `userResponse` attribution

The `adherence` field gains a fourth value, `"unclear"`, for recommendations the
app cannot check. The three existing values are unchanged.

- `adherence` (`"followed" | "partial" | "ignored" | "unclear"`):
  - `"followed"` — every recommended field present in `structuredNext` (grinderSetting, rpm, doseG, profileTitle) matches the actual within tolerance: grinderSetting equal as string, equal as compound notation ignoring spacing, OR within 0.25 of a numeric step (comparing the leading dial number, so a recorded annotation such as `"23.5 1400rpm"` matches a recommended `"23.5"`); rpm within ±25; doseG within ±0.3g; profileTitle equal.
  - `"partial"` — at least one but not all recommended fields match.
  - `"ignored"` — none of the recommended fields match.
  - `"unclear"` — at least one recommended field was present but **unscoreable**, so adherence cannot be determined. A field is unscoreable when its JSON type is wrong for the schema (e.g. `grinderSetting` emitted as a bare number rather than a string, `rpm` as a string rather than a number), when a numeric field is present but non-positive, or when `grinderSetting` is prose rather than a dial value (e.g. `"a touch coarser than 9"`). `"unclear"` SHALL take precedence over the other three values whenever any field is unscoreable.
  - When `structuredNext` had no parameter recommendations at all (only ranges/successCondition), `adherence` SHALL be `"ignored"` only when the actual shot is on different parameters from the prior turn's shot; otherwise `"followed"`. An unscoreable field is NOT "no parameter recommendation" — it SHALL yield `"unclear"`, never fall through to this rule.

#### Scenario: Prose in `grinderSetting` yields `"unclear"`

- **GIVEN** a prior turn whose `structuredNext.grinderSetting` is prose rather than a dial value
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"unclear"`
- **AND** it SHALL be `"unclear"` whether or not the user changed the grinder, because the recommendation named no setting to compare against

#### Scenario: A malformed `rpm` does not read as compliance

- **GIVEN** a prior turn whose `structuredNext.rpm` is a JSON string, or is present and `<= 0`
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"unclear"`
- **AND** SHALL NOT be `"followed"`

#### Scenario: Equivalent notations of the same setting count as followed

- **GIVEN** a prior turn recommending `"1 + 4"` and a follow-up shot recorded as `"1+4"`, or a turn recommending `"23.5"` and a shot recorded as `"23.5 1400rpm"`
- **WHEN** the follow-up shot is attributed
- **THEN** `adherence` SHALL be `"followed"` — spacing and recorded annotations SHALL NOT decide adherence

### Requirement: System prompt SHALL teach the LLM to read `recentAdvice` and weight it

The teaching SHALL now cover four `adherence` values rather than three.

- How to interpret `adherence`: `"followed"` + worse outcome ⇒ revise direction; `"ignored"` ⇒ stay the course; `"partial"` ⇒ ask before revising; `"unclear"` ⇒ the prior recommendation named nothing checkable, so treat it like `"ignored"`, do not assume the experiment ran, and restate the recommendation as a concrete value.

#### Scenario: System prompt contains recentAdvice teaching

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain a section discussing `recentAdvice`
- **AND** SHALL describe the four `adherence` values and how to react to each
- **AND** SHALL describe the omitted-rating fallback
