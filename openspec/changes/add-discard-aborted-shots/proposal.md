# Change: Discard espresso shots that did not start

## Why

The DE1 firmware can autonomously stop a shot during preinfusion-start (e.g. shot 885 in the v1.7.2 prerelease DB: 2.3 s, peak pressure 0.35 bar, 1.1 g final weight, no frame past 0 actually executed). These "did not start" shots are saved into the local history DB today and pollute downstream analysis: they fire badge false positives (PR #898 just gated `temperatureUnstable` against this case), they show up unfiltered in history chips, and they bias auto-favorites stats and dialing summaries.

The user-visible question (issue #899): we shouldn't be persisting these as if they were real shots. The user is unlikely to want them, but we still need an undo path for the rare false-drop and a setting for users who genuinely want everything saved.

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
- **ADD** save-time filter: when a shot classifies as aborted AND the new toggle is on, the espresso save path SHALL skip `ShotHistoryStorage::saveShot()` and the visualizer auto-upload, log the decision with both classifier values, and emit a UI signal to show a toast.
- **ADD** a new toast surface in the post-shot flow: `"Shot did not start — not recorded"` with a `Save anyway` action. Tapping `Save anyway` runs the same save path that would have run, with the same metadata. Toast auto-dismiss is the only legitimate timer here per the project design rule.
- **ADD** a settings toggle `discardAbortedShots` on `SettingsBrew`, default **on**. Exposed in the brew/recording settings page. When off, all shots save as before and the classifier is not consulted.
- **CARVE OUT** non-espresso paths: steam, hot water, and flush operations do not flow through `MainController::endShot()` save logic, so no explicit guard is needed — but the proposal documents the carve-out so future refactors don't regress it.
- **NO retroactive migration.** Legacy aborted shots already in the DB stay; they are handled via badge gating (PR #898). This change is going-forward only.
- **ADD** observability: every classifier evaluation logs `duration`, `finalWeight`, the classifier verdict (`aborted` / `kept`), and the resulting action (`discarded` / `saved` / `saved-anyway`) via the async logger. Telemetry on real users is read out of the log; no new transport.

### Out of scope (explicit)

- Retroactive deletion or hiding of already-saved aborted shots. Out of scope and risky — the user can always delete shots manually.
- Threshold tuning UI. The conjunction values are hard-coded constants validated against the 882-shot corpus; if the field reveals false-drops, we revise the constants in code, not via a setting.
- A separate "aborted shots" history view. Discarded shots are not persisted; if a user routinely needs them visible they can turn the toggle off.
- Steam / hot water "did not start" filtering. Different semantic and rarely problematic; out of scope.
- Visualizer-side cleanup of already-uploaded aborted shots.
- Peak-pressure gating. The dry-run shows duration+yield alone is sufficient: peak pressures on the four matched discards range 0.35 – 0.49 bar, well below any real shot. Adding a third clause buys nothing on this corpus and adds a future-tuning surface for no benefit.
- A "minimum-shot duration" setting separate from this one. The 10-s cutoff is the implementation of the discard rule, not a user-tunable knob — the toggle to disable the whole filter is sufficient escape hatch.

## Impact

- **Affected specs**: new `shot-save-filter` capability with two requirements (aborted-shot classifier + save-time filter, and the toast/undo surface).
- **Affected code**:
  - `src/controllers/maincontroller.cpp` — classifier call site in `endShot()` save path; emits new `shotDiscarded(text, classifierJson)` signal; provides a `saveAbortedShotAnyway()` Q_INVOKABLE for the toast action.
  - `src/core/settings_brew.h` / `.cpp` — `discardAbortedShots` Q_PROPERTY (default true).
  - `qml/components/Toast.qml` (or existing toast surface) — wire a new toast type that has an action button.
  - `qml/main.qml` — connect `MainController.shotDiscarded` → show toast.
  - `qml/pages/settings/SettingsBrewTab.qml` (or wherever brew toggles live) — expose the new setting.
  - `docs/CLAUDE_MD/SETTINGS.md` — list the new property under `SettingsBrew`.
- **Tests**: unit test exercising the classifier across the boundary cases — just-under and just-over each of the two thresholds, and the conjunction logic (only both-conditions-met triggers a drop). Use the matched shots from the corpus (885, 850, 836, 17, 1) as positive fixtures and the long-low-yield chokes (890, 708, 732, 117) as negative fixtures (must be *kept*).
- **Risks**:
  - Classifier could false-drop a legitimate very-short turbo abort. Mitigated by yield < 5 g (rules out anything that hit the cup) and by the `Save anyway` toast action.
  - Toast shown after the post-shot review page is dismissed could be missed. Surface the toast on the page that's visible at shot-end (Espresso/Idle), not gated behind navigation.
  - Settings collision with a future "minimum-shot duration" feature; documented in design.md so the property name doesn't get reused.
