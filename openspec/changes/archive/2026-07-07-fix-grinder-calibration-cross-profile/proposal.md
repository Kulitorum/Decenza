## Why

The `grinderCalibration` block (`DialingBlocks::buildGrinderCalibrationBlock`) hands the AI advisor confidently wrong cross-profile grind numbers. Issue [#1223](https://github.com/Kulitorum/Decenza/issues/1223): a user dialed in on D-Flow / Q was told to grind *finer* for TurboTurbo when it should be much coarser — both the in-app Gemini advisor and external Claude/MCP produced the same bad advice because they were fed the same bad data.

Offline replay against a real 928-shot database reproduced the failure exactly and isolated three root causes:

1. **Confounded slope.** The conversion key is fit from pooled all-coffee, all-time medians. The same profile is dialed anywhere from 5 to 12 depending on the *coffee* (origin, roast, age), so the pooled median averages across unrelated baselines. The result was a **negative** conversion key (−2.4 steps/UGS) — wrong sign for the grinder — yielding physically impossible recommendations (TurboTurbo rgs = −2.4).
2. **No coffee anchor.** There is no per-coffee intercept. The block emits one all-time number per profile regardless of which bean the user is actually dialing now.
3. **Unbounded extrapolation presented as fact.** Every usable data anchor spans ≤1.5 UGS, yet the block extrapolates 4–8× beyond that to UGS 6–8 profiles and labels the output identically to measured data. The literature (Al-Shemmeri 2023; Gagne 2020) says the relationship is nonlinear/quadratic precisely in that range.

The within-coffee signal, once the coffee confounder is removed, is real and consistent (~1.4–1.5 steps/UGS across independent beans) — matching the original UGS spec's decomposition (a fixed grinder "span" per grinder+burrs, re-anchored per coffee). The fix restores that decomposition and, critically, **stops the AI from using UGS in ways it was never meant to be used** — UGS is a relative/directional scale, not a license to do click arithmetic across the full range from short-span data.

## What Changes

**Phase 1 — Stop the bad advice (the 80% fix, ships first).** Correct `buildGrinderCalibrationBlock` and how its output is presented to the model:

- **Coffee-confounder removal**: derive the conversion key from *within-coffee* paired slopes (same bean, different profiles), not pooled all-coffee medians.
- **Quality filter**: only "dialed-in" shots anchor the calibration (rated ≥ threshold, OR landed within 10% of stop-at-weight target, OR has a refractometer reading) — not merely "≥15g and no badge".
- **Per-coffee anchor**: the numeric intercept is the user's most recent dialed-in shot on the *current coffee* on a known-UGS profile; projection uses the (coffee-independent) conversion key.
- **Extrapolation cap (the core #1223 fix)**: the block SHALL NOT emit a numeric RGS more than a bounded distance (~1.5–2 UGS) beyond the user's widest *validated* anchor pair. Beyond the cap → **directional guidance only** ("much coarser, start well up, pull and adjust"), never a number.
- **Honest unavailable state**: when no validated within-coffee anchor exists (the common single-style user), return qualitative-only — never a fabricated number.
- **AI-usage constraints**: the rendered prompt SHALL explicitly forbid the model from translating UGS distances into click counts beyond the validated range, and SHALL mark out-of-range profiles as directional-only. UGS is presented as relative ordering, not an absolute dial. **BREAKING**: `source: "extrapolated"` numeric `rgs` values are removed/replaced with a directional marker; payload consumers must handle a non-numeric out-of-range entry.

**Phase 2 — Do it properly (opt-in, validation-gated).** SPLIT OUT 2026-07-07 to its own change, `add-grinder-ugs-calibration`, parked pending a go/no-go decision. The deliberate two-distant-anchor calibration from the original UGS spec: user pulls two anchor profiles once per grinder+burrs, the system stores a Conversion Key, and the dialing block consumes the stored key for full-range numeric guidance. The `grinder-ugs-calibration` delta spec moved with the split; this change now covers Phase 1 only.

## Capabilities

### New Capabilities
<!-- none — `grinder-ugs-calibration` moved to add-grinder-ugs-calibration with the Phase 2 split -->

### Modified Capabilities
- `dialing-context-payload`: the `grinderCalibration` requirement is rewritten — within-coffee paired conversion key, dialed-in quality filter, per-coffee anchor, mandatory extrapolation cap, qualitative/unavailable fallback, and a `confidence` + `usageConstraint` field. Consumes a Phase 2 stored Conversion Key when present.
- `advisor-user-prompt`: the enriched-prompt requirement for the calibration section gains explicit AI-usage constraints — UGS is directional outside the validated range, extrapolated numbers are never presented as authoritative, and the model is instructed to give direction + "pull a reference shot" rather than a number when out of range.

## Impact

- **Code**: `src/ai/dialing_blocks.cpp` (`buildGrinderCalibrationBlock` rewrite + helpers), `src/ai/aimanager.cpp` (calibration prompt rendering), `src/mcp/mcptools_dialing.cpp` (`dialing_get_grinder_calibration` unavailable/qualitative responses). Phase 2: new calibration storage (a `SettingsGrinder` domain sub-object or dedicated store), a capture workflow surface, MCP tool.
- **Payload schema**: `grinderCalibration.profiles[]` out-of-range entries change shape (BREAKING for consumers that assume numeric `rgs` everywhere); new `confidence` / `usageConstraint` fields; new qualitative/unavailable response variants.
- **Prompts**: in-app advisor and `dialing_get_context` calibration sections re-worded with usage constraints; KB `cross-profile-grind-ordering` prose tightened to match (data-only touch-up, no requirement change).
- **Tests**: new `shot_eval`/unit regression cases asserting correct conversion-key sign for the grinder direction, the extrapolation cap, and the #1223 scenario (anchored low-UGS, asked high-UGS → directional, no number). Issue #1223 user database requested as a second regression fixture (validation gate for Phase 2).
- **No BLE / machine-control impact.** Advisory/data-path only.
- **Pre-existing base-spec drift (out of scope, flagged for the implementer).** The existing `openspec/specs/dialing-context-payload/spec.md` still describes a `profile_knowledge.md` markdown parser / `loadProfileKnowledge()`; the real KB has since moved to `resources/ai/profile_knowledge.json` (JSON; `shotsummarizer_kb.cpp` notes it replaced the markdown reader). This change does NOT touch those stale requirements and does not fix them — but an implementer reading the delta against the base spec should know the base spec's KB-format requirements are already drifted from reality. The `cross-profile-grind-ordering` ID this change touches does exist in the current JSON KB, so task 3.4 is correct as written.
