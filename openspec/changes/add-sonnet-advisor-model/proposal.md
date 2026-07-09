## Why

The AI Advisor pins the Anthropic (Claude) and OpenAI providers each to a single fixed model (`claude-sonnet-4-6` and `gpt-5.4-mini`), while the Gemini provider already lets users choose between several models. Claude Sonnet 5 has shipped, and OpenAI offers a meaningfully more capable same-family step up (`gpt-5.4`) alongside the cheap `gpt-5.4-mini` default — but users of these two providers currently have no way to select anything but the hard-coded model. Giving Anthropic and OpenAI the same generic model picker Gemini already has lets users opt into a more capable model (and future models) without a code change.

## What Changes

- Add **Sonnet 5** (`claude-sonnet-5`) as a selectable model for the Anthropic AI Advisor provider, alongside the existing Sonnet 4.6 (the default).
- Add **GPT-5.4** (`gpt-5.4`) as a selectable model for the OpenAI AI Advisor provider, alongside the existing GPT-5.4 mini (the default). GPT-5.4 is the same-family mid-tier: a real capability step up for ~3.3× the mini's per-token cost, softened by automatic prompt caching. GPT-5.5 (frontier, ~7× cost) and GPT-5.4 nano (weaker than the current default) are intentionally excluded.
- Convert both `AnthropicProvider` and `OpenAIProvider` from a single hard-coded model to the same model-catalog pattern `GeminiProvider` uses: an `availableModels()` catalog, a mutable `m_model`, `setModel()` validation, and a `shortModelName()` override.
- Wire each provider's model choice through `AIManager` construction and live settings-change sync, so the selection persists (under the existing generic `ai/model/<providerId>` keys) and takes effect immediately — matching Gemini's wiring exactly.
- The existing generic model-picker dropdown in the AI settings tab auto-appears for each provider once its catalog has more than one entry — no new UI is added; optionally surface a per-model hint for Anthropic and OpenAI the way Gemini has one.
- Update `docs/CLAUDE_MD/AI_ADVISOR.md` to reflect that Anthropic and OpenAI now offer a model choice.

No breaking changes: each provider's default model remains the current one, settings storage and the provider list are unchanged, and existing users keep their behavior until they explicitly pick the more capable model.

## Capabilities

### New Capabilities
- `advisor-model-selection`: Users can choose among the models a selected AI Advisor provider offers; each provider exposes a catalog of one or more models, the choice persists per provider, and providers with more than one model surface a picker in AI settings. Anthropic offers Sonnet 4.6 and Sonnet 5; OpenAI offers GPT-5.4 mini and GPT-5.4.

### Modified Capabilities
<!-- None — no existing spec's requirements change. -->

## Impact

- **Code**: `src/ai/aiprovider.h` and `src/ai/aiprovider.cpp` (`AnthropicProvider` and `OpenAIProvider`: add `availableModels()`, `setModel()`, `shortModelName()`, `m_model`; replace each provider's fixed-model request sites); `src/ai/aimanager.cpp` (Anthropic and OpenAI `setModel()` in `createProviders()` and `onSettingsChanged()`).
- **UI**: `qml/pages/settings/SettingsAITab.qml` — generic picker already handles it; optional Anthropic and OpenAI model hints.
- **Settings**: none — reuses the existing generic `providerModel()`/`setProviderModel()` storage (`ai/model/anthropic`, `ai/model/openai` keys). No new `Settings.ai` property.
- **MCP**: `ai_advisor_invoke` inherits the selected model automatically via `currentModelName()`; no per-model wiring needed.
- **Docs**: `docs/CLAUDE_MD/AI_ADVISOR.md`.
- **APIs/dependencies**: none — same Anthropic Messages / OpenAI Chat Completions endpoints, auth headers, and caching behavior; only each request's `model` field value varies.
