# advisor-structured-next — Delta

## ADDED Requirements

### Requirement: AI advisor responses SHALL carry an optional structured `nextShot` JSON block

The shot-analysis system prompt SHALL instruct the LLM that *when its response recommends a concrete change to grind, dose, or profile*, it SHALL append a fenced JSON block to the end of the response. The block SHALL be the last content in the message, optionally followed by whitespace, and SHALL be encoded as a fenced code block with the `json` language tag.

The block SHALL be OMITTED entirely when the response is a clarifying question, an acknowledgement, or otherwise does not make a concrete parameter recommendation. Omission is the documented null state — there SHALL NOT be a placeholder block carrying nulls.

The schema for the block SHALL be:

- `grinderSetting` (string) — REQUIRED iff the recommendation moves grind. Omitted when grind is unchanged.
- `doseG` (number) — REQUIRED iff the recommendation moves dose. Omitted when dose is unchanged.
- `profileTitle` (string) — REQUIRED iff the recommendation switches profile. Omitted otherwise.
- `expectedDurationSec` ([number, number]) — REQUIRED. The expected `[low, high]` window for the next shot's total duration assuming the recommendation is followed.
- `expectedFlowMlPerSec` ([number, number]) — REQUIRED.
- `expectedPeakPressureBar` ([number, number]) — OPTIONAL. Present when the recommendation specifically targets pressure dynamics.
- `successCondition` (string) — REQUIRED. A short natural-language predicate (stored verbatim by the app for display and for the LLM to read on subsequent turns).
- `reasoning` (string) — REQUIRED. One-sentence summary of *why* the recommendation was made.

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

### Requirement: `AIManager` SHALL parse the trailing structured block tolerantly

`AIManager` SHALL provide a parser (e.g., `parseStructuredNext(const QString&) -> std::optional<QJsonObject>`) that:

- Extracts the **last** fenced ` ```json ... ``` ` block in the assistant message, allowing trailing whitespace after the closing fence.
- Returns `std::nullopt` when no such trailing block exists. Mid-message ` ```json ` blocks (e.g., the model echoing a snippet from the user) SHALL NOT be extracted.
- Returns `std::nullopt` on JSON parse failure, logging a `qWarning` with the parser error string.
- Does NOT strip the block from the prose. The conversation overlay continues to show the full assistant message including the block.

The parser SHALL be invoked on every assistant message reaching `AIConversation`, both in the in-app advisor flow and in the `ai_advisor_invoke` MCP tool flow.

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

### Requirement: `AIConversation` SHALL persist `structuredNext` per assistant turn

Each assistant entry in `AIConversation::m_messages` SHALL carry the parsed `structuredNext` object as an optional sibling field next to `role` and `content`. Specifically:

- The signature `addAssistantMessage(const QString& content, const std::optional<QJsonObject>& structuredNext)` SHALL store `structuredNext` only when present (`has_value() == true`); when absent, the `structuredNext` key SHALL NOT appear in the persisted entry.
- Loading older conversations (saved before this change) — entries without a `structuredNext` key — SHALL succeed, with the field reading as `std::nullopt`. No schema migration is required.
- A reader `std::optional<QJsonObject> structuredNextForAssistantTurn(qsizetype index) const` SHALL return the parsed block for a given assistant turn, or `std::nullopt`.

#### Scenario: Saved and reloaded structuredNext round-trips

- **GIVEN** an `AIConversation` with one user message followed by an assistant message whose `structuredNext` was set to `{grinderSetting: "4.75", expectedDurationSec: [32,38], …}`
- **WHEN** the conversation is saved to its storage key and a fresh `AIConversation` loads from the same key
- **THEN** `structuredNextForAssistantTurn(0)` SHALL return a `QJsonObject` equal under `==` to the original

#### Scenario: Loading an older conversation with no structuredNext

- **GIVEN** a conversation persisted before this change (assistant messages have no `structuredNext` key)
- **WHEN** the conversation is loaded
- **THEN** loading SHALL succeed without error
- **AND** `structuredNextForAssistantTurn(i)` for every assistant turn SHALL return `std::nullopt`

### Requirement: `ai_advisor_invoke` SHALL surface `structuredNext` in its tool envelope

`ai_advisor_invoke` SHALL parse the structured block from the assistant response and emit it as a top-level optional field `structuredNext` in the tool result envelope, alongside `response` (prose) and `userPromptUsed`. The field SHALL be omitted from the envelope when `parseStructuredNext` returns `nullopt` — there SHALL NOT be a `null` placeholder.

The tool description metadata SHALL document the new field, including the omission semantics and the schema (by reference to this spec).

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
