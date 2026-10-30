# AI advisor model evaluation

Two scripts, answering two different questions. Both exist because vendor
benchmarks say nothing useful about espresso dial-in advice, and because the
method is the expensive part — not the API spend, which is around $1 for a full
comparison.

| Script | Question | Providers |
|---|---|---|
| `replay.py` | Does this model give good dial-in advice, and does it emit a usable `nextShot` block? | OpenAI, Anthropic, Gemini, OpenRouter (by model id) |
| `probe_request_shape.py` | Are the thinking/reasoning settings we send accepted by this model? | OpenAI, Anthropic, Gemini, OpenRouter |

Probe first: a model the probe has not passed 400s every request in the app.
Then replay to judge the advice.

**Read this before swapping a model in any catalog.** The rationale for the
current catalogs lives in `docs/CLAUDE_MD/AI_ADVISOR.md`; this directory is how
that rationale gets produced.

## `probe_request_shape.py` — the request-shape check

```bash
python3 probe_request_shape.py
```

Sends every catalog model the settings `src/ai/airequestshape.h` gives it
(mirrored at the top of the script), on the advisor, translator and web-tool
bodies:

- Anthropic must accept the thinking setting **and still return a `text`
  block**: a thinking-only reply is the #1691 symptom, so the status code alone
  proves nothing.
- Gemini's setting must be accepted **and effective**
  (`usageMetadata.thoughtsTokenCount == 0`): an ignored one still bills thinking
  at the output rate. Legal `thinkingLevel` values vary by model.
- OpenAI must accept `reasoning_effort` and, where the translator sends one,
  `temperature`.
- OpenRouter must accept the reasoning setting at the advisor cap and at Test
  Connection's 10 tokens.

Run it whenever a catalog gains an entry.

## The method

### 1. Capture real prompts — never reconstruct them

```
ai_advisor_invoke  { "shot_id": <id>, "dryRun": true }
```

`dryRun` returns the exact `systemPromptUsed` + `userPromptUsed` the app would
send, with no network call, no token cost, and no conversation side effects.
Copy each result to `captured/<scenario-key>.json`.

Writing the prompt by hand in the harness would test a system the app does not
run. The system prompt alone is ~40K characters and changes with the profile,
the bean, the knowledge base, and the shot's own history.

`captured/` and `runs/` are gitignored **on purpose**: the payloads embed
personal shot history — bean names, tasting notes, timestamps — and this is a
public repository.

### 2. Pick scenarios that discriminate

See `scenarios.json`. A scenario earns its place only if it has a
known-correct answer a weaker model can plausibly get wrong. A healthy,
unremarkable shot separates nothing.

**The trap that voids an emission test:** if the shot has no taste feedback,
the advisor correctly asks a clarifying question and correctly *omits* the
`nextShot` block. A run over untasted shots measures the taste gate, not
emission. `scenarios.json` marks this with `hasTasteFeedback`, and
`replay.py emission` filters on it.

### 3. Replay byte-for-byte

`replay.py` mirrors each provider's `analyze()` request — the output cap, the
per-model thinking setting (`src/ai/airequestshape.h`) and Anthropic's cache
markers. If that shape changes, update the helpers at the top of `replay.py`.
`model@effort` pins a model's effort (`gpt-6.1-sol@low`; on Anthropic, adaptive thinking at that effort; on OpenRouter, `reasoning.effort`).

It also mirrors the app's **acceptance** rule, which is a separate thing and
easy to get wrong. `extract_structured_next()` is a port of
`AIManager::parseStructuredNext`: the **last** fence pair, whose closer is the
final non-whitespace content, tagged `json` case-insensitively. The first
version of this harness took the *first* ```` ```json ```` block found
anywhere, so a model echoing an example mid-prose and emitting no trailing
block would have scored as compliant when the app would reject it. Keep the two
in step — a harness that measures a block the app won't accept is measuring
nothing.

### 4. Blind the judging properly

`blind` mode shuffles labels per scenario. It also deliberately prints **no
cost or token counts**, because on the 2026-07-30 run those fingerprinted the
model — the cheapest response is unmistakable — and broke the blind after the
fact. Objective criteria (did it emit the block, did it name the failure)
survive that leak; subjective quality ranking does not.

Write your judgments down *before* `replay.py reveal` or opening `usage.json`.
The latter records per-call input, output, cached and reasoning tokens for cost
comparisons. A provider-reported bill (OpenRouter) is stored as
`reported_cost_usd`; a token-rate estimate is stored as `uncached_cost_usd`.
Printed summaries keep the two totals separate, including in mixed-provider runs.
Only the token-rate estimate excludes cache read/write adjustments.
Each invocation resets `usage.json` before loading keys or making requests.
Every attempted call is recorded, including errors; billed/usage-bearing errors
count toward cost totals. Calls without usage or price data have no cost field
and are reported as excluded, rather than being treated as free.

### 5. Be honest about sample size

Six scenarios of single runs is enough to reproduce a known failure mode. It is
not enough to separate two models that both pass. Say which you have.

## Usage

```bash
python3 replay.py capture-help
python3 replay.py emission --models gpt-6.1-sol,gpt-6-luna
python3 replay.py emission --models gpt-6-luna --efforts none,low
python3 replay.py blind    --models gpt-6.1-sol,openai/gpt-6-luna
python3 replay.py reveal   --run blind
# Compare a prompt change: same shot data, new system prompt in captured_v2/
python3 replay.py emission --models gpt-6.1-sol@low --captured captured_v2 --label v2
```

A `vendor/model` id runs through OpenRouter. Keys come from `$OPENAI_API_KEY` /
`$ANTHROPIC_API_KEY` / `$GEMINI_API_KEY` / `$OPENROUTER_API_KEY`, else
Decenza's own configured keys on macOS. Capture prompts from the production
tablet (the de1 MCP), whose shot ids `scenarios.json` points at.

Prices in `replay.py` **rot**. Verify at
<https://developers.openai.com/api/docs/pricing>. Third-party pricing pages were
checked on 2026-07-30 and found wrong — one listed Terra at $2.50/$15 against an
actual $2.00/$12.

## Findings log

### 2026-10-30 — Adopt Terra low and Luna medium for advisor requests

Implemented the requested choices for analysis and conversation turns: Terra
`low`, Luna `medium`. Direct OpenAI and OpenRouter Luna share the advisor choice;
OpenAI translation retains `none` for Luna/Terra, and OpenRouter Test Connection
retains Luna `none` at its 10-token cap. The replay defaults and request-shape
probe mirror those paths. Sol's setting and the provider defaults are unchanged.

Live probes passed OpenAI Terra low and Luna medium, each translator at none +
temperature 0.3, both Responses web-search paths at low, and OpenRouter Luna
medium at 4,096 tokens / none at 10 tokens. No private espresso prompts were
sent to OpenRouter. Its medium advisor acceptance is verified; the quality and
cost estimate comes from the direct Luna replay below, not an OpenRouter replay.

Luna's displayed estimate is now $0.0022/shot, about $0.20/month at 3 shots/day,
using the tasted medium-effort workload. Terra remains ~$0.015/shot. Changed
translation keys prevent previously cached cost/guidance text from persisting.
Regression checks cover advisor reasoning on both one-shot and conversation
requests, plus the independent connection-test setting. Qt Creator/build/tests
remain on the maintainer's explicit hold; these are live API probe results.

### 2026-10-30 — Terra high/xhigh match low's checks at higher cost

Two runs of all six stored v4 scenarios at high/xhigh/max, preserving the app's
4,096-token cap. High and xhigh completed 12 calls each; max was rejected with
HTTP 400 `unsupported_value`. The Chat Completions error listed only none, low,
medium, high and xhigh, despite the
[model page](https://developers.openai.com/api/docs/models/gpt-6.1-terra) listing
max. This records the tested endpoint's behavior, not a claim about Responses or
future account availability. No max quality result was obtained.

| Terra effort | Usable blocks on tasted shots | Taste gate on untasted shots | Mean reasoning tokens | Mean cost/analysis |
|---|---|---|---|---|
| low (prior runs) | 8/8 | 4/4 | 55 | $0.014525 |
| medium (prior runs) | 8/8 | 4/4 | 298 | $0.015812 |
| high | 8/8 | 4/4 | 734 (328–1,024) | $0.018031 |
| xhigh | 8/8 | 4/4 | 2,348 (864–3,522) | $0.026248 |

Mean input was 13,131 tokens; high/xhigh total output including reasoning was
980/2,624 tokens. Costs use the same standard $1/$5 per million input/output
before cache read/write adjustments. High adds $0.003506 (+24%) over low;
xhigh adds $0.011723 (+81%). At 90 analyses/month, totals are ~$1.31 for low,
$1.62 for high and $2.36 for xhigh. Tasted scenarios alone averaged
$0.018987/$0.028047 at high/xhigh. Fully warm projections are $0.005557/$0.013774.
The 24 completed calls total $0.531355 before cache adjustments, not an invoice.

No output truncations, wrong-type/prose settings, missing required JSON fields,
grind-direction reversals or material reference-context mistakes were observed
in the high/xhigh replies. Suggestions matched the already defensible low/medium
changes: coarser on the bitter restricted shot, return to a better-rated setting
on the low-score shot, shorter yield on bitter overshoot, and a small coarser
step or longer yield on the mildly sour shot. One reply noticed the future
defrost date. No clear overall quality gain emerged on these six saved cases.

Keep Terra's advisor at low. The sample does not establish equal performance on
all tasks; a broader fresh-shot set would be needed to separate settings that
all passed these checks. Criteria and judgments were recorded before reading
per-effort keys/metering. No production settings or Qt Creator state changed.
Private runs: `blind-terra61-remaining-r1-20261030`,
`blind-terra61-remaining-r2-20261030`.

### 2026-10-30 — Luna high adds cost and hits the app's output cap

Follow-up: two runs of all six stored v4 scenarios at `high` (12 calls), with
the same 4,096 total output-token cap. High returned usable blocks on 7/8 tasted
cases and preserved the taste gate on 4/4 untasted cases. On the second sour
case, all 4,096 completion tokens were reasoning, `finish_reason` was `length`,
and the visible reply was empty. Medium had returned 8/8 without truncation.

High averaged 2,472 reasoning tokens (512–4,096), 2,735 total output tokens, and
13,131 input tokens. Normalized cost before cache read/write adjustments was
$0.002681 per analysis, versus medium's $0.002094: +$0.000586 (~28%), about
$0.053 more for 90 analyses/month. High totals ~$0.24/month on that basis.
Tasted scenarios alone averaged $0.002992, including the failed answer. A fully
warm prompt projects $0.001499 per analysis. The twelve calls total $0.032168
before cache adjustments; token cost includes the failed reply.

The visible high replies gave defensible grind/yield changes and showed no
material reference-context error in this small sample. That does not establish
superiority to medium; high lowered usable-answer consistency at the shipped cap.
Keep the recommendation at medium. A higher cap was not evaluated, and no Luna
production setting was changed. This single-effort follow-up was not blinded;
response judgments preceded opening metering. Qt Creator remained untouched.
Private runs: `blind-luna6-high-r1-20261030`, `blind-luna6-high-r2-20261030`.

### 2026-10-30 — Luna benefits most from medium reasoning in this replay

Two runs of all six `captured_v4` scenarios at none/low/medium (36 API calls),
using the existing 4,096-token output cap. Criteria and reply judgments were
written before opening effort labels and token usage. Labels used different
seeds across runs. This compares reasoning effort on one model, not model quality
across providers. Qt Creator was not used for this follow-up.

| Luna effort | Usable blocks on tasted shots | Taste gate on untasted shots | Mean reasoning tokens | Mean cost/analysis |
|---|---|---|---|---|
| none | 4/8 | 4/4 | 0 | $0.001436 |
| low | 6/8 | 4/4 | 137 (0–484) | $0.001507 |
| medium | 8/8 | 4/4 | 1,285 (251–2,048) | $0.002094 |

Mean input was 13,131 tokens; output including reasoning was 245/387/1,563
respectively. Costs use standard $0.10/$0.50 input/output per million, before cache
reads/writes, at identical input workloads. Low adds $0.000071 (~5%); medium
adds $0.000659 (~46%). Medium's increase is about $0.06 for 90 analyses/month.
On tasted prompts alone, medium averaged $0.002235, about $0.20 for 90 shots.
With a fully warm prompt, projected totals are $0.000254/$0.000325/$0.000913.
The 36 calls total $0.06044 before cache adjustments; this is not an invoice.

At none, two score-only replies asked for flavor instead of giving a next-shot
block, one bitter/overshoot reply gave only prose advice, and one sour reply
asked another question. Low still omitted the block on one bitter/overshoot
and one sour reply despite recommending changes in prose. Medium returned all
eight blocks, made defensible grind/yield changes, and never truncated. No
setting emitted prose in `grinderSetting` or gave a direction reversal. One none
reply said to return to 6.5 despite already being at 6.5 on the bitter 47-second
shot; low/medium proposed 6.75.

Context reasoning is still imperfect. None twice labeled the same-bean
`bestRecentShot` as a different bean and once treated another bean's session as
a grind change within the current setup. Low made an unsupported different-profile
claim; one medium reply also referenced another bean's session without explaining
that boundary. More thinking helped usable advice, not every context detail.

The capture audit also found that v4's history-heavy prompt lacks the older
blowout and recentAdvice block described by its caption. Those axes cannot be
scored here; the separate current-blowout prompt remains valid. The prior Terra
historical-blowout claim was corrected in its findings entry and AI_ADVISOR.md.

Recommendation: medium for Luna's advisor, low for Terra's. During this replay,
Luna's production setting was still none; the subsequent adoption entry above
records the requested implementation. This small stored-prompt sample is not a production success rate.
Private runs: `blind-luna6-reasoning-r1-20261030` and
`blind-luna6-reasoning-r2-20261030`.

### 2026-10-30 — Terra 6.1 added; low reasoning improves next-shot consistency

OpenAI's third option is `gpt-6.1-terra`, at $1/$5 per million input/output
and $0.05 cached input. Sol ($2/$10) and Luna ($0.10/$0.50) are still priced
correctly; [official pricing](https://developers.openai.com/api/docs/pricing).
Sol stays the default. The request-shape probe passed advisor `none`, translator
`none` + temperature 0.3, and Responses web search at `low`. The reasoning replays
also verified the advisor at `low` and `medium` with the app's 4,096-token cap.

Used stored `captured_v4` prompts (October 7), not a fresh app capture. First,
two responses per six scenarios at `none`: 6/8 tasted responses had a usable
next-shot block; one score-only and one bitter/overshoot response asked questions
instead. An emission run added 4/4; the matched reasoning comparison added 3/4,
for 13/16 overall at none. All four initial untasted responses asked for taste,
and both current blowouts prompted prep/scale checks without regrinding.

Compared none/low/medium once on all six scenarios, then repeated low/medium.
Both reasoning levels emitted 8/8 tasted blocks and respected the taste gate on
4/4 untasted responses. No direction reversals, prose settings, invented tasted
flavors or output truncations. Sour's coarser step was defensible against its high
peak pressure and restricted early flow, as in the prior October evaluation.
Correction from the Luna follow-up: `captured_v4/untasted-run.json` contains no
older blowout despite its caption. The earlier claim that Terra missed that
failed shot was wrong; historical-blowout detection cannot be scored on v4.

Low averaged 55 reasoning tokens (0–162), medium 298 (103–466). Mean input was
13,131 tokens. At equal uncached rates, including all output and reasoning:
none $0.01426, low $0.01453 (+$0.00027, ~2%), medium $0.01581 (+$0.00155, ~11%).
Actual cache hits and writes varied; these are normalized token-rate estimates,
not bills. A fully warm prompt projects $0.00178/$0.00205/$0.00334 respectively.
On tasted scenarios alone, low cost $0.01481 before cache adjustments.

Outcome: advisor requests use low on Terra; translator keeps none. Medium had
no clear quality gain. This is a small sample, not proof of superiority to Sol or
Luna. Judgments were written before opening per-effort usage/key files; blinding
hides effort labels, not the identity of this single model.

Private run directories: `blind-terra61-20261030`,
`emission-terra61-tokens-20261030`, `blind-terra61-reasoning-20261030`, and
`blind-terra61-reasoning-repeat-20261030`. New runs save `usage.json`; older pilot
runs only printed aggregate costs. Do not commit their private prompts/replies.

### 2026-10-07 — Haiku 5.5 admitted as Anthropic's value pick, with low-effort thinking

Recaptured all six scenarios (`captured_v4`): one system-prompt rule had changed since v3.
Blind, two runs each, judged against a bar written before reveal (bitter-47s coarser,
worst-score finer, no grind change on untasted-run or blowout, no invented taste).

With thinking off (`disabled`), Haiku 5.5 gave bad advice on 4 of 12 samples, the Haiku 4.5 shape:
twice it sent a 7.75 gusher back to 10 (coarser, and a grind change on a prep failure), once it
said "coarser, to 6.25" from 6.5, and once it called 7.75 "much coarser" than 10. Sonnet 5.5 failed
once (6.5 → 6.0 on the bitter 47 s shot), Luna none.

With adaptive thinking at `effort: low`, Haiku 5.5 passed 12 of 12 (plus 6 of 6 on a first
diagnostic over the failing scenarios), gave no direction reversals, and emitted usable blocks on
8 of 8 tasted samples. Luna failed one sample in that run, holding 6.5 on the bitter shot. A
two-turn follow-up with the first reply resent as text only returned text (646 thinking tokens),
so dropping thinking blocks from history does not trip the preserved-thinking check.

Cost: ~21K prompt tokens (15.7K cacheable), ~0.9K out with thinking off, about 2K with it.
$0.0034 a shot cold, $0.0016 warm, at $0.10/$0.50 (≤100K-token prompts). OpenRouter serves it
at the same price, with no cache hit: $0.0032 a shot at reasoning `low`, 1.3-1.7K reasoning
tokens. With reasoning unset, one reply ran to the 4,096 cap, so the setting is required. Its
replay arm ran once and is not re-judged. The evaluation spend was about $0.80.

Outcome: `claude-haiku-5-5` becomes the Anthropic value pick, with thinking on for the advisor
only. Test Connection, extraction and translation keep `disabled`, because at 10 tokens
thinking returns no text (#1691).

### 2026-10-06 — OpenRouter: the direct models plus GLM-5.3 Flash and Gemma 4

OpenRouter's model became a fixed list. Probe on `bitter-47s` and `sour` (real
prompts): Luna reasons 1.1–1.4K tokens with no `reasoning` field and 0 at
`none`; Sol, Sonnet 5.5 and 3.8 Flash make reasoning mandatory (`none` is a
400) and reasoned 0–62 tokens at `low`, each returning the block with the grind
direction it gives direct. Costs match the direct providers, except Sonnet:
$0.05 against $0.059, with no cache write.

Cheap candidates, one or two calls each on `bitter-47s` (needs one step
coarser, 6.75) and `worst-score` (score 40 at 8; anchor back to the 6.5 shot
that scored 75 is right, since a score exists). Reasoning off or default: Command A+ went
6.5 → 6.25 calling it coarser; Mistral Small 8 → 8.25 calling it finer, with an
invented taste; DeepSeek V4 Pro jumped two steps; Qwen 3.8 Flash picked 6.0 for
coarser and left its working in the reply; MiniMax M3 reasoned through the
4,096-token cap; Gemma 4 31B was right on both, guessing the taste. Reasoning on ($0.023 for 12 calls): DeepSeek V4.1 Flash called
6.0 coarser; Qwen hit the cap twice; Mistral Small expected a coarser shot to run
longer and jumped two steps; GLM-5.3 Flash (`low`) and Gemma 4 31B were right on
both, each stating a guessed taste. Both were added on the maintainer's call,
with reasoning on: two scenarios is thin evidence, and they cost about what
Luna does.

### 2026-10-05 — system prompt trimmed, contradictions removed

The system prompt went from 46.4K to 40.2K characters: repetition cut, plus
contradictions fixed (a 4-8 ml/s "pour", absolute gusher/choker times, "analyze
without taste" against the ask-first rule, a "directional only" grind rule that
predated `grinderContext`). The block gained `targetWeightG`, and its tag rule
moved to the first sentence after gpt-6-luna fenced one as ```nextShot.
Comparison: the new system prompt from a Mac dry run paired with the same tablet
payloads (compacted), so only the prompt varied. MCP and in-app payloads for one
shot were diffed field by field first: identical apart from `question` and
`shotLabel`.

Sol, Sonnet 5.5 and 3.8 Flash, two runs per prompt: usable blocks (present,
real setting values) went from 17/24 to 24/24 on the tasted scenarios (Sol's
prose `grinderSetting` gone; it now moves one `stepSize`). On `bitter-over` all three now recommend a `targetWeightG`
instead of a prose stop-weight change. On the untasted blowout, blocks went from
3 of 6 samples (two moved the grind) to 0 of 6. Input tokens fell 14% (Sonnet)
to 18% (Sol, Flash) against the indented MCP captures. All three now go one step
coarser on `sour`, citing the 10.2-bar peak and the bean's own acidity.

Value picks on the final prompt, two runs: gpt-6-luna went from 1/4 usable
blocks (intermediate prompt, one ```nextShot fence) to 7/8, right direction
throughout, and asked first on both untasted scenarios. 3.5 Flash-Lite emitted
8/8 but called 6.5 → 6 "coarser" again and moved the grind on the untasted
blowout in both runs. The other cheap models, same set: 3.1 Flash-Lite jumped
three steps finer on the blowout (a prep failure); 2.5 Flash went finer on
`bitter-over` twice and advised on 3 of 4 untasted shots; Haiku 4.5 reversed
direction and went coarser on the gusher; gpt-5.6-luna reversed direction and
advised on the untasted blowout twice.

Outcome: a value pick must give no bad advice. gpt-6-luna is the only one that
passes, so Gemini ships 3.8 Flash alone. Quality order: Sonnet 5.5, gpt-6.1-sol,
3.8 Flash, gpt-6-luna.

### 2026-10-05 — catalogs moved to the newest models

Probe: Sonnet 5.5 rejects `thinking: disabled` (wants `between_tools`); Gemini
3.8 Flash rejects `thinkingLevel: minimal` (`low` reports no thinking);
gpt-6.1-sol rejects `reasoning_effort: none` and, at `low`, any non-default
`temperature`. Haiku 4.5, 3.5 Flash-Lite and gpt-6-luna take the old forms.

Replay (12 models, four tasted tablet scenarios, $1.14): every model emitted
the block on all four except the GPT-6 models on `bitter-over`, where each
recommended a stop-weight change in prose. On grind direction, Sonnet 5.5 and
Sonnet 5 were right on all three scenarios that test it; Haiku 4.5 reversed it
twice (6.0 called "coarser" from 6.5; 11 on a sour shot, anchored on a different
bean). Every Gemini and OpenAI model said one step coarser on `sour`, with a
defensible reason (a 10.2-bar peak above the profile's 6–9 bar band), so that
scenario no longer has one right answer.

Caching: OpenAI hit on 11K of 15.7K tokens, Anthropic on 17.8K of 24.4K after
the first call, Gemini Flash-Lite on 8–12K; Gemini 2.5 and 3.8 Flash never hit.

Outcome: OpenAI gpt-6.1-sol + gpt-6-luna, Anthropic Sonnet 5.5 only, Gemini 3.8
Flash + 3.5 Flash-Lite, superseded by the trimmed-prompt entry above. A first run against a dev
machine's database was void: the scenario ids pointed at other, untasted shots.

### 2026-07-30 — GPT-5.6 family evaluated, Terra adopted as default

Models: `gpt-5.6-terra`, `gpt-5.6-luna`, `gpt-5.4`, `gpt-5.4-mini`. Six
scenarios, single runs. Total spend $1.03.

**The generational split.** On `untasted-run`, both 5.6 models flagged the
64.3 g / 8.5 s blowout in recent history and refused to dial on it; **both 5.4
models missed it entirely**. `gpt-5.4-mini` additionally invented a grind trend
that did not exist — plausibly the blowout contaminating its average without it
ever registering the blowout as an event. That is consistent with mini's
documented weakness in multi-shot trend reasoning from the
`fix-multishot-advice-tracking` A/B, though not the same symptom: there it
*missed* a real trend (false negative), here it *invented* one (false
positive).

**`reasoning_effort` — `"none"` is correct, `"low"` is a regression.** A
4-model × 4-scenario × 2-effort matrix emitted the `nextShot` block **16/16 at
`"none"` and lost 5 of 16 at `"low"`**.

This refuted the reason the code gave for the setting. The comment claimed
`"none"` stops hidden reasoning tokens from overrunning the output cap and
truncating the trailing block. **No run hit `finish_reason: "length"`** —
reasoning ran 208–1245 tokens against a 4096 cap, and the models finished
cleanly and simply chose to omit the block while reasoning. Right setting,
wrong mechanism, asserted as fact. `src/ai/aiprovider.cpp` now says so.

**Models write prose into `grinderSetting`.** `gpt-5.6-terra` emitted
`"a touch coarser than 9"` on the `sour` scenario at both efforts;
`gpt-5.4-mini` emitted `"slightly coarser than 9"`. Luna and `gpt-5.4` never
did. Unguarded this is silent corruption: prose matches no real setting and
parses as no number, so `computeAdherence()` scored it `"ignored"` and told the
next turn the user disregarded advice they may have followed exactly. Fixed by
`classifyGrinderRecommendation()` in `src/ai/dialing_blocks.cpp`.

**Outcome.** Terra became the default: cheaper than `gpt-5.4` on both axes and
a generation newer. Luna measured at least as well as Terra at 10× less and is
the cheap opt-in; it is not the default only because the sample is too thin to
promote the smallest tier against prior evidence that small tiers are weak —
which is precisely the belief this run undermined. **Open question worth
settling: 8–10 more rated scenarios, 2 runs each, Luna vs Terra only.**

`gpt-5.6-sol` was **not evaluated for advice quality** — its API parameter
acceptance *was* verified live (it takes `reasoning_effort: "none"` and
`web_search` at both `"none"` and `"low"`, same as Terra and Luna), but it was
never run through the scenarios. `gpt-5.4-nano` is now strictly dominated by
Luna (same input price, higher output price, older generation).

**Method bugs to avoid repeating**, both found in review of this run rather
than during it:

- The blind was broken by printing per-call cost — the cheapest response is
  unmistakable. Fixed; do not add cost printing back to `blind` mode.
- The emission audit used a first-fence regex instead of the app's last-fence
  acceptance rule (see "Replay byte-for-byte"). The 16/16 and 5-of-16 counts
  above were **re-derived from the saved responses under the corrected rule and
  did not change** — every rejection was a genuinely absent block, not a
  malformed one — but the harness would have mismeasured a model that wrapped
  its block in prose.
