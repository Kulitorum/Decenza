## MODIFIED Requirements

### Requirement: Model selection persists per provider

The system SHALL store the selected model independently for each provider and SHALL restore that selection when the application restarts. Selecting a model for one provider SHALL NOT affect any other provider's stored selection.

#### Scenario: Selection restored after restart

- **WHEN** the user selects Haiku 5.5 for the Anthropic provider and later restarts the app
- **THEN** the Anthropic provider still uses Haiku 5.5

#### Scenario: Per-provider isolation

- **WHEN** the user changes the Anthropic model
- **THEN** the stored model for the Gemini provider (and every other provider) is unchanged

### Requirement: Model selection takes effect immediately

The system SHALL apply a newly selected model to the active provider without requiring an app restart, so that the next advisor request uses the chosen model. An unknown or unrecognized model id SHALL never be sent to a provider: a supplied one is ignored, leaving the provider on its current valid model, and a stored one is cleared so the provider uses its default.

#### Scenario: Change applied to next request

- **WHEN** the user switches the Anthropic model from Sonnet 5.5 to Haiku 5.5
- **THEN** the next AI Advisor request is sent to the Anthropic API with the Haiku 5.5 model id

#### Scenario: Unknown model id ignored

- **WHEN** a supplied model id does not match any entry in the provider's catalog
- **THEN** the provider keeps its current valid model rather than sending an invalid id

#### Scenario: Stored model no longer offered

- **WHEN** the app starts with a stored model id that the provider's catalog no longer offers
- **THEN** the stored selection is cleared and the provider uses its default model

### Requirement: Model picker visibility

The AI settings screen SHALL present a model picker for the selected provider only when that provider offers more than one model. When a provider offers a single model, no picker is shown.

#### Scenario: Picker shown for Anthropic

- **WHEN** the user selects the Anthropic provider in AI settings
- **THEN** a model picker listing Sonnet 5.5 and Haiku 5.5 is displayed

#### Scenario: Picker hidden for single-model provider

- **WHEN** the user selects a provider that offers only one model
- **THEN** no model picker is displayed for that provider

### Requirement: Selected model reflected in advisor and MCP surfaces

The currently selected model SHALL be reported consistently wherever the advisor's active model is surfaced, including the MCP `ai_advisor_invoke` path, so that the model actually used for a request matches the reported short model name.

#### Scenario: MCP invoke uses selected model

- **WHEN** an advisor request is made via the MCP `ai_advisor_invoke` tool while Anthropic is the active provider with Haiku 5.5 selected
- **THEN** the request uses Haiku 5.5 and the reported model name reflects Haiku 5.5

## ADDED Requirements

### Requirement: Provider model catalog and default

Each AI Advisor provider SHALL expose a catalog of one or more selectable models, where every entry has a stable model id (sent to the provider API) and a human-readable display name (shown in the UI). A provider with a single fixed model exposes a one-entry catalog. The first entry SHALL be the provider's default, used whenever no model has been selected.

The Anthropic provider SHALL offer Claude Sonnet 5.5 as its default and Claude Haiku 5.5 as its value pick.

#### Scenario: Anthropic catalog lists Sonnet 5.5 then Haiku 5.5

- **WHEN** the AI settings UI queries the available models for the Anthropic provider
- **THEN** the catalog's first entry is Sonnet 5.5 and its second is Haiku 5.5, each with a distinct model id and display name

#### Scenario: Default is the first entry

- **WHEN** the user has never selected an Anthropic model
- **THEN** advisor requests to Anthropic use Sonnet 5.5

#### Scenario: Provider with one model

- **WHEN** the UI queries available models for a provider that offers only a single fixed model
- **THEN** the catalog contains exactly one entry

### Requirement: Catalog admission requires evaluation

A model SHALL enter a provider's catalog only after two checks pass on that model, with the results recorded. First, the provider accepts the thinking or reasoning setting the app sends for it and returns reply text. Second, a replay of real advisor prompts shows no bad advice. Bad advice means a wrong grind direction where the scenario has a right one, advice given on an untasted shot or a prep failure, or a taste the user never reported.

#### Scenario: Model that reverses grind direction is not admitted

- **WHEN** a candidate model calls a finer setting "coarser", or moves the grind the wrong way on a tasted scenario with a known right direction
- **THEN** the model is not added to any catalog, and the reason is recorded with the run's evidence

#### Scenario: Model whose thinking setting is rejected is not admitted

- **WHEN** the provider returns an error for the thinking setting the app would send for a candidate, or returns no reply text under it
- **THEN** the model is not added to any catalog until a setting that passes is found and recorded

#### Scenario: Maintainer exception is recorded

- **WHEN** the maintainer admits a model despite a recorded shortfall
- **THEN** the shortfall and the decision are documented next to the catalog rationale

## REMOVED Requirements

### Requirement: Provider model catalog

**Reason**: It required Sonnet 4.6, Sonnet 5, GPT-5.4 mini and GPT-5.4, none of which any catalog has offered since 2026-10-05, and it did not say which entry is the default.
**Migration**: Replaced by "Provider model catalog and default", which names the current Anthropic catalog and makes the first entry the default.
