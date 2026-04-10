# Universal Grind Setting (UGS) — Architecture Decision Record

## Status: Exploratory / Discussion

This document records the evaluation of the Universal Grind Setting (UGS) concept and the
resulting architecture decision for Decenza's dial-in assistant. Supporting research,
rejected alternatives, external landscape, and AI integration details are in the appendices.

---

## TL;DR

**The problem:** Users switching between espresso profiles (e.g., D-Flow → Blooming) don't
know how much to adjust their grinder. UGS proposes a two-anchor calibration system to solve
this. This document evaluates UGS against research, explores alternatives, and arrives at a
preferred architecture.

**What we concluded:**

1. **UGS works but has real limits.** The linear interpolation assumption breaks down in the
   middle of the range (UGS 3--5), puck resistance is nonlinear with grind size, and shot-to-
   shot noise means anchors are only "approximately correct." It's useful as a rough guide, not
   a precise dial number.

2. **The KB + Telemetry approach (§3) is preferred.** Instead of calibration shots, use the
   existing Profile Knowledge Base (what "dialed in" looks like per profile) combined with live
   shot telemetry to give immediate, zero-friction guidance. The infrastructure is mostly
   already built: `dialing_get_context` MCP tool, `ShotSummarizer`, phase markers, and 29
   profiles with KB entries. The LLM interprets qualitative KB guidance in context — better
   than rigid JSON thresholds.

3. **UGS's most valuable artifact is its grind ordering table** (16 profiles on a 0--8 scale),
   not its calibration method. This table seeds cross-profile direction hints ("grind much
   finer") without requiring any calibration shots.

**Three tiers of guidance (§2):**
- **Tier 1 — Passive (zero friction):** Per-profile resistance tracking from day 2 onward.
  Show a target resistance band on the live graph; report direction when switching profiles.
- **Tier 2 — Opportunistic (low friction):** Build a grinder calibration curve silently from
  rated shots with entered grinder settings. Unlocks magnitude recommendations ("2 clicks
  finer") once 3+ distinct-setting data points exist.
- **Tier 3 — UGS / explicit calibration (opt-in):** Pull two anchor shots for full-range
  immediate calibration. Deprioritized — most users won't need it once Tier 1 is running.

**What to build next (§4 priority order):**
1. Cross-profile grind ordering table (data file, ~1 day)
2. Enrich the qualitative KB with more profile dial-in entries (keep as prose, not JSON rules)
3. Surface in-app AI dial-in feedback after shots on a new profile
4. ~~Add conductance derivative to `ShotDataModel`~~ — already done (`P/F`, `F²/P`, `P/F²`,
   and `dC/dt` with 9-point Gaussian smoothing are all computed and exposed)
5. Per-user resistance baselines per profile

**Open decisions:** Which resistance formula to use for per-user baseline comparisons
in dial-in guidance (`P/F`, `P/F²`, or `F²/P` — all three are already tracked);
dripping-weight detection reliability; post-shot feedback UX (automatic vs. on-demand).

---

## 1. The Original UGS Concept

**Source:** [videoblurb.com/UGS](https://videoblurb.com/UGS) and
[UGS Info](https://videoblurb.com/UGS/ugsinfo.html)

UGS is a two-point linear calibration system that maps any grinder's dial settings onto a
universal 0--8 scale. Users pull two anchor shots on the DE1, record their grinder settings,
and the system interpolates grind recommendations for any of the 16 supported profiles.

| Anchor | UGS Value | Profile Style | Targets |
|--------|-----------|---------------|---------|
| **Cremina** | 0 (finest) | High puck resistance, lever-style | 18 g in, 36 g out, ~45 s |
| **Rao Allonge** | 8 (coarsest) | Maximum flow, minimal resistance | ~4.5 mL/s, 4--6 bar, ~30 s |

**Calibration:** Record anchor 0 and anchor 8 grinder settings. Grinder Span = anchor 8 −
anchor 0. For any profile at UGS value *u*: RGS = anchor 0 + (Span / 8) × *u*.

**Why we're not leading with it:** The linear interpolation assumption breaks down at the
midrange (UGS 3--5), puck resistance is nonlinear with grind size, and two-point calibration
introduces inherent noise. Users must pull two shots on profiles they may not care about. See
[Appendix A](#appendix-a-research-evaluation) for the full research evaluation.

The UGS **grind ordering table** — 16 profiles placed on a 0--8 scale — remains valuable as
a seed for cross-profile direction hints, independent of the calibration workflow.

**Adoption friction:** Users only enter accurate metadata when they have a reason to (rating
a shot, uploading to Visualizer). Any approach requiring deliberate calibration shots will
have low uptake. Rated shots are a reliable quality signal — users who rate a shot have
typically also entered correct metadata. See [Appendix B](#appendix-b-adoption-friction) for
user feedback.

---

## 2. Architecture: Three-Tier Guidance

A tiered approach that serves all user types without requiring upfront commitment:

### Tier 1: Passive Resistance Guidance (Zero Friction)

- Available from the user's second shot on a profile.
- Computes and stores steady-state resistance for every shot (already in `ShotDataModel`).
- Shows a target resistance band on the live shot graph for the active profile.
- When switching profiles, reports the expected resistance shift and direction to adjust.
- **Does not require any user-entered metadata.**

### Tier 2: Opportunistic Calibration (Low Friction)

- Activates when the user has 3+ rated shots at distinct grinder settings on the same grinder.
- Builds a grinder-specific calibration curve from observed (setting, resistance) pairs.
- Enables magnitude recommendations: "grind 2 clicks finer."
- Accuracy improves silently over time.

**Narrow range note:** Users who pull only one style (e.g., lever-only) will have a
calibration curve only in that range — Tier 2 cannot extrapolate to dramatically different
profiles. Tier 1 still works fully; Tier 3 handles the cross-style case if needed.

### Tier 3: UGS / Explicit Calibration (Opt-in, Low Priority)

- Pull the Cremina and Rao Allonge anchor profiles, record settings — full range immediately.
- Results can be validated against Tier 2 data as it accumulates.
- Deprioritized: the KB + Telemetry approach (§3) provides zero-friction guidance from shot 1,
  so few users will need explicit calibration. The diagnostic sweep shot alternative is
  rejected due to fundamental puck-erosion problems
  (see [Appendix C](#appendix-c-rejected-alternatives)).

### Convergence

When both Tier 2 and Tier 3 data exist, the system can compare predictions and show where
the user's grinder deviates from the UGS linear assumption.

---

## 3. KB-Driven Dial-In Assistant (Preferred Approach)

### Core Idea

Leverage the **existing Profile Knowledge Base** combined with **real-time shot telemetry**
to guide users when switching profiles. The KB documents what "dialed in" looks like per
profile — pressure/flow signatures, dripping weights, failure modes. The DE1 already
measures everything needed to evaluate those signatures in real time.

No calibration shots. No grinder metadata. No waiting.

### What the Knowledge Base Provides

**Relative grind ordering across profiles:**

```
FINEST ←————————————————————————————————————→ COARSEST

Blooming    Cremina     D-Flow      G&S         E61       Allonge
Allonge     Londinium   Adaptive    Extramundo            Rao Allonge
            Best Ovrl   Flow Prof
            80's (slightly coarser)
```

- Cremina, Londinium, Best Overall: **same fine grind** `[SRC:dark-video]`
- 80's Espresso: **slightly coarser** than the lever group `[SRC:dark-video]`
- E61: **coarsest dark-roast profile** `[SRC:dark-video]`
- Allonge: **coarsest of all** `[SRC:eaf-profiling]` `[SRC:light-video]`
- Adaptive: **coarser than Londinium** for same bean `[SRC:light-video]` `[SRC:medium-video]`
- Extractamundo: **slightly finer** than G&S to match 6 bar target `[SRC:light-video]`

**Dialed-in telemetry signatures per profile:**

| Profile | Dialed-In Signature | Failure Modes |
|---------|-------------------|---------------|
| **Blooming** | Pressure 6--9 bar; 6--8g dripping before pressure rise; water on puck bottom immediately after fill | Hits max wall → too fine; only ~4 bar → too coarse; 3--4s for water to appear → too fine |
| **Londinium/LRv3** | 2--8g dripping during soak before pressure rise; pressure peaks ~9 bar then stays high | Dripping >17g → too coarse; pressure crashes → puck eroded |
| **D-Flow** | Linear flow chart; flow limit 1.7 ml/s kicks in during pour | Non-linear flow = channeling; pressure elbow = extraction done |
| **Adaptive v2** | Pressure peaks near 9 bar then gradually declines; flow 2.0--2.7 ml/s | Pressure rising again during extraction = fines migration |
| **G&S** | Constant 6 bar; flow starts ~2 ml/s, increases to 3+ as puck erodes | Shot >30s with light roast = overextracted |
| **Allonge** | Flow ~4.5 ml/s; pressure 4--6 bar; ~30s | Hits max → too fine; some channeling is normal |
| **Cremina** | Pressure rises to peak then declines steeply; ~45s | — |
| **E61** | Flat 9 bar; flow increases as puck erodes; 22--25s | Too fast = too coarse |
| **80's Espresso** | Flow 1--1.2 ml/s; low temp 80°C declining | Flow ~1.9 ml/s = too fast, thin/dusty |

Sources: `docs/PROFILE_KNOWLEDGE_BASE.md` with original citations preserved.

The KB also includes a **roast-to-profile compatibility matrix** — helping the AI recommend
appropriate profiles before grind adjustment is even needed.

### How the Dial-In Flow Works

**Scenario:** User has been pulling D-Flow daily. Wants to try Blooming Espresso.

**Step 1 — Profile switch intent.** User asks the MCP or in-app AI.

**Step 2 — KB-informed grind guidance.** The AI responds:

> *"Blooming Espresso needs a significantly finer grind than D-Flow. It's in the finest
> group along with Cremina and Londinium. When dialed in, you should see 6--8g dripping
> before the pressure rise and extraction pressure between 6--9 bar. This is the hardest
> profile to dial in — plan on 3+ attempts."*

**Step 3 — Post-shot telemetry feedback.** After the first attempt:

> *"Your Blooming shot hit 9.8 bar — pressure hit the max wall for 6 seconds. You're a
> touch too fine. Open up half a click. Dripping was only 3g — ideally 6--8g. Both issues
> point the same direction."*

**Step 4 — Iterative refinement.** After 2--3 shots, the user converges on the right grind.

### Infrastructure Already Built

| Component | Location | What It Provides |
|-----------|----------|-----------------|
| `dialing_get_context` | `src/mcp/mcptools_dialing.cpp` | Single-call aggregation: shot summary, dial-in history, grinder context, bean metadata, full KB, reference tables |
| `ShotSummarizer` | `src/ai/shotsummarizer.h/cpp` | Per-phase metrics (avg/max/min pressure, flow, temp, weight), channeling detection, temp stability |
| Resistance metrics | `src/models/shotdatamodel.cpp` | `P/F`, `F²/P`, `P/F²`, and `dC/dt` (9-point Gaussian smoothed, matching Visualizer.coffee) — all computed and exposed |
| Phase detection | `src/controllers/maincontroller.cpp:1758` | Phase markers with transition reasons stored in `shot_phases` |
| Profile KB system | `docs/PROFILE_KNOWLEDGE_BASE.md` | 29 of 93 profiles (31%) with KB entries; three-tier matching; queryable by `profile_kb_id` |
| `shots_compare` | MCP tool | Side-by-side comparison with deltas between consecutive shots |

### What Remains to Be Built

1. **Cross-profile grind ordering table.** A data file mapping profile KB IDs to relative
   grind positions, seeded from the KB's documented relationships and the UGS 0--8 profile
   mapping. Enables "grind much finer/coarser" for both AI paths. ~1 day.

2. **Keep the KB qualitative — do not formalize into rigid JSON rules.** The LLM interprets
   qualitative guidance in context far better than hard thresholds. A threshold-based system
   (e.g., `"extractionPressure": { "min": 6.0, "max": 9.0 }`) caused incorrect D-Flow advice
   in March 2026. Invest in enriching the prose KB instead.

3. **Per-user resistance baselines per profile.** Track mean and IQR of steady-state
   resistance for each profile the user has pulled. Enables "your resistance is unusually low
   for this profile" feedback. **Per-user only** — do not aggregate across users. With 40%
   resistance variation from puck prep alone, community baselines will have too much noise.

### Scaling to Profiles Without KB Entries

Of 93 profiles, ~64 lack curated KB data. Fallback strategies:
- **Editor type heuristic**: Profiles of the same editor type share general behavior.
- **Frame analysis**: The AI can read profile frames (`profiles_get_detail`) and infer
  expected behavior from pressure/flow targets directly.
- **Community contribution**: Qualitative prose entries as the KB grows.

---

## 4. Recommendations and Prioritization

### High Impact, Low Effort (Do First)

1. **Cross-profile grind ordering table.** ~1 day of work. Immediately enables direction
   hints for both AI paths.

2. **Enrich the qualitative KB.** Add dial-in guidance for the most-pulled profiles that
   lack it. Prose format, not JSON rules.

3. **Surface dial-in feedback in the in-app AI.** Infrastructure exists. Remaining work is
   UX: when and how to prompt users for AI feedback after a shot on a new profile.

### Medium Impact, Medium Effort (Do Next)

4. **Per-user resistance baselines per profile.** Mean and IQR of steady-state resistance
   per profile. Per-user only.

### Low Impact or High Uncertainty (Defer)

5. **Community resistance baselines.** Signal-to-noise ratio too low due to 40% puck-prep
   variation and equipment differences. Defer until per-user baselines prove the concept.

6. **UGS calibration workflow (Tier 3).** KB + Telemetry covers most users. Keep the UGS
   profile ordering data (valuable); deprioritize the calibration UI.

7. **Diagnostic sweep shot.** Rejected — fundamental puck-erosion problems make the data
   unreliable. See [Appendix C](#appendix-c-rejected-alternatives).

8. **Cross-profile resistance normalization.** May not converge. Start with same-profile
   comparison only. See [Appendix C](#appendix-c-rejected-alternatives).

---

## 5. Open Questions

1. **Dripping weight detection.** The KB frequently references "X grams dripping before
   pressure rise" as a key signal for Blooming/lever profiles. Can we reliably detect this
   from scale telemetry + phase markers, or does it require a new heuristic?

2. **KB coverage.** Which of the ~64 uncovered profiles should be prioritized? Usage data
   from the community would identify the most-pulled profiles that lack guidance.

3. **Post-shot feedback UX.** Automatic (shown after every shot) vs. on-demand (user asks)?
   Middle ground: brief "dial-in health" indicator on the post-shot screen, full analysis on
   tap.

4. **Resistance formula for baselines.** All three formulas are tracked (`P/F`, `P/F²`,
   `F²/P`). Which should be used for per-user baseline comparisons in dial-in guidance?
   `P/F²` has the strongest theoretical grounding; `P/F` is more widely used.

5. **Bean change detection.** Can we detect a bean switch from telemetry alone — a sudden
   resistance change with no grinder setting change in the metadata?

6. **Puck prep outlier detection.** Gagne's data shows 40% resistance variation from
   preparation technique. Can we flag outlier shots (likely prep errors) automatically?
   The conductance derivative (`dC/dt`) may help — erratic values correlate with poor prep.

---

---

## Appendix A: Research Evaluation

### A.1 Linearity of Grinder Settings vs. Particle Size

UGS assumes a linear relationship between grinder dial setting and behavioral grind-size
equivalent across the full 0--8 range.

**Evidence from Al-Shemmeri (2023):**
Calibration curves for multiple grinders show grinder setting vs. median particle size is
**approximately linear in the espresso-to-filter range**, but breaks down at extremes. The
Niche Zero requires a **power-law fit** at fine settings. Al-Shemmeri recommends
**3--5 calibration points minimum**; two-point calibration is explicitly called insufficient.

- DF64 (SSP burrs): median = 205 + 13.4 × setting
- Fellow ODE: median = 486 + 59.6 × setting
- EK43 (standard): median = 221 + 58.4 × setting
- Niche Zero: power law at fine end, linear at coarser settings

**Implication:** UGS is most accurate near the anchors and likely drifts 1--2 clicks in
the middle of the range (UGS 3--5), where many popular profiles live.

> Al-Shemmeri, M. (2023). "Calibrating a Coffee Grinder."
> https://medium.com/@markalshemmeri/calibrating-a-coffee-grinder-ed55315c1390

### A.2 Particle Size Distributions Vary Wildly Between Grinders

**Evidence from Gagne (2023):**
At a reference median of 340 microns, grinders differ substantially in fines fraction,
distribution width, and modality. Two grinders at the same median particle size can produce
very different espresso.

**Implication:** This actually *supports* UGS's behavioral approach — particle size alone
doesn't predict extraction, so behavioral equivalence (same shot performance on a controlled
profile) may be more useful than targeting a micron number.

> Gagne, J. (2023). "What I Learned from Analyzing 300 Particle Size Distributions."
> https://coffeeadastra.com/2023/09/21/what-i-learned-from-analyzing-300-particle-size-distributions-for-24-espresso-grinders/

### A.3 Puck Resistance Is Nonlinear With Grind Size

**Evidence from Gagne (2020) and Corrochano et al. (2022):**
The relationship between grind size and puck resistance follows a **quadratic** relationship
due to puck compression under pressure. A 1-click change at the fine end (UGS 0--2) produces
a much larger behavioral change than the same click at the coarse end (UGS 6--8).

**Implication:** Even with perfect linear particle size mapping, UGS's equal-step
interpolation systematically underestimates grind changes needed at the coarse end and
overestimates at the fine end.

> Gagne, J. (2020). https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/
> Corrochano, B.R. et al. (2022). Journal of Food Engineering.
> https://www.sciencedirect.com/science/article/abs/pii/S0260877422003557

### A.4 Puck Resistance Variability

**Evidence from Gagne (2021):**
Even with identical grind, dose, and beans, shot-to-shot puck resistance varies by ~5%.
Puck preparation technique (WDT quality) introduces up to **40% variation** in peak
resistance.

**Implication:** Anchor shots have inherent noise. Two users with identical setups could get
different anchor settings from preparation technique alone. UGS is "approximately correct,"
not precise.

> Gagne, J. (2021). https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/

### A.5 Other Particle Size Measurement Efforts

Complementary to UGS (physical micron measurement vs. behavioral equivalence):

- **Coffee Grind Lab** — Shimadzu laser diffraction, public dataset. https://coffeegrindlab.com/
- **Titan Grinder Project** — Community PSD characterization. https://www.home-barista.com/blog/titan-grinder-project-particle-size-distributions-ground-coffee-t4203.html
- **Bettersize** — Coffee particle analysis methodology. https://www.bettersizeinstruments.com/learn/knowledge-center/coffee-particle-size-analysis/

---

## Appendix B: Adoption Friction

> *"I am a typical DE-1 user... the vast majority of my shot-affecting settings would be
> completely inaccurate. Preset chosen, grind setting, beans used, quantity of beans, and
> even the fact I had changed baskets a few times... so any conclusion based on all that
> would be completely wrong."*

Two distinct friction problems:

1. **UGS calibration friction**: Users must pull two profiles they may not care about, dial
   them in correctly, and record settings. No intrinsic motivation for single-style drinkers.

2. **Shot metadata quality**: User-entered grinder settings, bean info, and dose weights are
   mostly inaccurate. **Rated shots are a reliable quality signal** — users who rate a shot
   have typically entered correct metadata. This is the filter for Tier 2 calibration.

---

## Appendix C: Rejected Alternatives

### C.1 Single Diagnostic Sweep Shot

A single DE1 profile sweeping 3 bar → 6 bar → 9 bar to extract a puck permeability curve
from one shot. **Rejected** for a fundamental reason:

The puck at 9 bar after 20 seconds of lower-pressure extraction is physically different from
a puck starting at 9 bar — fines have migrated, channels have formed, solubles have been
extracted. The permeability curve is an artifact of the measurement sequence, not a stable
grinder fingerprint. This cannot be corrected for. The data from 2--3 real shots with
post-shot analysis is superior.

### C.2 Cross-Profile Resistance Normalization

Comparing resistance across profiles requires accounting for pressure-dependent puck
compression. Four options were evaluated:

- **Option A**: Same-profile comparison only (simplest, fewest data points)
- **Option B**: Normalize by pressure (`permeability = flow / pressure`)
- **Option C**: Compare flow at a reference pressure crossing (most physically grounded)
- **Option D**: Preinfusion flow (low-pressure, less compression effect)

**Verdict:** All four face the same fundamental challenge — puck compression is nonlinear
and pressure-dependent, and puck prep introduces up to 40% resistance variation. Option D
(preinfusion flow) is most promising but many profiles have different preinfusion pressures
or skip it. **Start with same-profile comparison only (Option A)** and treat cross-profile
normalization as a research question.

---

## Appendix D: External Landscape (2024--2026)

### Closed-Loop Grind Systems

**Mahlkonig Grind-by-Sync (2024--2025):** The only production closed-loop grind adjustment
system. Records brew time and sends it to the grinder via WiFi; the grinder auto-adjusts
burr distance. Simple time-based heuristic, not telemetry-curve analysis.

**Nunc (2025):** German startup with an integrated machine + grinder using AI to adjust
grind, dose, and RPM in real-time. Claims one test shot to dial in any bean. Fully closed
system.

### AI-Assisted Shot Analysis

**GaggiMate MCP Server (open-source, 2025):** Most directly comparable implementation. Nine
tools, puck resistance modeling (`R = P / F²`), channeling risk scoring, temperature
deviation tracking, pressure/flow stability analysis, per-phase profile compliance. Feedback
loop: user pulls shot → provides tasting notes → AI correlates with telemetry → suggests
adjustments.

**Key gap in the ecosystem:** No existing tool provides profile-specific "here's what
dialed-in looks like" guidance combined with post-shot telemetry evaluation. This is
Decenza's unique position with the Profile Knowledge Base.

### Diagnostic Metrics

**Conductance derivative (`dC/dt`):** Championed by Collin Arneson, displayed on
Visualizer.coffee. Where resistance shows overall puck state, `dC/dt` reveals *transient*
channeling events invisible in smoothed resistance curves. Already implemented in Decenza's
`ShotDataModel` (`computeConductanceDerivative()`).

**Resistance formula landscape:**
- `R = P / F` — Decenza's formula (DSx2 / Ohm's law analogy), simpler, widely used
- `R = P / F²` — GaggiMate's formula (Darcy's law for laminar flow through porous media)
- `R = √P / F` — Coffee ad Astra formulation

No community consensus. All three are tracked in Decenza; the open question is which to use
for per-user baseline comparisons (see §5).

---

## Appendix E: AI Integration Paths

### In-App AI Advisor

Built-in advisor (`src/ai/`) with 5 pluggable providers (Claude Sonnet 4.6, GPT-4.1,
Gemini 2.5 Flash, OpenRouter, Ollama). Supports multi-turn conversations with persistent
storage (5 slots keyed by bean + profile), dial-in history injection (last 5 same-profile
shots), and provider-specific prompt caching.

**What it already does for dial-in:**
- Full telemetry curve analysis via `ShotSummarizer`
- Per-phase breakdown with actual vs. target values
- Channeling detection (sustained `dC/dt` elevation)
- Temperature stability assessment
- Profile-specific KB injection (31 stock profiles with KB IDs)
- Grinder/burr awareness, bean knowledge, flavor problem diagnosis

**Current gaps** (from `docs/AI_ADVISOR.md` roadmap):
- No recipe-aware interpretation rules (caused incorrect D-Flow advice in March 2026)
- Dial-in reference tables not yet integrated into the system prompt
- No cross-profile awareness (can't recommend switching profiles)

### MCP Server

`dialing_get_context` (`src/mcp/mcptools_dialing.cpp`) provides richer context than the
in-app AI in a single call: full KB + reference tables, grinder context with observed
settings range, structured JSON for post-processing.

**Limitation:** Historical shot data only (no live `ShotDataModel` access); requires
MCP-compatible client setup.

### How Both Converge

| Aspect | In-App AI | MCP Server |
|--------|-----------|------------|
| **User type** | All users | Power users / developers |
| **Shot data** | Live curves from `ShotDataModel` | Historical from database |
| **Reference tables** | Not yet integrated | Included in response |
| **Cross-profile** | Current profile only | Full KB available |
| **Conversation** | Multi-turn, 5 persistent slots | Single-call |

**Recommendation:** Both paths should support dial-in. The in-app advisor is primary for
most users. The highest-impact improvement is completing the Phase 0 roadmap items from
`docs/AI_ADVISOR.md` — particularly recipe-aware interpretation rules and integrating the
dial-in reference tables.

---

## References

1. Al-Shemmeri, M. (2023). "Calibrating a Coffee Grinder." https://medium.com/@markalshemmeri/calibrating-a-coffee-grinder-ed55315c1390
2. Gagne, J. (2023). "300 Particle Size Distributions." https://coffeeadastra.com/2023/09/21/what-i-learned-from-analyzing-300-particle-size-distributions-for-24-espresso-grinders/
3. Gagne, J. (2020). "An Espresso Profile that Adapts to your Grind Size." https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/
4. Gagne, J. (2021). "Espresso Puck Resistance and Puck Preparation." https://coffeeadastra.com/2021/01/16/a-study-of-espresso-puck-resistance-and-how-puck-preparation-affects-it/
5. Corrochano, B.R. et al. (2022). Journal of Food Engineering, 335, 111177. https://www.sciencedirect.com/science/article/abs/pii/S0260877422003557
6. Rao, S. (2018). "Pressure Profiling on the Decent Espresso Machine." https://www.scottrao.com/blog/2018/6/3/introduction-to-the-decent-espresso-machine
7. Khamitova, G. et al. (2023). Foods, 12(15), 2871. https://pmc.ncbi.nlm.nih.gov/articles/PMC10418593/
8. Coffee Grind Lab. https://coffeegrindlab.com/
9. Titan Grinder Project. https://www.home-barista.com/blog/titan-grinder-project-particle-size-distributions-ground-coffee-t4203.html
10. UGS Original Specification. https://videoblurb.com/UGS
11. Arneson, C. / Visualizer.coffee (2024). "Conductance and Resistance." https://visualizer.coffee/updates/conductance-and-resistance
12. GaggiMate MCP Server (2025). https://github.com/julianleopold/gaggimate-mcp
13. Mahlkonig (2024). "The Sync System." https://www.mahlkoenig.us/products/the-sync-system
14. Nunc Coffee (2025). https://nunc.coffee/
15. Rao, S. (2018). "Flow Profiling on the DE1."
