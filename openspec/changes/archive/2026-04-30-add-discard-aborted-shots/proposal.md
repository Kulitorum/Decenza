# Change: Discard espresso shots that did not start

## Why

The DE1 firmware can autonomously stop a shot during preinfusion-start (e.g. shot 885 in the v1.7.2 prerelease DB: 2.3 s, peak pressure 0.35 bar, 1.1 g final weight, no frame past 0 actually executed). These "did not start" shots are saved into the local history DB today and pollute downstream analysis: they fire badge false positives (PR #898 just gated `temperatureUnstable` against this case), they show up unfiltered in history chips, and they bias auto-favorites stats and dialing summaries.

The user-visible question (issue #899): we shouldn't be persisting these as if they were real shots. The user is unlikely to want them. A settings toggle is provided for users who do want everything saved; there is no per-shot undo because the dry-run shows zero false-drops and the toggle is the right escape hatch.

## Threshold validation

Dry-run against Jeff's local DB (882 espresso shots, 2026-01-21 → 2026-04-28):

| Rule | Shots discarded | Notes |
|------|-----------------|-------|
| `dur < 3s AND yield < 0.5g` | 1 | Original strawman — **misses shot 885** (yield 1.1 g, the canonical example). Far too narrow. |
| `dur < 10s AND yield < 5g` | **4–5** | All genuine "did not start" cases. Ids 885, 850, 836, 17 by `durationSec` directly; id 1 also matches if you use the correct *extraction* duration (its stored 15 s is bogus — see below). |
| `dur < 20s AND yield < 5g` | 5 | Adds id 1, but at the cost of also catching long-running low-yield chokes that *should* be kept for diagnosis. Too wide. |

**Chosen rule: `extractionDuration < 10 s AND finalWeight < 5 g`.** Both clauses use strict `<`. The 10-s cutoff is the line above which the graph itself is informative (operator can see the flat-pressure trace and diagnose bad puck prep); below it the shot truly did not begin. Yield < 5 g leaves headroom over the largest matched yield (1.1 g) without false-keeping. Long, low-yield shots (59-s / 1.1 g chokes, 133-s / 3.8 g hard chokes) are explicitly kept because their graphs are what users dial against.

**Important — duration source matters.** Id 1 (the first shot in the DB) shows `durationSec = 15.0` but its `phaseSummaries` reveal only **0.5 s of actual frame execution**; the 15 s is wall-clock time including preheat purge. `MainController::endShot()` already passes the correct `extractionDuration` (from `ShotTimingController`) to `saveShot()` for current shots, and the new classifier MUST consume the same value, not raw clock time. With the correct extraction duration, id 1 also classifies as aborted, bringing the dry-run count to **5/882 (0.57%)**, all genuine "did not start" cases. No false-drops in the 882-shot corpus.

Suspicious data anomalies (Londonium ids 801 / 438: 2.7-s and 3.6-s with full 36 g yields — physically impossible for that profile) sit above the 5 g yield threshold and are correctly *not* discarded; they're a separate data-integrity bug, not in scope.

## What Changes

- **ADD** an "aborted shot" classifier in the espresso save path with the validated conjunction: a shot is *aborted* iff `extractionDuration < 10.0 s` AND `finalWeight < 5.0 g`. Both must hold; either alone is not enough.
- **ADD** save-time filter: when a shot classifies as aborted AND the new toggle is on, the espresso save path SHALL skip `ShotHistoryStorage::saveShot()` and the visualizer auto-upload, log the decision with both classifier values, and emit a UI signal so a toast can notify the user.
- **ADD** an informational toast `"Shot did not start — not recorded"` shown for a few seconds after a discard. **No "Save anyway" recovery path** — a discarded shot is intentionally gone. Users who want everything saved should turn the toggle off. Toast auto-dismiss is the only timer here per the project design rule.
- **ADD** a settings toggle `discardAbortedShots` on `SettingsBrew`, default **on**. Exposed in the brew/recording settings page. When off, all shots save as before and the classifier is not consulted.
- **CARVE OUT** non-espresso paths: steam, hot water, and flush operations do not flow through `MainController::endShot()` save logic, so no explicit guard is needed — but the proposal documents the carve-out so future refactors don't regress it.
- **NO retroactive migration.** Legacy aborted shots already in the DB stay; they are handled via badge gating (PR #898). This change is going-forward only.
- **ADD** observability: every classifier evaluation logs `duration`, `finalWeight`, the classifier verdict (`aborted` / `kept`), and the resulting action (`discarded` / `saved`) via the async logger. Telemetry on real users is read out of the log; no new transport.

### Out of scope (explicit)

- Retroactive deletion or hiding of already-saved aborted shots. Out of scope and risky — the user can always delete shots manually.
- Threshold tuning UI. The conjunction values are hard-coded constants validated against the 882-shot corpus; if the field reveals false-drops, we revise the constants in code, not via a setting.
- A separate "aborted shots" history view. Discarded shots are not persisted; if a user routinely needs them visible they can turn the toggle off.
- Steam / hot water "did not start" filtering. Different semantic and rarely problematic; out of scope.
- Visualizer-side cleanup of already-uploaded aborted shots.
- Peak-pressure gating. The dry-run shows duration+yield alone is sufficient: peak pressures on the four matched discards range 0.35 – 0.49 bar, well below any real shot. Adding a third clause buys nothing on this corpus and adds a future-tuning surface for no benefit.
- A "minimum-shot duration" setting separate from this one. The 10-s cutoff is the implementation of the discard rule, not a user-tunable knob — the toggle to disable the whole filter is sufficient escape hatch.
- A per-shot "Save anyway" recovery action. Considered and rejected: zero false-drops in the corpus, and a one-tap recovery surface adds a real cache-lifetime, double-callback, and metadata-state-coherence cost to a feature that should be a simple notification.

## Impact

- **Affected specs**: new `shot-save-filter` capability with two requirements (aborted-shot classifier + save-time filter, and the notification toast).
- **Affected code**:
  - `src/controllers/maincontroller.{h,cpp}` — classifier call site in `endShot()` save path; emits a new `shotDiscarded(durationSec, finalWeightG)` signal.
  - `src/controllers/abortedshotclassifier.h` — new header-only `decenza::isAbortedShot()` pure function with the threshold constants.
  - `src/core/settings_brew.{h,cpp}` — `discardAbortedShots` Q_PROPERTY (default true).
  - `qml/main.qml` — `Connections { onShotDiscarded }` shows the informational toast (no action button).
  - `qml/pages/settings/SettingsCalibrationTab.qml` — expose the new setting next to "Prefer Weight over Volume".
- **Tests**: unit test exercising the classifier across the boundary cases — just-under and just-over each of the two thresholds, and the conjunction logic (only both-conditions-met triggers a drop). Use the matched shots from the corpus (885, 850, 836, 17, 1) as positive fixtures and the long-low-yield chokes (890, 708, 732, 117) as negative fixtures (must be *kept*).
- **Risks**:
  - Classifier could false-drop a legitimate very-short turbo abort. Mitigated by yield < 5 g (rules out anything that hit the cup); zero false-drops in the 882-shot corpus. If a real false-drop ever surfaces, the toggle is the immediate user escape hatch.
  - Toast shown after the post-shot review page is dismissed could be missed. Surface the toast on the page that's visible at shot-end (Espresso/Idle), not gated behind navigation.
