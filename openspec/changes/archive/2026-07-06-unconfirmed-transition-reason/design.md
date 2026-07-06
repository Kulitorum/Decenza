# Design: Unconfirmed Transition Reason

## Context

`transitionReason` is recorded once per frame change in `MainController::onShotSampleReceived` (src/controllers/maincontroller.cpp ~2677-2715) on the marker of the *new* frame, describing why the *previous* frame exited. Values before this change: `weight` (app-initiated skip, 100% certain), `pressure`/`flow` (machine exit confirmed against the latest BLE sample), `time` (time expired, no exit configured, or — since PR #1421 — the ambiguous case), `""` (pre-feature data, or firmware-skip where `m_lastFrameNumber == -1`).

Decision-making consumers:
- `ShotAnalysis::detectSkipFirstFrame` (src/ai/shotanalysis.cpp ~677) — suppresses the "First step skipped" badge when the reason is a confirmed `pressure`/`flow`/`weight` (added in #1421).
- `ShotAnalysis::analyzeFlowVsGoal` (src/ai/shotanalysis.cpp ~413-419) — trims the trailing `GRIND_LIMITER_TAIL_SKIP_SEC` (1.5 s) from a flow-mode window when the next marker's reason is `pressure` (limiter engaged; controller no longer tracks the flow goal), guarded by a ≥1 s post-trim window.

Display consumers (pure string mapping with graceful unknown-value fallback):
- `qml/components/ShotGraph.qml` ~354 and `qml/components/HistoryShotGraph.qml` — marker label suffix switch (`[W]`/`[P]`/`[F]`/`[T]`, unknown → no suffix).
- `qml/pages/EspressoPage.qml` `_transitionText`/`_transitionColor` ~277-295 — live pill (unknown → "Next frame" / default color).
- `src/network/shotserver_shots.cpp` lines ~2012 and ~2848 — two embedded-JS suffix switches for the web graphs (unknown → no suffix).

Pass-through consumers (no interpretation, no changes needed): shot history SQLite persistence, `shotcomparisonmodel`, `shotdatamodel` marker map, `shotserver` JSON serialization, `shotsummarizer` (raw string into the LLM prompt).

The problem: #1421 records the ambiguous exit (exit configured, sensor threshold not confirmed by the latest ~5 Hz BLE sample, time not expired) as `"time"`. The skip-first-frame guard needs that case to NOT suppress (a firmware-skipped frame lands in the same branch). But the grind tail-trim wants it to trim (the ambiguous case is usually a real limiter exit delayed by BLE sample lag). One value cannot serve both.

## Goals / Non-Goals

**Goals:**
- Restore the pre-#1421 grind limiter-tail trim on ambiguous pressure exits.
- Preserve #1421's safety property: an unconfirmed exit never suppresses the skip-first-frame badge.
- Restore sensor-flavored UI labels (pill, marker letters, web graph) for ambiguous exits.
- Keep the reason vocabulary self-describing in persisted data (no migration, no side tables).

**Non-Goals:**
- No re-litigation of #1421's guard semantics for confirmed values.
- No changes to pass-through consumers or the persisted schema (the column is a free string).
- No data migration for shots recorded in the post-#1421 `"time"` window (~1 day of data; trim skipped on those is acceptable noise).
- No new settings (per project preference for fewer settings).

## Decisions

**D1: Two values (`pressure_unconfirmed`, `flow_unconfirmed`), not one generic `unconfirmed`.**
The grind tail-trim needs to know whether the ambiguous exit was pressure-flavored; `analyzeFlowVsGoal` only sees phase markers and curves, not the profile's `exitType`. Encoding the hint in the reason string (taken from `prevFrame.exitType.contains("pressure")`, exactly as the pre-#1421 guess did) keeps the marker self-contained. Alternative considered: single `"unconfirmed"` + threading `exitType` into the analyzer — more plumbing, no benefit.

**D2: Suffix naming `<sensor>_unconfirmed` rather than `unconfirmed_<sensor>` or `<sensor>?`.**
Prefix-matching bugs are the risk to design against: `detectSkipFirstFrame` and `analyzeFlowVsGoal` both use exact `QString::compare`, so `pressure_unconfirmed` can never accidentally match a `"pressure"` check. Avoid punctuation (`pressure?`) because the string is embedded in web JS and JSON contexts.

**D3: Skip-first-frame guard stays exact-match on confirmed values — no code change, comment update only.**
The guard's correctness (firmware-skip still flags) now depends on the recording side never emitting confirmed `pressure`/`flow` for unconfirmed exits. That contract is already established by #1421; this change keeps it and documents it at both ends.

**D4: Grind tail-trim matches `pressure` OR `pressure_unconfirmed`.**
Trimming on an unconfirmed-but-likely limiter exit is the safe polarity: worst case drops 1.5 s of clean data (with the existing ≥1 s post-trim guard), versus including limiter-suppressed flow that biases the verdict toward "too fine". `flow_unconfirmed` is NOT added to the trim — the trim exists specifically for the pressure-limiter tail.

**D5: Display maps unconfirmed to its sensor equivalent (same letter, text, color).**
`[P]`/`[F]`, "Pressure exit"/"Flow exit", `Theme.pressureColor`/`Theme.flowColor` — identical to what users saw pre-#1421 when the value was a guess. Alternative considered: distinct `[P?]` and "Pressure exit (unconfirmed)" — rejected: the distinction is meaningful to detectors, not to users mid-shot, and the guess is usually correct. Reuses existing translation keys (`espresso.transition.pressure`/`.flow`) since the rendered text is identical; no new keys needed.

**D6: `weight` needs no unconfirmed variant.**
Weight exits are app-initiated (`wasWeightExit`), never inferred — there is no ambiguous weight case.

## Risks / Trade-offs

- [Unknown-value fallbacks mask a missed mapping] → All display switches degrade gracefully (no suffix / "Next frame"), so a missed spot is cosmetic, not breaking. The tasks enumerate all five mapping sites (2 QML graphs, 1 QML pill ×2 functions, 2 embedded-JS switches).
- [Residual skip-first-frame false positive on unconfirmed exits remains] → Intentional (D3): this is the same conservative trade-off #1421 chose; the alternative silently masks the firmware-skip bug the detector exists to catch.
- [Third-party readers of exported shot JSON see a new reason string] → The field was always a free string with an "unknown/old data" empty case; consumers that switch on known values fall through the same way the app's own displays do.
- [Shots recorded between #1421 and this change carry `"time"` for ambiguous exits] → Accepted; ~1 day window, affects only the grind trim on those shots, no migration.

## Migration Plan

None required. Forward-only vocabulary addition; old values keep their meaning. Rollback = revert the PR; recorded `_unconfirmed` values would then hit unknown-value fallbacks in displays and be ignored by detectors (fail-safe: skip guard still doesn't suppress, trim doesn't fire — i.e., #1421 behavior).

## Open Questions

None.
