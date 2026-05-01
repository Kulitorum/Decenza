# Tasks: Unify `currentBean` to a single shot-derived shape

## Implementation

- [x] 1. Add `McpDialingBlocks::buildCurrentBeanBlock(const ShotProjection& shot)` to `src/mcp/mcptools_dialing_blocks.{h,cpp}` — pure function, builds the canonical JSON from the shot's bean / grinder / dose / roastDate fields; composes `beanFreshness` via the existing `McpDialingHelpers::buildBeanFreshness` helper using the shot's `roastDate`.
- [x] 2. Switch `src/mcp/mcptools_dialing.cpp` to call the new helper. Remove the `if (settings) { … McpDialingHelpers::buildCurrentBean(in) … }` block and the now-unused `Settings::dye()` reads for the bean payload.
- [x] 3. Remove `McpDialingHelpers::CurrentBeanInputs` and `McpDialingHelpers::buildCurrentBean` from `src/mcp/mcptools_dialing_helpers.h` (no callers remain). Keep `buildBeanFreshness`.
- [x] 4. Update `src/ai/shotsummarizer.cpp::buildCurrentBeanBlock(const ShotSummary&)` to delegate to `McpDialingBlocks::buildCurrentBeanBlock` by mapping the summary fields into a temporary `ShotProjection`-shaped input. Remove the `inferredFields` / `inferredFromShotId` branch.
- [x] 5. Remove `inferredFields` and `inferredFromShotId` from `src/ai/shotsummarizer.h::ShotSummary` (now unused).
- [x] 6. Update the espresso `shotAnalysisSystemPrompt` builder in `src/ai/shotsummarizer.cpp` to drop the `inferredFields` clause from the "How to read structured fields" section, and reword the `currentBean` description to "the setup that produced the resolved shot."
- [x] 7. Add a `currentBean equivalence` test in `tests/aimanager_tests/tst_aimanager.cpp`: build a fixed `ShotProjection` (id, bean fields, grinder fields, dose, roastDate), set a deliberately divergent live DYE state, drive both surfaces (the MCP `dialing_get_context` path and `ShotSummarizer::buildUserPromptObject(summarizeFromHistory(shot))`), assert the resulting `currentBean` JSON objects are equal under `==`.
- [x] 8. Run `openspec validate unify-current-bean-shape --strict --no-interactive` and resolve any issues.
- [x] 9. Build via Qt Creator (clean) and confirm the existing dialing-context test in `tst_aimanager` passes.
