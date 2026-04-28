# Shot Review: Current State

This is a snapshot of how the post-shot review and shot detail pages work today,
focused on the quality-badge / detector system and the Shot Summary dialog that
share the same underlying analysis. It is the source of truth for anyone modifying
detectors, persistence, the badge UI, or the summary text; the `git log` for
`docs/SHOT_REVIEW.md` (and its prior name `SHOT_REVIEW_IMPROVEMENTS.md`) is the
source of truth for the path that got us here.

The detectors are still under active iteration — expect this doc to drift, and
prefer reading the code (`src/ai/shotanalysis.h` is heavily commented) when in
doubt.

---

## 1. What the user sees

### Pages

Both **PostShotReviewPage** (auto-opens when a shot ends) and **ShotDetailPage**
(opened from history) render the same shot data with the same components. A
shared `shotReview/advancedMode` setting toggles information density on both.

### Basic mode (default)

- Graph: pressure, flow, temperature, weight, weight flow rate.
- Goal overlays for pressure / flow / temperature (dashed).
- Phase markers (frame boundaries) without label text.
- Quality badges (one or more chips below the graph).
- Metrics: duration, dose, output, ratio, rating.
- Notes, bean / grinder info, barista.

### Advanced mode adds

- Resistance (P/F), Darcy resistance (P/F²), conductance (F²/P), dC/dt, mix
  temperature curves (toggleable in the legend).
- Phase marker label text (frame boundaries with transition reasons —
  `[W]` weight, `[P]` pressure, `[F]` flow, `[T]` time).
- Phase summary panel — collapsible per-phase metrics table.
- TDS / EY fields (review page) and analysis card + debug log button (detail
  page).

### Quality badges

Four independent flags, surfaced via `qml/components/QualityBadges.qml`. When
any fire, those chips show; when none fire, a single green "Clean extraction"
chip shows. None of the chips suppress the others. A tappable "Shot
Summary" chip always sits at the end of the row — it's the entry point to
the analysis dialog described in §3.

| Flag                       | Color  | Label                  | Source                       |
| -------------------------- | ------ | ---------------------- | ---------------------------- |
| `channelingDetected`       | red    | "Channeling detected"  | `detectChannelingFromDerivative` |
| `temperatureUnstable`      | orange | "Temp unstable"        | `avgTempDeviation` + threshold |
| `grindIssueDetected`       | orange | "Grind issue"          | `detectGrindIssue` (`analyzeFlowVsGoal`) |
| `skipFirstFrameDetected`   | red    | "First step skipped"   | `detectSkipFirstFrame`       |

The badges are recomputed on every shot load (see §4) so detector improvements
take effect on existing shots without a manual re-analyze.

---

## 2. Detector internals

All four detectors live in `src/ai/shotanalysis.{h,cpp}` as static methods on
`ShotAnalysis`. Tuning constants are defined at the top of the header so they
can be tweaked in one place. The header is heavily commented — read it
alongside this section.

### 2.1 Channeling

Single source of truth for channeling severity is `detectChannelingFromDerivative`,
operating on the Gaussian-smoothed conductance derivative `dC/dt`. The badge
fires on the **Sustained** severity tier; the **Transient** tier surfaces only
in the Shot Analysis dialog popup.

**Severity tiers** (`ChannelingSeverity`):

- **None** — no sample's `|dC/dt|` exceeds `CHANNELING_DC_TRANSIENT_PEAK` (5.0).
- **Transient** — a single peak above 5.0 (a self-healed channel).
- **Sustained** — more than `CHANNELING_DC_SUSTAINED_COUNT` (10) samples above
  `CHANNELING_DC_ELEVATED` (3.0) **inside the inclusion windows**.

**Inclusion windows** are built by `buildChannelingWindows`. Without windowing
the detector flags every lever-style preinfusion as channeling because the
natural pressure-rise dynamic drives `dC/dt` strongly negative. A sample is
included only when, at that time:

1. The active control goal (flow goal during flow-mode phases, pressure goal
   during pressure-mode phases) is **stationary**: `|goal(t±0.75 s) − goal(t)|
   / goal(t) ≤ 0.15` on both sides.
2. The actual value has **converged** onto the goal: `|actual − goal| / goal ≤
   0.15`.
3. Actual pressure is not rising fast in either mode: when
   `pressureNow > 0.5` bar AND `pressureFut > pressureNow * 1.15` over 0.75 s
   ahead, the sample is disqualified. Two ramp dynamics produce the same
   conductance-drop signature and are both gated here:
   - Flow-mode lever rise (Cremina, Damian LRv3, 80's Espresso preinfusion):
     flow goal stationary while the puck builds pressure under a pressure-
     ceiling exit.
   - Pressure-mode rise-and-hold final leg: pressure goal locked at target
     but actual pressure still ramping toward it. The 15 %-of-goal
     convergence check admits these samples because actual is close enough
     to goal, even though it is still climbing fast — `dC/dt` clamps at the
     negative floor as conductance drops with pressure. (Shot 889 on 80's
     Espresso was the motivating false positive.)
   Falling pressure is always allowed — bloom transitions and
   pressure-mode → flow-mode handoffs are legitimate channeling signals,
   and the dC/dt detector counts both signs of conductance change as
   diagnostic. The 0.5 bar precondition gates out the near-atmospheric
   window where small absolute jitter would otherwise read as a 15 %
   relative rise.

Contiguous qualifying times collapse into windows; gaps ≤
`WINDOW_GAP_MERGE_SEC` (0.3 s) are merged.

**Fallback semantics** are deliberate:

- Empty `phases` → emits a single whole-pour window (legacy shots predating
  phase markers run unrestricted, preserving coverage).
- Non-empty `phases` but no qualifying window → returns empty, and
  `detectChannelingFromDerivative` reports `None` (not unrestricted). This is
  the "all-ramp shot, nothing reliable to analyze" path; better to be silent
  than to false-positive.

**Pour-window trim**: the first `CHANNELING_DC_POUR_SKIP_SEC` (2.0 s) and last
`CHANNELING_DC_POUR_SKIP_END_SEC` (1.5 s) of the pour are excluded from the
detector's time grid — they cover the pressure-ramp / flow-catchup transient
at start and the natural tail acceleration at end. `buildChannelingWindows`
applies the same trim to its output bounds.

**Hard skips** (`shouldSkipChannelingCheck` returns true → `channelingDetected`
forced to false):

- Beverage type is `filter`, `pourover`, `tea`, `steam`, or `cleaning`.
- Average flow during the pour exceeds `CHANNELING_MAX_AVG_FLOW` (3.0 mL/s) —
  turbo / very-coarse shots have no diagnostic dC/dt signal.

**Profile-level skip**: a `channeling_expected` flag in `ProfileKnowledge`
(reached via `ShotSummarizer::getAnalysisFlags(profileKbId)`) suppresses
channeling detection for profiles where channeling-shaped curves are intentional.

### 2.2 Grind issue

Implemented by `detectGrindIssue`, which delegates to `analyzeFlowVsGoal` and
returns true when either arm fires. Two arms run **additively**, not as
fallback — a clean flow-mode preinfusion can hide a pressure-mode choke and
vice versa.

**Arm 1: flow-vs-goal averaging** (the "primary" path).

For each flow-mode phase, builds an inclusive time range:

- Skip the first `GRIND_PUMP_RAMP_SKIP_SEC` (0.5 s) of the first flow-mode
  phase that coincides with `pourStart` — pump-ramp lag, not a grind signal.
- If the phase exits via `transitionReason == "pressure"`, skip the trailing
  `GRIND_LIMITER_TAIL_SKIP_SEC` (1.5 s) — the firmware's pressure ceiling has
  engaged and the controller is no longer tracking flow goal.
- Both trims are gated on the resulting range remaining ≥ 1 s long. Extreme
  puck-failure shots with sub-second flow-mode phases would otherwise have
  their entire data trimmed away.

Within those ranges, average actual flow vs. flow goal across all samples
where `goal ≥ FLOW_GOAL_MIN_AVG` (0.3 mL/s — gates out preinfusion sentinel
goals). Requires ≥ 5 qualifying samples to yield a result. The check fires
when `|delta| > FLOW_DEVIATION_THRESHOLD` (0.4 mL/s); positive delta = coarse,
negative = fine.

**Arm 2: choked-puck check** (pressure-mode only).

For each pressure-mode phase (or the whole pour window if all phases are
flow-mode-labeled), accumulate samples where `pressure ≥
CHOKED_PRESSURE_MIN_BAR` (4.0). Requires `flowSamples ≥ 5` and
`pressurizedDuration ≥ CHOKED_DURATION_MIN_SEC` (15 s) to fire. Two
sub-arms feed `chokedPuck`:

- **Severe (flow arm)**: mean pressurized flow `< CHOKED_FLOW_MAX_MLPS` (0.5
  mL/s). Catches the obvious failures (e.g., 80's Espresso with 1.1 g yield
  and ~0.3 mL/s mean).
- **Moderate (yield arm)**: `finalWeightG / targetWeightG < CHOKED_YIELD_RATIO_MAX`
  (0.85). Catches narrowly-above-flow-threshold cases where the puck still
  failed (e.g., 25 g of a 36 g target, ~0.6 mL/s mean).

The yield arm requires both `targetWeightG > 0` and `finalWeightG > 0` —
imported shots without target metadata correctly stay silent.
`finalWeightG` works on either a real BLE scale or Decenza's `FlowScale`
virtual scale (dose-aware flow integration), so the arm fires headless too.

**Hard skips** (`skipped = true`, both arms suppressed):

- Beverage type is `filter`, `pourover`, `tea`, `steam`, or `cleaning`.
- `analysisFlags` contains `grind_check_skip`.
- Pressure data is empty (the arm-2 path early-returns).

### 2.3 Temperature unstable

The simplest of the four. `temperatureUnstable = true` when:

- `temperature` and `temperatureGoal` both have > 10 samples, AND
- `hasIntentionalTempStepping(temperatureGoal)` is false (goal range ≤
  `TEMP_STEPPING_RANGE` = 5 °C — D-Flow 84 → 94 is intentional, ignore), AND
- `avgTempDeviation(temperature, temperatureGoal, pourStart, pourEnd) >
  TEMP_UNSTABLE_THRESHOLD` (2.0 °C).

`avgTempDeviation` is the average absolute deviation over the pour window
where the goal is non-zero.

### 2.4 Skip first frame

`detectSkipFirstFrame` operates on phase markers only — no curves needed. Two
distinct branches inside one function, picked from the marker stream:

**FW-bug branch** — frame 0 was never observed before a non-zero frame:
returns `phase.time < 2.0`. The 2-second window matches the de1app Tcl
plugin's polling cadence (it can only catch this before t = 2 s), so we use
the same hard window for parity. This case requires a power-cycle to fix.

**Short-first-step branch** — frame 0 was observed but ended early:
returns `phase.time < cutoff` where:

```
cutoff = (firstFrameConfiguredSeconds > 0)
       ? min(2.0, 0.5 * firstFrameConfiguredSeconds)
       : 2.0
```

The configured-aware path avoids false positives on profiles whose first
frame is configured for 2 seconds — BLE notification jitter routinely lands
the frame-1 marker a few hundred ms early, and a 1.87 s actual on a 2 s
configured frame should not flag.

`expectedFrameCount` is honored when known: values < 2 suppress detection
(no second frame to skip to), and out-of-range frame numbers are skipped as
malformed.

`firstFrameConfiguredSeconds` is fed in by callers from `ProfileFrameInfo`
(parsed from the stored profile JSON via `profileFrameInfoFromJson`).

### 2.5 Pour truncated (used by the summary path, not a badge)

`detectPourTruncated` returns true when peak pressure inside the pour window
stays below `PRESSURE_FLOOR_BAR` (2.5). It catches the failure mode where
conductance saturates at its clamp, `dC/dt` is flat, and flow tracks the
preinfusion goal perfectly — every other detector goes silent. Currently used
inside `ShotAnalysis::generateSummary` (the user-facing observation list); not
wired to a separate badge column.

---

## 3. Shot Summary dialog

The "Shot Summary" chip at the end of the badge row opens
`qml/components/ShotAnalysisDialog.qml` — a non-AI, non-modal-blocking
analysis pane that surfaces a list of observations plus a single verdict
line. The text is computed entirely in C++ from the captured curves; the
QML side is a thin display layer.

### Pipeline

`ShotAnalysisDialog` (visible) →
`MainController.shotHistory.generateShotSummary(shotData)` (Q_INVOKABLE on
`ShotHistoryStorage`) →
`ShotAnalysis::generateSummary(...)` →
`QVariantList` of `{ text, type }` lines →
`Repeater` in the dialog with a colored dot per line.

`generateShotSummary` is the bridge that converts the QML `shotData` map
into the typed vectors `generateSummary` expects (pressure, flow, weight,
temperature + goals, conductance derivative, phases, beverage type, profile
JSON for first-frame seconds, yield override + final weight for the choked-
puck yield arm). Empty / missing fields are tolerated where the underlying
detector tolerates them.

### Line types and rendering

Each line carries a `type`:

| Type          | Dot color                     | Used for                                   |
| ------------- | ----------------------------- | ------------------------------------------ |
| `good`        | success (green)               | "Puck stable", happy-path observations     |
| `caution`     | warning (orange)              | flow drift, temp drift, grind direction    |
| `warning`     | error (red)                   | sustained channeling, choked puck, frame skip, pour truncated |
| `observation` | text-secondary (neutral grey) | preinfusion drip mass / duration           |
| `verdict`     | no dot, larger font           | always exactly one, last in the list       |

The dialog renders the verdict in subtitle font with no dot to set it apart
from the observation lines.

### Observations emitted

Order follows `generateSummary` top-to-bottom:

1. **Channeling status** — uses the same `buildChannelingWindows` +
   `detectChannelingFromDerivative` path as the badge. Emits **Sustained**
   ("warning"), **Transient** ("caution") with the spike timestamp, or a
   "Puck stable" ("good") line. Skipped for filter / pourover / tea /
   steam / cleaning beverages, turbo shots, and profiles with the
   `channeling_expected` analysis flag.
2. **Flow trend** — compares mean flow in the first 30% of the pour
   against the last 30%. ±0.5 mL/s thresholds emit "Flow rose … (puck
   erosion)" or "Flow dropped … (fines migration or clogging)" as
   "caution". Suppressed for profiles with the `flow_trend_ok` analysis
   flag (Cremina lever and similar where declining/rising flow is by
   design).
3. **Preinfusion drip** — when preinfusion lasted > 1 s and at least 0.5 g
   landed during it, emits "Preinfusion: Xg in Ys" as an "observation".
4. **Temperature stability** — when `hasIntentionalTempStepping` is false
   and `avgTempDeviation` exceeds 2 °C, emits "Temperature drifted X °C
   from goal on average" as "caution".
5. **Grind direction** — uses the same `analyzeFlowVsGoal` path as the
   badge. Emits one of: "Pour produced near-zero flow while pressure
   held — puck choked" ("warning") when `chokedPuck` fires, "Flow
   averaged X mL/s below target — grind may be too fine" ("caution"),
   "Flow averaged X mL/s above target — grind may be too coarse"
   ("caution"), or nothing when within tolerance.
6. **Pour truncated** — `detectPourTruncated`. Emits "Pour never
   pressurized — puck failed" as "warning" when peak pressure stayed
   under 2.5 bar.
7. **Skip first frame** — `detectSkipFirstFrame`. Emits "First profile
   step skipped — likely a DE1 firmware bug…" as "warning".

### Verdict precedence

Exactly one verdict line is appended to every summary. The cascade picks
the first match (more specific failures take precedence over generic
puck-integrity advice):

1. **Pour truncated** → "Puck failed — pour never pressurized." Dominates
   over channeling / grind / temperature, since the puck never built any
   resistance worth analyzing.
2. **Skip first frame** → "First profile step was skipped — power-cycle…"
   Pre-empts choked-puck because a frame-skip can synthesise extraction
   dynamics that resemble a choke; fix the machine first, then re-evaluate.
3. **Choked puck** → "Puck choked — grind way too fine. Coarsen
   significantly." Pre-empts the generic "Puck integrity issue" verdict
   below.
4. **Has any "warning"** → "Puck integrity issue — improve distribution."
   When a directional grind signal is present alongside, it appends
   "Grind is running fine — try coarser." or "Grind is running coarse —
   try finer." as a modifier (since channeling alone doesn't tell you
   which direction grind is off).
5. **Has any "caution"** → If grind data is directional (`|delta| >
   FLOW_DEVIATION_THRESHOLD`), names the direction ("Grind appears too
   fine — try coarser." / "…too coarse — try finer.") regardless of any
   other cautions also present. Otherwise: "Decent shot with minor
   issues to watch." (The in-source comment phrases this as "if the
   only caution is a grind direction," but the implementation does not
   actually check for uniqueness — a directional grind delta wins over
   a co-occurring temperature or flow-trend caution.)
6. **Otherwise** → "Clean shot. Puck held well."

### Triggering and lifecycle

The dialog is instantiated declaratively inside `ShotDetailPage.qml` and
`PostShotReviewPage.qml`. The `Shot Summary` chip in `QualityBadges.qml`
emits `summaryRequested()` on tap, which the host page handles by calling
`open()` on its `ShotAnalysisDialog` instance. Analysis lines are
recomputed each time the dialog becomes visible (the
`MainController.shotHistory.generateShotSummary(shotData)` binding only
fires while `analysisDialog.visible` is true), so detector improvements
take effect on summary text the same way they do on badges — no save-time
freeze. There is no DB persistence for summary lines; they're regenerated
on demand.

---

## 4. Persistence semantics

### Save-time: stored columns

The `shots` table has four flag columns: `channeling_detected`,
`temperature_unstable`, `grind_issue_detected`, `skip_first_frame_detected`.
At shot save (or import), the badges are computed once from the captured
curves and written into these columns alongside the rest of the shot record.

### Load-time: always recompute (PR #893)

`ShotHistoryStorage::loadShotRecordStatic` reads the stored columns, then
**unconditionally recomputes all four badges** from the loaded curve data
before returning. This means the in-memory `ShotRecord` always reflects the
current detector logic, regardless of when the shot was saved or under which
detector version. The recompute block lives in `loadShotRecordStatic` (around
the comment "Always recompute every quality badge from the loaded curve
data").

The recompute uses on-the-fly derived curves for legacy shots that lack them:
`computeDerivedCurves` fills `conductanceDerivative` from `pressure`/`flow`
when the shot predates migration 10 (the `conductance` column).

### Lazy persist on view (PR #893)

When a shot is opened in `ShotDetailPage` or `PostShotReviewPage`, QML calls
`MainController.shotHistory.requestReanalyzeBadges(id)`. That method runs on
the DB worker thread and recomputes the four flags. If at least one flag
differs from the stored value, it issues an `UPDATE` *and* emits
`shotBadgesUpdated` so the UI can refresh without a full reload. If every
flag already matches the stored value, the worker exits silently — no
`UPDATE`, no signal, no UI refresh.

The wiring lives at `qml/pages/ShotDetailPage.qml` (in `onShotReady`) and
`qml/pages/PostShotReviewPage.qml`. The visualizer-update reload path
suppresses it (badges didn't change, and the visualizer flow already
triggers a second `onShotReady`).

### Residual gap (Issue #894)

`requestShotsFiltered` → `buildFilterQuery()` (around
`shothistorystorage.cpp:1463-1474`) builds badge filters against the **stored
columns**, not the recomputed values. The MCP `shots_list` tool reads the
same stored columns. So a shot whose recomputed badges no longer match the
stored values is consistent in the detail view (after one open, lazy persist
catches up) but inconsistent in:

- The history-list filter chips ("Show only channeling shots") for shots not
  yet viewed under the current detectors.
- `shots_list` MCP responses for the same.

Tracked in #894. Options on the table: a one-shot bulk resweep (option 1),
adding a manual "Re-analyze all shots" button (option 2), or both (hybrid).
Held until the detectors stabilize — the next round of fixes would just
require another sweep.

---

## 5. Code map

### Detector logic

- `src/ai/shotanalysis.{h,cpp}` — all four detectors, `analyzeFlowVsGoal`,
  `buildChannelingWindows`, `detectChannelingFromDerivative`,
  `detectGrindIssue`, `detectSkipFirstFrame`, `detectPourTruncated`,
  `hasIntentionalTempStepping`, `avgTempDeviation`, `shouldSkipChannelingCheck`,
  `generateSummary` (the Shot Summary dialog text — see §3), all tuning
  constants.

### Persistence

- `src/history/shothistorystorage.{h,cpp}`:
  - `loadShotRecordStatic` — load + recompute block (the "always recompute
    every quality badge" comment marks the section).
  - `requestReanalyzeBadges` — lazy-persist worker.
  - `requestShotsFiltered` + `buildFilterQuery` — history-list filter that
    reads the stored columns.
  - `generateShotSummary` — Q_INVOKABLE bridge that converts a QML
    `shotData` map into the typed inputs for `ShotAnalysis::generateSummary`.
  - DB migration for the four flag columns.
  - `computeDerivedCurves` — fills conductance / dC/dt for legacy shots that
    predate migration 10.
  - `profileFrameInfoFromJson` — extracts `frameCount` and
    `firstFrameSeconds` for `detectSkipFirstFrame` and the summary path.

### Profile-level analysis flags

- `src/profile/profileknowledge.{h,cpp}` — KB entries that carry per-profile
  `analysisFlags` (`channeling_expected`, `grind_check_skip`,
  `bloom_self_heal`, etc.).
- `ShotSummarizer::getAnalysisFlags(profileKbId)` is the canonical lookup
  used by both detector callers and `loadShotRecordStatic`.

### UI

- `qml/components/QualityBadges.qml` — chip rendering. One chip per active
  flag; when none active, a single "Clean extraction" chip; always a
  trailing "Shot Summary" chip that emits `summaryRequested()`.
- `qml/components/ShotAnalysisDialog.qml` — Shot Summary dialog. Calls
  `MainController.shotHistory.generateShotSummary(shotData)` while
  visible and renders the resulting `{ text, type }` lines.
- `qml/pages/ShotDetailPage.qml` and `qml/pages/PostShotReviewPage.qml` —
  consume `shotData.channelingDetected` / `temperatureUnstable` /
  `grindIssueDetected` / `skipFirstFrameDetected`, listen for
  `shotBadgesUpdated`, call `requestReanalyzeBadges` on load, host the
  `ShotAnalysisDialog` instance and wire `summaryRequested` to `open()`.
- `qml/pages/ShotHistoryPage.qml` — filter chips that consume the stored
  columns.

### MCP

- `src/mcp/mcptools_shots.cpp` — `shots_list` reads stored columns;
  `shots_get_detail` runs through `loadShotRecordStatic` and so reflects
  recomputed badges.

---

## 6. Regression corpus

`tools/shot_eval/` is a CLI harness that runs the live detectors against a
curated corpus of shot fixtures and prints (or validates) the verdicts.

- **Fixtures**: `tests/data/shots/*.json` — curated real shots covering
  lever-clean (Cremina, E61, Damian LRv3, classic Italian), lever / E61
  failure modes (80's Espresso choked moderate / puck failure, classic
  Italian fast, Londinium gusher, blooming aborted / choker, puck-failure
  short gusher), grind-fine (Malabar D-Flow), and Adaptive v2 cases (Gagne).
- **Manifest**: `tests/data/shots/manifest.json` — golden verdicts per
  fixture.
- **CTest target**: `shot_corpus_regression` (declared in
  `tests/CMakeLists.txt`) runs `shot_eval --validate manifest.json`. Linux
  CI catches detector regressions before merge.

To add a fixture:

1. Drop the captured shot JSON in `tests/data/shots/`.
2. Add an entry to `manifest.json` with the expected badge / verdict.
3. Run `shot_eval --validate` locally (or via the ctest target) to confirm.
4. Commit both files together so the manifest documents the new golden.

---

## 7. References

- PR #649 — original Tier 1 diagnostics (badges, dC/dt, phase summary, mix
  temperature, basic/advanced toggle).
- PR #699 — bloom/soak channeling suppression via per-profile flags.
- PR #811 — mode-aware shot analysis (`buildChannelingWindows` +
  flow-mode-only `analyzeFlowVsGoal`) plus the `shot_eval` corpus.
- PR #864 — first attempt at lever false-positive suppression
  (over-aggressive).
- PR #866 — corpus-regression repair (rising-pressure gate in flow-mode
  windows, ≥ 1 s post-trim guard on grind ranges).
- PR #890 — skip-first-frame uses the configured first-frame seconds, not
  a hard 2 s constant.
- PR #891 — choked-puck arm on pressure-mode pours.
- PR #892 — moderate grind-too-fine via yield/target ratio.
- PR #893 — recompute every quality badge on shot load + lazy persist on
  view.
- Issue #894 — residual stored-column drift (history-list filter and
  `shots_list` MCP read stored, not recomputed values).

External resources that informed the diagnostic patterns:

- Visualizer.coffee: <https://github.com/miharekar/visualizer> (conductance /
  resistance formulas, dC/dt smoothing kernel).
- GaggiMate MCP: <https://github.com/julianleopold/gaggimate-mcp> (Darcy
  resistance, channeling risk scoring, profile compliance).
- Coffee ad Astra puck resistance study: <https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/>.
- Espresso Compass (Barista Hustle): <https://www.baristahustle.com/the-espresso-compass/>.
