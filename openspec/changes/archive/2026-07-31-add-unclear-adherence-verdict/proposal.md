# Add an `"unclear"` adherence verdict for unscoreable recommendations

## Why

`computeAdherence()` reports whether the user followed the advisor's last
recommendation, and that verdict is fed back to the model in the next turn's
`recentAdvice` block. The three-value enum assumed every recommendation could be
checked. Live replay against real prompts (2026-07-30, see
`tools/ai_model_eval/`) showed that assumption is false: models write prose into
`structuredNext.grinderSetting` — GPT-5.6 Terra emitted `"a touch coarser than
9"`, GPT-5.4 mini `"slightly coarser than 9"` — and the schema's declared types
are not honoured either.

An unscoreable recommendation has no honest answer among the three values, and
both wrong answers are actively harmful:

- Reporting `"ignored"` tells the model the user disregarded advice they may have
  followed exactly.
- Reporting `"followed"` is worse — the system prompt reads it as *the experiment
  ran*, and instructs the model to revise direction or commit harder on the
  strength of a shot that may have changed nothing.

The first attempt at this fix produced exactly the second failure, by skipping
the field and falling through to the ranges-only `"followed"` default.

## What changes

- `adherence` gains a fourth value, `"unclear"`, taking precedence over the
  others whenever any recommended field is unscoreable.
- Every axis (`grinderSetting`, `rpm`, `doseG`, `profileTitle`) is classified
  uniformly before scoring, rather than only `grinderSetting`. `rpm` previously
  failed *open*: `QJsonValue::toInt()` returns `0` for a JSON string, and the
  matcher treated `<= 0` as a free match.
- The comparator accepts every notation the syntax gate admits — compound
  spacing (`"1 + 4"` vs `"1+4"`) and recorded annotations (`"23.5 1400rpm"` vs
  `"23.5"`) no longer decide adherence.
- The system prompt teaches the fourth value.

## Impact

- Affected specs: `advisor-user-prompt`
- Affected code: `src/ai/dialing_blocks.cpp`, `src/ai/shotsummarizer.cpp`,
  `src/core/grinderaliases.h`
- No migration: `adherence` is computed per turn from stored shots, never
  persisted, so old conversations simply get the new verdict on next render.
- No consumer switches on the value — `AIManager::renderRecentAdviceEntry` and
  the MCP path both pass the string through — so a fourth value cannot break a
  stale three-way branch.
