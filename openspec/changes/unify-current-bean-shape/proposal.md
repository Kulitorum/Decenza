# Change: Unify `currentBean` to a single shot-derived shape

## Why

For the same resolved shot, `dialing_get_context.currentBean` and the in-app advisor's user-prompt `currentBean` ship different shapes and disagree on values: dose, bean type, roast level, and the presence of `inferredFields` / `beanFreshness` all differ because one path reads from `Settings::dye()` (the live machine setup) while the other reads from the shot's saved metadata. The system prompt's "How to read structured fields" section teaches a single meaning for `currentBean`; the LLM today sees two. This is the same class of bug PR #1034 retired for `currentProfile` vs `profile`. See issue #1043.

## What Changes

- **BREAKING** (LLM contract): `dialing_get_context.currentBean` now describes "the setup that produced the resolved shot," not "the live DYE setup with shot fallback." All bean / grinder / dose / roastDate fields are read from the resolved shot's saved metadata.
- The `inferredFromShotId`, `inferredFields`, and (already-omitted-per-#977) `inferredNote` mechanism is **removed** from `currentBean`. Once the shot is the canonical source, nothing is "inferred" — every field originates from one place. Fields the shot did not record render as empty strings (or as omissions for `doseWeightG <= 0`).
- A shared `McpDialingBlocks::buildCurrentBeanBlock(const CurrentBeanBlockInputs&)` helper produces the JSON for both surfaces, so they cannot drift again. Inputs are a small adapter struct so the in-app advisor's `ShotSummary` path can call it without round-tripping through the heavyweight `ShotProjection` type.
- `currentBean.beanFreshness` is built off the resolved shot's `roastDate` on both surfaces (it was DYE-sourced in the MCP path; both now agree).
- The `shotAnalysisSystemPrompt` "How to read structured fields" section drops the `inferredFields` clause (the field no longer ships) and tightens the `currentBean` description to "the setup that produced the resolved shot."
- A new equivalence test in `tst_aimanager` drives both surfaces with a fixed shot + a deliberately divergent live DYE state and asserts the resulting `currentBean` JSON is equal under `==`.

Live-DYE-vs-shot drift (the user changed DYE since the shot was pulled) is no longer surfaced in `currentBean` — that is a deliberate scope reduction. If a future change needs it, a separate `liveSetup` block can be added without renegotiating `currentBean`'s meaning.

This deliberately supersedes the earlier guard from commit 704bc4e ("fall back to last shot for blank currentBean grinder/dose"), which forbade inferring bean fields (`brand` / `type` / `roastLevel`) from a shot on the grounds that "beans rotate per hopper." That guard was correct under the *old* semantics, where `currentBean` meant "the live setup" and a shot value was a fallback that could be stale relative to live DYE. Under the new semantics, `currentBean` means "the setup that produced the resolved shot" — the shot's bean fields *are* that shot's beans by definition, so within the shot's own context they cannot be stale. Reading bean fields from the shot is no longer inference; it is the canonical source. The "beans rotate per hopper" concern still matters when the user has changed beans between the resolved shot and the current dial-in iteration, but that is now visible via the absence of any value claiming to be "live": the AI sees only the shot's setup, has no contradicting live-DYE block to harmonize with, and falls back to asking the user about hopper changes.

## Impact

- **Affected specs**: `dialing-context-payload` (modifies the static-framing-strings requirement and the beanFreshness empty-roastDate scenario; adds a new requirement pinning the canonical source).
- **Affected code**:
  - `src/mcp/mcptools_dialing_blocks.{h,cpp}` — new `buildCurrentBeanBlock(CurrentBeanBlockInputs)` helper, defined inline in the header so test binaries that link only `shotsummarizer.cpp` do not pull in the DB-dependent block builders from the `.cpp`.
  - `src/mcp/mcptools_dialing.cpp` — drops the `Settings::dye()` reads + `McpDialingHelpers::buildCurrentBean` + DYE-sourced `buildBeanFreshness` and calls the new shared helper instead.
  - `src/mcp/mcptools_dialing_helpers.h` — `buildCurrentBean` and `CurrentBeanInputs` are removed (no callers remain).
  - `src/ai/shotsummarizer.cpp` — `buildCurrentBeanBlock(ShotSummary)` becomes a thin adapter that maps summary fields onto `CurrentBeanBlockInputs` and delegates; the `inferredFields` branch is removed.
  - `src/ai/shotsummarizer.h` — `ShotSummary::inferredFields` and `inferredFromShotId` are removed (unused after this change).
  - `src/ai/shotsummarizer.cpp` system-prompt builder — drops the `inferredFields` reference from the "How to read structured fields" section.
  - `tests/aimanager_tests/tst_aimanager.cpp` — new equivalence test.
- **Affected callers / consumers**: every LLM-facing consumer of `dialing_get_context` and the in-app advisor user prompt. The system prompt update lands in the same change so the prompt and payload move together.
