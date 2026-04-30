# Change: Share more between `ShotSummarizer::summarize()` and `summarizeFromHistory()` via a typed-input adapter

## Why

`ShotSummarizer::summarize(ShotDataModel*, profile, metadata, ...)` (live shot) and `ShotSummarizer::summarizeFromHistory(QVariantMap)` (saved shot) end with structurally identical work — populate `summary.summaryLines` via `analyzeShot`, then conditionally call `markPerPhaseTempInstability` under the same gate. They diverge only on input adapters: live reads `pressureData`, `flowData`, `cumulativeWeightData`, etc. from the `ShotDataModel*` accessor methods; history reads them from the `QVariantMap` via `variantListToPoints`.

After change `dedup-phase-summary-builder` (G) lands, the per-phase metric loop is shared. After change `expose-pour-window-on-analysis-result` (H) lands, the pour-window computation is shared. What's left in each function is:

1. Adapter-specific input extraction (live vs. history)
2. ~30 lines of identical post-extraction work — call `analyzeShot`, populate `summary.summaryLines`, gate `markPerPhaseTempInstability`

This change extracts that identical post-extraction work into a private static `runShotAnalysisAndPopulate(summary, curves..., markers, profile/metadata, analysisFlags...)` helper. The two callers reduce to "extract inputs from my source, call the helper, return summary."

## What Changes

- **ADD** a private static `ShotSummarizer::runShotAnalysisAndPopulate(...)` taking pre-extracted typed inputs (the curves, markers, beverage type, `analysisFlags`, frame info, target/final weights, ...) and a `ShotSummary&` to populate. The helper:
  1. Calls `ShotAnalysis::analyzeShot` (or the pre-computed cache lookup, if applicable)
  2. Sets `summary.summaryLines` from the result
  3. Sets `summary.pourTruncatedDetected` from `detectors.pourTruncated`
  4. Optionally calls `markPerPhaseTempInstability` under the existing gate
- **MODIFY** `summarize()` to extract its inputs from `ShotDataModel*` and call the helper.
- **MODIFY** `summarizeFromHistory()` to do the same (its `summaryLines` fast-path optimization from change `dedup-ai-advisor-history-path` (B) is preserved — the helper's first action checks for pre-computed lines).
- **NO change** to `analyzeShot`, `DetectorResults`, the prose lines, MCP, or any consumer.

## Depends on

- Change `dedup-phase-summary-builder` (G) should land first so the helper doesn't carry the per-marker phase-builder loop too.
- Change `expose-pour-window-on-analysis-result` (H) should land first so the helper's gate reads the pour window from `DetectorResults` instead of via the deleted `computePourWindow`.

If those land first, this change is a clean ~30-line extraction. If they don't, this change can still land but the helper would carry more code than necessary.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: detector-orchestration glue lives in exactly one helper).
- Affected code: `src/ai/shotsummarizer.{h,cpp}`. New private helper; both callers shrink by ~30 lines each.
- User-visible behavior: identical AI prompts, identical MCP output, identical badge state.
- Performance: identical.
- Risk: low–medium. The helper consolidates what's already duplicated; the test surface is the existing `tst_shotsummarizer` cases, plus a regression test asserting both paths produce equivalent `ShotSummary` for the same shot data.
