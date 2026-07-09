## 1. AnthropicProvider model catalog (`src/ai/aiprovider.h`)

- [x] 1.1 In the `AnthropicProvider` class declaration (~lines 120-160), add overrides matching the Gemini pattern: `QList<ModelOption> availableModels() const override;`, `void setModel(const QString& id) override;`, `QString shortModelName() const override;`, and a `QString m_model;` member.
- [x] 1.2 Keep `MODEL`/`MODEL_DISPLAY` constants only if still referenced; otherwise repurpose them as the catalog's first (default) entry so nothing else breaks.

## 2. AnthropicProvider implementation (`src/ai/aiprovider.cpp`)

- [x] 2.1 Implement `availableModels()` returning `{{ "claude-sonnet-4-6", "Sonnet 4.6" }, { "claude-sonnet-5", "Sonnet 5" }}` (Sonnet 4.6 first = default), mirroring `GeminiProvider::availableModels()` at `:619-631`.
- [x] 2.2 Implement `setModel()` to validate the incoming id against the catalog and ignore unknown ids, mirroring `:632-643`.
- [x] 2.3 Implement `shortModelName()` to map `m_model` → its display name, mirroring `:645-652`.
- [x] 2.4 In the `AnthropicProvider` constructor (~`:331-337`), default `m_model = availableModels().first().id`.
- [x] 2.5 Replace the three request-site uses of `QString::fromLatin1(MODEL)` with `m_model` (analyze `:375`, analyzeConversation `:400`, testConnection `:522`).

## 3. AIManager wiring (`src/ai/aimanager.cpp`)

- [x] 3.1 In `createProviders()`, after the Anthropic provider is constructed (~`:110`), call `anthropic->setModel(m_settings->ai()->providerModel("anthropic"))`, matching Gemini at `:119`.
- [x] 3.2 In `onSettingsChanged()`, extend the Anthropic block (~`:1389-1392`) to also call `anthropic->setModel(m_settings->ai()->providerModel("anthropic"))`, matching Gemini's block at `:1394-1398`.

## 4. Settings tab hint (optional, `qml/pages/settings/SettingsAITab.qml`)

- [x] 4.1 Confirm the generic model dropdown (`:240-308`) auto-appears for Anthropic now that its catalog has >1 entry (no code change expected — `visible: options.length > 1`).
- [x] 4.2 Optionally generalize the model-hint helper text (`:297-307`, currently gated to `currentProvider === "gemini"`) to also show an Anthropic hint.

## 5. Documentation

- [x] 5.1 Update `docs/CLAUDE_MD/AI_ADVISOR.md` to state that the Anthropic provider now offers a model choice (Sonnet 4.6 and Sonnet 5) rather than a single fixed model.

## 6. OpenAIProvider model catalog (`src/ai/aiprovider.h` / `.cpp`)

- [x] 6.1 In the `OpenAIProvider` class declaration (`:90-118`), add the same overrides used for Anthropic: `QList<ModelOption> availableModels() const override;`, `void setModel(const QString& modelId);`, declare `QString shortModelName() const override;` (drop the inline `return MODEL_DISPLAY;`), change `modelName()` to `return m_model;`, and add a `QString m_model;` member. Remove the now-unused `MODEL`/`MODEL_DISPLAY` constants (keep `API_URL`).
- [x] 6.2 Implement `availableModels()` returning `{{ "gpt-5.4-mini", "GPT-5.4 mini" }, { "gpt-5.4", "GPT-5.4" }}` (mini first = default), plus `setModel()` (validate against catalog, ignore unknown ids) and `shortModelName()` (id → display), mirroring the Anthropic/Gemini implementations.
- [x] 6.3 Default `m_model = availableModels().first().id` in the `OpenAIProvider` constructor.
- [x] 6.4 Replace the request-site uses of `QString::fromLatin1(MODEL)` in `OpenAIProvider` (`:170`, `:198`, and any test-connection site) with `m_model`.

## 7. OpenAI wiring, hint, and docs

- [x] 7.1 In `AIManager::createProviders()`, after the OpenAI provider is constructed (~`:100-106`), call `openai->setModel(m_settings->ai()->providerModel("openai"))`.
- [x] 7.2 In `AIManager::onSettingsChanged()`, extend the OpenAI block (~`:1384-1387`) to also call `openai->setModel(m_settings->ai()->providerModel("openai"))`.
- [x] 7.3 Add an OpenAI branch to the per-provider model hint in `SettingsAITab.qml` (the switch added for Anthropic), e.g. "GPT-5.4 is more capable. GPT-5.4 mini is cheaper and faster."
- [x] 7.4 Update `docs/CLAUDE_MD/AI_ADVISOR.md` provider table so the OpenAI row reads "User-selected (GPT-5.4 mini default, or GPT-5.4)".
- [x] 7.5 Send `reasoning_effort: "minimal"` on both `OpenAIProvider` request paths (`analyze` / `analyzeConversation`) so GPT-5 reasoning tokens don't eat the 1024 completion budget (risking `nextShot` truncation) and to keep latency/cost low. OpenAIProvider only — not OpenRouter (pass-through models) or `testConnection()`.

## 8. Review follow-ups & verification

- [x] 8.4 (comment rot) Move the pricing figures + omitted-tier rationale out of the `OpenAIProvider::availableModels()` code comment into `docs/CLAUDE_MD/AI_ADVISOR.md`; leave a code-local "why" + doc pointer.
- [x] 8.5 (footgun) Document the "every catalog model is a reasoning model that accepts `reasoning_effort`" invariant at the OpenAI request site; soften "completion budget" wording to "1024-token output cap".
- [x] 8.6 (coverage) Add `tests/tst_aiproviders.cpp` (+ CMake target) pinning availableModels()/setModel()/shortModelName()/modelName() for OpenAI, Anthropic, and Gemini: catalog order, constructor default, and the empty/unknown/valid setModel branches. Pure logic; target compiles only `aiprovider.cpp`.

- [x] 8.1 (partial) `tst_aiproviders` built and run via a scoped CLI Debug build (Qt 6.11.1, Ninja) — **5 passed, 0 failed**. This also compiled the production `src/ai/aiprovider.cpp` cleanly (the test target links it), confirming the catalog + `reasoning_effort` code builds. STILL PENDING: full-app compile (`aimanager.cpp` + `SettingsAITab.qml` in an app build) — the Qt Creator active project is the main checkout, not this worktree, so build the worktree app config to finish this.
- [ ] 8.2 Confirm `MainController.aiManager.availableModels("anthropic")` and `availableModels("openai")` each return two entries and both pickers render; have Jeff launch the app to verify selecting Sonnet 5 / GPT-5.4 persists across restart and that the next advisor request uses the chosen model.
- [x] 8.3 Confirm the Anthropic Messages API model id for Sonnet 5 and the OpenAI Chat Completions model id for GPT-5.4 are correct. Resolved: `claude-sonnet-5` (Claude 5 family) and `gpt-5.4` ($2.50/$15 per 1M in/out per OpenAI's official pricing/models docs). Cheaper model kept first as the default in each catalog, so upgrading users are unaffected until they opt in.
