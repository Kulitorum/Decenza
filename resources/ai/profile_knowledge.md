# Profile Knowledge Base — AI Reference

When analyzing a shot, use this section to understand what the profile was DESIGNED to do.
Do NOT flag intentional profile behaviors as problems.

## D-Flow / default
Also matches: "D-Flow / Q", "D-Flow / La Pavoni", "Damian's Q", "Damian's LRv2", "Damian's LRv3", "Damian's LM Leva", "Damian's D-Flow"
Category: Lever/Flow hybrid (Londinium family)
How it works: All D-Flow variants and Damian's profiles share the same core: pressurized pre-infusion with soak, then flow-controlled pour (default 1.7 ml/s). Pressure peaks then gradually declines — this is intentional lever-style behavior. Can heal uneven puck prep.
Variants: "D-Flow / default" is the starter profile. "D-Flow / Q" (also "Damian's Q") is optimized for medium-light beans. "D-Flow / La Pavoni" emulates a La Pavoni lever. "Damian's LM Leva" emulates a La Marzocco Leva — more creamy than chocolatey body.
Expected curves: Pressure peaks between 6 and 9 bar early, then declines as puck erodes. Flow stays near target (1.7-2.7 ml/s). Declining pressure is NORMAL and INTENTIONAL.
Damian's LRv2: If the puck erodes too fast during extraction, switches from pressure to flow control at 1.7 ml/s. This may cause a pressure drop — intentional puck-erosion recovery.
Damian's LRv3: Pure lever decline — does NOT switch to flow control. Simpler and preferred when dialed in well.
Damian's LRv2/LRv3 temperatures: Frame temperatures are 89°C (LRv2) and 90°C (LRv3) — higher than standard D-Flow variants due to different fill/soak behavior. These are standalone lever profiles that share the D-Flow family matching but behave more like the Londinium profile.
Temperature: Fill temperature varies by variant — D-Flow/default uses 88°C (frame temperature), while D-Flow/Q, La Pavoni, and Damian's Q use 84°C with a 94°C rise target. The low fill temperature is INTENTIONAL and controls bitterness — hotter fill water produces dark spots in the crema and more bitter taste. After filling, the target rises to 94°C, but the coffee never reaches 94°C — the high setpoint just makes the heater pump hot water that mixes with the cooler water above the puck, gradually reaching ~86-90°C at the basket (varies by fill temp and ambient conditions). This is a KEY aspect of D-Flow. DO NOT flag the large gap between temperature target (94°C) and actual (~86-90°C) as a problem — it is by design.
Flow calibration: If actual flow is consistently below target (e.g. 1.8 target but only 1.5 actual), the flow sensor may be over-reading by ~20%. Reducing the calibration value will show more pressure for the same grind.
Grind: Medium-fine. Grind determines curve shape: finer grinds produce constant-pressure extraction (boiler-like), coarser grinds produce declining pressure (lever-like). Both are valid — the pressure curve shape is a function of grind, not a problem.
Roast: All roasts. Excellent for medium (floral/fruity + chocolate). Good for light and dark.
DO NOT flag declining pressure as a problem — it defines this entire profile family.

## A-Flow
Category: Pressure-ramp into flow extraction
How it works: An alternative to D-Flow. Fill and optional infuse/soak, then pressure ramps UP to target (typically 9-10 bar), followed by optional pressure decline, then switches to flow-controlled extraction with a pressure limiter. The key difference from D-Flow: pressure intentionally RISES before extraction rather than starting high and declining.
Variants: "A-Flow / medium" is the default starter. "A-Flow / dark" optimized for dark roasts. "A-Flow / very dark" uses ramp-down for darkest beans. "A-Flow / like D-Flow" has a long 60s infuse and resembles D-Flow behavior.
Expected curves: Pressure ramps up to 9-10 bar during the ramp phase, may decline briefly, then flow takes over for extraction. Flow during extraction is typically ~2 ml/s with pressure capped by the limiter. The pressure ramp-up is INTENTIONAL — this is not overpressure.
Options: rampDownEnabled splits the ramp time between pressure-up and pressure-decline phases. flowExtractionUp ramps extraction flow smoothly upward (vs flat). secondFillEnabled adds an extra water fill before the pressure ramp.
Grind: Similar to D-Flow — medium-fine
Roast: All roasts. Variants optimized for medium through very dark.
DO NOT flag the pressure ramp-up phase as overpressure — the intentional rise to 9-10 bar before flow extraction is how this profile works.

## Adaptive v2
Category: Flow/adaptive
How it works: Ramps pressure toward ~9 bar (exits at 8.8 bar) for ~6 seconds, then adapts to grind coarseness by locking in whatever flow rate exists. Switches to stable flow after pressure peak. Extraction flow limiter at 9.5 bar.
Expected curves: Pressure peaks near 9 bar then gradually declines. Flow settles at 2.0-2.7 ml/s. Variable curves are expected — the profile adapts to grind size.
Duration: 26-40s. With large flat burrs, the extraction phase may be very short (rapid high-flow extraction in 15-20s after long pre-infusion soak).
Pre-infusion: Lots of dripping is fine with flat burr grinders (up to 17g dripping worked well in testing). Pressure drops steeply during drip phase with coarse/flat-burr grinds — this is normal.
Ratio: Best at 1:1.8-2.3 for light roasts (fruitiness). Can extend to 1:2.5 if no off-flavors.
Roast: Good for light (v2 updated for light roasts). Excellent for medium-light. Not recommended for dark.
DO NOT flag variable pressure curves or steep pre-infusion pressure drop as problems.

## Blooming Espresso
Category: Blooming
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

## Blooming Allonge
Also matches: "Blooming Allongé"
Category: Blooming/Allonge hybrid
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
AnalysisFlags: channeling_expected

## Default
Category: Lever
How it works: Classic lever-style declining pressure, 8.6 to 6 bar. Fast fill, immediate pressure rise, then decline.
Expected curves: Pressure peaks at 8.6 bar and declines to ~6 bar. Flow stays relatively constant as declining pressure compensates for puck erosion.
Temperature: 90°C (stepping down to 88°C during extraction)
Grind: Versatile — mimics 8.5 bar for fine grinds, Londinium for optimal grinds, limits flow for coarse grinds.
Flavor: Emphasizes chocolate intensity
Roast: Excellent for medium, good for dark. Not recommended for light.
DO NOT flag declining pressure as a problem — it defines lever-style profiles.

## Londinium / LRv3
Also matches: "Londonium"
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
Category: Flow/Pressure hybrid
How it works: Preinfusion at 8 ml/s flow until 2 bar, then extraction at 4.5 ml/s with a 6 bar pressure limiter. Very short extraction.
Expected curves: Fast pressure rise to ~6 bar, high flow at 4.5 ml/s. Shot finishes quickly.
Duration: 10-20s (speed is intentional)
Dose: 15-17g (smaller than traditional)
Ratio: 1:2-1:3
Temperature: 90°C default (presets range 86-92°C)
Grind: Medium-fine (coarser than typical espresso, fine enough to spike to 6 bar)
Roast: All coffee types
DO NOT flag short shot time or high flow as problems — speed is the point.

## Filter 2.0 / 2.1
Category: Filter (Blooming variant)
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
Category: Filter (no-bypass)
How it works: No-bypass filter coffee using the Filter3 basket. Prewet at 5 ml/s for 15s, then 30s bloom (zero flow), then slow percolation at 1.1 ml/s through four extraction steps at three declining temperature levels (92→92→90→88°C water temp), ending with a 10s drawdown at zero flow. Uses water temperature sensor (not coffee sensor). All water passes through the coffee bed — no bypass.
Expected curves: Very low pressure throughout (well under 5 bar limiter on prewet, near zero during percolation). Flow flat at 1.1 ml/s during extraction. Temperature steps down across phases.
Temperature: 94°C prewet/bloom, 92°C early extraction, declining to 88°C late extraction (water sensor). The declining temperature is intentional.
Dose: 22g in Filter3 basket
Grind: As coarse as the grinder allows (e.g. 11 o'clock setting on a Niche Zero, slightly coarser than V60). Most people grind too fine. If water comes out of the holes at the top of the basket, grind is too fine.
Setup: Requires Filter3 basket and appropriate paper (Decent precut, hand-cut Chemex, or Pulsar — most other espresso paper is not porous enough). Remove the portafilter spring. Center paper in the bottom of the basket, wet it with flush water. Add 22g of coffee and shake to level. Set water level refill to at least 500 ml in Settings/Machine to ensure enough water.
Flow calibration: Before first use, run the full profile without a portafilter and weigh the output. Target ~75g at the end of the first step (Prewet) and ~360 ml total. Adjust individual step flow rates (not the global calibration) to hit these targets — changing global calibration may affect espresso profiles.
Dialing in: 30 seconds into extraction, unlock the portafilter, swirl, and check for 2-4 cm of water on the grounds. Finer if not enough water, coarser if too much. Drawdown after the profile ends should be under 60 seconds (ideally ~30s) — if longer, grind is too fine.
Output: 300-330g of coffee in the cup. Use stop-at-weight set ~30g below target to account for residual water draining from the basket after the profile ends.
Duration: ~275s total (~4.5 minutes)
Flavor: Filter-like clarity with no bypass dilution. Described by Scott Rao as producing results from "the world's best single-cup filter-coffee machine."
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

## Extractamundo Dos
Category: Pressure
How it works: Fill at high pressure, then a dynamic bloom phase (flow drops to zero, pressure decays to ~2.2 bar), then rise to 6 bar for extraction. The bloom phase temperature drops to 67.5°C — this is intentional and controls extraction character.
Expected curves: Pressure spike during fill, then decay during bloom (zero flow, declining pressure), then 6 bar target pressure with flow increasing over time. May not quite reach 6 bar with same grind as G&S (bloom reduces puck resistance).
Temperature: 83.5°C fill, 67.5°C bloom, 74.5°C extraction — the low and varying temperatures are intentional
Grind: Slightly finer than G&S if you want to reach same pressure
Extraction: Slightly more than G&S due to pre-infusion/bloom
Flavor: More acidic/sharp, fruitier, juicier than G&S. More concentrated.
Roast: Good for light. Community favorite for consistent light roast espresso. Good for beans with slight off-flavors.
DO NOT flag the zero-flow bloom phase or the low/varying temperatures as problems — the dynamic bloom and temperature changes are core to this profile's design.

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
AnalysisFlags: flow_trend_ok

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

## Best Overall
Category: Lever (simple 3-step)
How it works: Three-step: fast fill → 8.4 bar peak → declining pressure to ~6 bar. No long soak time (unlike Londinium). Traditional pressure-controlled shot.
Expected curves: Fast fill in ~5s, pressure rises to 8.4 bar, then gradually declines to ~6 bar. Flow stays relatively constant as declining pressure compensates for puck erosion.
Temperature: 88°C
Grind: Can use same fine grind as Londinium/Cremina for a longer ~45s shot, or coarser for faster shot.
Flavor: Less extracted than Londinium/Cremina. Smooth, easy drinker. Not much acidity. Less crema than Cremina.
Best for: Milk drinks (flat whites, lattes). From Scott Rao's espresso book appendix.
Roast: Good for dark. Good for medium.

## E61
Category: Flat pressure
How it works: Flat 9 bar constant pressure. No pre-infusion, no declining pressure. Flow INCREASES as puck erodes — this is the characteristic E61 behavior.
Expected curves: 9 bar constant pressure throughout. Flow starts moderate and increases steadily. This increasing flow causes characteristic sourness/acidity in longer shots.
Duration: 22-25s (shorter than lever profiles). Longer shots get increasingly sour due to rising flow.
Grind: Coarsest of all dark roast profiles (coarser than 80s, which is coarser than Londinium/Cremina)
Temperature: 92°C (but historically very variable on real E61 machines)
Flavor: Some balanced acidity (which can be pleasant). Baker's chocolate. Less smooth than lever profiles.
Roast: Good for dark (balanced acidity). Can be edited via 3-step editor to add pre-infusion or declining pressure.
Adjustable: Use the 3-step editor to add pre-infusion (line pressure soak) or declining pressure (4-6 bar end point gives pleasing acidity balance). Avoid declining to less than 4 bar — too steep a decline mutes flavors and removes the pleasant acidity balance.
DO NOT flag increasing flow as a problem — it's characteristic of flat 9 bar profiles.
