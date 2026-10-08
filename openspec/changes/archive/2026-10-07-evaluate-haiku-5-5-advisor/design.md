## Context

The Anthropic catalog has one model, Sonnet 5.5 (`src/ai/aiprovider.cpp`, `AnthropicProvider::availableModels`). `AIRequestShape::disableAnthropicThinking` sends `between_tools` for Sonnet 5.5 and `disabled` for every other model. That default matters because Anthropic models that are left to think adaptively can spend the whole 4,096-token output cap and return no text (#1691). `tools/ai_model_eval` holds the method. Captures come from the production tablet through `ai_advisor_invoke dryRun`, and the replay mirrors the app's request and its acceptance rule for the `nextShot` block. The last full run (2026-10-05, trimmed prompt) is the baseline. Haiku 4.5 failed it, gpt-6-luna was the only value pick that passed, and that run set the rule: a value pick gives no bad advice.

What is known about Haiku 5.5 from Anthropic's documentation (claude-api reference, cached 2026-10-06):

| | Haiku 4.5 | Haiku 5.5 | Sonnet 5.5 |
|---|---|---|---|
| $/MTok in / out | 1.00 / 5.00 | 0.10 / 0.50 (≤100K-token prompts; 0.50 / 2.50 above) | 2.00 / 10.00 |
| Context | 200K | 1M | 1M |
| Thinking off | default | `disabled` accepted at effort `high` or below; on by default | `disabled` is a 400; `between_tools` |
| Default effort | — | `medium` | `high` |
| Non-default `temperature` | allowed | 400 | 400 |
| Minimum cacheable prefix | 4,096 | 512 | 512 |
| Tokenizer | old | newer (counts must be re-measured) | Sonnet 5.5's |

The prompt is about 21K tokens on Sonnet's tokenizer, well under the 100K price break. A rough estimate is $0.002–0.003 per shot, close to Luna's $0.0015. The cost line will use measured tokens, not this estimate.

## Goals / Non-Goals

**Goals:**
- Answer one question with evidence: does Haiku 5.5 give no bad advice on the tablet scenarios, with the request the app would send?
- If it does, ship it as the Anthropic value pick. If not, record why so nobody has to run the same evaluation again.

**Non-Goals:**
- Changing the Anthropic default. Sonnet 5.5 stays first even if Haiku matches it, because the rule is one balanced and one value pick and six scenarios are too few to promote a small tier.
- Re-evaluating other models, or adding new scenarios. The replay reuses the 2026-10-05 scenario set so results compare directly with Haiku 4.5's.
- Server-side refusal fallbacks. The app does not use them on any model, and Haiku 5.5 has none.
- Translator tuning. It inherits the user's choice and needs no new code.

## Decisions

**1. Thinking is measured, not assumed off. Haiku 5.5 runs adaptive thinking at `effort: low` on the advisor path.**
The bar is value: good advice per dollar. On 2026-10-07, thinking off (`disabled`, what the app sends every non-Sonnet model) failed. It gave four bad-advice samples in twelve, two grind changes on a prep failure and two direction reversals, which is the Haiku 4.5 shape. Adaptive thinking at `low` effort passed all six samples on the failing scenarios, for about $0.0005 more per shot. It has to pass the full six-scenario replay before it is admitted (tasks 3.7).
*Alternative:* reject Haiku 5.5 on the thinking-off result. Rejected by the maintainer: the bar is value, and thinking on still costs about a twentieth of Sonnet.

**1a. Thinking on applies to the advisor's analysis requests only.**
`analyze()` and `analyzeConversation()` get the new setting. Test Connection (`max_tokens: 10`), bag extraction (`analyzeUrl`), web search and the translator keep `disabled`: at 10 tokens adaptive thinking returns no text (#1691), and nothing showed that extraction or translation needs it. This is a second function in `AIRequestShape` beside `disableAnthropicThinking`, not a branch inside it, so the callers that must stay off cannot pick thinking up by accident. Follow-up turns resend assistant replies as text only, because the app keeps only text blocks. With no thinking block replayed, the preserved-thinking history check has nothing to reject. A real two-turn call confirms this before admission.
*Alternative:* thinking on for every Anthropic call to Haiku. Rejected because it breaks Test Connection.

**2. Same-day reference arms: Sonnet 5.5 and gpt-6-luna on the same captures.**
Haiku 4.5's failure was measured on 2026-10-05 captures. If the system prompt has changed since then, an old number is not comparable. Luna is the bar a value pick has to clear, and it costs the same, so the comparison is direct. Sonnet anchors the right answers. Cost: 6 scenarios × 2 runs × 3 arms is about $0.80, nearly all of it Sonnet. *(Ran: the system prompt had changed since `captured_v3`, so all six scenarios were recaptured into `captured_v4`.)*
*Alternative:* compare against the logged 2026-10-05 results only. Rejected unless a dry run shows the system prompt is byte-identical to `captured_v3`'s, in which case the reference arms can be skipped and the cost drops to about $0.10.

**3. Judging: blind, with the pass bar written down before reveal.**
`replay.py blind`, with judgments recorded before `reveal`, per the README. Pass means all of the following:
- right direction on `bitter-47s` (coarser) and `worst-score` (back toward the 6.5 shot that scored 75);
- no grind change on `untasted-run` or `blowout`, which must ask about taste or name the prep failure;
- no invented taste;
- a usable block on at least as many tasted samples as Luna.

`sour` and `bitter-over` are reported but are not pass/fail, because they no longer have one right answer (findings log, 2026-10-05). Any `stop_reason: "refusal"` is a fail.

**4. If it passes, the shape change is one comparison in `disableAnthropicThinking`.**
Haiku 5.5 takes `disabled`, which the function already sends to every model except Sonnet 5.5. If the probe confirms that, the only code change to the shape is the `tst_aiproviders` verified-table row that catalog admission requires. The function's comment is updated to say which models it covers.

**5. OpenRouter follows only if OpenRouter serves it.**
The OpenRouter catalog's rule is "the direct providers' models". If OpenRouter lists `anthropic/claude-haiku-5.5` on the day of the run, it goes through the same probe at OpenRouter's reasoning setting and is added with the direct entry. If it is not listed, that is recorded as a hold in tasks.md and does not block the direct addition.

**6. The spec states the admission rule instead of tracking model names.**
The main spec went stale because it named models that later rotated out. The rewritten requirements name only what this change decides (Anthropic: Sonnet 5.5, then Haiku 5.5) and add the admission requirement. **If Haiku fails**, the delta is revised before archive: the Anthropic scenarios name Sonnet 5.5 alone, the picker example moves to OpenAI, and the admission requirement stays.

## Risks / Trade-offs

- [Six scenarios × two runs cannot separate two models that both pass] → The findings entry says so. The decision is pass/fail against the no-bad-advice bar, not a ranking against Luna.
- [Adaptive thinking on by default: if `disabled` is silently ignored, the reply could be thinking-only (#1691)] → The probe checks for a `text` block and zero thinking tokens, not just a 200.
- [The newer tokenizer makes the cost estimate wrong] → The cost line comes from `usage` in the replay, including the 5-minute cache write, the same way Sonnet's line was derived.
- [Release-day model behaviour or pricing changes] → Prices in `replay.py` are verified against Anthropic's pricing page on the day of the run and the check date is recorded.
- [Translator users who pick Haiku 5.5 get Haiku-quality translations] → This is the user's choice and is documented in `translationModelFor`. The translator default stays Sonnet 5.5.

## Migration Plan

No migration. Rollback is removing the catalog entry. `AIManager::savedModelFor` then clears a saved `claude-haiku-5-5` at startup, and those users fall back to Sonnet 5.5.
