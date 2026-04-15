# Steam Calibration (Removed)

## Status: Removed from the codebase

A guided steam-calibration feature existed in earlier builds (flow sweep, CV-based stability analysis, dryness/dilution estimation, recommendation engine, QML wizard, MCP tools) but **it did not work well** and was removed in full.

Symptoms reported in use:

- The recommendation engine picked different flows across runs on the same machine.
- Recommendations disagreed with the community-established defaults (160 °C / 0.8 mL/s for Pro/XL, ~1.0–1.2 mL/s for XXL), which continue to out-steam whatever the tool produced.
- The feature was hidden from the UI on 2026-04-06 (`30775cc1`) and removed entirely shortly after.

This file is kept as a postmortem so a future attempt doesn't re-hit the same walls.

## Why it didn't work

The data-collection side was fine — CV, oscillation rate, peak-to-peak range, slope, dryness, and dilution estimates were all computed correctly and reproducibly. **The unsolved problem was turning those numbers into a recommendation.** Multiple approaches were tried and all failed in practice:

- **Weighted stability score (CV + oscillation + range + slope)** — weights were tuned on synthetic data, scored 0 on every real step.
- **Lowest-dilution among stable steps** — always picked the lowest flow (best theoretical dryness, weakest steam, useless in practice).
- **Highest stable flow** — picked ~1.2 mL/s on a DE1+ 120 V, producing obviously wet steam.
- **Highest flow within 10 % CV of best** — same problem, ignored dilution cliff at high flow.
- **CV + dilution cap** — closer, but the theoretical dilution estimates don't track the in-cup experience, and the tool still oscillated between 0.80 and 1.20 mL/s across runs.

The core issue is that the "best" steam flow isn't a single metric — it balances pressure stability, steam dryness, vortex strength, and user speed preference. Community defaults embody years of human tuning against that balance. An automated tool can measure the numbers, but mapping them to "this is the flow you should use" reliably is still open.

## What the physics says (kept for future reference)

The DE1 uses a flash heater, not a boiler. The optimal steam flow rate is the **highest rate at which the heater can still fully vaporize all water passing through it**:

- Too low → heater overheats, firmware triggers protective flow increase → sawtooth pressure, weak vortex.
- Too high → more water than the heater can vaporize → wet steam, dilute milk, strong vortex.
- Sweet spot → stable pressure, maximum dryness, enough kinetic energy.

Approximate sweet spots by model:

| Model      | Heater | Approx. sweet spot |
|------------|--------|--------------------|
| DE1 Pro/XL | 1.5 kW | ~0.6–0.8 mL/s      |
| DE1 XXL    | 2.2 kW | ~0.9–1.2 mL/s      |

These shift with voltage (110 V slides the sweet spot down) and individual machine flow-calibration (GFC).

**Dilution floor.** To raise 180 g of milk by 60 °C, the theoretical minimum water addition is ~11.3 %. Real-world 12–15 % with thermal losses. Cross-machine differences of 1–2 % are negligible in the cup; what users feel is the steam kinetic energy and texturing speed.

**GFC affects steam, not just espresso.** A wrong flow-calibration multiplier will silently wreck steam performance on top of espresso pours.

## Lessons that held up

1. **Air test ≈ water test** on a DE1+ 120 V. Comparable CV and pressure curves. Air is the simpler rig.
2. **Flash heater exhausts after 30–50 s.** Pressure collapses as thermal mass depletes. Auto-stop at ~22 s avoided this during calibration.
3. **Steam heater recovery between steps requires `keepSteamHeaterOn`.** The DE1 firmware only maintains steam temp in Ready with this on; otherwise steps run with a cooling heater.
4. **Temperature sweep into air is meaningless** — stability into air is a function of flow vs. heater capacity; temp only matters when heating real milk.
5. **CV is the right *primary* metric** — on real data it produces a clean U-curve around the sweet spot. It just isn't the *only* metric you need for a recommendation.

## Potential future direction (if someone picks this up again)

Don't try to auto-recommend. Instead:

- **Show the data** — plot the CV curve and let the user see where the valley is on their machine.
- **Compare to community baselines** — "Your CV bottoms at 0.80 mL/s; community default for your model is 0.80 mL/s" / "… differs — here's what that might mean."
- **Use it as a diagnostic** — flag "all CVs unusually high" (possible GFC miscalibration) or "heater exhausts under 20 s" (possible scale / flow-mult issue). The problem-detection use case is plausibly tractable; the recommendation use case probably isn't.

## Community research referenced during development

- Eduardo Passoa's enthalpy vs. kinetic-energy analysis on DE1 XXL vs. Fiamma (Basecamp, Decent Diaspora, April 2026) — systematic stability comparison.
- Michael Garcia's flow/temp sweep on a 120 V Pro (Basecamp, July 2020) — settled on 165 °C / 0.6 mL/s for smoothest pressure.
- Sergey Shevtchenko's flow-calibration discovery (Basecamp, July 2024 – Sept 2025) — bad GFC multiplier destroyed steam on an XXL.
- Collin Arneson's engineering note — heater efficiency rises with pressure/flow, dilution dips before climbing sharply past the sweet spot.
- Damian's thermodynamic calculation — ~11.3 % dilution floor.

## Representative test data (DE1+ 120 V, air test)

```
Flow (mL/s) | Avg Pressure | CV    | Est. Dryness | Est. Dilution
0.40        | 0.95 bar     | 0.286 | 1.00         | 10.4%
0.60        | 1.67 bar     | 0.294 | 0.74         | 13.8%
0.80        | 1.58 bar     | 0.193 | 0.55         | 17.8%    <-- best CV
1.00        | 2.11 bar     | 0.233 | 0.44         | 21.7%
1.20        | 2.19 bar     | 0.220 | 0.37         | 25.4%
```

CV valley at 0.80 mL/s matches the community default for this model. The recommender failed because the data supports 0.80 OR 1.20 depending on which secondary metric you prioritize — and neither the tool nor a user can tell from the numbers alone which one will steam better milk in their pitcher.
