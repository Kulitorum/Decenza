## ADDED Requirements

### Requirement: Provider model catalog

Each AI Advisor provider SHALL expose a catalog of one or more selectable models, where every entry has a stable model id (sent to the provider API) and a human-readable display name (shown in the UI). A provider with a single fixed model exposes a one-entry catalog.

The Anthropic provider SHALL offer at least two models in its catalog: Claude Sonnet 4.6 and Claude Sonnet 5. The OpenAI provider SHALL offer at least two models in its catalog: GPT-5.4 mini and GPT-5.4.

#### Scenario: Anthropic catalog lists multiple Sonnet models

- **WHEN** the AI settings UI queries the available models for the Anthropic provider
- **THEN** the returned catalog includes both a Sonnet 4.6 entry and a Sonnet 5 entry, each with a distinct model id and display name

#### Scenario: OpenAI catalog lists multiple GPT-5.4 models

- **WHEN** the AI settings UI queries the available models for the OpenAI provider
- **THEN** the returned catalog includes both a GPT-5.4 mini entry and a GPT-5.4 entry, each with a distinct model id and display name

#### Scenario: Provider with one model

- **WHEN** the UI queries available models for a provider that offers only a single fixed model
- **THEN** the catalog contains exactly one entry

### Requirement: Model selection persists per provider

The system SHALL store the selected model independently for each provider and SHALL restore that selection when the application restarts. Selecting a model for one provider SHALL NOT affect any other provider's stored selection.

#### Scenario: Selection restored after restart

- **WHEN** the user selects Sonnet 5 for the Anthropic provider and later restarts the app
- **THEN** the Anthropic provider still uses Sonnet 5

#### Scenario: Per-provider isolation

- **WHEN** the user changes the Anthropic model
- **THEN** the stored model for the Gemini provider (and every other provider) is unchanged

### Requirement: Model selection takes effect immediately

The system SHALL apply a newly selected model to the active provider without requiring an app restart, so that the next advisor request uses the chosen model. An unknown or unrecognized model id SHALL be ignored, leaving the provider on its current valid model.

#### Scenario: Change applied to next request

- **WHEN** the user switches the Anthropic model from Sonnet 4.6 to Sonnet 5
- **THEN** the next AI Advisor request is sent to the Anthropic API with the Sonnet 5 model id

#### Scenario: Unknown model id ignored

- **WHEN** a stored or supplied model id does not match any entry in the provider's catalog
- **THEN** the provider keeps its current valid model rather than sending an invalid id

### Requirement: Model picker visibility

The AI settings screen SHALL present a model picker for the selected provider only when that provider offers more than one model. When a provider offers a single model, no picker is shown.

#### Scenario: Picker shown for Anthropic

- **WHEN** the user selects the Anthropic provider in AI settings
- **THEN** a model picker listing the Anthropic models (including Sonnet 5) is displayed

#### Scenario: Picker hidden for single-model provider

- **WHEN** the user selects a provider that offers only one model
- **THEN** no model picker is displayed for that provider

### Requirement: Selected model reflected in advisor and MCP surfaces

The currently selected model SHALL be reported consistently wherever the advisor's active model is surfaced, including the MCP `ai_advisor_invoke` path, so that the model actually used for a request matches the reported short model name.

#### Scenario: MCP invoke uses selected model

- **WHEN** an advisor request is made via the MCP `ai_advisor_invoke` tool while Anthropic is the active provider with Sonnet 5 selected
- **THEN** the request uses Sonnet 5 and the reported model name reflects Sonnet 5
