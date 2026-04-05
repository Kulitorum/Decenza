# Profile Knowledge Base

Curated knowledge about Decent DE1 profiles — which beans they suit, expected flavor outcomes, and recommended parameters. This data feeds the AI advisor's system prompt and the automated shot summary tool.

Every claim is tagged with a source key (e.g., `[SRC:medium]`). See [Sources](#sources) at the bottom for full URLs. When updating this document, always tag new information with its source.

---

## KB File Format (`resources/ai/profile_knowledge.md`)

The runtime KB lives in `resources/ai/profile_knowledge.md`. It is parsed by `ShotSummarizer::loadProfileKnowledge()` and matched by `ShotSummarizer::matchProfileKey()`. This section documents the format so new entries can be added correctly.

### Section Header

Each entry begins with a `##` heading. The title becomes the primary lookup key after normalization (lowercase, diacritics stripped, punctuation normalized):

```
## D-Flow
## Blooming Espresso
## Filter 2.0
```

**Important:** If the title contains ` / ` (space-slash-space), the parser splits it and registers **each part** as a separate key. Use this deliberately for compound names (e.g. `## Traditional / Spring Lever Machine` registers both "traditional" and "spring lever machine"). Avoid it when one part would collide with another section's key — for example, `## D-Flow / default` would register "default" as a key and collide with `## Default`. In those cases, move the disambiguating part to `Also matches:` instead.

### Also matches:

Lists additional profile titles that should resolve to this entry. Each alias is registered as its own normalized key — it is **not** split on ` / `. Use this for variant names, typos, display-name variants, and legacy profile titles:

```
Also matches: "D-Flow / default", "D-Flow / Q", "Damian's LRv2", "Londinium / LRv3"
```

Matching is case- and accent-insensitive (`é` → `e`, etc.). Include the exact display title from the profile JSON `"title"` field.

### AnalysisFlags:

Space-separated (or comma-separated) flags consumed by `ShotAnalysis::generateLines()` and `ShotSummarizer::computeChannelingAnomaly()` to suppress false-positive warnings in the automated Shot Summary dialog:

```
AnalysisFlags: flow_trend_ok
AnalysisFlags: channeling_expected
AnalysisFlags: flow_trend_ok, channeling_expected
```

| Flag | Effect |
|------|--------|
| `flow_trend_ok` | Suppresses "Flow rose/dropped X ml/s" cautions. Use for lever profiles (declining flow is intentional) and constant-pressure profiles (rising flow is normal puck erosion). Do NOT use for flow-controlled profiles where flow deviation is informative. |
| `channeling_expected` | Suppresses the dC/dt channeling check entirely. Use only for profiles where channeling is inherent to the design (e.g. Allongé at 4.5 ml/s). |

### Field lines

All other lines are free-form `Field: value` pairs consumed verbatim by the AI system prompt and formatted for display in the profile KB viewer. Recognized conventions:

| Field | Purpose |
|-------|---------|
| `Category:` | Short descriptor shown in the viewer header area |
| `How it works:` | One-paragraph design intent |
| `Also matches:` | Alias list (parsed by code — see above) |
| `Expected curves:` | What pressure/flow graphs should look like |
| `Duration:` | Expected shot time range |
| `Temperature:` | Key temperatures and why |
| `Grind:` | Relative grind direction |
| `Roast:` | Roast suitability |
| `Flavor:` | Taste character |
| `Dose:` | Dose and ratio guidance |
| `DO NOT flag ...` | Explicit suppression instruction for the AI (plain sentence, no colon required) |
| `Note:` | Clarifications or disambiguation |
| `AnalysisFlags:` | Parsed by code — see above |

The display renderer (`ProfileSelectorPage.qml`) bolds lines matching `Label: value` where the label is ≤35 characters, italicizes `DO NOT` lines, and hides `Also matches:` and `AnalysisFlags:` lines entirely.

### knowledge_base_id in profile JSON

Each profile JSON can include a `"knowledge_base_id"` field that caches the resolved KB key so it survives profile renames and Save As. Its value should match the normalized form of the section title **or** one of its aliases. When a profile's `knowledge_base_id` is set, the shot summarizer skips the fuzzy title match and goes directly to that key.

### Matching priority

`matchProfileKey()` tries in this order:
1. Direct key lookup on the normalized profile title (or `knowledge_base_id`)
2. Prefix match — title starts with a known key, or a known key starts with the title
3. Substring fuzzy match — known key (≥4 chars) is contained within the normalized title
4. Editor-type fallback — `dflow` → D-Flow entry, `aflow` → A-Flow entry

---

## The 4 Mother Categories

All DE1 profiles descend from four fundamental approaches. `[SRC:4mothers]`

| Category | Pressure Style | Extraction Yield | Best Roast | Character |
|----------|---------------|-------------------|------------|-----------|
| **Lever** | Peak ~8.6 bar, declining | 19-21% (lowest) | Dark to medium-dark | Rich chocolate, Italian-style, shortest contact time |
| **Blooming** | Extended preinfusion, gentle extraction | Highest | Medium to light | Maximum flavor extraction, nuanced notes |
| **Allonge** | Constant flow, low pressure | 21-23% | Ultralight | Bright fruit/floral, between espresso and filter |
| **Flat 9 Bar** | Constant pressure | Similar to lever | Dark | Simplified lever, less optimal curve |

---

## Profile Details

### D-Flow

- **Category**: Lever/Flow hybrid `[SRC:medium]`
- **How it works**: Combines pressure and flow control; pressurized pre-infusion followed by flow-controlled pour. Adapts to grind coarseness. Fast fill, pressurized soak at ~3 bar until scale reads target dripping weight (default 4g), then pressure rise with flow limit. `[SRC:medium]` `[SRC:medium-video]`
- **Key advantage**: Ability to "heal" uneven puck preparation — the long pressurized soak under pressure repairs uneven puck distribution, producing linear extraction even when prep was poor `[SRC:medium]` `[SRC:medium-video]`
- **Adjustable variables**: Dose weight, pre-infusion temperature, infusion pressure, extraction pressure `[SRC:dflow-blog]`
- **Default flow rate**: 1.7 ml/s pour `[SRC:medium]` (community typical range: 2.0-2.7 ml/s `[SRC:adaptive-adastra]`)
- **Default pressure limit**: 8.5 bar maximum during extraction `[SRC:medium-video]`
- **Adaptive behavior**: Simplifies three profiles into one — if ground too fine, behaves like flat 8.5 bar E61; if dialed in correctly, behaves like Londinium (pressure then flow takeover); if too coarse, behaves like Gentle & Sweet (flow-limited, lower pressure) `[SRC:medium-video]`
- **Pre-infusion**: Weight-based trigger (default 4g dripping) rather than time-based; requires a scale. Longer soak than Londinium possible because weight-triggered. `[SRC:medium-video]`
- **Pressure curve**: Pressure rises after dripping target met; if flow exceeds 1.7 ml/s, flow control kicks in and pressure allowed to decline. Linear extraction line on flow chart indicates good extraction. `[SRC:medium-video]`
- **Stopping strategy**: Stopping at the pressure elbow (where extraction stalls and pressure decline slows) is a winning strategy for flow-profiled shots `[SRC:medium-video]`
- **Flavor**: With medium roast, concentrated mango, dried fruit, chocolate undertones. Floral/rose notes possible with medium-light beans. Tremendous aftertaste length. `[SRC:medium-video]`
- **Roast suitability**:
  - Light: Good — lower infusion pressure to 0 bar and increase dripping time for light roasts `[SRC:medium-video]`
  - Medium: Excellent — brings out floral/fruity notes (mango) while retaining chocolate undertones `[SRC:medium]` `[SRC:medium-video]`
  - Dark: Good `[SRC:medium]`
- **Best for**: Versatile, all roast levels; users who want forgiving puck prep. One of the three profiles most cited by users who "always make good espresso" (alongside Extractamundo Dos and Adaptive). `[SRC:medium]` `[SRC:medium-video]`

### Adaptive v2

- **Category**: Flow/adaptive `[SRC:medium]`
- **How it works**: Rises pressure to 8.6 bar for ~4 seconds, then attempts sequential flow-rate steps from 3.5 ml/s down to 0.5 ml/s, locking in whatever flow rate exists when pressure peaks. Effectively adapts to grind size. `[SRC:adaptive-adastra]`
- **V2 change from V1**: Pre-infusion is no longer pressurized — water fills, hits high pressure briefly, then water is turned off and pressure declines on its own. This prevents gushing with large flat burr grinders and light roasts where pressurized soak would cause water to pour through. `[SRC:light-video]`
- **Pre-infusion behavior**: Pressure rise times out after 7 seconds, then switches to stable flow at whatever pressure was achieved. With large flat burrs, expect significant dripping (17g dripping worked well in testing). Steep pressure decrease during dripping is normal with large burr grinders. `[SRC:light-video]`
- **Pressure adaptation**: Adapts to grind coarseness — e.g., 6.8 bar peak with a coarser grind, up to 9 bar with fine grind. The pressure rise phase determines grind coarseness and sets the flow rate accordingly. `[SRC:light-video]` `[SRC:medium-video]`
- **Flow range**: 2.0-2.7 ml/s (community data) `[SRC:adaptive-adastra]`
- **Shot duration**: 26-40 seconds (manual termination) `[SRC:adaptive-adastra]`
- **Ratio**: 1:2 to 1:3 `[SRC:adaptive-adastra]`. Best at shorter ratio (~1.8:1) for maximum fruitiness; longer ratios (2.3:1+) taste more like filter coffee with less brightness and mouthfeel `[SRC:light-video]`
- **Pressure curve**: Should peak near 9 bar then gradually decline. If pressure starts rising again during extraction, indicates fines migration (common with non-SSP burrs). `[SRC:adaptive-adastra]` `[SRC:light-video]`
- **Key advantage**: Tolerant of grind-size variation — too coarse produces allonge-style, too fine produces standard espresso, both potentially pleasant `[SRC:adaptive-adastra]`
- **Flavor**: Long aftertaste, layered flavors. Stone fruits (peach, pineapple). At shorter ratios: bright, fruity, good mouthfeel. At longer ratios: more filter-like, muted, thinner. `[SRC:light-video]`
- **Grind**: Coarser than Londinium for same bean. Coarser grind extracts more easily due to better water flow (more clean solvent contact). `[SRC:light-video]` `[SRC:medium-video]`
- **Roast suitability**:
  - Light: Good (v2 updated to work with light roasts, previously medium-optimized) `[SRC:adaptive-search]` `[SRC:light-video]`
  - Medium-light: Excellent — balanced, layered, pronounced acidity. Can make medium-light beans taste like light roast with coarser grind. `[SRC:medium]` `[SRC:medium-video]`
  - Dark: Not recommended — letting pressure drop sacrifices mouthfeel on dark beans `[SRC:dark-video]`
- **Best for**: Medium-light beans; users who want automated flow adjustment. One of the three profiles most cited by users who "always make good espresso." `[SRC:medium]` `[SRC:medium-video]`

### Blooming Espresso

- **Category**: Blooming `[SRC:4mothers]`
- **How it works**: Fill at ~7-8 ml/s until pressure peaks, close valve for 30s bloom (aim for first drips around this time), then open to desired pressure. `[SRC:eaf-profiling]`
- **Grind**: Fine — grind as fine as manageable. Bloom allows use of finer grind. `[SRC:eaf-profiling]` `[SRC:rao-blooming]`
- **Ratio**: Useful up to 1:3 `[SRC:eaf-profiling]`
- **Total shot time**: ~70 seconds `[SRC:rao-blooming]`
- **Percolation**: After bloom, gently increase pressure to 8-9 bar, flow ~2-2.5 ml/s `[SRC:rao-blooming]`
- **Flavor**: Extremely high flavor intensity relative to ratio `[SRC:eaf-profiling]`
- **Dialing in guidance**: `[SRC:light-video]`
  - Should see water on bottom of puck almost immediately after fill. If it takes 3-4 seconds, grind is too fine.
  - Aim for 6-8g dripping on the scale before pressure rise begins.
  - Pressure during extraction should be 6-9 bar. **Never** hit maximum pressure — if pressure hits the brick wall of max allowed, grind is too fine and shot will be muted.
  - If pressure only reaches ~4 bar, grind is slightly too coarse (still drinkable but not optimal).
  - Ideally pressure rises and then declines (possibly dramatically) — this is normal.
  - Hardest profile to dial in. Requires multiple attempts to find the right grind. Do not attempt with only 250g of beans — need at least 3kg to properly experiment. `[SRC:light-video]`
- **Paper filter trick**: Place a paper filter under the puck in the portafilter basket to prevent pressure crash. Especially helpful with new/unseasoned grinder burrs. `[SRC:light-video]`
- **Extraction yield**: Highest of any espresso profile — typically 23-24%, up to 28-29% demonstrated by Scott Rao. Compare to cafe standard of 18-19%. `[SRC:light-video]`
- **Roast suitability**:
  - Light: Excellent — biggest extraction, most rewarding when dialed in. Hardest to dial in. Best with flawless beans in quantity. `[SRC:light]` `[SRC:light-video]`
  - Medium: Good `[SRC:4mothers]`
  - Dark: Not ideal (ultralight beans may experience puck degradation) `[SRC:4mothers]`
- **Best for**: Experienced users with good beans seeking maximum extraction. Not recommended for beginners or those with limited bean supply. `[SRC:light]` `[SRC:light-video]`

### Blooming Allonge

- **Category**: Blooming/Allonge hybrid `[SRC:eaf-profiling]`
- **Grind**: Ultra-fine or Turkish `[SRC:eaf-profiling]`
- **Fill**: ~7-8 ml/s until pressure peaks `[SRC:eaf-profiling]`
- **Bloom**: Close valve completely, bloom until pressure returns to 1 bar (typically 4-10g in cup) `[SRC:eaf-profiling]`
- **Percolation flow**: ~4.5 ml/s `[SRC:eaf-profiling]`
- **Pressure**: Should not exceed 6 bar `[SRC:eaf-profiling]`
- **Flavor**: Relatively thin and filter-like texture with high clarity `[SRC:eaf-profiling]`
- **Best for**: Ultra-light coffees, Nordic filter roasts, maximum flavor clarity `[SRC:eaf-profiling]`

### Allonge (Simple / Rao Allonge)

- **Category**: Allonge `[SRC:4mothers]`
- **Grind**: Coarsest of all espresso profiles by far (e.g., 1.8 on test grinder vs 0.5 for Blooming, 1.4 for G&S) `[SRC:eaf-profiling]` `[SRC:4mothers]` `[SRC:light-video]`
- **Flow rate**: ~4.5 ml/s — the fastest flow rate espresso the DE1 makes `[SRC:eaf-profiling]` `[SRC:light-video]`
- **Pressure**: ~4-6 bar. Pressure should hit target but not crash. If pressure hits max, flow will adjust down — this means grind is a smidge too fine. A few seconds of max pressure before puck erosion brings it down is acceptable. `[SRC:eaf-profiling]` `[SRC:light-video]`
- **Ratio**: 1:4-1:5 `[SRC:eaf-profiling]`; up to 1:5-1:6 for Rao variant `[SRC:adaptive-adastra]`
- **Time**: ~30 seconds `[SRC:eaf-profiling]`
- **Temperature**: Produces hot coffee — the fast flow means the puck doesn't have time to cool the water. Allow cup to cool briefly before drinking, or use a large ceramic cup. `[SRC:light-video]`
- **Channeling**: Some channeling is normal and expected with Allonge due to the very fast flow rate. Don't be upset by it. A consistent needle of channeling adds slight dilution and acidity but is manageable. `[SRC:light-video]`
- **Extraction yield**: 21-23% `[SRC:4mothers]`
- **Flavor**: Bright, intense fruit and floral; gives up thickness for clarity. Despite the fast flow rate, not especially acidic. About twice the strength of filter coffee. `[SRC:eaf-profiling]` `[SRC:light-video]`
- **Bean selection**: Best with natural/fermented/fruity beans with "crazy fruitiness." Less impressive with clean washed coffees — makes a pleasant but unremarkable drink. `[SRC:light-video]`
- **Crema**: Still produces crema despite fast flow, because pressure creates crema `[SRC:light-video]`
- **Roast suitability**:
  - Ultralight: Best — minimal puck integrity demands `[SRC:4mothers]`
  - Light: Excellent — easiest to dial in for light roasts, fruity/natural beans `[SRC:light]` `[SRC:light-video]`
  - Medium+: Not recommended
- **Best for**: Ultralight/light roasts, fruity or natural beans; midway between espresso and filter `[SRC:light]` `[SRC:light-video]`

### Default Profile

- **Category**: Lever `[SRC:medium]`
- **How it works**: Classic lever-style declining pressure, 8.6 to 6 bar `[SRC:medium]`
- **Versatility**: Mimics traditional 8.5 bar for fine grinds, behaves like Londinium for optimal grinds, limits flow for coarse grinds `[SRC:medium]`
- **Flavor**: Emphasizes chocolate intensity `[SRC:medium]`
- **Roast suitability**:
  - Medium: Excellent `[SRC:medium]`
  - Dark: Good `[SRC:medium]`
  - Light: Not recommended
- **Best for**: General use across grinders; medium to dark roasts `[SRC:medium]`

### Londinium

- **Category**: Lever `[SRC:medium]`
- **How it works**: Inspired by spring-lever machines. Fast fill, then pressurized soak at ~3 bar until dripping appears, then ramp to ~9 bar with declining pressure. Emulates: lift lever (water fills), slam lever down (pressure hold), wait for dripping, release to full pressure, spring declines. `[SRC:medium]` `[SRC:dark-video]`
- **Temperature**: 88C `[SRC:dark-video]`
- **Versions**: `[SRC:dark-video]` `[SRC:medium-video]`
  - **LRv2**: If flow rate goes too fast at end of shot, switches from pressure-based to flow-based control (drops to ~1.7-2 ml/s). This "salvages" shots where puck erodes too much, preventing excessive acidity. May cause pressure crash but tames sourness.
  - **LRv3**: Pure lever decline — always pressure-controlled, just decreases pressure like a real lever. Preferred when dialed in perfectly. Different users prefer different versions.
- **Pressure curve**: Rises to ~9 bar, stays high during extraction. Pressure didn't decline much in dark roast testing (9 to ~8 bar) because flow was held constant by the declining pressure compensating for puck erosion. `[SRC:dark-video]`
- **Flow curve**: Flow stays relatively constant — the declining pressure compensates for puck erosion, which is considered desirable `[SRC:dark-video]`
- **Grind**: Fine grind required (same as Cremina). Finer than E61 or 80's Espresso. The pressurized soak requires finer grind to control dripping to 2-8g. `[SRC:dark-video]` `[SRC:medium-video]`
- **Dripping target**: 2-8g of dripping during soak phase before pressure rise. If dripping exceeds ~17g, grind is too coarse. `[SRC:medium-video]`
- **Extraction**: Highest extraction of dark roast profiles — the soak time extracts far more from the beans. Most full-bodied cup. `[SRC:dark-video]`
- **Flavor**: Dark chocolate, smooth, full-bodied. Baker's chocolate intensity without excessive harshness. Much thicker crema than 80's Espresso due to longer time under pressure and higher temperature. Great for quality dark beans with subtlety. `[SRC:dark-video]`
- **Roast suitability**:
  - Dark: Excellent — full body without harshness, highest extraction of dark profiles `[SRC:dark]` `[SRC:dark-video]`
  - Medium-dark: Good — finer grind, longer shot (45-53s) recommended. Powerful extraction. `[SRC:medium]` `[SRC:medium-video]`
  - Medium-light: Good — very fruity, long aftertaste, pleasing acidity. Works well with Ethiopian beans. `[SRC:medium-video]`
  - Light: Struggles — light roasts don't have the viscosity to survive the pressurized soak; water pours through and produces sour shot `[SRC:medium]` `[SRC:medium-video]`
- **Best for**: Darker roasts seeking full body and smoothness; quality dark beans where you want maximum extraction `[SRC:dark]` `[SRC:dark-video]`

### Gentle & Sweet

- **Category**: Pressure `[SRC:light]`
- **How it works**: Traditional espresso style — no pre-infusion, no hold, no dripping. Fills headspace, goes straight to 6 bar constant pressure, makes espresso. The puck compresses under flow, slows it down, then coffee flows out. `[SRC:light-video]`
- **Pressure**: Constant 6 bar. Espresso is generally 6-9 bar; G&S sits at the low end. Under 4 bar you stop making crema and enter drip-coffee territory. `[SRC:gentle-search]` `[SRC:light-video]`
- **Flow curve**: Starts ~2 ml/s, increases to 3+ ml/s as puck erodes (because constant pressure against eroding puck = rising flow). This is characteristic and expected. `[SRC:light-video]`
- **Shot duration**: Fast shot. Should not be long — if it takes 30+ seconds with a light roast, it will taste overextracted. `[SRC:light-video]`
- **Extraction yield**: ~18-19% (lowest of the light roast profiles) `[SRC:light-video]`
- **Grind**: Does not require as fine a grind as Blooming or Adaptive. Works with less capable grinders. `[SRC:light-video]`
- **Why 6 bar**: Lower pressure means less channeling. Light roasts resist pressure poorly — at 9 bar they channel, producing sour/watery shots. 6 bar is gentler. `[SRC:light-video]`
- **Flavor**: Smooth, sweet, low acidity; good alone or with milk. Less complexity than aggressive profiles. Peach, ripe red apple, gentle fruit. Pleasant and easy to drink. `[SRC:gentle-search]` `[SRC:light-video]`
- **Key rule**: Light roasted coffee wants either a long pre-infusion OR fast flow. G&S provides fast flow (no pre-infusion). Flow should not be down at 0.5-1 ml/s — that's fine for dark roasts, not for light. `[SRC:light-video]`
- **Roast suitability**:
  - Light: Good — beginner-friendly entry point, extracts the least of light roast profiles `[SRC:light]` `[SRC:light-video]`
- **Best for**: Beginners to light roasts; those wanting a smooth, sweet cup; users with less capable grinders; first shot on a new DE1 `[SRC:light]` `[SRC:light-video]`

### Extractamundo Dos

- **Category**: Pressure `[SRC:light-video]`
- **How it works**: Very similar to Gentle & Sweet but with a short pre-infusion pause. Fills headspace, pauses for a few seconds (water sits on puck, some dripping occurs), then rises to ~6 bar target pressure. `[SRC:light-video]`
- **Pressure**: Targets 6 bar, same as G&S. However, with the same grind setting the pre-infusion means it may not quite reach 6 bar — the extra soak creates less puck resistance. Slightly finer grind needed to match G&S pressure. `[SRC:light-video]`
- **Flow curve**: Flow increases as puck erodes (like G&S) but not as dramatically due to the pre-infusion evening out the extraction. `[SRC:light-video]`
- **Extraction**: Slightly more than G&S due to the short pre-infusion. Ordered: G&S < Extractamundo < Adaptive < Blooming < Allonge `[SRC:light-video]`
- **Flavor**: More acidic/sharp than G&S, fruitier, juicier. Higher concentration (TDS). Bitey-er and less gentle. Quick shot with fast flavor drop-off. `[SRC:light-video]`
- **Grind**: Same range as G&S, or slightly finer if you want to match 6 bar target pressure `[SRC:light-video]`
- **Popularity**: Most commonly cited profile among light-roast users who consistently make good espresso (from European tour survey with Scott Rao). One of the three profiles most cited by users who "always make good espresso." `[SRC:light-video]`
- **Roast suitability**:
  - Light: Good — recommended if you don't have a lot of beans (more forgiving than Blooming) `[SRC:light]` `[SRC:light-video]`
- **Best for**: Light roast users wanting slightly more extraction than G&S; beans with slight off-flavors you want to hide; users with limited bean supply `[SRC:light]` `[SRC:light-video]`

### Flow Profile

- **Category**: Flow `[SRC:medium]`
- **How it works**: Maintains constant flow rate (1.7 ml/s for milky drinks version, 2 ml/s for espresso version). Pressure is allowed to rise and fall naturally based on puck resistance. `[SRC:medium]` `[SRC:medium-video]`
- **Pressure curve**: Pressure rises as puck compresses (can reach ~9 bar if well-dialed), then declines linearly as puck erodes. The linear decline indicates clean extraction — no channeling. Inflection point (pressure elbow) indicates extraction has stalled. `[SRC:medium-video]`
- **Flow versions**: Two variants — "for milky drinks" (1.2-1.7 ml/s, finer grind, 40-50s shot) and "for espresso" (2 ml/s, coarser grind, faster shot). Must match flow rate to grind size. `[SRC:medium-video]`
- **Dialing in**: Grind should produce pressure between 6-9 bar. The tricky part is matching flow rate to grind/dose combination. `[SRC:medium-video]`
- **Flavor**: Fruitier, more nuanced than Default; more complex flavor development. Dramatically different taste from pressure profiles even on same bean — fruit and floral notes emerge, longer aftertaste. More delicate presentation. `[SRC:medium]` `[SRC:medium-video]`
- **Stopping strategy**: Pulling as a ristretto (stopping at pressure elbow) is often ideal — extraction is essentially done when pressure stops declining. `[SRC:medium-video]`
- **Roast suitability**:
  - Medium: Good — brings out unexpected fruitiness from beans that taste only chocolatey on Default `[SRC:medium]` `[SRC:medium-video]`
- **Best for**: Milky drinks and nuanced espresso; users who want to explore flow profiling theory `[SRC:medium]` `[SRC:medium-video]`

### Cremina

- **Category**: Lever `[SRC:dark]`
- **How it works**: Emulates the Cremina lever machine. Fast fill with a brief soak/dwell, then pressure rises. Key difference from Londinium: pressure declines steeply as shot progresses (Londinium stays high). Flow rate drops as shot continues — this is by design. `[SRC:dark-video]`
- **Temperature**: 92C — 4C higher than Londinium (88C). The higher temperature extracts more, producing more intense/traditional Italian flavors but also more harshness. `[SRC:dark-video]`
- **Shot duration**: ~45 seconds — a long shot. Really long shots with thick pucks and thick espresso. `[SRC:dark-video]`
- **Flow curve**: Flow rate drops as shot progresses. The declining pressure deliberately slows flow to maintain coffee thickness/concentration as the puck erodes. `[SRC:dark-video]`
- **Pressure curve**: Rises to peak then declines steeply (unlike Londinium which stays high). The steep decline is intentional — as the puck erodes, lowering pressure prevents increasingly watery coffee from coming through. `[SRC:dark-video]`
- **Grind**: Fine grind — same as Londinium and Best Overall. Finer than 80's Espresso or E61. `[SRC:dark-video]`
- **Mouthfeel**: Maximum mouthfeel of all profiles. Thick, syrupy espresso. Very thick crema — so thick a spoon cannot easily separate it. Consider pairing with a waisted basket (12g) for even thicker puck and more lever-like experience. `[SRC:dark-video]`
- **Flavor**: Baker's chocolate, intense, some burn/char notes. Not fruity at all. Like a traditional Italian petrol-station espresso at higher temperature. Maximum "wake-me-up" intensity. Longest aftertaste of dark roast profiles. `[SRC:dark-video]`
- **Roast suitability**:
  - Dark: Excellent — maximum mouthfeel and extraction intensity `[SRC:dark]` `[SRC:dark-video]`
- **Best for**: Classic Italian espresso; maximum mouthfeel; ristretto pulls; users who want the thickest, most intense dark roast extraction. Also good candidate for waisted basket (12g). `[SRC:dark]` `[SRC:dark-video]`

### 80's Espresso

- **Category**: Lever/Pressure `[SRC:dark]` `[SRC:dark-video]`
- **How it works**: Lever profile at LOW temperature. No pre-infusion — maximum water flow fills the puck, puck compresses, then flows out with declining pressure. Named "80's" because temperature starts at 80C and declines toward 70C. `[SRC:dark-video]`
- **Temperature**: 80C declining to ~70C — at least 8C cooler than normal espresso, 15C less than traditional machines (95-96C). The low temperature is the key innovation: dark tar flavors are extracted less at lower temperatures. `[SRC:dark-video]`
- **Flow rate**: Ideally 1-1.2 ml/s (slow and thick). If flow is too fast (~1.9 ml/s with 18g basket), shot will be thin and dusty. Slow flow = more mouthfeel and smoother result. `[SRC:dark-video]`
- **Grind**: Slightly coarser than lever profiles (Londinium/Cremina/Best Overall) but finer than E61. `[SRC:dark-video]`
- **Ratio**: 2:1 traditional, but works well at 1:1 (ristretto) — more like Italian-style small espresso. Pull shorter if getting bitter/dusty flavors. `[SRC:dark-video]`
- **With 18g basket**: ~25 second shot, flow peaked at ~1.9 ml/s. Can be thin and dusty — too fast. `[SRC:dark-video]`
- **With 12g waisted basket** (recommended): ~37 second shot. Flow peaked at 2.4 ml/s, ended at 0.5-1 ml/s. Much smoother, thicker crema with tiger striping, minimal channeling, minimal spray. Worlds better than 18g version. The thicker puck from narrower basket gives less channeling and more mouthfeel. `[SRC:dark-video]`
- **Flavor**: Bread fruit, raisins, baker's chocolate without the burn. Intentionally under-extracted (low temperature = less extraction). This is the least tar/burnt extraction method for dark beans. `[SRC:dark-video]`
- **Roast suitability**:
  - Dark: Good — minimizes tar/burnt flavors that dark beans always have `[SRC:dark]` `[SRC:dark-video]`
- **Best for**: Getting fruity flavors from dark roasts; minimizing burnt/tar notes; users who don't like dark roast defects. Best results with waisted (12g) basket for thicker puck. `[SRC:dark]` `[SRC:dark-video]`

### Best Overall

- **Category**: Lever/Pressure `[SRC:dark]` `[SRC:dark-video]`
- **How it works**: 3-step simple profile from the appendix of Scott Rao's espresso book ("Best Overall Pressure Profile"). Fast fill (no soak/dripping time), rise to ~8.6 bar, then declining pressure to ~6 bar. Stays within the 6-9 bar espresso range throughout. `[SRC:dark-video]`
- **Temperature**: 88C `[SRC:dark-video]`
- **Pressure curve**: Fast fill (~5 seconds to saturate puck), pressure rises quickly to ~8.6 bar, then slowly declines. The declining pressure compensates for puck erosion, keeping flow rate relatively constant — same principle as lever machines. `[SRC:dark-video]`
- **Flow curve**: Flow stays pretty constant because declining pressure offsets puck erosion. This is considered desirable. If well-dialed, looks identical to flow-profiled shots. `[SRC:dark-video]`
- **Grind**: Fine grind — same as Londinium and Cremina (for long ~45s shot style). `[SRC:dark-video]`
- **Fast fill importance**: Critical for dark roasts — fast fill minimizes channeling and maximizes body. Slow fill with dark roasts = more channeling, uneven extraction, less body. `[SRC:dark-video]`
- **Simple profile**: Uses the 3-step editor (pre-infusion, rise, decline) — easy to edit with sliders to adjust acidity/flavor balance. Adjusting end pressure between 4-6 bar gives pleasing balance of acidity to other flavors. `[SRC:dark-video]`
- **Crema**: Less crema than Cremina (much less time under high pressure). `[SRC:dark-video]`
- **Flavor**: Less extracted than Londinium or Cremina. Smooth, easy drinker. Not much acidity. Baker's chocolate without extreme intensity. `[SRC:dark-video]`
- **Roast suitability**:
  - Dark: Good — an easier, smoother dark roast espresso `[SRC:dark]` `[SRC:dark-video]`
- **Best for**: Milky drinks (latte, flat white) — no acidity, just a nice smooth shot that mixes well with milk. Less crema is actually preferable for milk drinks. `[SRC:dark]` `[SRC:dark-video]`

### E61

- **Category**: Flat 9 bar / Pressure `[SRC:dark]` `[SRC:dark-video]`
- **How it works**: Flat 9 bar constant pressure, no pre-infusion. Very fast fill, pressure rises smoothly, then sustains 9 bar throughout. Simplest possible espresso profile — the DE1 version has perfectly stable temperature (unlike real E61 machines which are notoriously temperature unstable). `[SRC:dark-video]`
- **Temperature**: 92C. Real E61 machines vary wildly in temperature due to their design (steam-temperature water cooling through exposed chrome group head). `[SRC:dark-video]`
- **Flow curve**: Flow starts moderate and increases as puck erodes — this is the characteristic E61 behavior. The constant 9 bar against an eroding puck drives flow up, which is why shots should be kept short. `[SRC:dark-video]`
- **Shot duration**: 22-25 seconds (shorter than lever profiles). First attempt at 15 seconds was too fast (too coarse). Dialed-in shot was 24 seconds. `[SRC:dark-video]`
- **Grind**: Coarsest of the dark roast profiles tested (coarser than Londinium, Cremina, Best Overall, and 80's). `[SRC:dark-video]`
- **Characteristic sourness**: Some acidity/sourness is inherent because constant 9 bar + increasing flow toward end of shot = more acidic extraction. Longer shots = more sourness. This is why E61 shots should be shorter. `[SRC:dark-video]`
- **Crema**: Fair amount of crema — sustained 9 bar pressure creates crema (pressure + CO2 outgassing from fresh beans). `[SRC:dark-video]`
- **Flavor**: Baker's chocolate with some balanced acidity. Less bitterness than Cremina. The faster flow rate and some acidity makes it John's personal favorite straight espresso from dark beans — he likes the acidity balanced with other flavors. Shorter shot time (~24s) may prevent tar flavors from emerging despite high temperature. `[SRC:dark-video]`
- **Editability**: The 3-step simple editor can transform E61 into many historical profiles: add pre-infusion (line pressure E61 evolution), add declining pressure (Cremina-like), or adjust decline rate to control acidity. End pressure between 4-6 bar gives good acidity balance. `[SRC:dark-video]`
- **Grinder note**: For dark roasts on E61, fines-producing grinders (like Niche, or fast RPM 60mm flat burrs) help maintain puck integrity and keep flow from increasing too fast. `[SRC:dark-video]`
- **Roast suitability**:
  - Dark: Good — balanced acidity, shorter shot prevents over-extraction `[SRC:dark]` `[SRC:dark-video]`
- **Best for**: Balanced acidity with dark roasts; users who enjoy some brightness in dark espresso; starting point for tinkering with the 3-step editor `[SRC:dark]` `[SRC:dark-video]`

### Turbo Shot

- **Category**: Flow/Pressure hybrid `[SRC:eaf-profiling]`
- **How it works**: Full pump output (~7-8 ml/s), reduce to maintain ~6 bar `[SRC:eaf-profiling]`
- **Grind**: Medium-fine — coarser than typical espresso, fine enough to spike to 6 bar quickly `[SRC:turbo-search]`
- **Dose**: 15-17g (smaller than traditional) `[SRC:turbo-search]`
- **Ratio**: 1:2-1:3 `[SRC:turbo-search]` `[SRC:eaf-profiling]`
- **Temperature**: 92-95C `[SRC:turbo-search]`
- **Time**: 10-20 seconds `[SRC:turbo-search]`; under 20s `[SRC:eaf-profiling]`
- **Pressure**: Hold at ~6 bar `[SRC:eaf-profiling]`
- **Flavor**: Good clarity, lower texture; sweeter with less bitterness `[SRC:eaf-profiling]` `[SRC:turbo-search]`
- **Best for**: All coffee types; rapid extractions prioritizing clarity and sweetness `[SRC:eaf-profiling]`

### Filter 2.0 / 2.1

- **Category**: Filter (Blooming variant) `[SRC:filter-blog]`
- **How it works**: Filter variation on Blooming Espresso with longer bloom phase `[SRC:filter-blog]`
- **Temperature**: 90C preinfusion, 85C percolation `[SRC:filter-search]`
- **Dose**: 20-22g in 24g basket `[SRC:filter-blog]`
- **Ratio**: 5:1 extraction, then dilute with 225-250g water `[SRC:filter-blog]`
- **Extraction yield**: 23-26% at 1.4-1.5% strength `[SRC:filter-search]`
- **Flow rate**: 3 ml/s (essential) `[SRC:filter-blog]`
- **Pressure limit**: 3 bar safeguard `[SRC:filter-search]`
- **Grind**: Finer than any filter grind, not quite espresso `[SRC:filter-blog]`
- **Setup**: Two micron 55mm paper filters in bottom, metal mesh on top `[SRC:filter-blog]`
- **Roast adjustment**: Lower temperature for darker or defective roasts `[SRC:filter-search]`
- **Best for**: Filter-style coffee from the DE1; high extraction, clarity `[SRC:filter-blog]`

### Sprover (Filter-Style)

- **Category**: Filter `[SRC:eaf-profiling]`
- **Dose**: Under-basket (e.g., 15g in 20g basket) `[SRC:eaf-profiling]`
- **Grind**: Coarser than allonge `[SRC:eaf-profiling]`
- **Flow rate**: 1.5-4.5 ml/s `[SRC:eaf-profiling]`
- **Pressure**: Keep below 1.5 bar during fill `[SRC:eaf-profiling]`
- **Ratio**: 1:10-1:14 `[SRC:eaf-profiling]`
- **Time**: ~100 seconds `[SRC:eaf-profiling]`
- **Starting point**: 15g:195g, 2 ml/s, ~100s `[SRC:eaf-profiling]`
- **Optional**: Paper/micromesh filter under puck to reduce oils and fines `[SRC:eaf-profiling]`
- **Best for**: Filter coffee simulation in espresso machine `[SRC:eaf-profiling]`

### Classic Italian / Gentler 8.4 Bar / Italian Australian

- **Category**: Flat pressure `[SRC:profile-notes]`
- **How it works**: Short preinfusion (4–8s at 4 bar) then sustained flat pressure extraction — 9 bar for Classic Italian, 8.4 bar for the gentler variant, 8.7 bar for Italian Australian. Emulates mainstream café espresso. `[SRC:profile-notes]`
- **Pressure curve**: Flat throughout extraction. Flow increases as puck erodes — this is expected and normal for flat-pressure profiles. `[SRC:profile-notes]`
- **Temperature**: 94°C (Classic Italian), 89.5°C (Gentler 8.4 bar), 88°C (Italian Australian) `[SRC:profile-notes]`
- **Duration**: ~30–40s extraction after preinfusion `[SRC:profile-notes]`
- **Grind**: Medium-fine. The 8.4 bar variant is more forgiving of puck prep. `[SRC:profile-notes]`
- **Roast suitability**:
  - Medium: Good — emulates café espresso with chocolate and body `[SRC:profile-notes]`
  - Dark: Good — Italian Australian specifically uses 88°C to prevent overextraction of dark roasts `[SRC:profile-notes]`
- **Best for**: Traditional Italian-style espresso; users familiar with E61-style machines; milk drinks

### Traditional Lever Machine (and Low Pressure / Two Spring variants)

- **Category**: Lever `[SRC:profile-notes]`
- **How it works**: Spring-lever machine emulations. ~20s preinfusion at 4 bar (drip fill), then pressure peaks and gradually declines as the simulated spring decompresses. Traditional lever: 9 bar peak. Low pressure 6 bar: 6 bar peak, sweeter and gentler. Two spring: 9 bar peak held longer (second spring maintains pressure). `[SRC:profile-notes]`
- **Pressure curve**: Gradual declining pressure after peak. The decline is intentional — this is a spring lever. `[SRC:profile-notes]`
- **Temperature**: 92–94°C `[SRC:profile-notes]`
- **Duration**: ~35s total extraction `[SRC:profile-notes]`
- **Grind**: Medium `[SRC:profile-notes]`
- **Flavor**: Sweet and smooth. Low pressure 6 bar variant is the gentlest and sweetest. Two spring variant produces more concentrated flavor from sustained peak pressure. `[SRC:profile-notes]`
- **Roast suitability**:
  - Medium: Good — sweet, full body `[SRC:profile-notes]`
  - Dark: Good — smooth, traditional character `[SRC:profile-notes]`
- **Best for**: Users who want to emulate a physical lever machine; sweet, smooth espresso without the complexity of advanced profiles

### Advanced Spring Lever (and Weiss variant)

- **Category**: Lever with flow recovery `[SRC:profile-notes]`
- **How it works**: Spring-lever emulation with a flow-limit safety valve. If puck erodes too fast (flow exceeds the limiter), the profile switches to 1.5 ml/s flow-controlled extraction to prevent gushing. The Weiss variant uses 90°C (vs 88°C) and is tuned slightly differently. Both variants are more tolerant of grind variation than a pure lever profile. `[SRC:profile-notes]`
- **Pressure curve**: 9 bar peak then gradual decline. If the flow-control step activates (visible as a pressure drop and stabilization), that is the safety valve working — not a problem. `[SRC:profile-notes]`
- **Temperature**: 88°C (Advanced), 90°C (Weiss) `[SRC:profile-notes]`
- **Duration**: ~35–45s `[SRC:profile-notes]`
- **Dose**: 18g → ~32g out `[SRC:profile-notes]`
- **Roast suitability**:
  - All roasts: Good — the flow recovery step makes these forgiving across roast levels `[SRC:profile-notes]`
- **Best for**: Users who want a lever-style profile but need more margin for grind variation; a good "training wheels" lever profile

### Best Practice (Light Roast)

- **Category**: Adaptive/Blooming hybrid `[SRC:profile-notes]`
- **How it works**: Unites the best practices learned from the Decent machine: Brakel's Londinium preinfusion technique, Rao's Blooming, and Gagné's Adaptive flow-locking. Designed as a single forgiving light-roast profile. `[SRC:profile-notes]`
- **Preinfusion**: Low pressure (3 bar) until dripping, then 1.5 bar gentle soak to fully saturate the puck `[SRC:profile-notes]`
- **Extraction**: Pressure ramps to ~9–10.5 bar then flow-controlled at ~2.5 ml/s with 10.5 bar limiter `[SRC:profile-notes]`
- **Temperature**: 92°C preinfusion, 90°C extraction `[SRC:profile-notes]`
- **Duration**: ~50–70s total `[SRC:profile-notes]`
- **Dose**: 18g in, 50g out in ~60 seconds (1:2.8) — canonical starting recipe `[SRC:profile-notes]`
- **Grind**: Coarse for light roasts, targeting ~2.5 ml/s extraction flow. Grind fine enough to maintain dripping during preinfusion. `[SRC:profile-notes]`
- **Roast suitability**:
  - Light: Excellent — forgiving, high-extraction, designed specifically for light roasts `[SRC:profile-notes]`
- **Best for**: Light roast users who want a single versatile profile that doesn't require precise grind targeting; combines the best of three approaches in one profile

### Easy Blooming (Active Pressure Decline)

- **Category**: Blooming (adaptive) `[SRC:community-index]`
- **Creator**: Stéphane `[SRC:community-index]`
- **How it works**: An accessible evolution of Rao Blooming. The key innovation is a pressure-threshold bloom exit: rather than a fixed timer, the bloom phase ends when pressure declines to a threshold value (~1.5–2 bar). This makes bloom duration self-adjust to grind coarseness — finer grinds hold pressure longer (longer bloom); coarser grinds depressurize quickly (shorter bloom). After bloom, active pressure decline manages increasing flow as the puck erodes. `[SRC:community-index]`
- **Motivation**: The original Rao Blooming profile requires precise grind timing — minor changes in grind can have massive impact on extraction pressure, making it difficult to dial in consistently. Easy Blooming removes the need to manually adjust bloom time when changing grind size. `[SRC:community-index]`
- **Temperature**: 88°C default (adjustable 84–90°C for different roasts) `[SRC:profile-notes]`
- **Duration**: ~25–45s (varies with grind — coarser grind = shorter bloom = shorter total) `[SRC:profile-notes]`
- **Dose**: 20g → 40–50g (1:2–2.5) `[SRC:profile-notes]`
- **Grind**: Flat burrs preferred. Conical grinders: raise temperature ~2°C. `[SRC:community-index]`
- **Roast suitability**:
  - Light: Excellent — primary target, more forgiving than standard Blooming `[SRC:community-index]`
  - Medium: Good — lower temperature to 84–86°C `[SRC:profile-notes]`
- **Best for**: Users who want the benefits of Rao Blooming without the difficulty of precision grind timing; those who frequently change beans or grind settings

### Gagné Adaptive Shot / Allongé

- **Category**: Adaptive/Flow `[SRC:bc-adaptive]`
- **Creator**: Jonathan Gagné (Coffee ad Astra) `[SRC:bc-adaptive]`
- **How it works**: After preinfusion, pressure rises to 8.6 bar. A series of "scan" frames then detects the current flow at peak pressure and locks it in for the rest of the shot. The profile adapts to the grind — finer grind produces ~2.2 ml/s standard espresso; coarser grind produces ~4 ml/s Allongé-style. You dial in by targeting a flow rate with grind, not by chasing a pressure curve. `[SRC:bc-adaptive]`
- **Key insight**: The profile makes it possible to produce either a flow-profile shot or a Rao Allongé from the same profile — grind coarser for Allongé, finer for standard espresso. Shots that are slightly off-dial still taste decent because the flow is always at a sensible rate. `[SRC:bc-adaptive]`
- **Temperature**: 92°C (Adaptive Shot), 94°C (Adaptive Allongé) `[SRC:profile-notes]`
- **Duration**: ~25–45s (Adaptive Shot); longer for Allongé `[SRC:profile-notes]`
- **Dose**: 20g → 40–50g (Adaptive Shot); higher ratio for Allongé `[SRC:profile-notes]`
- **Grind**: Flat burrs preferred. Coarser → higher flow → Allongé-style; finer → lower flow → espresso-style. `[SRC:bc-adaptive]`
- **Roast suitability**:
  - Light: Good (Adaptive Shot at 92°C; lower to 84–86°C for dark) `[SRC:profile-notes]`
  - Light-medium: Excellent — bright, layered, fruited `[SRC:bc-adaptive]`
- **Best for**: Users who want a forgiving, grind-adaptive profile; those who like to dial in by flow rate rather than pressure; bridge between standard espresso and Rao Allongé

### I Got Your Back

- **Category**: Adaptive (grind-invariant) `[SRC:profile-notes]`
- **Creator**: Shin `[SRC:profile-notes]`
- **How it works**: The goal is to never fail to produce an acceptable espresso at any grinder dial setting. During preinfusion, the profile detects puck resistance via pressure: low resistance (coarse grind) → immediately routes to flat 2.2 ml/s flow extraction; high resistance (fine grind) → triggers a bloom pause, then ramps pressure and transitions to flat flow. `[SRC:profile-notes]`
- **Temperature**: 90°C `[SRC:profile-notes]`
- **Duration**: ~25–40s `[SRC:profile-notes]`
- **Grind**: Any — the profile detects resistance and routes to the appropriate extraction path `[SRC:profile-notes]`
- **Roast suitability**:
  - All roasts: Good — designed to work regardless of grind or roast `[SRC:profile-notes]`
- **Best for**: New DE1 users still finding their grind range; anyone who wants a "can't fail" fallback profile; guests using the machine without dialing in

### TurboBloom

- **Category**: Blooming/Turbo hybrid `[SRC:community-index]`
- **Creator**: Collin `[SRC:community-index]`
- **How it works**: Created as a companion to TurboTurbo after Collin noticed that a very short bloom step recovers positive qualities lost at slower PI flow rates. Fast fill at 8 ml/s saturates the puck quickly, then a very short (~5s) bloom until pressure drops to 2.2 bar, then 6 bar extraction with a 4.5 ml/s flow limiter. The fast fill + short bloom combination achieves even extraction while allowing high flow during the extraction phase. `[SRC:community-index]`
- **Temperature**: 86°C fill → 70°C bloom (intentional, reduces harshness) → 80°C extraction `[SRC:profile-notes]`
- **Duration**: ~25–35s `[SRC:profile-notes]`
- **Dose**: 15g → 45g (1:3) `[SRC:community-index]`
- **Grind**: Coarse — high-extraction burrs targeting 3–4.5 ml/s final flow `[SRC:community-index]`
- **Flavor**: Same tasting notes as TurboTurbo (high clarity, brightness) but cleaner, less astringency. The short bloom reduces harshness compared to the no-bloom approach. `[SRC:community-index]`
- **Roast suitability**:
  - All roasts: Good — optimized for high-extraction flat burr grinders `[SRC:community-index]`
- **Best for**: High-extraction flat burr grinder users (SSP HU, EK-style); those who prefer TurboTurbo flavor but want slightly cleaner results; 1:3 ratio shots

### TurboTurbo

- **Category**: Turbo (no bloom) `[SRC:community-index]`
- **Creators**: Collin and Jan `[SRC:community-index]`
- **How it works**: High-extraction turbo shot without a bloom phase. Rapid preinfusion at 96°C to saturate the puck, then 6 bar extraction at 93°C with a 4.5 ml/s flow limiter. Original design used 97°C/8 ml/s preinfusion; refined to 96°C/4 ml/s for better consistency and less astringency. High temperature is appropriate for the coarse grind: coarser grounds need hotter water to reach the same extraction temperature at the puck. `[SRC:community-index]`
- **Temperature**: 96°C preinfusion, 93°C extraction `[SRC:profile-notes]`
- **Duration**: ~20–30s `[SRC:profile-notes]`
- **Dose**: 15g → 45g (1:3) `[SRC:community-index]`
- **Grind**: Coarse — much coarser than traditional espresso; targeting 3–4.5 ml/s `[SRC:community-index]`
- **Flavor**: High clarity, bright, concentrated. Clean espresso with less bitterness and astringency than traditional 9 bar profiles. More like concentrated filter coffee than classic espresso. Shines with large flat burrs (SSP HU, EK-style). `[SRC:community-index]`
- **Community note**: Some users tweak to 7 bar and extend preinfusion by 5s for more extraction and mouthfeel with more chocolate notes, while retaining the turbo character. `[SRC:community-index]`
- **Roast suitability**:
  - All roasts: Good — especially effective with flat burr high-extraction grinders `[SRC:community-index]`
- **Best for**: High-extraction flat burr grinder users; fast shots (20–30s); users who want filter-coffee clarity in an espresso format

### Gentle Flat / Long Preinfusion Family

- **Category**: Gentle/Long Preinfusion Flow `[SRC:profile-notes]`
- **How it works**: A family of Seattle-style profiles by John Weiss sharing a very long preinfusion soak (10–37s) before extraction. Designed to fully saturate light-roast pucks for even, channeling-free extraction. Also known as "Slayer-style" after the Slayer espresso machine which pioneered this technique. `[SRC:profile-notes]`
- **Variants and parameters**: `[SRC:profile-notes]`
  - *Gentle flat 2.5 ml/s*: 2.5 ml/s for 10s preinfusion, then 2.5 ml/s for 45s extraction at 6 bar
  - *Gentle preinfusion flow*: 3.5 ml/s for 30s preinfusion, then 2.2 ml/s for 21s at 9 bar
  - *Hybrid pour over espresso*: 2.0 ml/s for 25s to 1.5 bar, then 2.2 ml/s for 20s at 9 bar
  - *Innovative long preinfusion* (Slayer-style): 1.5 ml/s for 37s to 2.0 bar, then 2.5 ml/s for 25s at 9 bar
- **Temperature**: 92°C (most variants), 98°C (Innovative long preinfusion — intentionally high for light roasts) `[SRC:profile-notes]`
- **Duration**: ~50–65s total `[SRC:profile-notes]`
- **Roast suitability**:
  - Light/ultra-light aromatic roasts: Excellent — profiles designed for beans that resist extraction at normal parameters `[SRC:profile-notes]`
- **Best for**: Light/ultra-light aromatic roasts that resist standard extraction; users who want a gentler Slayer-style approach; the "Innovative long preinfusion" variant is the closest DE1 equivalent to a true Slayer shot

### Preinfuse Then 45ml of Water

- **Category**: Volume-based `[SRC:profile-notes]`
- **Creator**: Matt Perger technique `[SRC:profile-notes]`
- **How it works**: 25s preinfusion at 4 bar to fully saturate the puck, then extraction at 9 bar until a fixed 45ml of water has been delivered. Shot ends by volume, not weight or time — no scale required. The fixed water volume produces consistent results by bypassing scale dependency. `[SRC:profile-notes]`
- **Temperature**: 90°C `[SRC:profile-notes]`
- **Duration**: ~40–50s `[SRC:profile-notes]`
- **Ratio**: Output depends on dose — approximately 1:2–2.5 for 18–20g doses with 45ml water `[SRC:profile-notes]`
- **Grind**: Medium-fine `[SRC:profile-notes]`
- **Roast suitability**:
  - All roasts: Good `[SRC:profile-notes]`
- **Best for**: Users without a scale; anyone who wants consistent volume-based shots; a simple, reliable profile for new users

### 7g Basket

- **Category**: Micro-espresso `[SRC:profile-notes]`
- **How it works**: Optimized for the Decent 7g mini-basket. Reduced flow rate and lower peak pressure (7.5 bar) protect the small, thin puck from channeling and blowthrough. `[SRC:profile-notes]`
- **Temperature**: 90°C `[SRC:profile-notes]`
- **Duration**: ~16–20s `[SRC:profile-notes]`
- **Dose**: 7g → 18–28g out `[SRC:profile-notes]`
- **Grind**: Fine — small basket rewards fine grind for extraction efficiency `[SRC:profile-notes]`
- **Community tips**: Sub-14g doses are significantly harder to dial in than standard. Essential practices: use a dosing funnel (small baskets spill easily), place a paper filter on top of the puck (improves flow evenness for thin pucks), level carefully. Starting profiles: Extractamundo Dos and Gentle & Sweet work well for single shots — both are more forgiving than lever profiles for thin pucks. Some users find slightly lower temperatures (~87–88°C) reduce bitterness. `[SRC:profile-notes]`
- **Roast suitability**:
  - All roasts: Good `[SRC:profile-notes]`
- **Best for**: Single-dose shots with the Decent mini-basket; users who prefer small, concentrated espresso

### Espresso Forge (Dark and Light)

- **Category**: Manual machine emulation `[SRC:profile-notes]`
- **How it works**: Emulates the Espresso Forge piston-pump manual espresso machine. Both variants use rising pressure then a long gradual pressure decline to mimic the forge's manual pressure profile. `[SRC:profile-notes]`
- **Dark variant**: Preinfusion at 6 ml/s to 3 bar, then 7.5 bar peak, then 30s gradual decline to 3 bar. Temperature declining 84°C → 81°C → 78°C. For medium-dark roasts seeking more fruit flavors, similar character to Classic Italian Lever. `[SRC:profile-notes]`
- **Light variant**: Long 25s pre-brew soak at 1 bar, then high-pressure ramp to 10 bar over 20s, then 50s gradual decline to 6 bar. Temperature 96°C → declining. High temperature intentional for light roast extraction. `[SRC:profile-notes]`
- **Duration**: ~35–40s (Dark), ~60–70s (Light) `[SRC:profile-notes]`
- **Roast suitability**:
  - Dark variant: Medium to dark `[SRC:profile-notes]`
  - Light variant: Medium to light `[SRC:profile-notes]`
- **Best for**: Users who own or are familiar with the Espresso Forge manual machine; those who want a long declining-pressure profile with temperature variation

### Pour Over Basket

- **Category**: Pour over (filter through espresso machine) `[SRC:profile-notes]`
- **How it works**: Produces filter coffee using a pour-over basket placed under the group head. Multi-pulse brewing with prewet, bloom pause, and several water pulses. Not espresso — pressure stays near 0 bar throughout (gravity-fed). `[SRC:profile-notes]`
- **Variants**: `[SRC:profile-notes]`
  - *V60 variants*: 8 ml/s high-turbulence flow for 15–22g doses → 250–375g output
  - *Kalita*: 6 ml/s lower flow (prevents choking); suits Ethiopian/decaf beans
  - *Cold brew*: 2–2.5 ml/s flow at 20°C water → ~180s
- **Temperature**: ~99–100°C (standard), ~20°C (cold brew) `[SRC:profile-notes]`
- **Duration**: ~70–105s (standard); ~180s (cold brew) `[SRC:profile-notes]`
- **Grind**: Filter grind (coarser than espresso). Coarsen if basket chokes. `[SRC:profile-notes]`
- **Roast suitability**:
  - All roasts: Good; especially suited to light/medium roasts `[SRC:profile-notes]`
- **Best for**: Filter coffee from the DE1 without a separate filter brewer; light roast enthusiasts; cold brew convenience

### Trendy 6 Bar Low Pressure

- **Category**: Constant pressure (light roast) `[SRC:profile-notes]`
- **How it works**: 20s preinfusion at 4 bar then flat 6 bar extraction. Designed specifically for sophisticated light roasts that smell great but resist being well extracted at standard 9 bar pressure. Lower pressure reduces channeling risk and coaxes sweetness from resistant pucks. `[SRC:profile-notes]`
- **Temperature**: 92°C `[SRC:profile-notes]`
- **Duration**: ~35–55s extraction after preinfusion `[SRC:profile-notes]`
- **Grind**: Coarser than traditional espresso (lower pressure requires slightly coarser grind) `[SRC:profile-notes]`
- **Flavor**: Sweet, rounded, less astringent than 9 bar profiles. Try this profile when your light roast smells excellent but produces hollow, bitter, or sour results at standard pressure. `[SRC:profile-notes]`
- **Roast suitability**:
  - Light: Excellent — specifically for aromatic light roasts that struggle at 9 bar `[SRC:profile-notes]`
- **Best for**: Aromatic light roasts that taste off at standard pressure; a gentler alternative to Gentle & Sweet for resistant light roasts

### GHC Manual Control

- **Category**: Manual (operator-controlled) `[SRC:profile-notes]`
- **How it works**: For machines with a Group Head Controller (GHC) — the user manually adjusts flow or pressure in real time via the GHC control. Not an automated profile. Used to log manual shots with the DE1's sensors. `[SRC:profile-notes]`
- **Temperature**: 88–96°C configurable presets `[SRC:profile-notes]`
- **Best for**: Baristas who want full hands-on control while still logging shot data; experienced users experimenting with manual technique; machines equipped with a GHC

---

## General Roast-Level Advice

### Light Roasts
- Higher temperatures needed (93-96C for espresso) `[SRC:turbo-search]`
- Longer ratios (1:2.5-3, or up to 5:1 for fast shots) `[SRC:rao-approach]`
- Shot times commonly exceed 40 seconds — let taste guide, not the clock `[SRC:eaf-beginner]`
- Profiles ordered by increasing extraction: Gentle & Sweet < Extractamundo < Adaptive < Blooming < Allonge `[SRC:light]`
- Tested with: Chelchele from L'Alchimiste, 18g dose, 98mm flat burr grinder `[SRC:light]`

### Medium Roasts
- Medium-light: chocolate without bitterness `[SRC:medium]`
- Medium-dark: removes fruit but avoids char `[SRC:medium]`
- Comfortable base for espresso or milk drinks `[SRC:medium]`
- Stopping at the pressure elbow during flow profiling is often a winning strategy `[SRC:medium]`
- No "correct" profile — taste preferences dictate choices `[SRC:medium]`
- Darker roasts suit pressure-focused profiles (Default/Londinium); lighter roasts benefit from flow-based (Adaptive/D-Flow) `[SRC:medium]`
- Tested with: medium-light from L'Alchimiste, medium-dark from Dark Arts `[SRC:medium]`

### Dark Roasts
- High solubility — lever/pressure profiles work naturally `[SRC:4mothers]`
- Lower temperatures (88-91C) to avoid over-extraction `[SRC:turbo-search]`
- Shorter ratios (1:1.5-2) to prevent bitterness
- 3-stage profiles can be edited with sliders to adjust acidity/flavor balance `[SRC:dark]`

### Universal Advice
- Lock in dose based on basket rating (e.g., 18g in 18g basket) `[SRC:eaf-beginner]`
- Start with 1:2 ratio, adjust by taste `[SRC:eaf-beginner]`
- Grind as fine as possible while maintaining a tasty, balanced shot `[SRC:eaf-beginner]`
- When switching coffees at similar roast level, adjust only the grind `[SRC:rao-approach]`
- If sour: increase ratio. If bitter: shorten ratio. If both: fix puck prep. `[SRC:eaf-beginner]`
- Temperature: lower extracts less, higher extracts more — adjust after locking dose/ratio/grind `[SRC:eaf-beginner]`

---

## Cross-Roast Profile Summary

Quick reference for which profiles work across roast levels. Profiles without a `knowledge_base_id` in their JSON are matched by fuzzy title.

### Espresso profiles

| Profile | Light | Medium | Dark | Category | `AnalysisFlags` |
|---------|-------|--------|------|----------|-----------------|
| D-Flow (all variants) | Good | Excellent | Good | Lever/Flow hybrid | `flow_trend_ok` |
| A-Flow (all variants) | - | Good | Excellent (dark/very dark) | Pressure-ramp/Flow | - |
| Adaptive v2 | Good | Excellent (med-light) | - | Flow/adaptive | - |
| Blooming Espresso | Excellent (hard) | Good | - | Blooming | - |
| Blooming Allongé | Excellent (ultralight) | - | - | Blooming/Allongé | - |
| Allongé / Rao Allongé | Best (ultralight) | - | - | Allongé | `channeling_expected` |
| Default | - | Excellent | Good | Lever | `flow_trend_ok` |
| Londinium | Struggles | Good (med-dark) | Excellent | Lever | `flow_trend_ok` |
| Cremina | - | - | Excellent | Lever | `flow_trend_ok` |
| Gentle & Sweet | Good (beginner) | - | - | Constant pressure | `flow_trend_ok` |
| Extractamundo Dos | Good | - | - | Constant pressure + bloom | `flow_trend_ok` |
| Flow Profile (straight/milky) | - | Good | - | Flow | - |
| 80's Espresso | - | - | Good (dark fruit) | Lever/low-temp | `flow_trend_ok` |
| Best Overall | - | Good | Good (milk) | Lever | `flow_trend_ok` |
| E61 (all variants) | - | Good | Good (balanced) | Flat pressure | `flow_trend_ok` |
| Classic Italian espresso | - | Good | Good | Flat pressure | `flow_trend_ok` |
| Gentler but still traditional 8.4 bar | - | Good | Good | Flat pressure | `flow_trend_ok` |
| Italian Australian espresso | - | - | Good | Flat pressure | `flow_trend_ok` |
| Turbo Shot | Good | Good | Good | Flow/Pressure hybrid | - |
| TurboBloom | Good | Good | Good | Blooming/Turbo | - |
| TurboTurbo | Good | Good | Good | Turbo | - |
| Traditional lever machine | - | Good | Good | Lever | `flow_trend_ok` |
| Low pressure lever machine at 6 bar | - | Good | Good | Lever | `flow_trend_ok` |
| Two spring lever machine to 9 bar | - | Good | Good | Lever | `flow_trend_ok` |
| Advanced spring lever | Good | Good | Good | Lever + flow recovery | `flow_trend_ok` |
| Weiss advanced spring lever | Good | Good | Good | Lever + flow recovery | `flow_trend_ok` |
| Best practice (light roast) | Excellent | - | - | Adaptive/Blooming hybrid | - |
| Easy blooming - active pressure decline | Good | Good | - | Blooming (adaptive) | - |
| Gagné/Adaptive Shot 92C v1.0 | Good | - | - | Adaptive/Flow | `flow_trend_ok` |
| Gagné/Adaptive Allongé 94C v1.0 | Good | Good | - | Adaptive/Allongé | `flow_trend_ok` |
| I got your back | Good | Good | Good | Adaptive (grind-invariant) | - |
| Gentle flat 2.5 ml per second | Excellent (aromatic) | - | - | Gentle/Long preinfusion | - |
| Gentle preinfusion flow profile | Excellent (aromatic) | - | - | Gentle/Long preinfusion | - |
| Hybrid pour over espresso | Excellent (ultra-light) | - | - | Long preinfusion | - |
| Innovative long preinfusion | Excellent (ultra-light) | - | - | Long preinfusion | - |
| Preinfuse then 45ml of water | Good | Good | Good | Volume-based | - |
| Espresso Forge Dark | - | Good | Good | Manual emulation | `flow_trend_ok` |
| Espresso Forge Light | Good | Good | - | Manual emulation | `flow_trend_ok` |
| Trendy 6 bar low pressure shot | Excellent | - | - | Constant pressure | `flow_trend_ok` |
| 7g basket | Good | Good | Good | Micro-espresso | - |

### Filter / pour-over profiles

| Profile | Notes | Category |
|---------|-------|----------|
| Filter 2.0 / 2.1 | All roasts; lower temp for dark | Filter (blooming) |
| Filter3 | Light/medium; coarsest grind | Filter (no-bypass) |
| Pour over basket (all V60/Kalita variants) | All roasts; gravity-fed | Pour over |
| Pour over basket/Cold brew | All roasts; cold water | Cold brew |

### Specialty / manual profiles

| Profile | Notes |
|---------|-------|
| GHC/manual flow control | Operator-controlled; no target curves |
| GHC/manual pressure control | Operator-controlled; no target curves |
| Tea portafilter / Tea in a basket | Tea beverage; not espresso analysis |
| Cleaning / Descale / Test profiles | Maintenance; not analyzed |

---

## TODO: Additional Data Needed

- [x] ~~Specific temperature recommendations per profile per roast level~~ — Added for Cremina (92C), Londinium (88C), Best Overall (88C), E61 (92C), 80's Espresso (80-70C declining) from dark-video; D-Flow defaults from medium-video
- [x] ~~D-Flow default temperature and pressure values from the profile JSON~~ — Added default 8.5 bar max pressure, 1.7 ml/s flow, 3 bar infusion pressure from medium-video
- [x] ~~Turbo Shot profile specifics as implemented in Decenza~~ — Covered in profile_knowledge.md
- [x] ~~Grind size guidance relative between profiles~~ — Added relative grind ordering from dark-video (E61 coarsest > 80's > Londinium=Cremina=Best Overall finest) and light-video (Allonge coarsest > Adaptive > G&S=Extractamundo > Blooming finest)
- [ ] Blooming Espresso temperature guidance by roast (not mentioned in transcripts)
- [x] ~~Community favorites and real-world dial-in tips for newer profiles~~ — Added creator attributions (Stéphane/Easy Blooming, Jonathan Gagné/Gagné Adaptive, Collin/TurboBloom, Collin+Jan/TurboTurbo, Shin/I Got Your Back), sourced dial-in tips for all; profile_knowledge.md updated with creator info and dial-in guidance
- [x] ~~Sourced profile detail sections for the ~20 profiles added in 2026~~ — Added `### ProfileName` sections for all: Classic Italian, Traditional/Spring Lever, Advanced Spring Lever, Best Practice, Easy Blooming, Gagné Adaptive, I Got Your Back, TurboBloom, TurboTurbo, Gentle Flat / Long Preinfusion family, Preinfuse Then 45ml, 7g Basket, Espresso Forge, Pour Over Basket, Trendy 6 Bar, GHC Manual. Sources: `community-index`, `bc-adaptive`, `profile-notes`. (Tea and Cleaning omitted — not espresso profiles, no dial-in guidance applicable.)
- [ ] Video timestamps from Decent's YouTube for user reference

---

## Sources

| Key | Description | URL |
|-----|-------------|-----|
| `4mothers` | The 4 mothers: a unified theory of espresso making recipes | https://decentespresso.com/docs/the_4_mothers_a_unified_theory_of_espresso_making_recipes |
| `light` | 5 espresso profiles for light roasted coffee beans (blog + video) | https://decentespresso.com/blog/5_espresso_profiles_for_light_roasted_coffee_beans |
| `medium` | 5 profiles for medium roasted beans (blog + video) | https://decentespresso.com/blog/5_profiles_for_medium_roasted_beans |
| `dark` | 5 profiles for dark roasted beans (blog + video) | https://decentespresso.com/blog/5_profiles_for_dark_roasted_beans |
| `dflow-blog` | D-Flow: an easy editor for the Londinium family | https://decentespresso.com/blog/dflow_an_easy_editor_for_the_londinium_family_of_espresso_profiles |
| `adaptive-adastra` | An Espresso Profile that Adapts to your Grind Size (Coffee ad Astra) | https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/ |
| `adaptive-search` | Dialing in the Rao Allonge and Adaptive v2 profiles | https://decentespresso.com/blog/dialing_in_the_rao_allonge_and_adaptive_v2_profiles |
| `eaf-profiling` | Espresso Profiling guide (Espresso Aficionados) | https://espressoaf.com/guides/profiling.html |
| `eaf-beginner` | Dialling In Basics (Espresso Aficionados) | https://espressoaf.com/guides/beginner.html |
| `rao-approach` | How to approach brewing different coffees (Scott Rao) | https://www.scottrao.com/blog/2024/2/26/how-to-approach-brewing-different-coffees |
| `rao-blooming` | Blooming espresso guidance (Scott Rao, via search) | https://www.scottrao.com/blog/2021/5/18/best-practice-espresso-profile |
| `filter-blog` | Filter 2.0 profile by Scott Rao (Decent blog) | https://decentespresso.com/blog/a_new_way_to_make_filter_coffee_filter_20_profile_by_scott_rao |
| `filter-search` | Filter 2.0 settings (Rao blog) | https://www.scottrao.com/blog/2021/9/28/decent-coffee-shots |
| `gentle-search` | Gentle & Sweet profile details (search results) | https://decentespresso.com/blog/5_espresso_profiles_for_light_roasted_coffee_beans |
| `turbo-search` | Turbo Shot espresso guides (aggregated search) | https://espressoaf.com/guides/profiling.html |
| `light-video` | 5 espresso profiles for light roasted coffee beans (video transcript) | https://decentespresso.com/blog/5_espresso_profiles_for_light_roasted_coffee_beans |
| `medium-video` | 5 profiles for medium roasted beans (video transcript) | https://decentespresso.com/blog/5_profiles_for_medium_roasted_beans |
| `dark-video` | 5 profiles for dark roasted beans (video transcript) | https://decentespresso.com/blog/5_profiles_for_dark_roasted_beans |
| `community-index` | Community Profiles Index (Decent Diaspora Basecamp, doc 4263724650) | https://3.basecamp.com/3671212/buckets/7351439/documents/4263724650 |
| `bc-adaptive` | "New Adaptive Profiles for the DE1" by Jonathan Gagné (Decent Diaspora Basecamp) | https://3.basecamp.com/3671212/buckets/7351439/messages/3359060617 |
| `profile-notes` | Decent profile JSON `notes` field — built-in documentation shipped with each profile | *(embedded in profile JSON files in `resources/profiles/`)* |
