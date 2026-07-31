# AI advisor model evaluation

Two scripts, answering two different questions. Both exist because vendor
benchmarks say nothing useful about espresso dial-in advice, and because the
method is the expensive part — not the API spend, which is around $1 for a full
comparison.

| Script | Question | Providers |
|---|---|---|
| `replay.py` | Does this model give good dial-in advice, and does it emit a usable `nextShot` block? | **OpenAI only** — it posts to `chat/completions` and has no provider branching |
| `probe_request_shape.py` | Do the thinking/reasoning knobs we send actually work on this model? | OpenAI, Anthropic, Gemini |

The split matters: `replay.py` decides *which* model to pick, and it only knows
how to talk to OpenAI. Adding an Anthropic or Gemini model to a catalog means
running `probe_request_shape.py` (its request-shape INVARIANTs are the thing
that breaks silently), and judging advice quality some other way until
`replay.py` grows provider branching.

**Read this before swapping a model in any catalog.** The rationale for the
current catalogs lives in `docs/CLAUDE_MD/AI_ADVISOR.md`; this directory is how
that rationale gets produced.

## `probe_request_shape.py` — the invariant check

```bash
python3 probe_request_shape.py
```

Verifies, live, three things asserted in code comments that were never checked
until 2026-07-30:

- Anthropic accepts `thinking: {type: "disabled"}` **and still returns a `text`
  block** — a thinking-only reply with no text is the #1691 symptom, so the
  status code alone proves nothing.
- Gemini's knob is both accepted **and effective** — it asserts
  `usageMetadata.thoughtsTokenCount == 0`, because a silently ignored knob still
  bills thinking at the output rate. Note Gemini's legal `thinking_level` values
  **vary by model**, so a new 3.x entry must be probed, not assumed.
- OpenAI accepts the translator's body shape (`temperature` alongside
  `reasoning_effort`), which reasoning models have rejected in the past.

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
run. The system prompt alone is ~44K characters and changes with the profile,
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

`replay.py` mirrors the request shape in `OpenAIProvider::analyze()` — same
`max_completion_tokens`, same `reasoning_effort`. If that shape changes in
`src/ai/aiprovider.cpp`, update the constants at the top of `replay.py`.

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

Write your judgments down *before* `replay.py reveal`.

### 5. Be honest about sample size

Six scenarios of single runs is enough to reproduce a known failure mode. It is
not enough to separate two models that both pass. Say which you have.

## Usage

```bash
python3 replay.py capture-help
python3 replay.py emission --models gpt-5.6-terra,gpt-5.6-luna --efforts none,low
python3 replay.py blind    --models gpt-5.6-terra,gpt-5.6-luna
python3 replay.py reveal   --run blind
```

Key comes from `$OPENAI_API_KEY`, else Decenza's own configured key on macOS.

Prices in `replay.py` **rot**. Verify at
<https://developers.openai.com/api/docs/pricing>. Third-party pricing pages were
checked on 2026-07-30 and found wrong — one listed Terra at $2.50/$15 against an
actual $2.00/$12.

## Findings log

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
