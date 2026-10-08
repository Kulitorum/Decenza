# advisor-structured-next Specification

## Purpose
Defines the optional trailing `nextShot` JSON block an AI Advisor response appends when it makes a concrete grind/dose/profile recommendation — its schema, the system-prompt teaching that produces it, `AIManager`'s tolerant parser, `AIConversation`'s per-turn persistence of the parsed block, and its surfacing through the `ai_advisor_invoke` MCP tool envelope.

## Requirements
### Requirement: AI advisor responses SHALL carry an optional structured `nextShot` JSON block

The shot-analysis system prompt SHALL instruct the LLM that, when its response
recommends a concrete change to grind, dose or profile, it SHALL append a
`json`-tagged fenced block as the final content of the message (only whitespace
may follow). The block SHALL be omitted entirely for clarifying questions,
acknowledgements and any response without a concrete parameter recommendation.
There SHALL NOT be a placeholder block carrying nulls.

#### Scenario: System prompt teaches the response format

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain a section header for response format (e.g., `## Response Format` or `### Structured nextShot output`)
- **AND** SHALL document the required and optional fields by name
- **AND** SHALL include at least one fenced-`json` example block
- **AND** SHALL state the omission rule for clarifying-question responses

#### Scenario: Recommendation response carries a parseable block

- **GIVEN** an LLM response whose prose recommends moving the grinder setting from `5.0` to `4.75`
- **WHEN** the response is rendered to the user
- **THEN** the response SHALL end with a fenced ` ```json ` block whose parsed object contains `grinderSetting: "4.75"`, `expectedDurationSec` as a 2-element array, `expectedFlowMlPerSec` as a 2-element array, `successCondition` as a non-empty string, and `reasoning` as a non-empty string

#### Scenario: Clarifying-question response omits the block

- **GIVEN** an LLM response that asks "How did this shot taste?" and makes no parameter recommendation
- **WHEN** the response is rendered
- **THEN** it SHALL NOT contain a trailing fenced JSON block matching the `nextShot` schema

### Requirement: The `nextShot` block uses a fixed field set

The block SHALL use these fields. `grinderSetting` (string) is required iff
grind moves, `doseG` (number) iff dose moves, and `profileTitle` (string) iff
profile switches. `expectedDurationSec` and `expectedFlowMlPerSec` ([low,
high]), `successCondition` (a short natural-language predicate), and `reasoning`
(one sentence) are required. `expectedPeakPressureBar` ([low, high]) is
optional.

#### Scenario: Grind-only recommendation omits the other move fields

- **WHEN** a response moves the grind setting but leaves dose and profile unchanged
- **THEN** the block contains `grinderSetting` and omits `doseG` and `profileTitle`


### Requirement: `AIManager` SHALL parse the trailing structured block tolerantly

`AIManager` SHALL provide a parser (`parseStructuredNext(const QString&) ->
std::optional<QJsonObject>`) that extracts only the last fenced `json` block,
allowing trailing whitespace, and returns `std::nullopt` when no such trailing
block exists. On a JSON parse failure it SHALL return `std::nullopt` and log a
`qWarning` containing the parser error. It SHALL NOT strip the block from the
prose.

#### Scenario: Trailing block is parsed; mid-message block is ignored

- **GIVEN** an assistant message whose body contains a fenced ` ```json ` block in a quoted example, followed by additional prose, and ending without a fenced block
- **WHEN** `parseStructuredNext` runs on the message
- **THEN** it SHALL return `std::nullopt`

- **GIVEN** an assistant message whose final non-whitespace content is a fenced ` ```json ` block parseable as a `nextShot` object
- **WHEN** `parseStructuredNext` runs on the message
- **THEN** it SHALL return a populated `QJsonObject`

#### Scenario: Malformed JSON yields nullopt and a warning log

- **GIVEN** an assistant message ending with ` ```json {grinderSetting: 4.75 ``` ` (broken JSON — unquoted key, unterminated brace)
- **WHEN** `parseStructuredNext` runs
- **THEN** it SHALL return `std::nullopt`
- **AND** SHALL emit a `qWarning` containing `structuredNext` and the parser error text

### Requirement: The structured parser runs on every assistant message

The parser SHALL be invoked on every assistant message reaching
`AIConversation`, in both the in-app advisor flow and the `ai_advisor_invoke`
MCP flow.

#### Scenario: Both surfaces run the parser

- **WHEN** an assistant reply arrives through the in-app advisor or through the `ai_advisor_invoke` MCP tool
- **THEN** `parseStructuredNext` runs on that reply before it is stored or returned

### Requirement: `AIConversation` SHALL persist `structuredNext` per assistant turn

Each assistant entry in `AIConversation::m_messages` SHALL carry the parsed
`structuredNext` as an optional sibling of `role` and `content`, stored only
when present. `addAssistantMessage(const QString& content, const
std::optional<QJsonObject>& structuredNext)` SHALL omit the key when the
optional is empty.

#### Scenario: Saved and reloaded structuredNext round-trips

- **GIVEN** an `AIConversation` with one user message followed by an assistant message whose `structuredNext` was set to `{grinderSetting: "4.75", expectedDurationSec: [32,38], …}`
- **WHEN** the conversation is saved to its storage key and a fresh `AIConversation` loads from the same key
- **THEN** `structuredNextForAssistantTurn(0)` SHALL return a `QJsonObject` equal under `==` to the original

#### Scenario: Loading an older conversation with no structuredNext

- **GIVEN** a conversation persisted before this change (assistant messages have no `structuredNext` key)
- **WHEN** the conversation is loaded
- **THEN** loading SHALL succeed without error
- **AND** `structuredNextForAssistantTurn(i)` for every assistant turn SHALL return `std::nullopt`

### Requirement: Older conversations load without `structuredNext`

A conversation persisted before this change SHALL load without error and without
a schema migration. Every assistant turn in it SHALL read as having no
`structuredNext`.

#### Scenario: Older entries read as absent

- **WHEN** a conversation saved before this change is loaded
- **THEN** loading succeeds and `structuredNextForAssistantTurn` returns `std::nullopt` for every assistant turn


### Requirement: `structuredNextForAssistantTurn` returns the stored block per turn

`std::optional<QJsonObject> structuredNextForAssistantTurn(qsizetype index)
const` SHALL return the parsed block for the given assistant turn, or
`std::nullopt` when it has none.

#### Scenario: A saved block round-trips through a reload

- **GIVEN** an assistant turn whose `structuredNext` was saved
- **WHEN** the conversation is saved and a fresh `AIConversation` loads it
- **THEN** `structuredNextForAssistantTurn` returns an object equal to the saved one

### Requirement: `ai_advisor_invoke` SHALL surface `structuredNext` in its tool envelope

`ai_advisor_invoke` SHALL emit the parsed block as a top-level `structuredNext`
field of its tool result envelope, beside `response` and `userPromptUsed`. The
field SHALL be omitted when `parseStructuredNext` returns `std::nullopt`; there
SHALL NOT be a `null` placeholder.

#### Scenario: Tool envelope carries structuredNext on a recommendation response

- **GIVEN** a stub provider configured to return a fixed assistant reply ending in a valid `nextShot` JSON block
- **WHEN** `ai_advisor_invoke` runs end-to-end
- **THEN** the tool result envelope SHALL contain `structuredNext` with the parsed object
- **AND** SHALL contain the unchanged prose under `response`

#### Scenario: Tool envelope omits structuredNext on a clarifying-question response

- **GIVEN** a stub provider configured to return prose with no trailing JSON block
- **WHEN** `ai_advisor_invoke` runs end-to-end
- **THEN** the tool result envelope SHALL NOT contain a `structuredNext` key
- **AND** SHALL NOT contain `structuredNext: null`

### Requirement: The tool description documents `structuredNext`

The `ai_advisor_invoke` tool description SHALL document the `structuredNext`
field, including its omission semantics, and SHALL refer to this spec for the
schema.

#### Scenario: Tool description names the field

- **WHEN** a client reads the `ai_advisor_invoke` tool description
- **THEN** it names `structuredNext`, states that the field is omitted when absent, and points to this spec for the schema

