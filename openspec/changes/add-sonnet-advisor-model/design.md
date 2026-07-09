## Context

The AI Advisor supports five providers (OpenAI, Anthropic, Gemini, OpenRouter, Ollama). Model selection is already generic across the stack:

- `AIProvider::availableModels()` returns a `QList<ModelOption{ id, displayName }>`; an empty list means "no picker" (`src/ai/aiprovider.h:21-40`).
- `GeminiProvider` overrides `availableModels()`, `setModel()`, `shortModelName()`, and holds a mutable `m_model` (`src/ai/aiprovider.h:162-201`, `src/ai/aiprovider.cpp:610-652`).
- Settings persist the choice generically by provider id via `providerModel()`/`setProviderModel()` → QSettings key `ai/model/<providerId>` (`src/core/settings_ai.h:52-53`, `src/core/settings_ai.cpp:105-117`).
- The QML picker in `qml/pages/settings/SettingsAITab.qml:240-308` is populated from `MainController.aiManager.availableModels(provider)` and is `visible: options.length > 1`, writing back through `Settings.ai.setProviderModel(...)`.
- `AIManager::createProviders()` and `onSettingsChanged()` call `setModel(providerModel(id))` for Gemini but not for Anthropic.

`AnthropicProvider` and `OpenAIProvider` are the odd ones out: each hard-codes a single model via `static constexpr MODEL` / `MODEL_DISPLAY` (Anthropic `src/ai/aiprovider.h:158-159` = `claude-sonnet-4-6`; OpenAI `:116-117` = `gpt-5.4-mini`) and uses `QString::fromLatin1(MODEL)` at its request sites. Because neither overrides `availableModels()`, no picker appears and the model is uneditable.

The task is to make both `AnthropicProvider` and `OpenAIProvider` follow the Gemini pattern so Sonnet 5 and GPT-5.4 become selectable.

## Goals / Non-Goals

**Goals:**
- Add Sonnet 5 (`claude-sonnet-5`) as a selectable Anthropic model alongside Sonnet 4.6, and GPT-5.4 (`gpt-5.4`) as a selectable OpenAI model alongside GPT-5.4 mini.
- Reuse the existing generic catalog/settings/UI/MCP machinery; the change is confined to the Anthropic and OpenAI providers plus their `AIManager` wiring points.
- Preserve current default behavior for existing users (defaults stay Sonnet 4.6 / GPT-5.4 mini until they pick otherwise).

**Non-Goals:**
- No new `Settings.ai` property or new QML file (storage and picker are already generic).
- No changes to the OpenRouter/Ollama providers (already user-selectable models).
- No change to the Anthropic Messages / OpenAI Chat Completions endpoints, auth headers, or caching behavior — only the `model` field value varies.
- Not adding GPT-5.5 (frontier, ~7× the mini's input cost, aimed at complex reasoning this task doesn't need) or GPT-5.4 nano (a step down from the current default).

## Decisions

**Decision: Mirror the Gemini catalog pattern on `AnthropicProvider` rather than inventing a new mechanism.**
Give `AnthropicProvider` an `availableModels()` override returning `{{ "claude-sonnet-4-6", "Sonnet 4.6" }, { "claude-sonnet-5", "Sonnet 5" }}`, a mutable `QString m_model`, a validating `setModel()`, and a `shortModelName()` override that maps id → display name. Replace the three `QString::fromLatin1(MODEL)` request sites with `m_model`. Default `m_model` to `availableModels().first().id` in the constructor.
- *Rationale*: This is the exact contract the generic UI, settings, and MCP surfaces already consume. Copying Gemini keeps behavior uniform and requires zero changes outside the provider (plus manager wiring). The `MODEL`/`MODEL_DISPLAY` constants can be dropped or repurposed as the catalog's default.
- *Alternative considered*: Add a second constant and a boolean toggle — rejected; it duplicates the catalog concept, doesn't generalize to future models, and wouldn't drive the existing picker.

**Decision: Send `reasoning_effort: "minimal"` on OpenAI advisor requests.**
The GPT-5 family are reasoning models; on the Chat Completions API, hidden reasoning tokens are billed against the same `max_tokens` (1024) budget as the visible answer. At the API-default effort a request can spend budget reasoning and then truncate before emitting the trailing ` ```json nextShot ``` ` block the parser depends on. Setting `reasoning_effort: "minimal"` on both OpenAI request paths (`analyze` / `analyzeConversation`) keeps chain-of-thought minimal — dial-in advice needs little — protecting the completion budget and lowering latency/cost. This mirrors Gemini's explicit thinking-off config and makes "no deep thinking" true for OpenAI, not just the default behavior.
- *Scope*: `OpenAIProvider` only. Not applied to `OpenRouterProvider`, which passes through to arbitrary user-selected models (many non-reasoning) that would ignore or reject the field. Not applied to `testConnection()` (the `/v1/models` GET sends no model/body).
- *Note*: this also corrects the pre-existing shipping default (`gpt-5.4-mini` previously ran at default effort). If minimal ever reads as too shallow in practice, bumping to `"low"` is a one-word change.
- *Alternative considered*: leave effort unset — rejected; risks `nextShot` truncation and contradicts the "no deep thinking" intent.

**Decision: Add GPT-5.4 as the OpenAI "more capable" opt-in; keep GPT-5.4 mini the default.**
Give `OpenAIProvider` the same catalog `{{ "gpt-5.4-mini", "GPT-5.4 mini" }, { "gpt-5.4", "GPT-5.4" }}` (mini first = default). GPT-5.4 mini is $0.75/$4.50 per 1M in/out; GPT-5.4 is $2.50/$15 — the same-family mid-tier, ~3.3× the cost for a real capability step up, and OpenAI's automatic prompt caching (~90% off the >1024-token system-prompt prefix) softens the effective per-request cost.
- *Rationale*: Mirrors the two-tier shape of the other providers (cheap default + one opt-in step up). GPT-5.4 is the natural step up for shot-dialing analysis with the structured `nextShot` JSON.
- *Alternatives considered*: **GPT-5.5** — frontier, ~7× the mini's input cost, aimed at complex reasoning/coding this task doesn't need; rejected as a default-adjacent choice. **GPT-5.4 nano** ($0.20/$1.25) — cheaper but weaker than the current default; no reason to offer something worse than what users already get.

**Decision: Keep Sonnet 4.6 and GPT-5.4 mini as the defaults (first catalog entry each).**
List the cheaper model first in each catalog so `availableModels().first()` remains the default for users who never open the picker.
- *Rationale*: No behavior change on upgrade; opting into Sonnet 5 / GPT-5.4 is an explicit user action. Honors the "smarter defaults, no surprise regressions" preference.
- *Alternative considered*: Default to the more capable model — deferred to the user; can be a one-line reorder later if desired.

**Decision: Wire each provider's `setModel()` into `AIManager` in both the construction and live-sync paths.**
Add `provider->setModel(m_settings->ai()->providerModel("<id>"))` for Anthropic (~`aimanager.cpp:110` and its `onSettingsChanged()` block ~`:1391`) and for OpenAI (its `createProviders()` construction and `onSettingsChanged()` block ~`:1385`), matching Gemini's `:119` and `:1394-1398`.
- *Rationale*: Without this, the persisted choice is never applied and live changes won't take effect until restart. `setModel()` ignores unknown ids, so an empty/legacy stored value harmlessly leaves the default.

**Decision: Optionally add Anthropic and OpenAI model hints in the settings tab.**
The hint text at `SettingsAITab.qml:297-307` is hard-gated to `currentProvider === "gemini"`. Generalize it to a per-provider switch so Anthropic and OpenAI each get their own hint. Low priority; the picker itself works without it.

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
