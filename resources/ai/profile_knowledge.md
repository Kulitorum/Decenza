# Profile Knowledge Base — AI Reference

When analyzing a shot, use this section to understand what the profile was DESIGNED to do.
Do NOT flag intentional profile behaviors as problems.

## D-Flow
Also matches: "D-Flow / default", "D-Flow / Q", "D-Flow / La Pavoni", "Damian's Q", "Damian's LRv2", "Damian's LRv3", "Damian's LM Leva", "Damian's D-Flow"
AnalysisFlags: flow_trend_ok
Category: Lever/Flow hybrid (Londinium family)
How it works: All D-Flow variants and Damian's profiles share the same core: pressurized pre-infusion with soak, then flow-controlled pour (default 1.7 ml/s). Pressure peaks then gradually declines — this is intentional lever-style behavior. Can heal uneven puck prep.
Variants: "D-Flow / default" is the starter profile. "D-Flow / Q" (also "Damian's Q") is optimized for medium-light beans with a 6 bar approach. "D-Flow / La Pavoni" emulates a La Pavoni lever — created by Damian running D-Flow and a real La Pavoni side by side; uses 18g VST basket; tuned for milk drinks. "Damian's LM Leva" is a pressure-profile recreation of a real La Marzocco Leva recording. "Damian's LRv2" and "Damian's LRv3" (also "Londonium") are pure lever-style Londinium R simulations.
Expected curves: Pressure peaks between 6 and 9 bar early, then declines as puck erodes. Flow stays near target (1.7-2.7 ml/s). Declining pressure is NORMAL and INTENTIONAL.
Damian's LM Leva: Created by recording a real La Marzocco Leva machine shot on a Smart Espresso Profiler and reverse-engineering it. Low 2.2 bar pre-infusion then rise to 8 bar then decline. Temperature 88–89°C. Dose 18g → 42g (1:2.3). Flavor: creamy body, smooth balance, highlights flavors in a gentle way — best as a straight shot. Less suited to milk drinks (flavors can get lost). Portafilter must be fully preheated — cold portafilter causes temperature crash and loss of mouthfeel.
Damian's LRv2: Londinium R simulation with several tweaks for coarser grind and faster pour. If puck erodes too fast during extraction, switches from pressure to flow control at 2.5 ml/s (prevents gushing). Temperature 89°C. Dose 18g → 36g (1:2). Flavor: "milkshake with extra syrup," rich body, thick, chocolatey — great for milk drinks and dark roasts.
Damian's LRv3 / Londonium: Pure lever decline with an added 9 bar hold step after pressure rise — waits until flow exceeds 1.9 ml/s before starting the decline. More sustained peak pressure phase gives richer body vs LRv2. Temperature 90°C. Does NOT switch to flow control — preferred when dialed in well. Dose 18g → 36g.
Damian's Q: D-Flow variant with 84°C fill temperature and 6 bar pressure approach, optimized for medium-light beans. Produces brightness in milk (contrast with LRv2's thick chocolate). Dose 18–19g → 34g. The 84°C fill temperature is INTENTIONAL — low fill temp controls bitterness during saturation.
Damian's LRv2/LRv3 temperatures: Frame temperatures are 89°C (LRv2) and 90°C (LRv3) — higher than standard D-Flow variants due to different fill/soak behavior.
Temperature: Fill temperature varies by variant — D-Flow/default uses 88°C, while D-Flow/Q and Damian's Q use 84°C fill with 94°C rise target. The low fill temperature is INTENTIONAL — hotter fill water produces dark spots in the crema and more bitter taste. The coffee never actually reaches 94°C; the high setpoint makes the heater pump hot water that gradually raises basket temperature to ~86–90°C. DO NOT flag the large gap between temperature target (94°C) and actual (~86-90°C) as a problem — it is by design.
Flow calibration: If actual flow is consistently below target (e.g. 1.8 target but only 1.5 actual), the flow sensor may be over-reading by ~20%. Reducing the calibration value will show more pressure for the same grind.
Grind: Medium-fine. Grind determines curve shape: finer grinds produce constant-pressure extraction, coarser grinds produce declining pressure (lever-like). Both are valid.
Roast: All roasts. Excellent for medium (floral/fruity + chocolate). Good for light and dark.
DO NOT flag declining pressure, the flow safety step switching to flow control (LRv2), or the 9-bar hold (LRv3) as problems — all are intentional profile behaviors.
DO NOT flag slow 0–0.4 ml/s flow in the first 20s as a problem — this is the pressurized soak phase and is intentional across all Damian variants.

## A-Flow
Creator: Janek (Jan-Erling Johnsen)
Category: Pressure-ramp into flow extraction
How it works: Created by Janek as a mix of D-Flow and Adaptive. Fill and optional infuse/soak, then pressure ramps UP to target (typically 9-10 bar), followed by optional pressure decline, then switches to flow-controlled extraction with a pressure limiter. The key difference from D-Flow: pressure intentionally RISES before extraction rather than starting high and declining. Works with all grinder types including conical.
Variants: "A-Flow / medium" is the default starting point. "A-Flow / dark" optimized for dark roasts. "A-Flow / very dark" uses ramp-down for darkest beans. "A-Flow / like D-Flow" has a long 60s infuse and resembles D-Flow behavior.
Expected curves: Pressure ramps up to 9-10 bar during the ramp phase, may decline briefly, then flow takes over for extraction. Flow during extraction is typically ~2 ml/s with pressure capped by the limiter. The pressure ramp-up is INTENTIONAL — this is not overpressure.
Options: rampDownEnabled splits the ramp time between pressure-up and pressure-decline phases (pressure rises, then declines, then flow takes over). flowExtractionUp ramps extraction flow smoothly upward (vs flat). secondFillEnabled adds an extra water fill before the pressure ramp.
Flavor dial-in: Pour time controls the flavor character — a longer pour time produces more chocolatey body; a shorter pour time produces more caramel and brighter notes.
Dial-in: Start with A-Flow medium. Adjust grind to achieve target pressure curve; adjust Pour time to tune flavor character.
Grind: Medium-fine, similar to D-Flow. Compatible with all grinder types including conical.
Roast: All roasts. Variants optimized for medium through very dark.
AnalysisFlags: flow_trend_ok
DO NOT flag the pressure ramp-up phase as overpressure — the intentional rise to 9-10 bar before flow extraction is how this profile works.

## Adaptive v2
Category: Flow/adaptive
How it works: Ramps pressure toward ~9 bar (exits at 8.8 bar) for ~6 seconds, then adapts to grind coarseness by locking in whatever flow rate exists. Switches to stable flow after pressure peak. Extraction flow limiter at 9.5 bar. The Low Pressure Infusion variant (by Trevor Rainey + Jonathan Gagné) modifies this with a high-flow fill (8 ml/s), 3 bar soak pressure, and 8 bar rise — targeting ~30s total shot time. Canonical recipe for LPI variant: 15g dose, ~33g out in ~30s, targeting ~4g drip-through during bloom and ~1.5 ml/s flow at pressurize step.
Expected curves: Pressure peaks near 9 bar then gradually declines. Flow settles at 2.0-2.7 ml/s. Variable curves are expected — the profile adapts to grind size.
Duration: 26-40s. With large flat burrs, the extraction phase may be very short (rapid high-flow extraction in 15-20s after long pre-infusion soak).
Pre-infusion: Lots of dripping is fine with flat burr grinders (up to 17g dripping worked well in testing). Pressure drops steeply during drip phase with coarse/flat-burr grinds — this is normal.
Ratio: Best at 1:1.8-2.3 for light roasts (fruitiness). Can extend to 1:2.5 if no off-flavors.
Stop strategy: The LPI variant's extraction step has a 60s safety timeout — shots should be stopped manually at ~30s (or by weight/time stop condition) well before the timeout fires.
Roast: Good for light (v2 updated for light roasts). Excellent for medium-light. Not recommended for dark.
DO NOT flag variable pressure curves, steep pre-infusion pressure drop, or a slight flow jump at peak pressure as problems — all are by design.

## Blooming Espresso
Category: Blooming
AnalysisFlags: flow_trend_ok, channeling_expected
How it works: Fill at ~4 ml/s until pressure peaks, close valve for 30s bloom, then ramp to 2.2 ml/s flow-controlled extraction with an 8.6 bar limiter. The 30-second pause with zero flow is INTENTIONAL.
Expected curves: Initial pressure spike during fill, then 30s of declining/zero pressure (bloom phase), then pressure rise to 6-9 bar for extraction. Flow near zero during bloom is BY DESIGN.
Temperature: 97.5°C preinfusion/fill, drops to 90°C during bloom, 92°C extraction. The high preinfusion temperature and the drop during bloom are intentional.
Dialing in: Water should appear on bottom of puck almost immediately after fill. Aim for 6-8g dripping before pressure rise. Pressure during extraction should be 6-9 bar and NEVER hit maximum. If pressure hits max, grind is too fine.
Grind: Very fine — grind as fine as manageable. Paper filter under puck can prevent pressure crash.
Duration: ~70s total
Extraction: 23-26% typical (among the highest of espresso profiles, comparable to Filter 2.0)
Ratio: Up to 1:3
Roast: Excellent for light (hardest to dial in but most rewarding). Good for medium. Not ideal for dark.
This is the hardest profile to dial in — requires lots of beans to experiment. DO NOT flag the 30s bloom pause as a problem.
DO NOT flag channeling in dC/dt — the zero-flow bloom followed by ramp to extraction flow always produces large conductance derivative spikes. dC/dt is unreliable for any profile with a zero-flow phase before extraction.

## Blooming Allonge
Also matches: "Blooming Allongé"
Category: Blooming/Allonge hybrid
AnalysisFlags: flow_trend_ok, grind_check_skip, channeling_expected
How it works: Fast fill at 4.5 ml/s until pressure peaks, ramp down to 2.0 ml/s, then bloom (zero flow, pressure decays), then ramp up to 3.5 ml/s percolation with an 8.6 bar pressure limiter.
Expected curves: Pressure spike, bloom decay, then low pressure (<6 bar) during percolation at 3.5 ml/s.
Temperature: 91-95°C (declining across phases)
Grind: Ultra-fine or Turkish
Pressure: Should not exceed 6 bar during percolation. If pressure exceeds 6 bar, grind is too fine — coarsen the grind.
Flavor: Filter-like texture, high clarity
Roast: Best for ultra-light, Nordic filter roasts.
DO NOT flag zero flow during bloom or low pressure during percolation as problems.

## Allonge
Also matches: "Allongé", "Rao Allongé"
Category: Allonge
How it works: Constant high flow at 4.5 ml/s with low pressure (~4-6 bar). Coarsest grind of all espresso profiles.
Expected curves: Flow at 4.5 ml/s, pressure 4-6 bar. Some channeling (visible as a needle stream) is NORMAL and expected at this flow rate.
Duration: ~30s
Ratio: 1:4-1:5 (high ratio is intentional)
Grind: Coarsest of espresso profiles
Temperature: High — produces hottest coffee of any profile due to fast flow (puck doesn't cool water)
Roast: Best for ultralight/light. Excellent with natural/fermented/fruity beans. Less impressive with clean washed coffees.
DO NOT flag high ratio, low pressure, or minor channeling as problems — all are expected.
AnalysisFlags: channeling_expected, grind_check_skip

## Default
Category: Lever
How it works: Classic lever-style declining pressure, 8.6 to 6 bar. Fast fill, immediate pressure rise, then decline.
Expected curves: Pressure peaks at 8.6 bar and declines to ~6 bar. Flow stays relatively constant as declining pressure compensates for puck erosion.
Temperature: 90°C (stepping down to 88°C during extraction)
Grind: Versatile — mimics 8.5 bar for fine grinds, Londinium for optimal grinds, limits flow for coarse grinds.
Flavor: Emphasizes chocolate intensity
Roast: Excellent for medium, good for dark. Not recommended for light.
DO NOT flag declining pressure as a problem — it defines lever-style profiles.
AnalysisFlags: flow_trend_ok

## Londinium
Also matches: "Londonium", "Londinium / LRv3"
AnalysisFlags: flow_trend_ok
Category: Lever
Note: This is the standalone Londinium profile. "Damian's LRv2" and "Damian's LRv3" are D-Flow family variants covered in the D-Flow section — they share the LR naming but have different fill/infuse behavior and higher frame temperatures (LRv2: 89°C, LRv3: 90°C, vs 88-89°C here).
How it works: Fast fill at 2 bar, then spring-lever soak at ~3 bar until dripping appears, then 9 bar declining pressure. If puck erodes too fast (flow exceeds 2.8 ml/s), switches to flow control at 2.5 ml/s to prevent gushing.
Expected curves: 3 bar hold during soak (dripping phase), then 9 bar peak with gradual decline. Pressure stays high (9→8 bar) because flow is held constant. Flow relatively constant throughout.
Temperature: 88-89°C (stepping down slightly during extraction)
Grind: Fine (same as Cremina, finer than E61/80s)
Extraction: Highest of dark roast profiles — most full-bodied cup
Flavor: Dark chocolate, smooth. Rich and bold.
Roast: Excellent for dark (full body without harshness). Good for medium-dark. Struggles with light.
DO NOT flag the 3 bar soak phase as "low pressure" — it's intentional pre-infusion.

## Turbo Shot
Also matches: "Hendon Turbo", "Hendon Turbo 6b Pressure Decline", "Hendon Turbo Bloom", "Hendon Turbo Flow"
Category: Flow/Pressure hybrid
How it works: Inspired by the Hendon/Cameron 2020 paper on espresso extraction — coarser grinds with fast flow reduce channeling and can achieve equal or higher extraction yield than fine grinds, with more clarity. Preinfusion at 8 ml/s flow until ~6 bar, then extraction at 4.5 ml/s with a 6 bar pressure limiter. Very short total extraction. Jan's original Hendon Turbo variants use a declining pressure (6b Pressure Decline) or flat flow (Hendon Turbo Flow) approach; the Bloom variant adds a very short ~5s bloom before extraction.
Expected curves: Fast pressure rise to ~6 bar, then high flow at 3–4.5 ml/s throughout extraction. Shot finishes in 20–25s total. Fast flow and short time are INTENTIONAL.
Duration: 20-25s (Jan's preferred range; original paper suggests 16-20s)
Dose: 15-18g (smaller than traditional)
Ratio: 1:2.5–1:3 (higher ratio compensated by fast extraction)
Temperature: 90–97°C — higher temperature needed to compensate for fast flow (water has less contact time to transfer heat, so higher set temperature needed to reach same extraction temp at puck). Jan uses 95–97°C for PI.
Grind: Coarse — much coarser than traditional espresso. Best with large flat burrs (SSP HU, EK-style, EG1, MAX, Ultra). Conical grinders have less margin for error.
Flavor: Extremely clean, high-clarity, high aroma, acidity and fruited sweetness, moderate body. EY competitive with non-blooming profiles at high temperature (~22–24%). Less suited to dark roasts.
Roast: Light and medium primarily. High-extraction flat burr grinders excel here.
DO NOT flag short shot time, high flow, high temperature, or high ratio as problems — these are all defining features of the Turbo approach.
AnalysisFlags: grind_check_skip

## Filter 2.0
Also matches: "Filter 2.1"
Category: Filter (Blooming variant)
AnalysisFlags: flow_trend_ok
How it works: Bloom phase then flow-controlled extraction at 3 ml/s with <3 bar pressure.
Temperature: 92°C preinfusion, 85°C percolation (Filter 2.1 uses 98°C preinfusion declining to 84°C)
Dose: 20-22g in 24g basket
Ratio: 5:1 extraction, then dilute with 225-250g water
Flow: 3 ml/s (essential)
Pressure limit: 3 bar safeguard
Grind: Finer than filter, not quite espresso
Setup: Two micron 55mm paper filters in bottom, metal mesh on top
Extraction yield: 23-26% at 1.4-1.5% strength
Roast: Lower temperature for darker or defective roasts
DO NOT flag low pressure or high ratio as problems.

## Filter3
Creator: Scott Rao (profile design and basket design)
Category: Filter (no-bypass)
AnalysisFlags: flow_trend_ok
How it works: No-bypass filter coffee using the Filter3 basket. Prewet at 5 ml/s for 15s, then 30s bloom (zero flow), then slow percolation at 1.1 ml/s through four extraction steps at three declining temperature levels (92→92→90→88°C water temp), ending with a 10s drawdown at zero flow. Uses water temperature sensor (not coffee sensor). All water passes through the coffee bed — no bypass. Scott Rao calls the DE1 with Filter3 "the world's best single-cup filter-coffee machine."
Expected curves: Very low pressure throughout (well under 5 bar limiter on prewet, near zero during percolation). Flow flat at 1.1 ml/s during extraction. Temperature steps down across phases.
Temperature: 94°C prewet/bloom, 92°C early extraction, declining to 88°C late extraction (water sensor). The declining temperature is intentional — it compensates for extraction efficiency increasing as the brew progresses.
Dose: 22g in Filter3 basket
Grind: As coarse as the grinder allows — target 660–720 µm particle size (slightly coarser than V60). Most people grind too fine. If water comes out of the holes at the top of the basket, grind is too fine.
Setup: Requires Filter3 basket and appropriate paper (Decent precut, hand-cut Chemex, or Pulsar — most other espresso paper is not porous enough). Remove the portafilter spring. Center paper in the bottom of the basket, wet it with flush water. Add 22g of coffee and shake to level. Set water level refill to at least 500 ml in Settings/Machine to ensure enough water.
Flow calibration: Before first use, run the full profile without a portafilter and weigh the output. Target ~75g at the end of the first step (Prewet) and ~360 ml total. Adjust individual step flow rates (not the global calibration) to hit these targets — changing global calibration may affect espresso profiles.
Dialing in: At ~100s into the profile, unlock the portafilter, swirl, and check. 2–4 cm of water standing above the grounds = grind is correct. If less than 2 cm, grind finer. If more than 4 cm (or pooling), grind coarser. Drawdown after the profile ends should be under 60 seconds (ideally ~30s) — if longer, grind is too fine.
Output: 300-330g of coffee in the cup. Use stop-at-weight set ~30g below target to account for residual water draining from the basket after the profile ends.
Extraction targets: EY 22–23%, TDS 1.4–1.5%.
Duration: ~275s total (~4.5 minutes)
Flavor: Filter-like clarity with no bypass dilution. Clean, bright, high-definition filter coffee.
Roast: Good for light and medium roasts.
DO NOT flag low pressure, long brew time, or very coarse grind as problems — this is filter-style brewing, not espresso.
DO NOT flag declining temperature as a problem — the temperature steps are intentional for balanced extraction across the long brew time.

## Sprover
Category: Filter simulation
How it works: Intentionally under-dose the basket (e.g. ~15g in a 20g basket), coarse grind, low pressure. Simulates pour-over coffee through the espresso machine.
Dose: Intentionally under-dose: ~15g in a 20g basket
Grind: Coarser than allonge
Flow: 1.5-4.5 ml/s
Pressure: Keep below 1.5 bar during fill
Ratio: 1:10-1:14 (ultra-high, intentional)
Duration: ~100s
Starting point: 15g:195g, 2 ml/s, ~100s
DO NOT flag ultra-high ratio or ultra-low pressure as problems — this is filter simulation.

## Gentle & Sweet
Also matches: "Gentle and sweet"
Category: Pressure
How it works: 6 bar constant pressure, no pre-infusion. Water fills headspace and immediately goes to pressure. As puck erodes, flow increases from ~2 ml/s to 3+ ml/s.
Expected curves: Flat 6 bar pressure. Flow starts ~2 ml/s and increases steadily as puck erodes. This increasing flow is NORMAL for constant pressure profiles.
Temperature: 88°C — moderate temperature is intentional for this profile's gentle extraction character
Duration: Fast shot expected (~25s). Not a long shot.
Grind: Does not need as fine a grind as Blooming or Adaptive. Good for beginner grinders.
Extraction: ~18-19% (lowest of light roast profiles)
Roast: Good beginner-friendly entry for light roasts. Lower pressure means less channeling risk.
DO NOT flag increasing flow rate as a problem — it's the natural result of constant pressure with puck erosion.
DO NOT recommend increasing temperature — the moderate 88°C is by design to produce a gentler, sweeter extraction. Higher temperatures would shift the flavor toward bitterness and undermine the profile's purpose.
AnalysisFlags: flow_trend_ok

## Extractamundo Dos
Category: Pressure
How it works: Fill at high pressure, then a dynamic bloom phase (flow drops to zero, pressure decays to ~2.2 bar), then rise to 6 bar for extraction. The bloom phase temperature drops to 67.5°C — this is intentional and controls extraction character.
Expected curves: Pressure spike during fill, then decay during bloom (zero flow, declining pressure), then 6 bar target pressure with flow increasing over time. May not quite reach 6 bar with same grind as G&S (bloom reduces puck resistance).
Temperature: 83.5°C fill, 67.5°C bloom, 74.5°C extraction — the low and varying temperatures are intentional
Duration: ~35–50s total (preinfusion ~5s, bloom ~15–25s adaptive, extraction ~15–20s)
Grind: Slightly finer than G&S if you want to reach same pressure
Extraction: Slightly more than G&S due to pre-infusion/bloom
Flavor: More acidic/sharp, fruitier, juicier than G&S. More concentrated.
Roast: Good for light. Community favorite for consistent light roast espresso. Good for beans with slight off-flavors.
DO NOT flag the zero-flow bloom phase or the low/varying temperatures as problems — the dynamic bloom and temperature changes are core to this profile's design.
DO NOT flag channeling in dC/dt — the zero-flow bloom followed by extraction ramp always produces large conductance derivative spikes regardless of puck quality.
AnalysisFlags: flow_trend_ok, channeling_expected

## Flow Profile
Also matches: "Flow profile for straight espresso", "Flow profile for milky drinks"
Category: Flow
How it works: Maintains constant flow rate throughout extraction. Pressure adapts to puck resistance. Straight espresso variant uses 2.0 ml/s; milky drinks variant uses 1.2 ml/s.
Expected curves: Flat flow at target rate. Pressure varies based on grind and puck condition — this is normal for flow-controlled profiles. Pressure limiter caps at 8.6 bar.
Temperature: 92°C (straight espresso), 88°C (milky drinks)
Grind: Medium-fine, similar to pressure profiles. Adjust grind to keep pressure in 6-8.6 bar range during extraction.
Flavor: Fruitier, more nuanced than Default. More complex flavor development. Milky drinks variant produces a sweeter, less acidic base for milk.
Roast: Good for medium. Good for milky drinks.
DO NOT flag varying pressure as a problem — the machine controls flow, not pressure.
DO NOT flag pressure fluctuations during extraction as channeling — pressure naturally moves as puck resistance changes under constant flow.

## Cremina lever machine
Also matches: "Cremina"
Category: Lever
How it works: Lever machine emulation. Fast fill, brief soak, then steep pressure decline. Flow rate drops as shot progresses (opposite of constant-pressure profiles). Produces maximum mouthfeel.
Expected curves: Pressure peaks at 9 bar then STEEPLY declines (much more than Londinium). Flow starts moderate and DECREASES. This declining flow is the key feature — it keeps coffee concentrated as puck erodes.
Temperature: 91.5-92°C (rises slightly during ramp-down phase)
Duration: ~45s (longer than other lever profiles)
Grind: Fine (same as Londinium)
Flavor: Maximum mouthfeel, thick syrupy espresso. Baker's chocolate, intense. Very thick crema. "King of chocolate."
Roast: Excellent for dark. Highest temperature of dark profiles = most extraction = most intense.
DO NOT flag steeply declining pressure or declining flow as problems — this IS the Cremina style.
DO NOT flag channeling in dC/dt — the soak→ramp transition (flow jumps from ~0 to 2.5 ml/s as pressure ramps 1→9 bar) always produces large conductance derivative spikes regardless of puck quality. dC/dt is only diagnostic on flat-pressure or constant-flow profiles.
AnalysisFlags: flow_trend_ok, channeling_expected

## Idan's Strega Plus
Also matches: "Strega Plus", "Strega Plus medium", "Strega Plus dark", "Idan Strega"
Category: Lever emulation
Creator: Idan
How it works: Simulates a Bezzera Strega lever machine using a technique Idan developed over years of lever manipulation. Gradual water filling combined with a blooming phase saturates the puck evenly before extraction. After the bloom, extraction proceeds at low flow (1–1.5 ml/s) with sustained pressure — passive lever resistance rather than active pump push. Two variants: medium (1:1–1:2) and dark (1:2–1:2.5).
Expected curves: Slow fill, extended bloom (pressure rises then decays), then low-flow extraction. Flow of 1–1.5 ml/s throughout is NORMAL AND INTENTIONAL — do not flag as too slow.
Temperature: Tested with medium to medium-light roasts on a Niche Zero; temperature, trigger pressures, and peak pressure may need adjustment for other grinders or roasts.
Duration: ~50–70s (longer than standard espresso — lever style)
Ratio: 1:1 to 1:2 (medium variant), 1:2 to 1:2.5 (dark variant)
Flavor: High texture, strong flavor, rich body — optimized for milk-based drinks, especially dry cappuccino. NOT designed for clarity or flavor separation — those are not its strengths. Expect a distinctive, full-flavored cup for each bean.
Roast: Medium to medium-light. Dark variant for slightly darker beans.
AnalysisFlags: flow_trend_ok, channeling_expected
DO NOT flag slow flow (1–1.5 ml/s), long duration, or low ratio as problems — all are defining characteristics of the Strega simulation.
DO NOT flag channeling in dC/dt — the blooming phase (near-zero flow) followed by extraction onset produces conductance derivative spikes unrelated to puck quality. This is a milk-drink texture-first profile.

## 80's Espresso
Category: Lever at low temperature
How it works: Lever profile at very low temperature (82→72°C declining). Fast preinfusion fill at 7.5 ml/s, then 7.8 bar declining to 5 bar. Intentionally under-extracts to minimize tar/burnt flavors from dark beans.
Expected curves: Lever-style declining pressure (7.8→5 bar). Temperature starts at 82°C and declines toward 72°C. Flow should be SLOW (ideally 1-1.2 ml/s for best results). If flow peaks at 1.9+ ml/s, shot will be dusty/thin — slower is better.
Duration: 35-45s is ideal (lever-style timing). 25s is too short.
Grind: Medium (coarser than Londinium/Cremina, finer than E61)
Flavor: Bread fruit, raisins, baker's chocolate without burn. Minimizes tar flavors.
Best with 12g waisted basket for maximum mouthfeel and minimal channeling. 1:1 ratio (Italian style) also works well.
Roast: Dark only. Designed specifically to get fruitiness from dark beans by using low temperature.
DO NOT flag low temperature as a problem — it's the entire point of this profile.
AnalysisFlags: flow_trend_ok

## Best Overall
Category: Lever (simple 3-step)
How it works: Three-step: fast fill → 8.4 bar peak → declining pressure to ~6 bar. No long soak time (unlike Londinium). Traditional pressure-controlled shot.
Expected curves: Fast fill in ~5s, pressure rises to 8.4 bar, then gradually declines to ~6 bar. Flow stays relatively constant as declining pressure compensates for puck erosion.
Temperature: 88°C
Grind: Can use same fine grind as Londinium/Cremina for a longer ~45s shot, or coarser for faster shot.
Flavor: Less extracted than Londinium/Cremina. Smooth, easy drinker. Not much acidity. Less crema than Cremina.
Best for: Milk drinks (flat whites, lattes). From Scott Rao's espresso book appendix.
Roast: Good for dark. Good for medium.
AnalysisFlags: flow_trend_ok

## E61
Also matches: "E61 classic gently up to 10 bar", "E61 rocketing up to 10 bar", "E61 with fast preinfusion to 9 bar"
Category: Flat pressure
How it works: Flat 9 bar constant pressure with optional short preinfusion (4–10s at 4 bar). Flow INCREASES as puck erodes — this is the characteristic E61 behavior. The rocketing variant ramps to 10 bar; the fast preinfusion variant uses a 6s fill. All variants share the same extraction character.
Variants: "E61 classic gently up to 10 bar" ramps gently to 10 bar; "E61 rocketing up to 10 bar" goes to 10 bar (requires technician tuning, great for milk drinks); "E61 with fast preinfusion to 9 bar" uses 6s fast preinfusion for better evenness.
Expected curves: 9 bar constant pressure throughout. Flow starts moderate and increases steadily. This increasing flow causes characteristic sourness/acidity in longer shots.
Duration: 22-25s (shorter than lever profiles). Longer shots get increasingly sour due to rising flow.
Grind: Coarsest of all dark roast profiles (coarser than 80s, which is coarser than Londinium/Cremina)
Temperature: 92°C (but historically very variable on real E61 machines)
Flavor: Some balanced acidity (which can be pleasant). Baker's chocolate. Less smooth than lever profiles.
Roast: Good for dark (balanced acidity). Can be edited via 3-step editor to add pre-infusion or declining pressure.
Adjustable: Use the 3-step editor to add pre-infusion (line pressure soak) or declining pressure (4-6 bar end point gives pleasing acidity balance). Avoid declining to less than 4 bar — too steep a decline mutes flavors and removes the pleasant acidity balance.
DO NOT flag increasing flow as a problem — it's characteristic of flat 9 bar profiles.
AnalysisFlags: flow_trend_ok

## Classic Italian / Traditional Flat Pressure
Also matches: "Classic Italian espresso", "Gentler but still traditional 8.4 bar", "Italian Australian espresso"
Category: Flat pressure
Creator: Luca Frangella (Classic Italian Espresso profile)
How it works: Short preinfusion (4–8s at 4 bar) then flat pressure extraction — 9 bar for Classic Italian, 8.4 bar for the gentler variant, 8.7 bar for Italian Australian. The lower pressure of the gentler variant (8.4 bar) is more forgiving of puck prep. Italian Australian uses lower temperature (88°C) to prevent overextraction of dark beans. All variants produce increasing flow as the puck erodes.
Expected curves: Pressure holds flat throughout extraction. Flow starts moderate and increases as puck erodes. This is normal for flat-pressure profiles.
Temperature: 94°C (Classic Italian), 89.5°C (Gentler 8.4 bar), 88°C (Italian Australian)
Duration: ~25–30s (intentionally shorter than typical DE1 profiles — classic Italian style)
Ratio: ~1:1.8 (e.g. 14g in / 25g out)
Grind: Medium-fine. Gentler 8.4 bar is the most forgiving of the three.
Preinfusion fill: 8 ml/s (fast fill — creator confirmed; not the old 4 ml/s)
Basket: IMS Big Bang 14–16g recommended; Decent waisted 14g also works. Waisted baskets are more forgiving. 7g basket blows the puck away — use 12g or 14g for Italian-style shots.
Headspace: Critical — dry puck must be within 1–2mm of shower screen ("coin test"). Without correct headspace the profile does not behave as designed.
Bean selection: Medium to dark, washed preferred. Tropical fruit-smelling beans will not produce good results regardless of roast label.
Use case: Cappuccino, milk drinks, ristretto-style pulls, traditional café replication. 9 bar limit set deliberately — higher pressure is unpleasingly bitter for this style.
Roast: Medium to dark. Italian Australian specifically targets dark roasts at lower temperature. Classic Italian emulates mainstream café espresso.
DO NOT flag increasing flow as a problem — this is the expected behavior for flat-pressure profiles.
DO NOT flag ~25s extraction as under-extracted — Classic Italian is intentionally a short, concentrated shot.
AnalysisFlags: flow_trend_ok

## Traditional / Spring Lever Machine
Also matches: "Traditional lever machine", "Low pressure lever machine at 6 bar", "Two spring lever machine to 9 bar"
Category: Lever
How it works: Spring-lever machine emulations. Fill at 4 bar to saturate the puck, then pressure peaks and gradually declines over ~27s as the spring decompresses. Traditional lever: 9 bar peak. Low pressure 6 bar: 6 bar peak, sweeter and gentler. Two spring: 9 bar peak held longer (second spring maintains pressure before declining).
Expected curves: ~20s preinfusion at 4 bar (drip-fill), then peak pressure with gradual decline. Declining pressure is INTENTIONAL — this is a spring lever. Flow stays moderate throughout.
Temperature: 92–94°C
Duration: ~35s total extraction
Grind: Medium (coarser than Londinium, finer than E61)
Flavor: Sweet and smooth. Low pressure 6 bar variant is the sweetest and most gentle. Two spring maintains more concentrated flavor from sustained peak pressure.
Roast: Medium to dark roasts. Low pressure 6 bar suited for medium-sweet beans.
DO NOT flag declining pressure as a problem — spring decompression is exactly how lever machines work.
AnalysisFlags: flow_trend_ok

## Advanced Spring Lever
Also matches: "Advanced spring lever", "Weiss advanced spring lever"
Creator: John Weiss (JW)
Category: Lever (with flow recovery)
How it works: Spring-lever emulation with an added flow-limit safety valve: fill → rise to 9 bar → declining pressure → if puck erodes too fast (flow exceeds limiter), switches to 1.5 ml/s flow-controlled extraction to prevent gushing. The Weiss variant uses 90°C (vs 88°C) and is tuned slightly differently. JW updated this to ASL2 (Advanced Spring Lever 2) with refined step transitions.
Expected curves: 9 bar peak then gradual pressure decline. Flow stays moderate. If a flow-control step kicks in (visible as pressure drop then stabilization), that is the safety valve working — not a problem. A pressure notch/dip at a consistent point during extraction is a SIGN OF PROPER DIAL-IN, not a flaw — this means the puck is behaving as intended. JW: "I actually aim for that result."
Temperature: 88°C (Advanced), 90°C (Weiss)
Duration: ~35–45s
Dose: 18g target → 32g out
Grind: All roasts — the flow recovery step makes these more tolerant of grind variation.
Roast: All roasts. Good for shots where you're not sure if the puck will channel.
Note: Use "Advanced spring lever" (not "Weiss advanced spring lever") — the Weiss variant has a known skip-first-step bug where the initial soak step may be bypassed under certain flow conditions.
DO NOT flag the flow-control recovery step as a problem — it is the profile's designed choke-rescue mechanism.
DO NOT flag a pressure notch or dip during extraction as a problem — it is a sign the shot is dialing in correctly.
AnalysisFlags: flow_trend_ok

## Best Practice Light Roast
Also matches: "Best practice (light roast)"
Creator: John Buckman (Decent founder) — synthesized from Brakel's preinfusion technique, Scott Rao's Blooming, and Jonathan Gagné's Adaptive flow-locking. This profile represents the distillation of what the Decent community learned was optimal for light roasts; it subsequently evolved into the "Adaptive" profile.
Category: Adaptive/Blooming hybrid
How it works: Unites the best practices learned from the Decent: Brakel's Londinium preinfusion technique, Rao's Blooming, and Gagné's Adaptive flow-locking. Preinfusion at low pressure (3 bar) until dripping, then a 1.5 bar gentle soak to fully saturate the puck, then pressure ramps to ~9–10.5 bar, then flow-controlled extraction at ~2.5 ml/s with a 10.5 bar pressure limiter. Canonical recipe: 18g in, 50g out, in ~60 seconds.
Expected curves: Very low pressure during fill/soak (~1.5–3 bar), then pressure rise to ~9–10 bar, then pressure-limited flow extraction. Pressure may not reach the limiter if grind is coarse. Variable curves are expected — the profile adapts to grind.
Temperature: 92°C preinfusion, 90°C extraction
Duration: ~50–70s total (longer than most due to extended preinfusion soak)
Dose: 18g → 50g out (~1:2.8)
Grind: Coarse for light roasts (target ~2.5 ml/s extraction flow). Grind fine enough to maintain dripping during preinfusion; if dripping stops completely, grind is too coarse.
Roast: Light roasts primarily. Can adapt to medium with temperature adjustment.
DO NOT flag the long low-pressure soak phase as a problem — it is the intentional bloom/preinfusion designed for light-roast pucks.
DO NOT flag channeling in dC/dt — the extended low-flow soak (1.5 bar) followed by pressure ramp to 9–10 bar produces large conductance derivative spikes regardless of puck quality.
AnalysisFlags: flow_trend_ok, channeling_expected

## Easy Blooming Active Pressure Decline
Also matches: "Easy blooming - active pressure decline"
Category: Blooming (adaptive)
AnalysisFlags: flow_trend_ok, channeling_expected
How it works: Created by Stéphane as an accessible evolution of Rao Blooming. The key innovation is a pressure-threshold bloom exit: rather than a fixed timer, the bloom phase ends when pressure declines to a threshold value. This makes bloom duration self-adjust to grind coarseness — finer grinds hold pressure longer (longer bloom), coarser grinds depressurize quickly (shorter bloom). This eliminates the need to adjust bloom time when changing grind size, making it far more forgiving than Rao Blooming. After bloom, active pressure decline reduces pressure from 7 bar toward a lower endpoint as the shot progresses, preventing over-extraction and managing increasing flow as the puck erodes.
Expected curves: ~4 bar during fill, declining pressure during bloom (zero flow, pressure falls from 4 to threshold ~2 bar), then 6–7 bar extraction with gradual pressure decline. Zero flow during bloom is INTENTIONAL. Variable bloom duration is NORMAL and expected — it is the adaptive feature working correctly.
Temperature: 88°C default (adjustable 84–90°C for different roasts; flat burrs can tolerate higher temperatures)
Duration: ~25–45s (varies with grind coarseness and bloom duration; coarser = shorter bloom = shorter total)
Dose: 20g → 40–50g (1:2–2.5)
Grind: Flat burrs preferred (handle higher flow with less channeling); conical grinders compatible with higher temperature settings (raise temp by ~2°C).
Roast: Light roasts preferred. Dark roasts work with lower temperature presets (84–86°C).
DO NOT flag variable bloom duration or zero flow during bloom as problems — the pressure-threshold bloom exit and variable timing are the core adaptive features.
DO NOT flag channeling in dC/dt — the zero-flow bloom followed by extraction ramp produces large conductance derivative spikes regardless of puck quality.

## Gagné Adaptive
Also matches: "Gagné/Adaptive Shot 92C v1.0", "Gagné/Adaptive Allongé 94C v1.0", "Gagne Adaptive Shot", "Gagne Adaptive Allonge"
Category: Adaptive/Flow
How it works: Created by Jonathan Gagné (Coffee ad Astra). An evolution of Rao Blooming that adapts to your grind — preinfusion at 3.5–4 ml/s until 4 bar, then pressure rises to 8.6 bar, then "scan" frames detect the current flow at peak pressure and lock it in for the rest of the shot. You dial in by targeting a flow rate with your grind, not by chasing a pressure curve. Finer grind → lower flow (~2.2 ml/s standard espresso style); coarser grind → higher flow (~4 ml/s Allongé style). The Allongé variant at 94°C targets longer shots (1:4+) with coarser grind.
Expected curves: Pressure rises to 8.6 bar then stabilizes around 6 bar as flow control takes over. Flow adapts to grind — coarser grinds yield higher flow. Variable curves are EXPECTED — the profile detects grind resistance and adapts.
Temperature: 92°C (Adaptive Shot), 94°C (Adaptive Allongé)
Duration: ~25–45s (Adaptive Shot), longer for Allongé
Dose: 20g → 40–50g (Adaptive Shot); higher ratio for Allongé
Grind: Flat burrs preferred. Dial grind to target the flow rate you want — finer for ~2.2 ml/s standard espresso, coarser for ~4 ml/s Allongé-style. The profile will lock in whatever flow it sees at peak pressure.
Flavor: Light roasts: bright, layered, fruited. At Allongé ratios: filter-like clarity with less body. At standard espresso ratios: concentrated and structured. The flow-locked extraction produces consistent character across a wide range of grind settings.
Roast: Light roasts primarily (Adaptive Shot). Allongé: all roasts, optimized for light. For dark roasts, lower temperature to 84–86°C.
DO NOT flag variable pressure curves or multi-step scan frames as problems — the profile is actively detecting and adapting to grind resistance.
AnalysisFlags: flow_trend_ok

## I Got Your Back
Category: Adaptive (grind-invariant)
How it works: Created by Shin. The goal is to never fail to produce an acceptable espresso at any grinder setting. During preinfusion, the profile detects puck resistance via pressure: if resistance is LOW (coarse grind — pressure stays low), it routes immediately to a flat 2.2 ml/s flow extraction; if resistance is HIGH (fine grind — pressure peaks), it triggers a bloom pause, then ramps pressure and transitions to flat flow. Both paths converge to stable flow-controlled extraction.
Expected curves: Variable by design — the pressure/flow path taken depends on grind resistance. Both paths produce stable flow-controlled extraction. Either curve shape is correct.
Temperature: 90°C
Duration: ~25–40s
Grind: Any — the profile detects resistance and routes itself to the appropriate extraction path
Flavor: Reliable baseline espresso at any grind setting. The coarse-grind (low-resistance) path produces cleaner, brighter cups; the fine-grind (bloom) path adds more body and complexity. Both produce drinkable espresso.
Roast: All roasts
DO NOT flag variable curves or unexpected path-switching as problems — the adaptive branching is the entire purpose of this profile.
DO NOT flag channeling in dC/dt — the bloom path (triggered by fine grinds) produces a zero-flow pause followed by extraction ramp, which always creates large conductance derivative spikes.
AnalysisFlags: channeling_expected

## TurboBloom
Category: Blooming/Turbo hybrid
AnalysisFlags: flow_trend_ok, grind_check_skip, channeling_expected
How it works: Created by Collin as a companion to TurboTurbo. Dynamic bloom into high-flow pressure extraction, targeting high-extraction grinders with flat burrs. Fast fill at 8 ml/s, then dynamic bloom until pressure drops to 2.2 bar (approximately 5s — very short bloom by design), then ramps to 6 bar extraction with a 4.5 ml/s flow limiter. The short, fast bloom is intentional: the fast fill saturates the puck quickly, allowing the bloom to be brief while still achieving even extraction, which in turn permits a higher flow rate during extraction.
Expected curves: Fill spike, then bloom decay (zero flow, declining pressure), then 6 bar extraction with high flow (3–4.5 ml/s). Zero flow during bloom is INTENTIONAL. High flow during extraction is EXPECTED.
Temperature: 86°C fill, drops to 70°C during bloom (intentional to reduce harshness), ramps to 80°C extraction — these temperature variations are all intentional.
Duration: ~25–35s
Dose: 15g → 45g (1:3 ratio — same as TurboTurbo; optimized for high-extraction burrs that can achieve this ratio)
Grind: Coarse (high-extraction burrs, targeting 3–4.5 ml/s final flow)
Flavor: Same tasting notes as TurboTurbo but cleaner, less astringency. The bloom phase reduces harshness compared to TurboTurbo's no-bloom approach.
Roast: All roasts, optimized for high-extraction grinders.
DO NOT flag zero flow during bloom, low bloom temperature, high extraction flow, or the short bloom duration as problems — all are intentional design elements.

## TurboTurbo
Also matches: "Turboturbo"
Category: Turbo (no bloom)
AnalysisFlags: flow_trend_ok, grind_check_skip
How it works: Created by Collin and Jan. High-extraction turbo shot without a bloom phase. Rapid fill/preinfusion at 96°C to saturate the puck, then 6 bar extraction at 93°C with a 4.5 ml/s flow limiter. Companion to TurboBloom for when bloom-style is not wanted. The original design used 97°C/8ml/s preinfusion, refined to 96°C/4ml/s for better consistency. The high temperature is justified by the coarse grind: coarser grounds have more exposed surface area and less thermal mass to heat, so higher water temperature is needed to achieve the same extraction temperature at the puck.
Expected curves: Fast preinfusion pressure rise, then 6 bar with high flow (3–4.5 ml/s). Shot is fast and high-flow by design.
Temperature: 96°C preinfusion, 93°C extraction — high temperatures are intentional and appropriate for the coarse grind.
Duration: ~20–30s
Dose: 15g → 45g (1:3 ratio, same as TurboBloom; high ratio achieved quickly by coarse grind and high flow)
Grind: Coarse (high-extraction burrs; much coarser than traditional espresso)
Flavor: High clarity, bright. Turbo shots produce clean, high-clarity espresso by reducing extraction of bitter/astringent compounds through short contact time at high flow. Less body than lever profiles, more like a concentrated filter coffee.
Roast: All roasts.
DO NOT flag high flow, fast duration, high temperatures, or high ratio as problems — these are intentional design elements for high-extraction coarse-grind setups.

## Nu Skool
Also matches: "Nu Skool 14g", "Nu Skool 18g", "Nu Skool 20g", "Nu Skool large basket"
Category: Flow/New wave light roast
Creator: Dan Calabro
How it works: A family of 3 flow-curve profiles (one per standard basket size) for maximally prepped light roast (or any roast) coffee. The philosophy: maximize extractability during prep (quality grinders, flat burrs, precise techniques) so you can brew with lower pressure, lower temperature, and coarser grinds while still achieving very high extraction with sweetness and clarity. Dial-in is done by adjusting three flow parameters: flow floor (minimum flow), flow plateau (target level), and flow spectrum (spread between floor and plateau). Reading Dan's user guide is essential before dialing in this profile.
Expected curves: Low-pressure preinfusion, then moderate flow-controlled extraction. No dramatic pressure spikes. Pressure stays moderate and gradually declines. Shot is calm and controlled.
Temperature: 82–89°C — substantially lower than standard espresso profiles. The very low temperature is INTENTIONAL; it avoids harsh extraction and only works when puck prep and grind quality are optimized.
Duration: ~25–40s
Grind: Coarser than most espresso profiles, similar in range to Turbo-style profiles. Flat burrs strongly preferred (SSP HU, EK-style, MAX, EG-1, etc.) — the profile was designed around high-quality flat burr grinders.
Flavor: High clarity, sweetness, vibrancy, and flavor definition. Designed to showcase light roast terroir and aromatics with minimal harshness.
Roast: Primarily light roast, but works with any roast. Quality of prep has a greater-than-normal impact on outcome — improving grinder and prep techniques directly improves results.
DO NOT flag very low temperature (82–89°C) or coarser grind as problems — they are central to the Nu Skool philosophy. These parameters require dialed-in preparation to achieve the intended result.

## Gentle Flat / Long Preinfusion Family
Also matches: "Gentle flat 2.5 ml per second", "Gentle preinfusion flow profile", "Hybrid pour over espresso", "Innovative long preinfusion"
Category: Gentle/Long Preinfusion Flow
How it works: A family of Seattle-style gentle profiles documenting John Weiss's technique sharing a very long preinfusion soak (10–37s) before extraction. The goal is to fully saturate light-roast pucks for even, channeling-free extraction. These are also called "Slayer-style" profiles after the Slayer espresso machine, which pioneered this technique. Community confirms "Innovative Long Preinfusion" is the closest equivalent to the classic Slayer shot.
Exact profile parameters:
- Gentle flat 2.5: 2.5 ml/s for 10s preinfusion, then 2.5 ml/s for 45s extraction at 6 bar flat pressure
- Gentle preinfusion flow: 3.5 ml/s for 30s preinfusion, then 2.2 ml/s for 21s extraction at 9 bar
- Hybrid pour over espresso: 2.0 ml/s for 25s to 1.5 bar, then 2.2 ml/s for 20s at 9 bar
- Innovative long preinfusion (Slayer-style): 1.5 ml/s for 37s to 2.0 bar, then 2.5 ml/s for 25s at 9 bar
Expected curves: Very long low-pressure preinfusion (near zero flow during soak), then pressure rise to extraction pressure. Pressure may be moderate (6 bar) or high (9 bar) depending on variant. The long preinfusion with minimal flow is INTENTIONAL.
Temperature: 92°C (most variants), 98°C (Innovative long preinfusion — very high is intentional for light roasts)
Duration: ~50–65s total (long by design)
Grind: Coarser to finer depending on variant — dial grind to achieve target flow during extraction.
Roast: Light to very light aromatic roasts. These profiles are specifically designed for beans that resist extraction at normal parameters.
Known issue (Innovative long preinfusion) — preinfusion skipping: Profile sometimes skips preinfusion entirely, producing ~25s short shots. Root cause: exit condition "move on if pressure > X bar" defaults to 1.0 bar, but machines with premium German pressure sensors register higher baseline pressure. Fix: raise the threshold to <2.0 bar.
Known issue (Innovative long preinfusion) — early stop at low yield: If extraction ends before target weight, grind coarser. The step has a 25s timeout; too-fine grind means flow never reaches the 2.5 ml/s move-on condition and the step times out short.
DO NOT flag the long preinfusion duration, low flow during soak, or temperature extremes as problems — these are defining features of this profile family.

## Preinfuse Then 45ml of Water
Category: Volume-based / Matt Perger technique
AnalysisFlags: flow_trend_ok
How it works: Matt Perger's technique: 25s preinfusion at 4 bar to fully saturate the puck, then extraction at 9 bar until a fixed 45ml of water has been delivered (not time- or weight-based). No scale required. The fixed water volume produces consistent results by bypassing scale dependency.
Expected curves: Preinfusion at 4 bar (low flow), then 9 bar flat extraction. Shot ends by volume, not weight or time.
Temperature: 90°C
Duration: ~40–50s
Grind: Medium-fine
Ratio: Output ratio depends on dose — approximately 1:2–2.5 for 18–20g doses with 45ml water.
Roast: All roasts.
DO NOT flag the volume-based stop condition as unusual — fixed water volume without scale is the entire purpose of this profile.

## 7g Basket
Category: Micro-espresso
AnalysisFlags: flow_trend_ok
How it works: Optimized for the Decent 7g mini-basket. Reduced flow rate and lower pressure (7.5 bar) protect the small, thin puck from channeling and blowthrough. Short overall duration.
Expected curves: Low flow during preinfusion (~2.4 ml/s at 4.5 bar), then 7.5 bar declining to ~4.5 bar. Lower pressure than standard profiles is INTENTIONAL for small puck integrity.
Temperature: 90°C (some community members find slightly lower temperatures, ~87–88°C, reduce bitterness in small doses)
Duration: ~16–20s
Dose: 7g → 18–28g out
Grind: Fine (small basket rewards fine grind for extraction efficiency)
Puck prep: Community consensus: sub-14g shots are significantly harder to dial in than standard doses. Essential practices: use a dosing funnel (small baskets spill easily), paper filter on top of the puck (improves flow evenness for small/thin pucks), level carefully. Starting profiles: Extractamundo Dos and Gentle & Sweet work well for single/small shots; both are more forgiving of thin puck issues than lever profiles.
Classic Italian note: 7g basket blows the puck away with Classic Italian-style profiles. Community recommends 12g basket (11g dose) or 14g waisted basket for Italian-style single shots.
Roast: All roasts.
DO NOT flag the lower peak pressure (7.5 bar) or short duration as problems — the small basket requires gentler parameters to maintain puck integrity.

## Espresso Forge
Also matches: "Espresso Forge Dark", "Espresso Forge Light"
Category: Manual machine emulation
How it works: Emulates the Espresso Forge piston-pump manual espresso machine. The Dark variant uses lower temperatures for fruit flavors from medium-dark beans; the Light variant uses very high temperature (96°C) and a long pre-brew soak for light roasts.
Espresso Forge Dark: Preinfusion at 6 ml/s to 3 bar, then 7.5 bar peak, then 30s gradual decline to 3 bar. Temperature 84°C preinfusion → 81°C → 78°C decline (intentional temperature drop during extraction).
Espresso Forge Light: Long 25s pre-brew soak at 1 bar, then high-pressure ramp to 10 bar over 20s, then 50s gradual decline to 6 bar. Temperature 96°C preinfusion → 92.5°C → declining (high temperature intentional for light roast extraction).
Expected curves: For both variants — rising pressure then long gradual decline. The extended decline (30–50s) is INTENTIONAL to emulate the forge's pressure profile. Flow stays moderate to low during decline.
Temperature: 84°C → 78°C declining (Dark), 96°C → declining to 92.5°C (Light) — temperature changes are intentional.
Duration: ~35–40s (Dark), ~60–70s (Light)
Dose: ~42g out
Roast: Medium to dark (Dark variant), medium to light (Light variant)
DO NOT flag gradual pressure decline, long extraction duration, or temperature changes as problems — all are core to the Espresso Forge emulation.
AnalysisFlags: flow_trend_ok

## Pour Over Basket
Also matches: "Pour over basket/Decent pour over", "Pour over basket/Kalita 20g in, 340ml out", "Pour over basket/V60 15g in, 250g out", "Pour over basket/V60 20g in, 340g out", "Pour over basket/V60 22g in, 375g out", "Pour over basket/Cold brew 22g in, 375ml out"
Category: Pour over (filter brewing through espresso machine)
AnalysisFlags: flow_trend_ok
How it works: Produces filter coffee using a pour-over basket or filter cone placed under the group head. Multi-pulse brewing with a prewet, bloom pause, and several water pulses. Water temperature near 100°C. Not espresso — pressure stays very low (near 0 bar) throughout.
Variants: V60 profiles use 8 ml/s high-turbulence flow to leverage V60's design. Kalita uses lower flow (6 ml/s) to prevent choking. Cold brew uses very low flow (2–2.5 ml/s) at 20°C water. Larger dose sizes (22g) use the same technique but take longer.
Expected curves: Very low to zero pressure throughout (gravity-fed, not pressure-extracted). Flow pulses between ~0 and target ml/s. Zero flow during bloom and pauses is INTENTIONAL.
Temperature: ~99–100°C (standard), ~20°C (cold brew) — cold water is intentional for cold brew.
Duration: ~70–105s (standard), ~180s (cold brew)
Dose: 15–22g depending on variant; output is 250–375g (filter ratio, not espresso ratio)
Grind: Coarser than espresso (filter grind). Must grind coarser than you would for a manual V60 — the basket creates much higher turbulence than hand-pouring. Even very coarse settings may not be coarse enough for some beans. Coarsen further if basket is choking. Ethiopian/decaf beans may need Kalita profile (lower flow) to prevent choking in V60.
Output volume: Default V60 profiles are designed to pour ~344ml total even for a 250g target — intentional, since gravity retains water after you remove the dripper. Add a SAV or delete the final step to change output.
Preheat: Preheat water reservoir before use (default profiles include this step; allow ~2 minutes).
Height clearance: V60 02 dripper is tall — may require removing drip tray and rotating machine 90 degrees to fit under group head.
Community: Opinion is divided between V60 basket and Filter 2.1. V60 basket produces good pour over when dialed in; Filter 2.1 is easier and more consistent for most users. Profile stopping on first attempt is usually the "skip first step" bug — hard-reboot the machine (back switch).
Roast: All roasts. Filter brewing is especially good for light/medium roasts.
DO NOT flag zero or near-zero pressure, high water-to-coffee ratio, long duration, or cold water temperature (cold brew) as problems — this is filter brewing, not espresso.

## Trendy 6 Bar Low Pressure
Category: Flat pressure (light roast)
How it works: Long 20s preinfusion at 4 bar then flat 6 bar extraction. Lower pressure than traditional profiles reduces channeling risk and produces a sweeter, less astringent cup from beans that resist extraction at 9 bar. Specifically designed for sophisticated light roasts that smell great but resist being well extracted at standard pressure.
Expected curves: 4 bar during preinfusion, then flat 6 bar extraction. Flow increases over time as puck erodes — normal for constant-pressure profiles. Pressure stays at 6 bar throughout extraction.
Temperature: 92°C
Duration: ~35–55s extraction after preinfusion
Grind: Coarser than traditional (lower pressure requires slightly coarser grind to maintain flow)
Flavor: Sweet, rounded, less astringent than 9 bar profiles. Suited to aromatic light roasts that produce hollow, bitter, or sour results at standard pressure. The lower pressure coaxes sweetness from resistant beans without forcing harsh extraction.
Roast: Light roasts specifically — for sophisticated light roasts that struggle with standard 9 bar. Try this when your light roast smells great but tastes off at normal pressure.
DO NOT flag the 6 bar pressure as underpressure — the reduced extraction pressure is the point of this profile.
AnalysisFlags: flow_trend_ok

## GHC Manual Control
Also matches: "GHC/manual flow control", "GHC/manual pressure control", "GHC / manual flow control", "GHC / manual pressure control"
Category: Manual (user-controlled)
AnalysisFlags: flow_trend_ok
How it works: For machines with a Group Head Controller (GHC) — the user manually controls flow or pressure in real time by holding/releasing the GHC control. Not an automated profile. Used for baristas who want hands-on espresso control while logging the shot.
Expected curves: Entirely user-defined — whatever the operator produces. No automated pressure/flow targets. Any pattern of pressure or flow is valid.
Temperature: 88–96°C (configurable presets)
DO NOT flag any pressure or flow pattern as a problem — these profiles record whatever the human operator does. There is no "expected" curve to compare against.

## Tea
Also matches: "Tea/in a basket", "Tea portafilter/black tea", "Tea portafilter/Japanese green", "Tea portafilter/Chinese green", "Tea portafilter/Sencha", "Tea portafilter/white tea", "Tea portafilter/tisane", "Tea portafilter/oolong", "Tea portafilter/oolong dark", "Tea portafilter/Pressurized tea", "Tea portafilter/no pressure"
Category: Tea brewing
AnalysisFlags: flow_trend_ok
How it works: Brews loose-leaf tea using the portafilter basket or a dedicated tea portafilter. Water temperature and pressure are tuned per tea type — green teas use lower temperatures (~70–80°C) and near-zero pressure; black teas use higher temperatures (~90–95°C); oolongs fall in between. These are tea beverages, not espresso.
DO NOT analyze tea profiles for espresso grind advice, extraction yield, or shot technique — tea brewing has completely different parameters and goals.

## Cleaning and Maintenance
Also matches: "Cleaning/Forward flush", "Cleaning/Forward Flush x5", "Cleaning/Weber Spring Clean", "Cleaning/Descale Wizard", "Cleaning/Descale wizard", "Steam only", "Test/Flow calibration", "Test/pressure calibration", "Test/temperature calibration", "Test/temperature accuracy", "Test/for a small leak", "Test/low pressure leak", "Test/leakage stress test", "Test/profile_editor_demo"
Category: Maintenance / Diagnostic
How it works: These profiles perform machine cleaning, descaling, steam sterilization, and diagnostic tests. They are not beverage extractions.
DO NOT analyze these profiles for grind advice, extraction quality, or beverage technique — they are maintenance or calibration routines.
