# Change: Grinder calibration block — derive user's RGS from shot history

## Why

The in-app advisor and MCP advisor know the Universal Grind Setting (UGS) ordering for common profiles, but UGS is a relative scale with no unit. Translating direction ("D-Flow is finer than Adaptive v2") into an actionable number requires a per-user conversion key — the factor that maps UGS distance to actual grinder clicks on the user's specific grinder. Without it, the AI either stays vague ("go a bit coarser") or guesses a click count from UGS arithmetic, a known hallucination failure mode.

The UGS calculator at videoblurb.com/UGS derives this conversion key from two anchor shots: the user's Cremina setting (UGS 0) and their Rao Allongé setting (UGS 8). The app has an equivalent data source: shot history. If the user has pulled two or more profiles with known UGS values, the app can compute the same `conversionKey = (coarseSetting − fineSetting) / (coarseUGS − fineUGS)` and derive a Relative Grind Setting (RGS) for every profile in the knowledge base.

The result is a `grinderCalibration` block injected into both the `dialing_get_context` payload and the in-app advisor's user prompt. Within the user's calibrated UGS range the RGS values are directly grounded in real shots; outside it they are extrapolations, clearly flagged so the AI can qualify them as rough starting estimates rather than targets.

## What Changes

- Parse `UGS:` lines in `profile_knowledge.md` into structured numeric fields on the `ProfileKnowledge` struct (`double ugs`, `bool ugsInferred`). Strip the `~` prefix (inferred marker) and `(…)` annotations when parsing the value; set `ugsInferred` for lines that carry those markers. Skip-Catalog sections have no UGS and will remain `NaN`.

- Add `buildGrinderCalibrationBlock(db, grinderModel, grinderBurrs, beverageType, resolvedShotId)` to `src/ai/dialing_blocks.{h,cpp}`.
  - SQL: query all shots on the same grinder model + burrs (no time window — the conversion key is a physical property of the grinder+burrs pair), espresso only; compute per-profile median numeric setting.
  - For each profle with a median, look up its canonical (non-inferred) UGS from the KB.
  - Require ≥ 2 profiles with qualifying canonical UGS values — otherwise return empty (no block).
  - Pick the pair with the widest UGS span as anchors (fine and coarse).
  - Compute `conversionKey` and `calibratedUgsRange` from the anchor pair.
  - For every profile in the KB with a known UGS (canonical or inferred), compute RGS and tag the source: `"history"` (anchor or other history profile), `"derived"` (within calibrated range), or `"extrapolated"` (outside calibrated range). Inferred UGS profiles are always `"extrapolated"`.
  - Return empty when `grinderModel` is empty or `beverageType` is filter/pourover.

- Wire into `dialing_get_context` (MCP): call alongside `buildGrinderContextBlock`; include as `grinderCalibration` in the response.

- Wire into `AIManager::analyzeShotWithMetadata` (in-app advisor): call in the background-thread DB closure; merge into the user-prompt JSON envelope on the main-thread continuation.

- Update `resources/ai/claude_agent.md`: describe the block, and add the rule that concrete grinder numbers for profile switches come from `grinderCalibration.profiles[].rgs`, not from UGS arithmetic. For `"extrapolated"` entries outside `calibratedUgsRange`, the AI must qualify the number as a starting estimate ("you'll likely need to adjust from there").

- Update `docs/CLAUDE_MD/MCP_SERVER.md`.

## Impact

- Affected specs: `dialing-context-payload` — gains `grinderCalibration` as a documented block.
- Affected code:
  - `src/ai/shotsummarizer.h` / `shotsummarizer.cpp` — add `ugs` / `ugsInferred` to `ProfileKnowledge`; parse in `commitSection()`; expose `static double ugsForKbId(const QString& kbId)`
  - `src/ai/dialing_blocks.h` / `dialing_blocks.cpp` — new builder
  - `src/mcp/mcptools_dialing.cpp` — wire new block into response
  - `src/ai/aimanager.cpp` — wire new block into advisor enrichment path
  - `resources/ai/claude_agent.md` — add usage guidance
  - `docs/CLAUDE_MD/MCP_SERVER.md` — update tool description
