## Context

`ShotAnalysis::analyzeShot` is the single cascade that produces every Shot Summary observation: five profile-agnostic detectors → `DetectorResults` → prose `summaryLines` (rendered by `ShotAnalysisDialog.qml`) + four boolean badge columns (`decenza::deriveBadgesFromAnalysis`). The pipeline spec mandates the cascade live in exactly one place, run exactly once per shot, and recompute identically on save / load / detail. Today none of it is profile-aware: a clean shot that violated its profile's pressure intent passes silently.

`capture-dialin-coaching-guidance` (which owns the full `ProfileExpectation` seam; it superseded and archived `split-ugs-pressure-variants`) makes the profile's intent machine-readable: `ProfileExpectation` carries the centred envelope, the two-sided `GrindTooCoarse:` / `GrindTooFine:` symptom arms, a `PressureShape:` qualifier, preinfusion-dripping bounds, and a structured per-profile `Suppress:` catalogue — every value citation-bound, no invention. That change deliberately consumes it only as advisor *prose*; the structured struct is left code-unconsumed for exactly this follow-up. This change adds the deterministic consumer that `capture-dialin-coaching-guidance` (non-goals) explicitly defers.

The governing hazard, established across this work: a Shot Summary line is closer to a verdict than advisor prose — there is no conversational channel for the user to push back, and the non-AI audience that benefits most has the least context. A confident false "your good shot is bad" is the #1155 trust-erosion failure at its highest-visibility surface. The value is highest and the false-positive cost is highest on the same surface.

## Goals / Non-Goals

**Goals:**
- One profile-aware deterministic detector inside the existing one-place cascade, consuming `ProfileExpectation`.
- A single soft, taste-deferring, suppression-gated `summaryLines` entry — no badge column, no chip, no verdict.
- Hard AND-gating and a deliberate false-negative bias so it under-fires.
- Backwards-compatible plumbing: absent expectation → exact current behavior for every existing/legacy/imported/test caller.
- An objective shadow-validation gate (regression corpus false-positive rate) before the line is ever shown.

**Non-Goals:**
- No new boolean badge / quality chip; no change to the four-badge projection.
- No LLM involvement; the advisor incidentally reads the same `summaryLines` but is not required and is not a consumer this change designs for.
- No #1159 history trust-gating (a separate later consumer of the same struct).
- No persistence beyond the existing recompute-on-load line contract; no Visualizer write; no synthesized score.
- Not authoring `ProfileExpectation` data or grammar — owned upstream by `capture-dialin-coaching-guidance`.

## Decisions

**D1 — Soft `summaryLines` entry, never a badge column.** Badges read as binary verdicts and persist as columns; a profile-deviation judgment is inherently softer and confounder-laden. It emits one `[observation]`/`[caution]` line with taste-deferring wording, joining the prose list the dialog already renders. *Alternative:* a 6th boolean badge — rejected: a red chip on a deliberately-pulled shot is precisely the #1155 failure, and badges carry more authority than the signal warrants.

**D2 — Lives in the single `analyzeShot` cascade.** The spec requires the cascade in one place and identical recompute on save/load/detail. Putting the detector anywhere else would fork the cascade and break recompute parity. Running inside `analyzeShot` gives save-time, recompute-on-load, and detail-load the line for free and consistently. *Alternative:* a separate post-pass in `ShotSummarizer` — rejected: violates the one-place-cascade requirement and creates a drift surface.

**D3 — `ProfileExpectation` plumbed as an optional `analyzeShot` parameter.** Exact precedent: `expectedFrameCount` was added the same way (default = unknown, behavior preserved). Absent expectation → the detector no-ops, so legacy shots, imported shots, direct test callers, and the live in-progress path are byte-identical to today. No DB migration; the expectation is resolved from the shot's profile by the save/load callers that already resolve profile KB. *Alternative:* give `ShotAnalysis` a KB handle — rejected: widens its dependency surface; the optional-parameter pattern is already blessed by the spec.

**D4 — Hard AND-gate; bias to false negatives.** The line fires only if ALL hold: a `ProfileExpectation` exists for the resolved profile; the observed values match a `GrindTooCoarse:`/`GrindTooFine:` signature (or clearly depart from `PressureShape:`) with margin; the cascade did NOT already fire pour-truncated/channeling; the profile's `Suppress:` catalogue does not cover the behavior; the profile is not grind-tolerant-by-design; bean freshness is not unknown/very-fresh. Anything ambiguous → silent. Rationale: in a no-dialogue surface a false positive costs far more than a missed flag, so it must under-fire by construction. *Alternative:* symmetric sensitivity tuned for recall — rejected: recall is the wrong objective for a verdict-ish surface with the #1159/#1155 history.

**D5 — Shadow-validation gate before the line surfaces.** A dry-run path computes the line over the `tests/data/shots/` regression corpus via the `shot_eval` harness without rendering it; the false-positive rate against known-good shots is measured and the firing margin tuned until conservative, before the line is enabled for users. This is an objective launch gate that does not depend on subjective prose-stage feedback. *Alternative:* ship behind a runtime setting — rejected: contradicts the project's prefer-fewer-settings rule and the "smarter default, validated offline" path is available.

**D6 — Prefer cited per-profile targets; taste outranks; ADR-consistent.** The detector reads the citation-bound per-profile arms, not generic slope/graph heuristics (the EAF guide explicitly warns curve-shape diagnosis is unreliable). It is ADR-consistent because the March-2026 rejection was a rigid min/max band fed *to the LLM as a rule*; this is a deterministic detector emitting a soft advisory that AND-gates against per-profile suppression and never overrides taste — structurally unable to reproduce the false-positive that flagged intentional declining-pressure D-Flow.

**D7 — Hard invariant: nothing synthesized, no new persistence, no export.** No confidence/quality score is computed and fed anywhere as a hint; the only output is one prose line through the existing line contract; nothing new is written to the shot record, a column, or visualizer.coffee. This restates the durable #1155 lesson for the highest-visibility surface, as a spec-level invariant.

**D8 — Hard dependency on `capture-dialin-coaching-guidance`.** Without the two-sided arms and the per-profile `Suppress:` set the detector cannot fire safely (the one-sided envelope alone is the confounded half; suppression is the gate). If that change has not landed, this one is blocked, not reimplemented. The upstream design named this consumer, so the contract is pre-agreed.

## Risks / Trade-offs

- **[False positive on an intentional behavior — the #1155 surface]** → D4 hard AND-gate (per-profile `Suppress:` + cascade-fired + grind-tolerant + bean-freshness + margin) and D1 soft, taste-deferring, non-badge wording; D5 measures the corpus false-positive rate and tunes conservative before exposure.
- **[Drift between save/load/detail renderings]** → D2: detector is inside the one-place cascade; the existing recompute-on-load consistency requirement and its tests extend to cover the new line.
- **[Legacy/imported shots regress]** → D3: absent expectation → strict no-op; spec scenario asserts byte-identical behavior when the parameter is unset.
- **[Detector marginal value is low after gating]** → Accepted, and that is what D5's shadow run measures; if the conservative-tuned firing rate on real off-guideline shots is negligible, the line is simply not enabled — the detector code is cheap and the gate is the decision point.
- **[Upstream `ProfileExpectation` not yet available]** → D8: hard prerequisite; apply ordering enforced.
- **[Users read the soft line as a hard verdict anyway]** → wording is observational and explicitly defers to taste; it shares the existing `[observation]`/`[caution]` styling, not the warning/badge styling; it never appears alongside a contradicting fired mechanical detector (cascade gate).

## Migration Plan

Additive code only: a new detector function in `src/ai/shotanalysis.cpp` within the existing cascade; an optional `ProfileExpectation` parameter on `analyzeShot` (default absent); save/load/detail callers resolve and pass the resolved profile's expectation. No DB migration, no schema/storage change, no new badge column, no QML change beyond the line flowing through the existing `summaryLines` render path, no network/Visualizer surface. Rollback is a single revert; absent-expectation no-op means partial rollback (reverting upstream data) also silences it safely. Verification: existing Qt Test suite stays green; new `tst_shotanalysis` assertions for gating, suppression, absent-expectation no-op, and save/load/detail recompute parity; the `shot_eval` shadow run over `tests/data/shots/` reports the known-good false-positive rate and the chosen conservative firing margin is recorded in the change log before the line is enabled.

## Open Questions

- Exact `summaryLines` `type` for the line — reuse `[observation]` vs `[caution]`; resolved in the specs delta against the dialog's existing styling, leaning `[observation]` (lowest authority).
- Quantitative firing margin (how far outside the cited signature before firing) — deliberately not fixed here; it is the output of the D5 shadow run, tuned to a target known-good false-positive rate recorded at apply time.
- Whether the clean-but-off-`PressureShape:` case (no arm match, e.g. 10-bar D-Flow) emits the same soft line or a distinct "outside intended shape — judge by taste" wording — finalized in the specs delta; both are taste-deferring and suppression-gated.
- Whether to keep the shadow-run path as a permanent `shot_eval` regression guard after launch (recommended) or remove it post-tuning — leaning keep, as a standing false-positive regression assertion.
