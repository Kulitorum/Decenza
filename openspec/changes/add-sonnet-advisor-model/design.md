## Context

The AI Advisor supports five providers (OpenAI, Anthropic, Gemini, OpenRouter, Ollama). Model selection is already generic across the stack:

- `AIProvider::availableModels()` returns a `QList<ModelOption{ id, displayName }>`; an empty list means "no picker" (`src/ai/aiprovider.h:21-40`).
- `GeminiProvider` overrides `availableModels()`, `setModel()`, `shortModelName()`, and holds a mutable `m_model` (`src/ai/aiprovider.h:162-201`, `src/ai/aiprovider.cpp:610-652`).
- Settings persist the choice generically by provider id via `providerModel()`/`setProviderModel()` → QSettings key `ai/model/<providerId>` (`src/core/settings_ai.h:52-53`, `src/core/settings_ai.cpp:105-117`).
- The QML picker in `qml/pages/settings/SettingsAITab.qml:240-308` is populated from `MainController.aiManager.availableModels(provider)` and is `visible: options.length > 1`, writing back through `Settings.ai.setProviderModel(...)`.
- `AIManager::createProviders()` and `onSettingsChanged()` call `setModel(providerModel(id))` for Gemini but not for Anthropic.

`AnthropicProvider` is the odd one out: it hard-codes a single model via `static constexpr MODEL = "claude-sonnet-4-6"` / `MODEL_DISPLAY = "Sonnet 4.6"` (`src/ai/aiprovider.h:158-159`) and uses `QString::fromLatin1(MODEL)` at three request sites (`aiprovider.cpp:375`, `:400`, `:522`). Because it has no `availableModels()` override, no picker appears and the model is uneditable.

The task is to make `AnthropicProvider` follow the Gemini pattern so Sonnet 5 becomes selectable.

## Goals / Non-Goals

**Goals:**
- Add Sonnet 5 (`claude-sonnet-5`) as a selectable Anthropic model alongside Sonnet 4.6.
- Reuse the existing generic catalog/settings/UI/MCP machinery; the change is confined to the Anthropic provider plus two `AIManager` wiring points.
- Preserve current default behavior for existing users (default stays Sonnet 4.6 until they pick otherwise).

**Non-Goals:**
- No new `Settings.ai` property or new QML file (storage and picker are already generic).
- No changes to the OpenAI/OpenRouter/Ollama providers.
- No change to the Anthropic API endpoint, auth headers, or prompt-cache logic — only the `model` field value varies.
- Not verifying/altering the exact Sonnet 5 API model id string beyond wiring it in (see Open Questions).

## Decisions

**Decision: Mirror the Gemini catalog pattern on `AnthropicProvider` rather than inventing a new mechanism.**
Give `AnthropicProvider` an `availableModels()` override returning `{{ "claude-sonnet-4-6", "Sonnet 4.6" }, { "claude-sonnet-5", "Sonnet 5" }}`, a mutable `QString m_model`, a validating `setModel()`, and a `shortModelName()` override that maps id → display name. Replace the three `QString::fromLatin1(MODEL)` request sites with `m_model`. Default `m_model` to `availableModels().first().id` in the constructor.
- *Rationale*: This is the exact contract the generic UI, settings, and MCP surfaces already consume. Copying Gemini keeps behavior uniform and requires zero changes outside the provider (plus manager wiring). The `MODEL`/`MODEL_DISPLAY` constants can be dropped or repurposed as the catalog's default.
- *Alternative considered*: Add a second constant and a boolean toggle — rejected; it duplicates the catalog concept, doesn't generalize to future models, and wouldn't drive the existing picker.

**Decision: Keep Sonnet 4.6 as the default (first catalog entry).**
List Sonnet 4.6 first so `availableModels().first()` remains the default for users who never open the picker.
- *Rationale*: No behavior change on upgrade; opting into Sonnet 5 is an explicit user action. Honors the "smarter defaults, no surprise regressions" preference.
- *Alternative considered*: Default to Sonnet 5 — deferred to the user; can be a one-line reorder later if desired.

**Decision: Wire Anthropic `setModel()` into `AIManager` in both the construction and live-sync paths.**
Add `anthropic->setModel(m_settings->ai()->providerModel("anthropic"))` in `createProviders()` (after the Anthropic provider is built, ~`aimanager.cpp:110`) and in `onSettingsChanged()`'s Anthropic block (~`aimanager.cpp:1391`), matching Gemini's `:119` and `:1394-1398`.
- *Rationale*: Without this, the persisted choice is never applied and live changes won't take effect until restart. `setModel()` ignores unknown ids, so an empty/legacy stored value harmlessly leaves the default.

**Decision: Optionally add an Anthropic model hint in the settings tab.**
The hint text at `SettingsAITab.qml:297-307` is hard-gated to `currentProvider === "gemini"`. Optionally generalize or add an Anthropic branch. Low priority; the picker itself works without it.

## Risks / Trade-offs

- **Sonnet 5 API model id may differ from `claude-sonnet-5`** → The catalog id string is the single source of truth sent to the API; if Anthropic's actual id differs, update the one string in `availableModels()`. Verify against the Anthropic Messages API before shipping (see Open Questions).
- **Legacy stored value under `ai/model/anthropic`** → Previously Anthropic never wrote this key, so it is typically empty; `setModel()` ignoring unknown/empty ids means the provider falls back to the catalog default — no migration needed.
- **Prompt-cache assumptions per model** → The existing `cache_control` / 1h-TTL logic (`aiprovider.cpp:408-463`) is model-agnostic; switching the model id does not change caching behavior, but a new model could have different cache-eligibility. Low risk; behavior degrades gracefully to non-cached if unsupported.
- **Model-name reporting consistency** → `currentModelName()`/`shortModelName()` must reflect `m_model` so MCP `ai_advisor_invoke` reports the model actually used. Covered by overriding `shortModelName()`.

## Migration Plan

No data migration. Ship the code change; existing users stay on Sonnet 4.6 until they open AI settings and pick Sonnet 5. Rollback is a plain revert — the settings key is generic and harmless if left populated (an unknown id is ignored). Update `docs/CLAUDE_MD/AI_ADVISOR.md` to note Anthropic now offers a model choice.

## Open Questions

- What is the exact Anthropic Messages API model id and display name for Sonnet 5? The design assumes `claude-sonnet-5` / "Sonnet 5"; confirm the canonical id (and whether a dated variant is preferred) before merge.
- Should Sonnet 5 become the default (first catalog entry) now, or stay opt-in behind Sonnet 4.6? Defaulting to opt-in unless the user says otherwise.
