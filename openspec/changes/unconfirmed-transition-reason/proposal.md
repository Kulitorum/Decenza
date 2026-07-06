# Unconfirmed Transition Reason

## Why

PR #1421 fixed the "First step skipped" false positive by trusting `transitionReason` as ground truth, and to keep that trust honest it changed MainController's ambiguous frame-exit branch (exit condition configured, sensor threshold not confirmed by the latest BLE sample, time not expired) to record `"time"` instead of a guessed `"pressure"`/`"flow"`. That collapse loses information the grind detector needs: `analyzeFlowVsGoal`'s limiter-tail trim (drop the last 1.5 s of a flow-mode window when the next marker says `"pressure"`) no longer fires on ambiguous exits that are, in practice, usually real pressure-limiter exits delayed by BLE sample lag. Including that limiter-suppressed tail contaminates the flow-vs-goal average and biases the grind verdict toward "too fine". It also regressed the UI cosmetically: the frame-transition pill and graph marker letters show "Time exit"/[T] where a sensor exit almost certainly happened.

The two decision-making consumers need opposite defaults for the ambiguous case — the skip-first-frame guard must NOT suppress on it (a firmware-skipped frame lands in the same branch), while the grind tail-trim SHOULD fire on it (trimming 1.5 s of maybe-good data is harmless; including contaminated data is not). A single `"time"` value cannot express both; a distinct unconfirmed reason can. This is the alternative the PR author explicitly offered in #1421.

## What Changes

- MainController's ambiguous exit branch records `"pressure_unconfirmed"` or `"flow_unconfirmed"` (hint taken from the frame's `exitType`, as the pre-#1421 guess did) instead of `"time"`.
- `detectSkipFirstFrame`'s exit-condition guard is unchanged in behavior: it continues to exact-match only confirmed `"pressure"`/`"flow"`/`"weight"`, so unconfirmed values fall through and a genuinely short first frame still flags (preserves #1421's safety property).
- `analyzeFlowVsGoal`'s limiter-tail trim also fires on `"pressure_unconfirmed"`, restoring the pre-#1421 trim behavior on ambiguous pressure exits.
- Display mappings handle the new values explicitly, rendering them as their sensor equivalent (what users saw pre-#1421): graph marker suffixes in `ShotGraph.qml`/`HistoryShotGraph.qml`, the frame-transition pill text/color in `EspressoPage.qml`, and the ShotServer web graph's reason mapping.
- Documentation: `docs/SHOT_REVIEW.md` §2.2/§2.3 reason vocabulary updated (including the §2.2 cross-reference note deferred from #1421's review), and the `HistoryPhaseMarker::transitionReason` field comment.
- No data migration: pre-#1421 shots carry the guessed `"pressure"`/`"flow"` (trim fires as before); shots recorded in the brief post-#1421 window carry `"time"` (trim skipped — acceptable noise).

## Capabilities

### New Capabilities

- `frame-transition-reason`: the recorded vocabulary and semantics of `transitionReason` on phase markers — which values are confirmed ground truth (`weight`, `pressure`, `flow`), which are unconfirmed hints (`pressure_unconfirmed`, `flow_unconfirmed`), when `time` and empty apply, and how each renders in the live pill, graph markers, and web graph.

### Modified Capabilities

- `shot-analysis-pipeline`: adds requirements for how the two decision-making detector consumers treat unconfirmed reasons — the skip-first-frame exit-condition guard suppresses only on confirmed sensor reasons, and the grind detector's limiter-tail trim fires on both confirmed and unconfirmed pressure reasons.

## Impact

- `src/controllers/maincontroller.cpp` — ambiguous-branch reason recording (one branch).
- `src/ai/shotanalysis.cpp` — `analyzeFlowVsGoal` tail-trim match; comment update in `detectSkipFirstFrame` (no logic change).
- `qml/components/ShotGraph.qml`, `qml/components/HistoryShotGraph.qml` — marker suffix switch.
- `qml/pages/EspressoPage.qml` — `_transitionText`/`_transitionColor` cases (with translation keys).
- `src/network/shotserver_layout.cpp` (or wherever the web graph maps `reason`) — same mapping for the web editor/graph.
- `src/history/shothistory_types.h` — field comment.
- `tests/tst_shotanalysis.cpp` — new rows: skip detector still flags on `pressure_unconfirmed`; grind tail-trim fires on it.
- `docs/SHOT_REVIEW.md` — §2.2 and §2.3.
- Persisted data: `transitionReason` is a free string end-to-end (SQLite, serializer, comparison model, summarizer) — pass-through consumers need no changes; the AI summarizer receives the raw string, which reads naturally.
