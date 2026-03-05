# Profile Knowledge Base

Curated knowledge about Decent DE1 profiles — which beans they suit, expected flavor outcomes, and recommended parameters. This data feeds the AI advisor's system prompt to enable cross-profile recommendations.

Every claim is tagged with a source key (e.g., `[SRC:medium]`). See [Sources](#sources) at the bottom for full URLs. When updating this document, always tag new information with its source.

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
- **How it works**: Combines pressure and flow control; pressurized pre-infusion followed by flow-controlled pour. Adapts to grind coarseness. `[SRC:medium]`
- **Key advantage**: Ability to "heal" uneven puck preparation `[SRC:medium]`
- **Adjustable variables**: Dose weight, pre-infusion temperature, infusion pressure, extraction pressure `[SRC:dflow-blog]`
- **Default flow rate**: 1.7 ml/s pour `[SRC:medium]` (community typical range: 2.0-2.7 ml/s `[SRC:adaptive-adastra]`)
- **Roast suitability**:
  - Light: Good `[SRC:medium]`
  - Medium: Excellent — brings out floral/fruity notes (mango) while retaining chocolate undertones `[SRC:medium]`
  - Dark: Good `[SRC:medium]`
- **Best for**: Versatile, all roast levels; users who want forgiving puck prep `[SRC:medium]`

### Adaptive v2

- **Category**: Flow/adaptive `[SRC:medium]`
- **How it works**: Rises pressure to 8.6 bar for ~4 seconds, then attempts sequential flow-rate steps from 3.5 ml/s down to 0.5 ml/s, locking in whatever flow rate exists when pressure peaks. Effectively adapts to grind size. `[SRC:adaptive-adastra]`
- **Flow range**: 2.0-2.7 ml/s (community data) `[SRC:adaptive-adastra]`
- **Shot duration**: 26-40 seconds (manual termination) `[SRC:adaptive-adastra]`
- **Ratio**: 1:2 to 1:3 `[SRC:adaptive-adastra]`
- **Pressure curve**: Should peak near 9 bar then gradually decline `[SRC:adaptive-adastra]`
- **Key advantage**: Tolerant of grind-size variation — too coarse produces allonge-style, too fine produces standard espresso, both potentially pleasant `[SRC:adaptive-adastra]`
- **Roast suitability**:
  - Light: Good (v2 updated to work with light roasts, previously medium-optimized) `[SRC:adaptive-search]`
  - Medium-light: Excellent — balanced, layered, pronounced acidity `[SRC:medium]`
  - Dark: Not recommended
- **Best for**: Medium-light beans; users who want automated flow adjustment `[SRC:medium]`

### Blooming Espresso

- **Category**: Blooming `[SRC:4mothers]`
- **How it works**: Fill at ~7-8 ml/s until pressure peaks, close valve for 30s bloom (aim for first drips around this time), then open to desired pressure. `[SRC:eaf-profiling]`
- **Grind**: Fine — grind as fine as manageable. Bloom allows use of finer grind. `[SRC:eaf-profiling]` `[SRC:rao-blooming]`
- **Ratio**: Useful up to 1:3 `[SRC:eaf-profiling]`
- **Total shot time**: ~70 seconds `[SRC:rao-blooming]`
- **Percolation**: After bloom, gently increase pressure to 8-9 bar, flow ~2-2.5 ml/s `[SRC:rao-blooming]`
- **Flavor**: Extremely high flavor intensity relative to ratio `[SRC:eaf-profiling]`
- **Roast suitability**:
  - Light: Excellent — biggest extraction, most rewarding when dialed in. Hardest to dial in. Best with flawless beans in quantity. `[SRC:light]`
  - Medium: Good `[SRC:4mothers]`
  - Dark: Not ideal (ultralight beans may experience puck degradation) `[SRC:4mothers]`
- **Best for**: Experienced users with good beans seeking maximum extraction `[SRC:light]`

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
- **Grind**: Coarsest of the espresso profiles; slightly coarser than standard `[SRC:eaf-profiling]` `[SRC:4mothers]`
- **Flow rate**: ~4.5 ml/s `[SRC:eaf-profiling]`
- **Pressure**: ~4-6 bar `[SRC:eaf-profiling]`
- **Ratio**: 1:4-1:5 `[SRC:eaf-profiling]`; up to 1:5-1:6 for Rao variant `[SRC:adaptive-adastra]`
- **Time**: ~30 seconds `[SRC:eaf-profiling]`
- **Extraction yield**: 21-23% `[SRC:4mothers]`
- **Flavor**: Bright, intense fruit and floral; gives up thickness for clarity `[SRC:eaf-profiling]`
- **Roast suitability**:
  - Ultralight: Best — minimal puck integrity demands `[SRC:4mothers]`
  - Light: Excellent — easiest to dial in for light roasts, fruity/natural beans `[SRC:light]`
  - Medium+: Not recommended
- **Best for**: Ultralight/light roasts, fruity or natural beans; midway between espresso and filter `[SRC:light]`

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

### Londinium / LRv3

- **Category**: Lever `[SRC:medium]`
- **How it works**: Inspired by spring-lever machines; pressurized soak time to enhance extraction `[SRC:medium]`
- **Roast suitability**:
  - Dark: Excellent — full body without harshness `[SRC:dark]`
  - Medium-dark: Good `[SRC:medium]`
  - Light: Struggles — risk of over-extraction or sourness `[SRC:medium]`
- **Best for**: Darker roasts seeking full body and smoothness `[SRC:dark]`

### Gentle & Sweet

- **Category**: Pressure `[SRC:light]`
- **Pressure**: Max 6 bar, slowly decreasing to 4 bar `[SRC:gentle-search]`
- **Flavor**: Smooth, sweet, low acidity; good alone or with milk. Less complexity than aggressive profiles. `[SRC:gentle-search]`
- **Roast suitability**:
  - Light: Good — beginner-friendly entry point `[SRC:light]`
- **Best for**: Beginners to light roasts; those wanting a smooth, sweet cup `[SRC:light]`

### Extractamundo Dos

- **Category**: Pressure
- **Roast suitability**:
  - Light: Good `[SRC:light]`
- **Best for**: Beans with slight off-flavors you want to hide `[SRC:light]`

### Flow Profile

- **Category**: Flow `[SRC:medium]`
- **How it works**: Maintains constant flow rate of 1.7 ml/s `[SRC:medium]`
- **Flavor**: Fruitier, more nuanced than Default; more complex flavor development `[SRC:medium]`
- **Roast suitability**:
  - Medium: Good `[SRC:medium]`
- **Best for**: Milky drinks and nuanced espresso `[SRC:medium]`

### Cremina

- **Category**: Lever `[SRC:dark]`
- **Flavor**: Big, strong, wake-me-up; traditional intense Italian extraction `[SRC:dark]`
- **Roast suitability**:
  - Dark: Excellent `[SRC:dark]`
- **Best for**: Classic Italian espresso `[SRC:dark]`

### 80's Espresso

- **Category**: Pressure `[SRC:dark]`
- **Flavor**: Fruit-forward from dark beans — raisins, red wine `[SRC:dark]`
- **Roast suitability**:
  - Dark: Good `[SRC:dark]`
- **Best for**: Getting fruity flavors from dark roasts `[SRC:dark]`

### Best Overall

- **Category**: Pressure `[SRC:dark]`
- **Roast suitability**:
  - Dark: Good `[SRC:dark]`
- **Best for**: Milky drinks `[SRC:dark]`

### E61

- **Category**: Pressure `[SRC:dark]`
- **Roast suitability**:
  - Dark: Good `[SRC:dark]`
- **Best for**: Balanced acidity with dark roasts `[SRC:dark]`

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

Quick reference for which profiles work across roast levels:

| Profile | Light | Medium | Dark | Category |
|---------|-------|--------|------|----------|
| D-Flow | Good | Excellent | Good | Lever/Flow hybrid |
| Adaptive v2 | Good | Excellent (med-light) | - | Flow/adaptive |
| Blooming | Excellent (hard) | Good | - | Blooming |
| Blooming Allonge | Excellent (ultralight) | - | - | Blooming/Allonge |
| Allonge | Best (ultralight) | - | - | Allonge |
| Default | - | Excellent | Good | Lever |
| Londinium/LRv3 | Struggles | Good (med-dark) | Excellent | Lever |
| Cremina | - | - | Excellent | Lever |
| Gentle & Sweet | Good (beginner) | - | - | Pressure |
| Extractamundo Dos | Good (hide defects) | - | - | Pressure |
| Flow Profile | - | Good | - | Flow |
| 80's Espresso | - | - | Good (fruit) | Pressure |
| Best Overall | - | - | Good (milk) | Pressure |
| E61 | - | - | Good (balanced) | Pressure |
| Turbo Shot | Good | Good | Good | Flow/Pressure |
| Filter 2.0/2.1 | Good | Good | Adjust temp down | Filter |

---

## TODO: Additional Data Needed

- [ ] Specific temperature recommendations per profile per roast level (most profiles lack this)
- [ ] D-Flow default temperature and pressure values from the profile JSON
- [ ] Blooming Espresso temperature guidance by roast
- [ ] Community favorites and real-world dial-in tips from Diaspora/Home-Barista
- [ ] Turbo Shot profile specifics as implemented in Decenza (vs generic turbo advice)
- [ ] Video timestamps from Decent's YouTube for user reference
- [ ] Grind size guidance relative between profiles (e.g., "2-3 clicks finer than D-Flow")

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
