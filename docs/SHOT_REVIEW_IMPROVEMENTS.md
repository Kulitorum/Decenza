# Shot Review Page Improvements

## Status: Tier 1 Implemented

This document identifies improvements to the post-shot review and shot detail pages
that help users understand shot quality without AI interaction. Prioritized by impact
and implementation effort.

---

## 0. Implementation Summary (Tier 1)

All Tier 1 items (1.1--1.4) are implemented on both PostShotReviewPage and
ShotDetailPage. A Basic/Advanced mode toggle controls information density.

### Basic/Advanced Mode

A pill-style toggle (persisted to `shotReview/advancedMode`) sits below the graph
legend on both pages. The same setting syncs between pages.

**Basic mode** (default):
- Graph: pressure, flow, temperature, weight, weight flow (5 curves)
- Quality badge (single status chip)
- Metrics: duration, dose, output, ratio, rating
- Notes, bean info, grinder info, barista

**Advanced mode** adds:
- Graph legend: resistance (P/F), conductance (F²/P), dC/dt, Darcy resistance
  (P/F²), mix temperature
- Phase marker labels on graph (frame boundary labels with transition reasons)
- Phase summary panel (collapsible table with per-phase metrics)
- TDS/EY fields (review page) and analysis card (detail page)
- Debug log button (detail page)

### What Was Built

| Item | Status | Notes |
|------|--------|-------|
| 1.1 Conductance + dC/dt curves | Done | 3 new curves + existing resistance moved to advanced legend |
| 1.2 Quality badges | Done | Single chip: green/red/orange. Both modes. |
| 1.3 Phase summary panel | Done | Collapsible, advanced only. Computed at save, backfilled for legacy. |
| 1.4 Mix temperature curve | Done | Already stored, now displayed. Advanced legend. |

### Additional Changes

- **Unified channeling detection (dC/dt)**: `channelingDetected` now comes from
  the conductance derivative, matching the Shot Summary popup's signal. Previously
  the badge used a flow-spike heuristic (flow-mode phases only) while the popup
  used dC/dt, so they could disagree — especially on pressure-profile shots where
  the badge could never fire. `ShotAnalysis::detectChannelingFromDerivative()` is
  now the single source of truth, used by the badge, the AI prompt observations,
  and `ShotAnalysisDialog`. The badge fires on Sustained events (>10 samples with
  `|dC/dt| > 3.0`); transient self-healed channels surface only in the popup.
- **Detail page layout**: Bean and grinder info cards display side by side.
- **Auto-close timer**: Only runs on post-shot auto-open. User-initiated opens
  (history E button) do not auto-close.
- **DB migration 10**: Added `channeling_detected` and `temperature_unstable`
  columns to `shots` table.
- **Legacy shot backfill**: Conductance, Darcy resistance, conductance derivative,
  phase summaries, and quality flags are computed on-the-fly when loading shots
  saved before this feature.
- **Theme colors**: 4 new chart colors (conductanceColor, conductanceDerivativeColor,
  darcyResistanceColor, temperatureMixColor).

### Files Changed

- `src/models/shotdatamodel.h/cpp` --- conductance, Darcy resistance, dC/dt vectors
- `src/history/shothistorystorage.h/cpp` --- migration, compress/decompress, derived
  curve backfill, phase summary computation
- `qml/Theme.qml` --- 4 new chart colors
- `qml/components/GraphLegend.qml` --- Row→Flow, 4 entries, advancedMode filtering
- `qml/components/HistoryShotGraph.qml` --- 4 new LineSeries
- `qml/components/ShotGraph.qml` --- 3 new FastLineRenderers
- `qml/components/QualityBadges.qml` --- new component
- `qml/components/PhaseSummaryPanel.qml` --- new component
- `qml/pages/PostShotReviewPage.qml` --- data bindings, badges, toggle, phase panel,
  TDS/EY advanced-only, auto-close fix
- `qml/pages/ShotDetailPage.qml` --- data bindings, badges, toggle, phase panel,
  side-by-side cards, analysis/debug advanced-only
- `qml/pages/ShotHistoryPage.qml` --- pass autoClose: false on manual open
- `CMakeLists.txt` --- 2 new QML files

---

## 1. Current State

### What the Shot Review Page Shows Today

**Graph curves** (6 total, via `GraphLegend.qml`):
1. Pressure (bar) --- on by default
2. Flow (mL/s) --- on by default
3. Temperature (°C) --- on by default
4. Weight (g) --- on by default
5. Weight flow rate (g/s) --- on by default
6. Resistance (P/F) --- **off by default**

**Goal overlays** (dashed lines): pressure goal, flow goal, temperature goal.

**Phase markers**: Frame boundaries with transition reason labels ([W]eight, [P]ressure,
[F]low, [T]ime). Pump mode indicators (flow vs. pressure mode) as color-coded bars.

**Metrics**: Duration, Dose, Output (with optional target yield), Ratio (1:X), Rating
(0--100%).

**Metadata**: Notes, TDS, EY, bean info (brand, type, roast date, roast level), grinder
info (brand, model, burrs, setting), barista, beverage type.

**Interactive elements**: Resizable graph, tap/drag crosshair inspection
(`GraphInspectBar`), right-axis toggle (Weight vs. Temperature), legend toggles,
shot navigation (swipe), rating slider, refractometer TDS reading.

### What's Computed But Not Shown

`ShotSummarizer` (`src/ai/shotsummarizer.h`) computes extensive per-shot metrics that
are only used for AI prompt generation, never displayed to the user:

**Per-phase metrics** (preinfusion, extraction, decline, etc.):
- `avgPressure`, `maxPressure`, `minPressure`
- `avgFlow`, `maxFlow`, `minFlow`
- `avgTemperature`, `tempStability` (std deviation from goal)
- `weightGained` (grams during this phase)
- `pressureAtStart`, `pressureAtMiddle`, `pressureAtEnd`
- `flowAtStart`, `flowAtMiddle`, `flowAtEnd`

**Anomaly flags**:
- `channelingDetected` --- sustained dC/dt elevation (>10 samples with
  `|dC/dt| > 3.0`) — puck integrity loss, works across all frame modes
- `temperatureUnstable` --- avg deviation from goal > 2°C

**Stored data series not displayed**:
- `m_temperatureMixPoints` --- mix temperature (group head thermal stability)
- `m_waterDispensedPoints` --- cumulative water volume (mL)
- `m_weightFlowRateRawPoints` --- pre-smoothing weight flow rate

---

## 2. What Visualizer.coffee and Peer Tools Show

### Visualizer.coffee

Source: [github.com/miharekar/visualizer](https://github.com/miharekar/visualizer)

**Additional graph curves** (hidden by default, toggled via legend):
- Resistance: `R = P / F²` (Darcy's law, not `P / F`)
- Conductance: `C = F² / P` (inverse of resistance)
- Conductance derivative: `dC/dt`, Gaussian smoothed --- the most diagnostic curve
- Mix temperature (alongside basket temperature)
- Water dispensed

**Exact formulas** (from `app/models/shot_chart/additional_charts.rb`):

```ruby
# Resistance: pressure / flow^2, clamped to 19
r = pressure / (flow ** 2)
v = r > 19 ? nil : r

# Conductance: flow^2 / pressure, clamped to 19
c = (flow ** 2) / pressure
v = c > 19 ? nil : c

# Conductance derivative: rate of change, scaled x10, Gaussian smoothed
derivative = ((c2 - c1) / ((t2 - t1) / 1000)) * 10
# Then 9-point Gaussian kernel:
GAUSSIAN_MULTIPLIERS = [0.048297, 0.08393, 0.124548, 0.157829, 0.170793,
                        0.157829, 0.124548, 0.08393, 0.048297]
# Clamped to [-5, 19]
```

**Additional features**:
- Weight at transition points (preinfusion dripping weight)
- Enjoyment over time chart (trend across shots)
- Tasting spider/radar chart (8 SCA dimensions, Premium 2026)
- Shot comparison with curve overlay
- Custom metadata fields (key-value, Premium)
- Shot tags for organization

### Smart Espresso Profiler (SEP)

- **Profile Drift Indicator (PDI)** --- single-number deviation from target profile
- **Reference shot overlay** --- display a previous shot's curves behind the current
  shot for visual replication
- Movable time cursor for precise value inspection

### GaggiMate MCP

- Resistance using `P / F²` (Darcy's law)
- Channeling risk scoring
- Per-phase profile compliance metrics with human-readable bands
  (MODERATE, STABLE, SLIGHT_OVERSHOOT)
- Physics-informed diagnostic heuristics

### Beanconqueror

- Average flow quantity as a summary metric
- Cupping by aromatics or flavors
- 30+ customizable brew parameters

### Key Diagnostic Patterns (Community Knowledge)

| Graph Pattern | Meaning | Adjustment |
|---------------|---------|------------|
| Smooth pressure ramp, stable plateau, gentle taper | Good extraction | None |
| Sudden flow spike + pressure dip | Channeling | Improve puck prep / WDT |
| Erratic pressure during extraction | Multiple channels | Grind finer, better distribution |
| Consistently high flow | Grind too coarse | Grind finer |
| Very low, trickling flow | Choked / too fine | Grind coarser |
| Flow increasing over time | Puck erosion (rate matters) | Monitor --- fast increase = problem |
| Conductance derivative spike then return | Transient channel (self-healed) | Minor issue |
| Conductance derivative sustained elevation | Persistent channeling | Major puck prep issue |

**Core insight**: "The pressure line reveals the machine's intent; the flow line shows
the coffee's response." The conductance derivative shows puck integrity.

---

## 3. Proposed Improvements

### Tier 1: High Impact, Data Already Exists

#### 1.1 Conductance and Conductance Derivative Curves ✅

**What**: Add three new toggleable curves to the graph legend: Conductance (`F²/P`),
Conductance Derivative (`dC/dt`), and optionally Darcy Resistance (`P/F²`).

**Why**: The conductance derivative is the single most diagnostic visual for puck
integrity. It reveals transient channeling events --- spikes where channels form and
heal --- that are invisible in the pressure, flow, and resistance curves. Visualizer
shows all three; the DE1 community considers `dC/dt` the most valuable diagnostic
metric available. Adding it to the on-device app means users get this feedback
immediately after pulling a shot, without uploading to Visualizer.

**Implementation**:
- Add `conductancePoints`, `conductanceDerivativePoints`, `darcyResistancePoints` to
  `ShotDataModel` as computed `QVector<QPointF>` series
- Compute in the existing `addSample()` loop (conductance/resistance) and a post-shot
  smoothing pass (derivative with 9-point Gaussian kernel)
- Persist in `shot_samples` compressed blob alongside existing resistance
- Add three entries to `GraphLegend.qml` (hidden by default):
  - "Conduct(F²/P)" --- conductance
  - "dC/dt" --- conductance derivative
  - "Resist(P/F²)" --- Darcy resistance (alternative to existing P/F)
- Add corresponding `FastLineRenderer` entries in `ShotGraph.qml` and
  `HistoryShotGraph.qml`
- Keep existing "Resist(P/F)" toggle for backward compatibility

**Resistance formula note**: Decenza currently uses `P / F` (Ohm's law analogy).
Visualizer and GaggiMate use `P / F²` (Darcy's law for laminar flow through porous
media). Neither is definitively "correct" --- they emphasize different aspects of
puck behavior. Offer both as toggleable curves rather than switching, so users
familiar with either convention can use what they know.

#### 1.2 Channeling and Quality Badges ✅

**What**: Show small visual indicators on the post-shot review and shot detail pages
when `ShotSummarizer` detects issues.

**Why**: `channelingDetected` and `temperatureUnstable` are already computed but only
sent to the AI. Surfacing them as visible badges gives users immediate feedback
without needing to ask the AI or interpret raw curves.

**Implementation**:
- Expose `ShotSummary` fields to QML (either via new Q_PROPERTY on a summary object,
  or by storing flags in shot metadata at save time)
- Display as colored chips below the graph or in the metrics row:
  - Red: "Channeling detected" (sustained dC/dt elevation — puck integrity loss)
  - Orange: "Temp unstable" (avg deviation > 2°C from goal)
  - Green: "Clean extraction" (neither flag triggered)
- On the shot detail page, show the same badges from stored flags
- Consider adding: "Pressure hit wall" (pressure at max limiter for > N seconds)
  and "Choked" (flow < 0.5 mL/s for > N seconds during extraction)

**Caution**: The current `temperatureUnstable` flag triggers on profiles with
intentional temperature stepping (e.g., D-Flow's 84°C→94°C). Before surfacing it,
make the flag recipe-aware: only flag instability when the temperature deviates from
the *goal curve*, not from a flat average. This is listed as Phase 0 item 4 in
`docs/CLAUDE_MD/AI_ADVISOR.md`.

#### 1.3 Phase Summary Panel ✅

**What**: Collapsible panel below the graph showing per-phase metrics in a compact
table.

**Why**: `ShotSummarizer` computes detailed per-phase statistics (avg pressure, flow,
temp, weight gained, duration) but none are shown. These metrics answer the questions
users most often ask: "How much dripped during preinfusion?" "What was my average
extraction pressure?" "How long was each phase?"

**Design**:
```
▼ Phase Summary
┌──────────────┬──────────┬───────────┬──────────┬─────────────┐
│ Phase        │ Duration │ Avg Press │ Avg Flow │ Weight      │
├──────────────┼──────────┼───────────┼──────────┼─────────────┤
│ Preinfusion  │ 8.2s     │ 3.1 bar   │ 1.2 mL/s │ 6.4g       │
│ Extraction   │ 22.1s    │ 7.8 bar   │ 1.7 mL/s │ 28.3g      │
│ Decline      │ 5.4s     │ 5.2 bar   │ 2.1 mL/s │ 8.9g       │
└──────────────┴──────────┴───────────┴──────────┴─────────────┘
```

The preinfusion weight gained is particularly important --- the Profile Knowledge Base
frequently references "X grams dripping before pressure rise" as the primary dial-in
signal for Blooming and lever profiles. Making this visible without AI transforms a
hidden diagnostic into an actionable metric.

**Implementation**:
- Compute phase summary at shot save time (extend `ShotSummarizer` or add a separate
  lightweight computation)
- Store as JSON in the shot record (or compute on-the-fly from phase markers + curves)
- QML: `Repeater` over phases, collapsed by default, expandable via tap

#### 1.4 Mix Temperature Curve ✅

**What**: Add mix temperature as a toggleable curve in the graph legend.

**Why**: Stored as `m_temperatureMixPoints` but never displayed. Mix temperature shows
the actual water temperature reaching the puck (vs. basket/thermocouple temperature).
The difference between basket and mix temp reveals thermal stability of the group
head. Visualizer shows both.

**Implementation**:
- Add "Mix temp" entry to `GraphLegend.qml` (hidden by default)
- Add `FastLineRenderer` entry sharing the right Y axis with existing temperature
- Use a distinct color (e.g., lighter shade of temperature color)

---

### Tier 2: Medium Impact, Small New Computation

#### 2.1 Shot-to-Shot Delta Card

**What**: When viewing a shot in history, show the delta from the previous shot on the
same profile.

**Why**: Users dial in iteratively --- change grind, pull a shot, evaluate. The most
valuable feedback is "what changed vs. last time." Currently users must mentally
compare two shots or navigate to the comparison page. A compact delta card makes
cause-and-effect visible at a glance.

**Design**:
```
vs. previous (10 min ago):
  Grind: 12 → 11 (-1)
  Pressure: +0.8 bar avg
  Flow: -0.3 mL/s avg
  Duration: +4.1s
  Enjoyment: +12%
```

**Implementation**:
- Query previous shot with same `profile_kb_id` from `ShotHistoryStorage`
- Compute deltas for key metrics (already done in `shots_compare` MCP tool)
- Display as a compact card below the metrics row, collapsed by default
- Color-code deltas (green = improvement direction, based on whether metric moved
  toward the profile's target)

#### 2.2 Goal Deviation Highlighting

**What**: Shade the region between actual and goal curves with a translucent fill when
deviation exceeds a threshold.

**Why**: The graph already draws goal lines (dashed), but users must mentally compare
two lines to spot deviations. Shading makes it visually obvious *where* and *how much*
the shot departed from the profile's intent. More informative than a single deviation
number (like SEP's Profile Drift Indicator) because it shows spatial context.

**Implementation**:
- For each controlled variable (pressure or flow, depending on the frame's mode),
  compute the difference between actual and goal at each sample point
- Where `|actual - goal| > threshold` (e.g., 0.5 bar for pressure, 0.3 mL/s for flow),
  draw a translucent polygon between the two curves
- Use warm color (red/orange) for over-target, cool color (blue) for under-target
- This requires a new `ShaderEffect` or Canvas element in the graph components

#### 2.3 Reference Shot Overlay

**What**: Let users pin a "reference" shot and overlay its curves as faint background
traces on the current shot's graph.

**Why**: Smart Espresso Profiler's most-praised feature. Users want to replicate their
best shots. Overlaying the reference shot's curves gives a visual target beyond the
profile's goal lines (which represent machine targets, not the user's best actual
performance). The comparison page already supports multi-shot overlay --- this extends
it to the single-shot view.

**Implementation**:
- Add a "Compare to..." button on the shot detail page
- Default reference: the user's highest-rated shot on the same profile
- Load reference shot data from `ShotHistoryStorage` into a second `ShotDataModel`
- Draw reference curves as faint (20--30% opacity), thinner lines behind the primary
  curves
- Store the reference shot ID as a user preference per profile

#### 2.4 Enjoyment Trend Sparkline

**What**: Small inline chart showing recent enjoyment ratings for this profile.

**Why**: Shows whether the user is improving, regressing, or stable. Visualizer has a
dedicated `EnjoymentChart` for this. On a mobile device, a compact sparkline
(last 10--20 shots) provides this at a glance without navigating to a separate page.

**Implementation**:
- Query recent shots with same `profile_kb_id`, extract enjoyment values
- Render as a minimal sparkline (no axes, just the trend line + current value dot)
- Place near the enjoyment rating on the post-shot review page
- Color the line based on trend direction (improving = green, declining = red)

---

### Tier 3: Nice-to-Have, More Work

#### 3.1 Puck Resistance Shape Classification

**What**: Classify the resistance curve shape and display a label: "Smooth decline"
(good), "Erratic" (channeling), "Flat then crash" (puck failure), "Rising" (fines
migration).

**Why**: Helps users who can't yet read resistance curves. But with the conductance
derivative (1.1) showing channeling visually, and the quality badges (1.2) flagging
detected issues, this becomes less necessary.

**Implementation**: Heuristics on the resistance time-series slope, variance, and
sudden changes. Low confidence in classification accuracy without community validation.

#### 3.2 Profile Compliance Score

**What**: Single percentage representing how closely the actual shot tracked the
profile's intended curves.

**Why**: GaggiMate MCP computes this. Gives an at-a-glance quality signal.

**Caution**: Misleading for deliberately adjusted shots (user changed temperature
mid-shot, or profile has wide tolerances). A "low compliance" shot may taste excellent.
Consider showing only when deviation is large enough to be meaningful.

#### 3.3 Tasting Radar Chart

**What**: Spider chart across sensory dimensions (fragrance, aroma, flavor, aftertaste,
acidity, bitterness, sweetness, mouthfeel).

**Why**: Visualizer Premium added this in 2026 using SCA CVA Descriptive methodology.
Enables rich shot-to-shot taste comparison.

**Caution**: Requires significant UI work, user education, and most users don't have
the vocabulary or training to rate 8 dimensions consistently. The simpler enjoyment
rating + free-form notes covers most use cases. Consider this only if there's
demonstrated user demand.

#### 3.4 Water Dispensed Curve

**What**: Add cumulative water volume (mL) as a toggleable curve.

**Why**: Stored as `m_waterDispensedPoints` but not displayed. Useful for understanding
dispersion and pre-wetting phases, but overlaps significantly with the weight curve
for most users.

---

## 4. What NOT to Adopt

- **Profile Drift Indicator as a single number (SEP)** --- A percentage deviation
  hides *where* the deviation occurred. Goal deviation highlighting (2.2) provides
  more useful spatial context.

- **Cupping-by-aromatics (Beanconqueror)** --- Too niche for a general-purpose app.
  The enjoyment rating + notes covers this.

- **Replacing `P/F` resistance with `P/F²`** --- The formula debate is unresolved in
  the community. Offer both as toggleable curves rather than switching and breaking
  existing users' expectations.

- **Custom metadata fields** --- Decenza already has comprehensive structured metadata
  (bean, grinder, notes, TDS, EY). Free-form key-value pairs add complexity without
  proportional value.

---

## 5. Implementation Priority

If doing only three things:

1. **Conductance derivative** (1.1) --- highest-value diagnostic curve, data exists,
   differentiates Decenza from every other on-device espresso app
2. **Quality badges** (1.2) --- surface existing `ShotSummarizer` flags, near-zero
   new computation
3. **Phase summary panel** (1.3) --- expose per-phase metrics that are already computed

These three give users the most shot-quality understanding for the least effort, and
none require AI interaction.

---

## 6. References

1. Visualizer.coffee source: https://github.com/miharekar/visualizer
2. Visualizer conductance/resistance: https://visualizer.coffee/updates/conductance-and-resistance
3. GaggiMate MCP: https://github.com/julianleopold/gaggimate-mcp
4. Smart Espresso Profiler: https://apps.apple.com/us/app/smart-espresso-profiler/id1391707089
5. Beanconqueror: https://beanconqueror.com/
6. Coffee ad Astra puck resistance study:
   https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/
7. Espresso Compass (Barista Hustle): https://www.baristahustle.com/the-espresso-compass/
