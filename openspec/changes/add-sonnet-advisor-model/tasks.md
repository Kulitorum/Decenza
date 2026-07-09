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

## 6. Verification

- [ ] 6.1 Quick compile check via Qt Creator MCP (build the worktree project). BLOCKED: Qt Creator's active project is the main checkout, not this worktree, and both projects are named "Decenza" so the build tool can't target the worktree. Switch the active project to the worktree, then build.
- [ ] 6.2 Confirm `MainController.aiManager.availableModels("anthropic")` returns two entries and the picker renders; have Jeff launch the app to verify selecting Sonnet 5 persists across restart and that the next advisor request uses Sonnet 5.
- [x] 6.3 Confirm the Anthropic Messages API model id string for Sonnet 5 is correct (resolve the Open Question in design.md) before merge. Resolved: canonical id is `claude-sonnet-5` (Sonnet 5 in the Claude 5 family); kept Sonnet 4.6 as the default first catalog entry, so upgrading users are unaffected until they opt in.
