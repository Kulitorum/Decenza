## Why

Anthropic shipped Claude Haiku 5.5 on 2026-10-07. Users who chose Anthropic have one option, Sonnet 5.5 at about $0.06 per shot. Haiku 4.5 was turned down on 2026-10-05 because it reversed the grind direction on two of three tasted scenarios, and `AI_ADVISOR.md` already says to probe and replay Haiku 5.5 when it ships. At $0.10/$0.50 per MTok it costs the same as gpt-6-luna, our best value pick, and a tenth of Haiku 4.5. If it gives no bad advice it fills the empty value slot in the Anthropic catalog. If it fails, we record that and stop.

## What Changes

- **Evaluate first, decide second.** Run Haiku 5.5 through `tools/ai_model_eval`: the request-shape probe, then a replay of the six tablet scenarios with Sonnet 5.5 and gpt-6-luna as same-day reference arms. It is held to the existing value-pick rule: no bad advice.
- **If it passes:** add `claude-haiku-5-5` to the Anthropic catalog as the second entry (the value pick). Sonnet 5.5 stays the default. Add its thinking setting to `AIRequestShape` and to `tst_aiproviders`' verified table, a measured cost line, an updated model hint, and the OpenRouter equivalent if OpenRouter serves it with an accepted reasoning setting.
- **If it fails:** no catalog change. The findings log and `AI_ADVISOR.md` record the reason, replacing the "announced for the coming weeks" line.
- **Either way:** fix the `advisor-model-selection` spec. It still requires Sonnet 4.6, Sonnet 5, GPT-5.4 mini and GPT-5.4, none of which the catalog has offered since 2026-10-05, and it describes an Anthropic picker that the current one-model catalog does not show. Add the rule that has been governing catalog changes since July without being written down: a model enters a catalog only after the probe and the replay.
- The evaluation harness learns Haiku 5.5 (prices, thinking shape, probe list), so the probe and replay send exactly what the app would.

## Capabilities

### New Capabilities
<!-- None -->

### Modified Capabilities
- `advisor-model-selection`: catalog requirements name the current models instead of retired ones; Anthropic's catalog gains Haiku 5.5 as a non-default value pick if it passes; new requirement that a model is admitted to a catalog only after passing the request-shape probe and a replay with no bad advice; examples in the other requirements updated to models the catalog actually offers.

## Impact

- **Tooling**: `tools/ai_model_eval/replay.py` (`PRICES`, `anthropic_thinking`), `probe_request_shape.py` (`ANTHROPIC_MODELS`, thinking shape), README findings log.
- **Code (pass only)**: `src/ai/airequestshape.h` (`disableAnthropicThinking`, `setOpenRouterReasoning` if OpenRouter is added), `src/ai/aiprovider.cpp` (Anthropic and possibly OpenRouter `availableModels()`, `modelHint()`, `kCostLines`), `tests/tst_aiproviders.cpp` (verified-shape table, catalog expectations). Translation fallbacks for the new cost key.
- **Translator**: no change. It uses the user's chosen model, so a user who picks Haiku 5.5 translates with it too. Its default stays the first catalog entry, Sonnet 5.5. It sends no `temperature` to Anthropic, and Haiku 5.5 rejects any non-default value.
- **Docs**: `docs/CLAUDE_MD/AI_ADVISOR.md` (providers table, rationale, cost table); wiki Manual §13 model table (pass only; push timing is the maintainer's call).
- **Spend**: about $1 of API calls for the evaluation, on the maintainer's approval.
- No settings migration. Saved selections are untouched; Sonnet 5.5 stays the default.
