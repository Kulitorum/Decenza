# Universal Grind Setting (UGS) — Exploration Document

## Status: Exploratory / Discussion

This document describes the Universal Grind Setting (UGS) concept proposed for integration
into Decenza, evaluates its validity against published research, identifies limitations, and
explores alternative and hybrid approaches for helping users translate grind settings across
profiles and grinders.

---

## 1. The Original UGS Concept

**Source:** [videoblurb.com/UGS](https://videoblurb.com/UGS) and
[UGS Info](https://videoblurb.com/UGS/ugsinfo.html)

### Overview

UGS is a two-point linear calibration system that maps any grinder's dial settings onto a
universal 0--8 scale. Users pull two specific anchor shots on the DE1, record their grinder
settings, and the system interpolates grind recommendations for any of the 16 supported
profiles.

### Anchor Profiles

| Anchor | UGS Value | Profile Style | Targets |
|--------|-----------|---------------|---------|
| **Cremina** | 0 (finest) | High puck resistance, lever-style | 18 g in, 36 g out, ~45 s |
| **Rao Allonge** | 8 (coarsest) | Maximum flow, minimal resistance | ~4.5 mL/s, 4--6 bar, ~30 s |

### Calibration Method

1. Dial in the Cremina profile on your grinder --- record the setting as **anchor 0**.
2. Dial in the Rao Allonge profile --- record the setting as **anchor 8**.
3. **Grinder Span** = anchor 8 setting - anchor 0 setting.
4. **Conversion Key** = Grinder Span / 8.
5. For any profile at UGS value *u*: **Relative Grind Setting (RGS)** = anchor 0 + (Conversion Key * u).

The system includes an analog paper-ruler method and an online calculator.

### Strengths

- **Simple**: Two shots and you're calibrated.
- **Actionable**: Gives a specific dial number, not an abstract particle size.
- **DE1-native**: Uses the machine's controlled profiles as a behavioral ground truth.
- **Bean-change resilience**: If both anchors shift by the same amount with a new bean,
  the span and conversion key remain stable (in theory).

---

## 2. Evaluation Against Published Research

### 2.1 Linearity of Grinder Settings vs. Particle Size

UGS assumes a linear relationship between grinder dial setting and the behavioral
grind-size equivalent across the full 0--8 range.

**Evidence from Al-Shemmeri (2023):**
Calibration curves for multiple grinders (DF64/SSP, Fellow ODE, EK43, Niche Zero) show
that grinder setting vs. median particle size is **approximately linear in the
espresso-to-filter range**, but breaks down at extremes. The Niche Zero requires a
**power-law fit** at fine settings. Al-Shemmeri recommends **3--5 calibration points
minimum**; two-point calibration is explicitly called insufficient.

Representative calibration equations from measured data:
- DF64 (SSP burrs): median = 205 + 13.4 * setting
- Fellow ODE: median = 486 + 59.6 * setting
- EK43 (standard): median = 221 + 58.4 * setting
- Niche Zero: power law at fine end, linear at coarser settings

**Implication for UGS:** The linear assumption is the system's biggest structural weakness.
It's most accurate near the anchors and likely drifts by 1--2 clicks in the middle of the
range (UGS 3--5), which is where many popular profiles live.

> Reference: Al-Shemmeri, M. (2023). "Calibrating a Coffee Grinder."
> https://medium.com/@markalshemmeri/calibrating-a-coffee-grinder-ed55315c1390

### 2.2 Particle Size Distributions Vary Wildly Between Grinders

**Evidence from Gagne (2023):**
Analysis of 300 particle size distributions across 24 espresso grinders shows that at a
reference median of 340 microns, grinders differ substantially in fines fraction,
distribution width, and modality (bimodal vs. unimodal). Two grinders producing the same
median particle size can produce very different espresso.

Distributions are best modeled as **three overlapping log-normals** (fines, nominal,
boulders), with each component evolving independently across grind sizes. The fines peak
location shifts up to 8 microns between grinder models.

**Implication for UGS:** This actually supports UGS's behavioral approach --- particle size
alone doesn't predict extraction, so measuring behavioral equivalence (same shot
performance on a controlled profile) may be more useful than targeting a micron number.

> Reference: Gagne, J. (2023). "What I Learned from Analyzing 300 Particle Size
> Distributions for 24 Espresso Grinders." Coffee ad Astra.
> https://coffeeadastra.com/2023/09/21/what-i-learned-from-analyzing-300-particle-size-distributions-for-24-espresso-grinders/

### 2.3 Puck Resistance Is Nonlinear With Grind Size

**Evidence from Gagne (2020) and Corrochano et al. (2022):**
The relationship between grind size and puck resistance follows a **quadratic** (not linear)
relationship due to puck compression under pressure. Darcy's law predicts that flow rate
is proportional to pressure drop, but in compressible espresso pucks, flow scales with
roughly the **square root** of pressure. Equivalently, a puck twice as resistant requires
four times the pressure.

This means a 1-click grind change at the fine end (UGS 0--2) produces a much larger change
in extraction behavior than the same click change at the coarse end (UGS 6--8).

**Implication for UGS:** Even if grinder dial settings mapped perfectly linearly to
particle size, the *behavioral* response (what the shot actually does) would still be
nonlinear. UGS's equal-step interpolation systematically underestimates grind changes
needed at the coarse end and overestimates at the fine end.

> References:
> - Gagne, J. (2020). "An Espresso Profile that Adapts to your Grind Size." Coffee ad Astra.
>   https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/
> - Corrochano, B.R. et al. (2022). "Influence of particle size distribution on espresso
>   extraction via packed bed compression." Journal of Food Engineering.
>   https://www.sciencedirect.com/science/article/abs/pii/S0260877422003557

### 2.4 Puck Resistance Variability

**Evidence from Gagne (2021):**
Even with identical grind, dose, and beans, shot-to-shot puck resistance varies by ~5%
(comparable to adding/removing 1 g of dose). Puck preparation technique (WDT quality)
introduces up to **40% variation** in peak resistance.

**Implication for UGS:** The anchor shots have inherent noise. Two users with identical
grinders and beans could get different anchor settings due to preparation technique alone.
UGS should be understood as "approximately correct" rather than precise.

> Reference: Gagne, J. (2021). "A Study of Espresso Puck Resistance and How Puck
> Preparation Affects It." Coffee ad Astra.
> https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/

### 2.5 Other Particle Size Measurement Efforts

Several projects have built grinder particle size datasets using laser diffraction or
imaging, measuring actual micron distributions rather than behavioral equivalence:

- **Coffee Grind Lab** --- Shimadzu laser diffraction analyzer, public dataset.
  https://coffeegrindlab.com/
- **Titan Grinder Project** --- Community effort on Home-Barista to characterize grinder PSDs.
  https://www.home-barista.com/blog/titan-grinder-project-particle-size-distributions-ground-coffee-t4203.html
- **Bettersize** --- Coffee particle analysis methodology.
  https://www.bettersizeinstruments.com/learn/knowledge-center/coffee-particle-size-analysis/

These are complementary to UGS: they measure physical reality (microns), while UGS measures
behavioral equivalence (what grind setting produces a good shot on a given profile).

---

## 3. Adoption Friction

Feedback from a prospective user highlights a key concern:

> *"I am a typical DE-1 user... the vast majority of my shot-affecting settings would be
> completely inaccurate. Preset chosen, grind setting, beans used, quantity of beans, and
> even the fact I had changed baskets a few times... so any conclusion based on all that
> would be completely wrong."*

Two distinct friction problems:

1. **UGS calibration friction**: Users must deliberately pull two profiles they may not
   care about (Cremina and Rao Allonge), dial them in correctly, and record settings.
   Users who only drink one style of espresso have no intrinsic motivation to do this.

2. **Shot metadata quality**: Any approach that relies on user-entered grinder settings,
   bean info, or dose weights will be working with mostly inaccurate data. Users only
   maintain accurate metadata when they have a specific reason (e.g., uploading to
   Visualizer, rating a shot they want to remember).

The same user suggested that **rated shots are a reliable quality signal** --- users who
bother to rate a shot have typically also entered correct metadata. This could serve as a
data quality filter.

---

## 4. Alternative Approaches

### 4.1 Telemetry-Only Resistance Profiling (Zero Friction)

**Core idea:** Skip grinder dial numbers entirely. Use machine-measured telemetry
(pressure, flow, weight) to characterize each shot's puck resistance, and use that as the
common language between profiles.

**What we already capture per shot (always accurate, machine-measured):**

| Field | Source | Always Reliable |
|-------|--------|----------------|
| `pressure` time-series (bar) | DE1 sensor | Yes |
| `flow` time-series (mL/s) | DE1 pump model | Yes |
| `resistance` time-series (bar*s/mL) | Derived: P/F | Yes |
| `weight` time-series (g) | BLE scale | Yes (if connected) |
| `profile_name` + `profile_json` | Auto-recorded | Yes |
| Phase markers + transition reasons | Auto-recorded | Yes |
| `grinder_setting` | User-entered | **No** |
| `bean_brand`, `bean_type` | User-entered | **No** |
| `dose_weight` | User-entered / scale | Sometimes |

**Implementation:**

1. **Extract a characteristic resistance per shot.** Use the median resistance during the
   pouring phase (after preinfusion, before ending), isolated using the existing phase
   markers. Avoid the first few seconds (unstable) and any channeling artifacts.

2. **Build per-profile resistance distributions.** Over time, each profile accumulates a
   distribution of observed resistance values from the user's shots. No grinder metadata
   needed.

3. **When switching profiles, show the resistance delta.** If the user's D-Flow shots
   average 3.2 resistance and Blooming Espresso expects ~5.1, tell them: "Grind finer ---
   your current grind is too coarse for this profile."

4. **Show a live resistance target band.** On the shot graph, overlay the expected
   resistance range for the active profile. The user gets real-time feedback on whether
   their grind is in the right zone.

**Advantages:**
- Zero adoption friction --- works from the user's second shot onward
- Uses only machine-measured data (no stale metadata problem)
- No assumptions about grinder linearity
- Learns the user's actual grind behavior per profile

**Limitations:**
- Gives **direction** ("grind finer") but not **magnitude** ("by 3 clicks")
- Resistance depends on dose, temperature, and puck prep, not just grind
- Different profiles apply different pressures, so resistance values need normalization
  (see Section 4.4)

### 4.2 Opportunistic Grinder Calibration (Low Friction)

**Core idea:** Don't ask users for grinder data upfront. Instead, capture it when they
voluntarily provide it and gradually build a calibration curve.

**Implementation:**

1. When a user updates `grinder_setting` in shot metadata, anchor that setting to the
   shot's observed resistance.
2. Use the **rated-shots-only filter** --- if a shot has a rating, trust its metadata.
3. Over time, accumulate (grinder_setting, resistance) pairs per grinder model.
4. With 3+ data points at distinct settings, fit the user's actual calibration curve
   (no linearity assumption needed --- use the shape the data gives you).
5. Once the curve exists, translate resistance targets into dial number recommendations.

**Advantages:**
- No dedicated calibration shots required
- Accuracy improves organically over time
- Captures actual grinder nonlinearity from real data
- Rated-shot filter addresses metadata quality concern

**Limitations:**
- Requires patience --- may take weeks/months to accumulate enough data points
- Users who rarely change grind settings may never get enough spread
- Users who only pull one style of shot (lever-only, turbo-only) will have a narrow
  calibration range --- cannot extrapolate to distant profiles

### 4.3 Single Diagnostic Sweep Shot

**Core idea:** Instead of two separate anchor shots, design a single DE1 profile that
sweeps through pressure levels, extracting a complete puck permeability curve from one shot.

**Profile design:**
1. 3 bar hold for 10 s (low-pressure flow data)
2. Ramp to 6 bar, hold for 10 s (mid-range)
3. Ramp to 9 bar, hold for 10 s (full-pressure compression behavior)

**From one ~30-second shot, you get:**
- Flow rate at three pressure levels
- Puck compression behavior (how resistance changes with pressure)
- Enough data to characterize both the grind size and the grinder's distribution type
  (unimodal burrs compress differently than bimodal)

**Advantages:**
- One shot instead of two
- Richer data than UGS (continuous curve, not two points)
- Still produces a drinkable (if unremarkable) espresso
- The pressure-dependent resistance curve is a fingerprint of grind + grinder type

**Limitations:**
- Still requires a deliberate calibration shot (same adoption friction as UGS)
- Non-standard profile --- users may find it odd
- **Puck erosion is a fundamental confounder, not just noise.** The puck at 9 bar after
  20 seconds of extraction at lower pressures is physically different from a puck that
  starts at 9 bar --- fines have migrated, channels have formed, soluble material has
  been extracted. The permeability curve you measure is an artifact of the measurement
  sequence, not a stable grinder fingerprint. This cannot be corrected for.
- Bean-dependent (same as UGS)
- The data it produces is inferior to what 2--3 real shots with post-shot telemetry
  analysis would provide, while being less enjoyable to drink ("drinkable if unremarkable"
  means wasting good beans on a shot the user won't enjoy)

### 4.4 Cross-Profile Resistance Normalization

Any approach that compares resistance across different profiles must account for the
fact that **resistance depends on applied pressure** (puck compression is
pressure-dependent). Options:

**Option A: Same-profile comparison only.** Only compare shots that used the same profile.
Simplest, but severely limits data points.

**Option B: Normalize by pressure.** Divide resistance by the target pressure at the
measurement point, yielding a "permeability-like" metric: `permeability = flow / pressure`.
Darcy's law predicts this should be roughly constant for a given grind before compression
nonlinearity dominates. Practical for moderate pressure differences (6--9 bar); less
reliable for extreme ranges (2 bar vs. 9 bar).

**Option C: Reference-pressure flow.** Find the moment in each shot where pressure
crosses a reference level (e.g., 6 bar during ramp-up) and compare flow rates at that
instant. Most physically grounded, but requires shots that pass through the reference
pressure and have stable flow at that point.

**Option D: Preinfusion flow.** Many profiles include a low-pressure preinfusion phase
(2--4 bar). Flow during preinfusion is less affected by puck compression and may be a
more stable cross-profile comparison metric. Available for most profiles except those
that skip preinfusion.

**Assessment:** All four options face a fundamental challenge: puck compression is
nonlinear and pressure-dependent (Section 2.3), and puck prep introduces up to 40%
resistance variation (Section 2.4). Cross-profile resistance normalization may not
converge on a reliable metric regardless of method. Option D (preinfusion flow) is
the most promising because it operates in the low-pressure regime where compression
effects are smallest, but many profiles have different preinfusion pressures or skip
it entirely. **Recommendation:** Start with same-profile comparison only (Option A)
and treat cross-profile normalization as a research question, not a prerequisite for
the dial-in assistant.

---

## 5. Proposed Hybrid Architecture

A tiered approach that serves all user types without requiring upfront commitment:

### Tier 1: Passive Resistance Guidance (Zero Friction)

- Available from the user's second shot on a profile.
- Computes and stores steady-state resistance for every shot (already implemented in
  `ShotDataModel`).
- Shows a target resistance band on the live shot graph for the active profile.
- When switching profiles, reports the expected resistance shift and direction to adjust.
- **Does not require any user-entered metadata.**

### Tier 2: Opportunistic Calibration (Low Friction)

- Activates when the user has 3+ rated shots at distinct grinder settings on the same
  grinder.
- Builds a grinder-specific calibration curve from observed (setting, resistance) pairs.
- Enables magnitude recommendations: "grind 2 clicks finer."
- Accuracy improves silently over time.

### Tier 3: UGS / Diagnostic Calibration (Opt-in, Low Priority)

- For users who want immediate, precise dial numbers across all profiles.
- Supports the original UGS two-anchor method (the diagnostic sweep has fundamental
  puck-erosion problems --- see Section 4.3).
- Provides instant full-range calibration at the cost of pulling 2 non-standard shots.
- Results can be validated against Tier 2 data as it accumulates.
- **Assessment:** With the KB + Telemetry approach (Section 7) providing immediate,
  zero-friction guidance, few users will need this tier. The primary value of UGS data
  is the relative profile ordering (Section 7), not the calibration method itself.

### Convergence

When both Tier 2 and Tier 3 data exist, the system can compare predictions and show the
user where their grinder deviates from the UGS linear assumption. This turns a static
calibration into a living model that improves with use.

---

## 6. Narrow Range Problem

A user who exclusively pulls lever-style shots (UGS 0--2) will have:
- **Tier 1**: Full functionality --- resistance guidance for the profiles they actually use.
- **Tier 2**: A calibration curve only in the fine range. Cannot extrapolate to turbo shots.
- **Tier 3**: Full range (if they complete calibration).

This is acceptable because **Tier 1 doesn't need range** --- it only requires data on
profiles the user actually uses. The narrow range only matters if the user wants to predict
grind settings for a dramatically different profile style, which is the scenario UGS was
designed for and where the opt-in Tier 3 calibration is appropriate.

---

## 7. KB-Driven Dial-In Assistant (Preferred Approach)

### Core Idea

Rather than calibrating grinders with anchor shots (UGS) or waiting for passive data to
accumulate (Tier 1/2), leverage the **existing Profile Knowledge Base** combined with
**real-time shot telemetry** to guide users when switching profiles. The KB already
contains rich, curated dialing-in knowledge per profile — what "dialed in" looks like,
relative grind ordering, expected pressure/flow signatures, and failure modes. The DE1
already measures everything needed to evaluate those signatures in real time.

No calibration shots. No grinder metadata. No waiting. The MCP reads the profile's KB
entry, watches the shot telemetry, and tells the user whether they're in the zone.

### What the Knowledge Base Already Provides

The Profile Knowledge Base (`docs/PROFILE_KNOWLEDGE_BASE.md`) documents ~15-20 major
profiles with:

**1. Relative grind ordering across profiles:**

```
FINEST ←————————————————————————————————————→ COARSEST

Blooming    Cremina     D-Flow      G&S         E61       Allonge
Allonge     Londinium   Adaptive    Extramundo            Rao Allonge
            Best Ovrl   Flow Prof
            80's (slightly coarser)
```

Sources: `[SRC:dark-video]` `[SRC:medium-video]` `[SRC:light-video]` `[SRC:eaf-profiling]`
from the KB. Specific documented relationships include:
- Cremina, Londinium, Best Overall all use the **same fine grind** `[SRC:dark-video]`
- 80's Espresso is **slightly coarser** than the lever group `[SRC:dark-video]`
- E61 is the **coarsest dark-roast profile** `[SRC:dark-video]`
- Allonge is the **coarsest of all** (e.g., 1.8 on test grinder vs 0.5 for Blooming)
  `[SRC:eaf-profiling]` `[SRC:light-video]`
- Adaptive grinds **coarser than Londinium** for same bean `[SRC:light-video]`
  `[SRC:medium-video]`
- Extractamundo needs **slightly finer** than G&S to match 6 bar target `[SRC:light-video]`

**2. Dialed-in telemetry signatures per profile:**

| Profile | Dialed-In Signature | Failure Modes |
|---------|-------------------|---------------|
| **Blooming** | Pressure 6--9 bar during extraction; 6--8g dripping before pressure rise; water on puck bottom immediately after fill | Pressure hits max wall → too fine (shot will be muted); pressure only ~4 bar → too coarse; takes 3--4s for water to appear → too fine |
| **Londinium/LRv3** | 2--8g dripping during soak before pressure rise; pressure peaks ~9 bar then stays high; flow stays constant | Dripping >17g → too coarse; pressure crashes → puck eroded (LRv2 handles this) |
| **D-Flow** | Linear extraction line on flow chart; flow limit 1.7 ml/s kicks in during pour; pressure rises after dripping target met | Non-linear flow = channeling; pressure elbow = extraction done |
| **Adaptive v2** | Pressure peaks near 9 bar then gradually declines; flow 2.0--2.7 ml/s | Pressure rising again during extraction = fines migration |
| **G&S** | Constant 6 bar; flow starts ~2 ml/s, increases to 3+ as puck erodes; fast shot | Shot >30s with light roast = overextracted |
| **Allonge** | Flow ~4.5 ml/s; pressure 4--6 bar; ~30s shot | Pressure hits max → too fine; some channeling is normal and expected |
| **Cremina** | Pressure rises to peak then declines steeply; flow rate drops as shot progresses; ~45s shot | — |
| **E61** | Flat 9 bar; flow increases as puck erodes; 22--25s shot | Shot too fast = too coarse |
| **80's Espresso** | Flow ideally 1--1.2 ml/s (slow); low temp 80C declining | Flow ~1.9 ml/s = too fast, thin/dusty result |

Sources: All from `docs/PROFILE_KNOWLEDGE_BASE.md` with original citations preserved.

**3. Roast-to-profile compatibility matrix** — which profiles work with which roast levels,
helping the MCP recommend appropriate profiles before grind adjustment is even needed.

### How the MCP Dial-In Flow Works

**Scenario**: User has been pulling D-Flow / Q daily. Wants to try Blooming Espresso.

**Step 1 — Profile switch intent.** User asks the MCP: "I want to try Blooming Espresso"
or simply loads the profile.

**Step 2 — KB-informed grind guidance.** MCP reads the KB and responds:

> *"Blooming Espresso needs a significantly finer grind than D-Flow. It's in the finest
> group along with Cremina and Londinium. When dialed in, you should see:*
> - *Water appearing on the bottom of the puck almost immediately after fill*
> - *6--8g dripping on the scale before the pressure rise begins*
> - *Extraction pressure between 6--9 bar — never hitting the max wall*
>
> *This is the hardest profile to dial in. Plan on 3+ attempts. If you only have 250g of
> beans, consider starting with Extractamundo Dos instead (similar grind to your current
> D-Flow, much easier to dial in for light roasts)."*

No resistance numbers needed. The guidance is qualitative and profile-specific.

**Step 3 — Post-shot telemetry feedback.** After the first attempt, MCP reads the shot
data and evaluates against the dialed-in signature:

> *"Your Blooming shot hit 9.8 bar — pressure hit the max wall for 6 seconds. This means
> you're a touch too fine. Open up half a click. Also, dripping was only 3g before pressure
> rise — ideally you want 6--8g. The combination suggests grinding slightly coarser will
> fix both issues."*

Or conversely:

> *"Pressure only reached 4.2 bar — the KB says this means you're slightly too coarse.
> The shot is still drinkable but not optimal. Try half a click finer."*

**Step 4 — Iterative refinement.** Each subsequent shot gets evaluated against the same
signature. After 2--3 shots, the user converges on the right grind. The MCP can also
compare against the user's own best shots on this profile once they have rated ones.

### What Already Exists (Infrastructure Audit)

The infrastructure for the KB + Telemetry approach is substantially further along than
originally estimated. A detailed audit reveals:

**`dialing_get_context` MCP tool** (`src/mcp/mcptools_dialing.cpp`): This single tool
already aggregates everything an AI needs for dial-in assistance in one call:
- Current shot summary (dose, yield, duration, enjoyment, ratio)
- Dial-in history (last N shots on the same profile family via `profile_kb_id`)
- Grinder context (observed settings range, min/max, step size, numeric vs. alphanumeric)
- Current bean metadata (brand, roast date, calculated bean age)
- Current profile info (target weight, temperature, recommended dose)
- AI-generated shot analysis (from `ShotSummarizer`)
- Full Profile Knowledge Base content injected as reference
- Multi-variable dial-in reference tables (`docs/ESPRESSO_DIAL_IN_REFERENCE.md`)

**`ShotSummarizer`** (`src/ai/shotsummarizer.h/cpp`): Computes per-phase metrics at save
time including:
- Per-phase: avgPressure, maxPressure, minPressure, avgFlow, maxFlow, minFlow,
  avgTemperature, tempStability (std deviation), weightGained
- Sampled at: start, peak-deviation, and end of each phase
- Target comparison: actual temps vs. `tempGoalCurve` deviation
- Anomaly detection: `channelingDetected` (sustained dC/dt elevation — puck
  integrity loss), `temperatureUnstable` (avg deviation > 2°C)

**Resistance calculation** (`src/models/shotdatamodel.cpp:233`): Already computed and
persisted per shot using `R = P / F` (DSx2 formula), clamped to 15.0, flow-gated at
> 0.05 mL/s. Stored in the `shot_samples` table as a compressed time-series.

**Phase detection** (`src/controllers/maincontroller.cpp:1758`): Frame-number-based with
transition reason inference (weight exit, pressure exit, flow exit, time exit). Phase
markers stored in the `shot_phases` table with frame number, label, flow mode flag,
and transition reason.

**Profile KB system**: 29 of 93 profiles (31%) have `knowledge_base_id` fields linking
to `docs/PROFILE_KNOWLEDGE_BASE.md`. Three-tier matching: direct KB ID → fuzzy match →
editor type fallback. Profiles with KB entries show a sparkle icon in the profile
selector. Shot history is queryable by `profile_kb_id` via `getRecentShotsByKbId()`.

**Shot comparison** (`shots_compare` MCP tool): Compares 2--10 shots side-by-side with
calculated deltas between consecutive shots (grind, dose, yield, duration, enjoyment).

### What Remains to Be Built

Given the existing infrastructure, the remaining work is smaller than originally scoped:

1. **Cross-profile grind direction mapping.** A lookup table encoding the relative grind
   ordering so the AI can say "grind much finer" or "grind slightly coarser" when
   switching profiles, even without resistance data. Seed from the KB's documented
   relationships (Section 7 grind ordering table) and from the UGS profile-to-grind
   mapping (16 profiles across UGS 0--8). This is a simple data artifact, not an
   engineering project.

2. **Conductance derivative metric.** Complement the existing `R = P / F` resistance
   with conductance `C = F² / P` and its time derivative `dC/dt`. Championed by Collin
   Arneson and now displayed on Visualizer.coffee, the conductance derivative reveals
   transient channeling events (spikes where channels form and heal) that are invisible
   in the resistance curve. Recipes producing linear puck degradation (smooth `dC/dt`)
   tend to produce better espresso. The raw data (pressure and flow time-series) already
   exists --- this is a derived calculation added to `ShotDataModel`.

3. **Keep the KB qualitative --- do not formalize into rigid JSON rules.** The original
   proposal included structured JSON rules with hard thresholds (e.g.,
   `"extractionPressure": { "min": 6.0, "max": 9.0 }`). This is the wrong approach for
   three reasons:
   - **Threshold brittleness**: Coffee extraction is too variable for rigid rules. A
     good Blooming shot at 5.8 bar would trigger a false "too low" alert. The KB already
     documents a case where it said "8--9 bar" but the actual profile allows 6--9 bar,
     causing the AI to give wrong advice.
   - **Maintenance burden**: 93 profiles × multiple rules × evolving profile behavior =
     rules that drift out of sync with reality (already observed with D-Flow).
   - **The LLM already handles this better.** The MCP/LLM interprets qualitative KB
     guidance in context: "pressure was 5.8 bar, slightly below the typical 6--9 range
     but your flow looks good, so this is fine." This nuanced judgment is exactly what
     LLMs excel at and what rigid thresholds cannot do.

   Instead, invest in **enriching the qualitative KB** with more profile entries and
   keeping existing entries accurate. The LLM + `dialing_get_context` + KB prose is the
   right architecture.

4. **Per-user resistance baselines (not community).** As users accumulate shots on each
   profile, track per-profile resistance distributions (mean, IQR) for that user. This
   enables the AI to say "your resistance was 3.2, but your good D-Flow shots average
   5.1 --- grind finer." **Do not aggregate across users** --- with 40% resistance
   variation from puck prep alone (Section 2.4), plus differences in water chemistry,
   temperature, dose weight, and basket type, community-aggregated distributions will
   have enormous variance. Per-user baselines implicitly control for that user's equipment
   and technique, giving a much higher signal-to-noise ratio.

### Where UGS Data Contributes

The UGS system's most valuable artifact is not the calibration method — it's the
**profile-to-grind mapping table** that places 16 profiles on a 0--8 scale. This table
can seed the cross-profile grind direction mapping (item 3 above) with approximate
quantitative ratios rather than just "finer/coarser" directions.

For example, if UGS places D-Flow at ~3 and Blooming at ~1, the MCP knows Blooming needs
roughly 25% of the UGS range finer — a meaningful magnitude hint even without absolute
grinder calibration. Combined with the post-shot telemetry feedback loop, this gets the
user to the right grind faster than qualitative guidance alone.

### Advantages Over Pure UGS or Pure Telemetry

| | UGS | Passive Telemetry | KB + Telemetry |
|---|---|---|---|
| **Friction** | Pull 2 calibration shots | None (but slow to build data) | None |
| **Time to first recommendation** | Immediate after calibration | After accumulating data | Immediate (from KB) |
| **Recommendation quality** | "Set dial to 4.7" (may be wrong due to linearity) | "Grind finer" (no magnitude) | "Grind much finer, here's what to look for, here's what went wrong" |
| **Post-shot feedback** | None | Limited | Rich, profile-specific |
| **Profiles covered** | 16 mapped profiles | Only profiles user has pulled | All profiles with KB entries |
| **Handles failure modes** | No | No | Yes — "pressure hit the wall, you're too fine" |
| **Improves with use** | Static | Yes (slowly) | Yes (resistance baselines grow) |

### Example MCP Interaction Transcript

```
User: I've been making D-Flow shots. I want to try something different for
      this bag of Ethiopian natural.

MCP:  Ethiopian naturals are fruity and light — great candidates for several
      profiles:

      1. **Allonge** (easiest to dial in for light roasts) — grind MUCH coarser
         than your D-Flow. UGS 8 vs your current ~3. Expect 4.5 ml/s flow,
         4-6 bar, ~30s. Best with "crazy fruity" naturals.

      2. **Extractamundo Dos** (similar grind to D-Flow) — minimal adjustment
         needed. 6 bar constant, fast shot. Good if you don't have a lot of
         beans.

      3. **Blooming Espresso** (grind much finer) — highest extraction yield
         (23-24%), most complex. BUT hardest to dial in — need 3+ attempts
         and at least 3kg of beans.

      I'd suggest Allonge for this natural. Want to try it?

User: Let's try Allonge.

MCP:  Loaded. Your D-Flow grind is around setting 9 on your Niche Zero.
      Allonge needs the coarsest grind of any espresso profile. Open up
      significantly — try around 15-17 as a starting point.

      When dialed in, you should see:
      - Flow around 4.5 ml/s
      - Pressure 4-6 bar (should not hit max)
      - ~30 second shot
      - Some channeling is normal — don't worry about it

      Pull a shot and I'll check the telemetry.

User: [pulls shot]

MCP:  Looking at your shot data:
      - Flow peaked at 2.8 ml/s (target ~4.5) — too slow
      - Pressure hit 8.2 bar (target 4-6) — too high
      - Duration 41s (target ~30s)

      All signs point to grind being too fine still. The puck is resisting
      too much. Open up 2-3 more clicks and try again.

User: [adjusts to 19, pulls shot]

MCP:  Much better:
      - Flow 4.1 ml/s (close to 4.5 target)
      - Pressure 5.4 bar (in the 4-6 range ✓)
      - Duration 32s (close to 30s target ✓)

      You're dialed in. The slight channeling visible in the flow curve is
      normal for Allonge — don't try to fix it by grinding finer. How does
      it taste?
```

### Scaling to All 93 Profiles

The approach works best for profiles with KB entries (~15--20 today). For the remaining
~73 profiles without curated KB data, fallback strategies:

1. **Editor type heuristic**: Profiles share editor types (pressure, flow, dflow, aflow,
   advanced). Profiles of the same editor type share general behavioral characteristics.
   A new "pressure" type profile probably behaves similarly to other pressure profiles
   for grind purposes.

2. **Frame analysis**: The MCP can read profile frames (`profiles_get_detail`) and infer
   expected behavior from the pressure/flow targets in each frame. A profile with
   9 bar constant pressure and no preinfusion behaves like E61 regardless of its name.

3. **Community contribution**: As the KB grows, users and the community can contribute
   dialing-in data for less common profiles via qualitative prose entries.

---

## 8. External Landscape (2024--2026)

Several parallel efforts address the same problem space:

### Closed-Loop Grind Systems

**Mahlkonig Grind-by-Sync (2024--2025):** The only production closed-loop grind
adjustment system. The espresso machine records brew time and sends it to the grinder
via WiFi. If brew time deviates from target, the grinder automatically adjusts burr
distance. Compatible with La Marzocco machines; the E64 WS (2025) brought this to the
home market. This is a simple time-based heuristic, not telemetry-curve analysis, but
it's the only shipping product that automatically corrects grind.

**Nunc (2025):** A German startup (debuting at IFA Berlin 2025) with an integrated
espresso machine + grinder that uses AI to adjust grind size, dose, and RPM in
real-time. Claims one test shot to dial in any bean. Fully closed system, not an open
platform.

### AI-Assisted Shot Analysis

**GaggiMate MCP Server (open-source, 2025):** The most directly comparable
implementation. An MCP server for GaggiMate (Gaggia mod) firmware with 9 tools and 8
resources. Its `analyze_shot` tool performs physics-informed diagnostics including:
puck resistance modeling (`R = P / F²`), channeling risk scoring, temperature deviation
tracking, pressure/flow stability analysis, and per-phase profile compliance metrics.
Creates a feedback loop: user pulls shot → provides tasting notes (sour/balanced/bitter)
→ AI correlates taste with telemetry → suggests adjustments. Notably uses `P / F²`
(Darcy's law) rather than `P / F` for resistance, and includes human-readable band
annotations (MODERATE, STABLE, SLIGHT_OVERSHOOT).

**Key gap in the ecosystem:** No existing tool --- including Visualizer.coffee,
Beanconqueror, or the GaggiMate MCP --- provides profile-specific "here's what dialed-in
looks like" guidance combined with post-shot telemetry evaluation. This is where
Decenza's KB + Telemetry approach (Section 7) fills a unique gap: the Profile Knowledge
Base with curated per-profile signatures has no equivalent in other tools.

### Diagnostic Metrics

**Conductance derivative (`dC/dt`):** Championed by Collin Arneson and now displayed on
Visualizer.coffee alongside resistance and conductance. Where resistance (`P / F`)
shows overall puck state, the derivative of conductance (`F² / P`) reveals *transient*
channeling events --- spikes where channels form and heal that are invisible in
smoothed resistance curves. Recipes producing linear puck degradation (smooth `dC/dt`)
correlate with better espresso. This metric should be added to Decenza's
`ShotDataModel` (see Section 7, "What Remains to Be Built", item 2).

**Resistance formula debate:** Three formulations are in active use:
- `R = P / F` --- Decenza's current formula (DSx2 / Ohm's law analogy)
- `R = P / F²` --- GaggiMate's formula (closer to Darcy's law for laminar flow
  through porous media)
- `R = √P / F` --- Coffee ad Astra formulation

No community consensus exists. `P / F²` has the strongest theoretical grounding for
porous-media flow, but `P / F` is simpler and more widely used. Worth evaluating
whether switching or offering both would improve diagnostic quality.

---

## 9. AI Integration Paths

Decenza has two distinct AI integration paths that can both serve the dial-in assistant.
Both share the same underlying `ShotSummarizer` and Profile Knowledge Base, but serve
different user segments and use cases.

### In-App AI Advisor

The built-in AI advisor (`src/ai/`) runs inside the app with 5 pluggable providers
(Anthropic Claude Sonnet 4.6, OpenAI GPT-4.1, Google Gemini 2.5 Flash, OpenRouter,
Ollama for local models). It supports multi-turn conversations with persistent storage
(up to 5 conversation slots keyed by bean + profile), dial-in history injection (last
5 same-profile shots), and provider-specific prompt caching (Anthropic: ~90% discount
on cached system prompts; OpenAI: ~50% automatic).

**What the in-app AI already does for dial-in:**
- Analyzes shots with full telemetry curves (pressure, flow, temp) via `ShotSummarizer`
- Per-phase breakdown with actual vs. target values at start/peak-deviation/end
- Channeling detection (sustained dC/dt elevation — conductance derivative)
- Temperature stability assessment (std deviation vs. target)
- Profile-specific knowledge injection (31 stock profiles with KB IDs)
- Grinder/burr awareness (when user enters structured grinder data)
- Bean knowledge (origin, variety, processing, roaster recognition)
- Flavor problem diagnosis (sour, bitter, hollow, etc.)
- Multi-turn dial-in conversations tracking shot-to-shot progression

**Strengths for dial-in:**
- Available on-device during the shot workflow (pull shot → open advisor → get feedback)
- Live access to `ShotDataModel` with full curves (not just stored data)
- No external tool required (Claude Desktop, IDE, etc.)
- Can be surfaced proactively (e.g., post-shot notification)
- Lower barrier to entry for non-technical users
- Provider-specific caching reduces repeat-query costs

**Current gaps (from `docs/AI_ADVISOR.md` roadmap):**
- No recipe-aware interpretation rules yet (AI can't derive expected behavior from
  profile recipe --- caused incorrect D-Flow advice in March 2026)
- Dial-in reference tables (`docs/ESPRESSO_DIAL_IN_REFERENCE.md`) collected but not yet
  integrated into the system prompt
- Limited cross-profile awareness (AI only knows current profile, can't recommend
  switching)
- No bean enrichment (Visualizer coffee_bags API, Loffee Labs Bean Base --- Phase 2)
- Anomaly flags are simplistic (`temperatureUnstable` triggers on profiles with
  intentional temperature stepping)

### MCP Server

The MCP server (`src/mcp/`) exposes Decenza's full functionality to external AI agents
(Claude Desktop, IDE integrations, custom agents). `dialing_get_context` is the primary
dial-in tool, returning comprehensive structured JSON in a single call.

**What `dialing_get_context` provides that the in-app AI doesn't (yet):**
- Full `ESPRESSO_DIAL_IN_REFERENCE.md` reference tables (roast→temp→flow→flavor)
- Full `PROFILE_KNOWLEDGE_BASE.md` as raw text (not just the matched profile)
- Grinder context with observed settings range, min/max, step size analysis
- Bean age calculated from roast date
- Structured JSON output that external tools can post-process

**Strengths for dial-in:**
- Richer context (full KB + reference tables + grinder context in one call)
- External LLM has larger context window and stronger reasoning
- Can integrate with other tools (web search, external databases, user notes)
- Power users can build custom workflows and agents

**Limitations:**
- Requires MCP-compatible client setup (higher barrier to entry)
- Historical shot data only (no live `ShotDataModel` access)
- Not integrated into the shot workflow UI

### How Both Paths Converge

| Aspect | In-App AI | MCP Server |
|--------|-----------|------------|
| **User type** | All users | Power users / developers |
| **Trigger** | After-shot in app | On-demand from external tool |
| **Shot data** | Live curves from `ShotDataModel` | Historical from database |
| **Dial-in history** | Last 5 same-profile shots in prompt | Same, as structured JSON |
| **Reference tables** | Not yet integrated (Phase 1) | Included in response |
| **Cross-profile** | Current profile only | Full KB available |
| **Conversation** | Multi-turn, 5 persistent slots | Single-call (client manages turns) |
| **Cost** | Per-query API cost | External tool handles cost |

### Recommendation

Both paths should support dial-in assistance. The in-app advisor is the primary path
for most users (lower friction, live shot access, available during the brew workflow).
The MCP path serves power users who want deeper analysis or custom integrations.

**Closing the gap:** The highest-impact improvement for the in-app AI is completing the
Phase 0 roadmap items from `docs/AI_ADVISOR.md` --- particularly recipe-aware
interpretation rules and integrating the dial-in reference tables. These are the main
areas where the MCP currently provides better guidance than the in-app advisor.

Implementation work on the data layer (conductance derivative, grind ordering table,
per-user resistance baselines) benefits both paths equally since both consume the same
`ShotSummarizer` output and KB content.

---

## 10. Open Questions

### KB + Telemetry Approach (Section 7)

1. **Dripping weight detection**: The KB frequently references "X grams dripping before
   pressure rise" as a key dial-in signal for Blooming/lever profiles. Can we reliably
   detect this from scale telemetry + phase markers, or does it require a new phase
   detection heuristic?

2. **KB coverage expansion**: Which of the ~64 uncovered profiles should be prioritized
   for KB entries? Usage data from the community could identify the most-pulled profiles
   that lack dialing guidance. The 29 profiles with KB IDs cover the most popular ones,
   but gaps may exist.

3. **Post-shot evaluation UX**: Should telemetry feedback be automatic (shown after every
   shot) or on-demand (user asks the AI)? Automatic risks being annoying for experienced
   users; on-demand risks being invisible to new users. A middle ground: show a brief
   "dial-in health" indicator on the post-shot screen, with full analysis available on tap.

4. **Resistance formula**: Should Decenza switch from `P / F` to `P / F²` (Darcy's law),
   offer both, or stay with the current formula? The GaggiMate MCP uses `P / F²` with
   good results, but changing the formula would affect historical data interpretation.

### Resistance and Calibration (Sections 4--6)

5. **Bean change detection**: Can we detect when a user switches beans (resistance shift
   without grinder setting change) from telemetry alone? A sudden resistance change with
   no grinder setting change in the metadata is suggestive.

6. **Puck prep noise**: Gagne's data shows 40% resistance variation from preparation
   technique. Can we detect and flag outlier shots (likely prep errors) automatically?
   The conductance derivative (Section 8) may help --- erratic `dC/dt` correlates with
   poor puck prep.

---

## 11. Recommendations and Prioritization

### High Impact, Low Effort (Do First)

1. **Cross-profile grind ordering table.** A simple data file mapping profile KB IDs to
   relative grind positions, seeded from the KB's documented relationships and UGS's
   0--8 profile mapping. Enables "grind much finer/coarser" guidance immediately for
   both AI paths. ~1 day of work.

2. **Enrich the qualitative KB.** Add dialing-in guidance for the most popular profiles
   that lack it. Keep the format as prose (not JSON rules) --- the LLM interprets it
   well, and prose is easier to maintain and less brittle than thresholds. Prioritize
   by community usage data.

3. **Surface dial-in feedback in the in-app AI.** The infrastructure exists
   (`dialing_get_context`, `ShotSummarizer`, KB). The remaining work is UX: when and
   how to prompt users for AI feedback after a shot, especially during the first few
   shots on a new profile.

### Medium Impact, Medium Effort (Do Next)

4. **Add conductance derivative to ShotDataModel.** Compute `C = F² / P` and `dC/dt`
   from the existing pressure/flow time-series. Store alongside resistance. Surface in
   shot graphs and make available to both AI paths. Gives richer channeling diagnostics.

5. **Per-user resistance baselines per profile.** Track mean and IQR of steady-state
   resistance for each profile the user has pulled. Enables "your resistance is unusually
   low for this profile" feedback. Per-user only (not community-aggregated).

### Low Impact or High Uncertainty (Defer)

6. **Community resistance baselines.** Signal-to-noise ratio is likely too low due to
   40% puck-prep variation and equipment differences across users. Defer until per-user
   baselines prove the concept.

7. **UGS calibration method (Tier 3).** With KB + Telemetry providing zero-friction
   guidance, few users will need dedicated calibration shots. Keep the UGS profile
   ordering data (valuable) but deprioritize the calibration workflow.

8. **Diagnostic sweep shot.** Fundamental puck-erosion problems (Section 4.3) make
   this unreliable. The data from 2--3 real shots with post-shot analysis is superior.

9. **Cross-profile resistance normalization.** May not converge on a reliable metric
   due to pressure-dependent puck compression physics (Section 4.4). Start with
   same-profile comparison only.

---

## 12. References

1. Al-Shemmeri, M. (2023). "Calibrating a Coffee Grinder."
   https://medium.com/@markalshemmeri/calibrating-a-coffee-grinder-ed55315c1390

2. Gagne, J. (2023). "What I Learned from Analyzing 300 Particle Size Distributions for
   24 Espresso Grinders." Coffee ad Astra.
   https://coffeeadastra.com/2023/09/21/what-i-learned-from-analyzing-300-particle-size-distributions-for-24-espresso-grinders/

3. Gagne, J. (2020). "An Espresso Profile that Adapts to your Grind Size." Coffee ad Astra.
   https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/

4. Gagne, J. (2021). "A Study of Espresso Puck Resistance and How Puck Preparation
   Affects It." Coffee ad Astra.
   https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/

5. Corrochano, B.R. et al. (2022). "Influence of particle size distribution on espresso
   extraction via packed bed compression." Journal of Food Engineering, 335, 111177.
   https://www.sciencedirect.com/science/article/abs/pii/S0260877422003557

6. Rao, S. (2018). "Pressure Profiling on the Decent Espresso Machine."
   https://www.scottrao.com/blog/2018/6/3/introduction-to-the-decent-espresso-machine

7. Khamitova, G. et al. (2023). "Influence of Flow Rate, Particle Size, and Temperature
   on Espresso Extraction Kinetics." Foods, 12(15), 2871.
   https://pmc.ncbi.nlm.nih.gov/articles/PMC10418593/

8. Coffee Grind Lab. https://coffeegrindlab.com/

9. Titan Grinder Project. Home-Barista.
   https://www.home-barista.com/blog/titan-grinder-project-particle-size-distributions-ground-coffee-t4203.html

10. UGS Original Specification. https://videoblurb.com/UGS and
    https://videoblurb.com/UGS/ugsinfo.html

11. Arneson, C. / Visualizer.coffee (2024). "Conductance and Resistance."
    https://visualizer.coffee/updates/conductance-and-resistance

12. GaggiMate MCP Server (2025). Open-source MCP for GaggiMate firmware.
    https://github.com/julianleopold/gaggimate-mcp

13. Mahlkonig (2024). "The Sync System" --- closed-loop grind adjustment.
    https://www.mahlkoenig.us/products/the-sync-system

14. Nunc Coffee (2025). AI-enhanced espresso system.
    https://nunc.coffee/

15. Rao, S. (2018). "Flow Profiling on the DE1."
    https://www.scottrao.com/blog/2018/6/27/flow-profiling-on-the-de1
