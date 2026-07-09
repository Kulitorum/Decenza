## Why

The AI Advisor pins the Anthropic (Claude) provider to a single fixed model (`claude-sonnet-4-6`), while the Gemini provider already lets users choose between several models. Claude Sonnet 5 has shipped, and users on the Anthropic provider currently have no way to select it — they are stuck on Sonnet 4.6 until the hard-coded constant is changed. Giving Anthropic the same generic model picker Gemini already has lets users opt into Sonnet 5 (and future Claude models) without a code change.

## What Changes

- Add **Sonnet 5** (`claude-sonnet-5`) as a selectable model for the Anthropic AI Advisor provider, alongside the existing Sonnet 4.6.
- Convert `AnthropicProvider` from a single hard-coded model to the same model-catalog pattern `GeminiProvider` uses: an `availableModels()` catalog, a mutable `m_model`, `setModel()` validation, and a `shortModelName()` override.
- Wire the Anthropic provider's model choice through `AIManager` construction and live settings-change sync, so the selection persists (under the existing generic `ai/model/anthropic` key) and takes effect immediately — matching Gemini's wiring exactly.
- The existing generic model-picker dropdown in the AI settings tab auto-appears for Anthropic once its catalog has more than one entry — no new UI is added; optionally surface a per-model hint for Anthropic the way Gemini has one.
- Update `docs/CLAUDE_MD/AI_ADVISOR.md` to reflect that Anthropic now offers a model choice.

No breaking changes: the default model remains the current one, settings storage and the provider list are unchanged, and existing users keep their behavior until they explicitly pick Sonnet 5.

## Capabilities

### New Capabilities
- `advisor-model-selection`: Users can choose among the models a selected AI Advisor provider offers; each provider exposes a catalog of one or more models, the choice persists per provider, and providers with more than one model surface a picker in AI settings. Anthropic offers Sonnet 4.6 and Sonnet 5.

### Modified Capabilities
<!-- None — no existing spec's requirements change. -->

## Impact

- **Code**: `src/ai/aiprovider.h` and `src/ai/aiprovider.cpp` (`AnthropicProvider`: add `availableModels()`, `setModel()`, `shortModelName()`, `m_model`; replace the three fixed-model request sites); `src/ai/aimanager.cpp` (Anthropic `setModel()` in `createProviders()` and `onSettingsChanged()`).
- **UI**: `qml/pages/settings/SettingsAITab.qml` — generic picker already handles it; optional Anthropic model hint.
- **Settings**: none — reuses the existing generic `providerModel()`/`setProviderModel()` storage (`ai/model/anthropic` key). No new `Settings.ai` property.
- **MCP**: `ai_advisor_invoke` inherits the selected model automatically via `currentModelName()`; no per-model wiring needed.
- **Docs**: `docs/CLAUDE_MD/AI_ADVISOR.md`.
- **APIs/dependencies**: none — same Anthropic Messages API endpoint, auth headers, and prompt-cache logic; only the `model` field value varies.
