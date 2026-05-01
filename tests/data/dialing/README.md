# Dialing-context regression baselines

Pre-change conversation captures used as regression targets for the
`optimize-dialing-context-payload` OpenSpec change (see
`openspec/changes/optimize-dialing-context-payload/`).

These files are real AI-advisor conversations on a Niche Zero + Northbound
Coffee Roasters Spring Tour 2026 #2 setup, captured **before** the
canonical-source separation + dedup work landed. They preserve the
pre-change shape of the prose pipeline (`AIManager::buildRecentShotContext`
calling `ShotSummarizer::buildUserPrompt` per historical shot) so PR 2 of
the optimization change has a stable diff target.

## Files

| File | Profile | Shot count | Captured | Notes |
|---|---|---|---|---|
| `northbound_80s_espresso_pre_change_baseline.json` | 80's Espresso | 4 | 2026-04-30 | Intentional-temperature-stepping profile; pre-#1018 (12 false "Temperature instability" lines) |
| `northbound_dflow_q_pre_change_baseline.json` | D-Flow / Q | 4 | 2026-04-25 | Lever-style flow profile; pressure-peak target 6–9 bar |

## Schema

Each file is the AI conversation envelope as serialized by Decenza's AI advisor:

```jsonc
{
  "metadata": { "beanBrand", "beanType", "messageCount", "profileName", "timestamp" },
  "systemPrompt": "...",
  "messages": [ { "role": "user|assistant", "content": "..." }, ... ]
}
```

`messages[2]` of each file is the first context-rich user turn — the
4-shot history block that `AIManager::buildRecentShotContext` produced.
This is the primary regression target.

## Measured baselines (pre-change)

From `messages[2]` of `northbound_80s_espresso_pre_change_baseline.json`:

- Total: **14,652 chars / ~3,663 tokens**
- "Profile intent" repetitions: 4 (~1,000 chars each)
- "Profile Recipe" repetitions: 4 (~210 chars each)
- "Detector Observations" legend repetitions: 2 (~430 chars each)
- "31 days since roast" occurrences: 4
- "Temperature instability" line firings: 12 (all bogus per-#1018; this conversation predates the fix)

## Regression check

After PR 2 lands, the same 4-shot history re-rendered through the new
pipeline should produce a turn-2 user message with:

- Total: ≤ ~2,300 chars (~575 tokens) — **≥84% reduction**, or ≤ ~3,000
  chars if some redundancy is intentionally preserved (final number TBD
  by implementation).
- "Profile intent" occurrences: 1
- "Profile Recipe" occurrences: 1
- "Detector Observations" legend occurrences: 0 (moved to system prompt)
- "days since roast" occurrences across the WHOLE conversation envelope: 0
- "2026-03-30" occurrences across the WHOLE envelope: ≤ 1 (only in
  `currentBean.beanFreshness.roastDate` — but note this conversation
  predates the JSON path; if the prose path doesn't carry the date, 0 is
  the expected count)
- "Temperature instability" occurrences: 0 (already fixed by merged #1018)

The `Profile intent` paragraph and per-shot phase data SHOULD still be
present (the AI uses both — see `design.md` empirical anchors section).

## Maintenance policy

- Do NOT regenerate these files when the prose pipeline changes. They are
  the "before" snapshot; their value is precisely that they're frozen.
- DO add a parallel `*_post_change_baseline.json` capture after PR 2 lands
  so future spec changes can diff against the new baseline.
- These files contain real (anonymized — bean and grinder are public
  setup details, not user PII) conversations. No additional scrubbing
  required.
