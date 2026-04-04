# AI Advisor — Current State and Future Vision

## Current Architecture

### Components

| File | Purpose |
|------|---------|
| `src/ai/aimanager.h/cpp` | Central coordinator: provider management, conversation routing (max 5, keyed by bean+profile), shot analysis, email fallback |
| `src/ai/aiprovider.h/cpp` | Abstract base + 5 provider implementations (OpenAI, Anthropic, Gemini, OpenRouter, Ollama) |
| `src/ai/aiconversation.h/cpp` | Multi-turn conversation with persistent storage (QSettings). Follow-ups, shot context injection, history trimming |
| `src/ai/shotsummarizer.h/cpp` | Extracts structured shot data (phases, curves, anomalies) and builds text prompts. Separate system prompts for espresso vs filter |
| `src/network/shotserver_ai.cpp` | Web API endpoints for AI assistant |

### Providers

| Provider | Model | Caching | Cost |
|----------|-------|---------|------|
| Anthropic | Claude Sonnet 4.6 | Explicit `cache_control` on system prompt. 5-min TTL, ~90% discount on cached input | Cloud |
| OpenAI | GPT-4.1 | Automatic for prefixes >1024 tokens. ~50% discount | Cloud |
| Google Gemini | 2.5 Flash | Not implemented (supports explicit Context Caching API with longer TTL) | Cloud |
| OpenRouter | User-selected | Passes through to underlying provider | Cloud |
| Ollama | User-selected | N/A — local, no cost | Local/free |

### What the AI Receives Today

**System prompt** (~3-4K tokens base + ~150-300 tokens per-profile section): Espresso or filter-specific guidance covering DE1 machine behavior, pressure/flow interpretation, common shot patterns, roast considerations, grinder/burr geometry, and response guidelines. When the shot's profile matches a curated entry in the profile knowledge base (`resources/ai/profile_knowledge.md`), a profile-specific section is appended describing expected curve behavior, intentional design choices, and what NOT to flag as problems.

**Per-shot user prompt** (~1-2K tokens):
- Profile name, author, type, intent (notes)
- Profile recipe (frame-by-frame description)
- Dose, yield, ratio, duration
- Phase breakdown with actual vs target values at start/peak-deviation/end
- Coffee metadata (bean brand, type, roast level, roast date) — **only if user enters it manually**
- Grinder model and setting — **only if user enters it manually**
- TDS/EY — **only if user enters it manually**
- Tasting notes and enjoyment score — **only if user enters it manually**
- Anomaly flags (channeling, temperature instability)

**Dial-in history** (~200-500 tokens per historical shot, up to 5): When the current shot has a `knowledge_base_id`, the system queries the last 5 shots with the same KB ID from `ShotHistoryStorage::getRecentShotsByKbId()`. Each historical shot includes: profile name, recipe (frame-by-frame), dose/yield/ratio, duration, grind setting, temperature override, bean info, TDS/EY, score, and tasting notes. This lets the AI see what changed between shots (e.g., "you went 2 clicks finer and the sourness improved").

**Multi-shot conversations**: Previous shots are summarized and compressed. Older messages get trimmed to manage token count.

### What the AI Now Knows (Profile Knowledge Base)

When the shot uses one of ~19 curated profiles, the system prompt includes:
- Expected curve shapes (pressure, flow, temperature behavior)
- What is intentional vs. problematic (e.g., declining pressure in D-Flow is by design)
- Roast suitability and grind guidance
- Duration and ratio expectations

This prevents the AI from flagging intentional profile behaviors as problems (e.g., D-Flow's declining pressure, Blooming's 30s pause, Allongé's high ratio).

**Profile matching** (three-tier priority):
1. **Direct KB ID**: `knowledge_base_id` stored in profile JSON (Decenza extension field, de1app ignores unknown keys) and persisted in the shots DB (`profile_kb_id` column, migration 9). Survives Save As, reboots, and profile inheritance.
2. **Fuzzy title matching**: `normalizeProfileKey()` strips accents (é→e), normalizes punctuation (& → and), then tries direct → prefix → substring match against KB section headers and aliases.
3. **EditorType fallback**: For custom-named D-Flow/A-Flow profiles, maps the recipe editor type ("dflow" → "d-flow / default", "aflow" → "a-flow").

**Implementation**: `ShotSummarizer::shotAnalysisSystemPrompt()` tries the direct KB ID first, then falls back to `matchProfileKey()` for fuzzy matching. `ShotSummarizer::computeProfileKbId()` computes the ID at profile load time and stores it on the `Profile` object. 31 stock profiles ship with `knowledge_base_id` baked into their JSON files.

**UI indicator**: Profiles with a knowledge base entry show a sparkle icon (from `qrc:/icons/sparkle.svg`) in the profile selector list. The `hasKnowledgeBase` flag is read from the JSON during `refreshProfiles()` and exposed through all profile list methods.

### What the AI Does NOT Know Today

1. What other profiles are available on the machine
2. What profiles might work better for the user's beans/roast (deferred until bean database integration)
3. Community recommendations for bean/profile pairings
4. Details about the roaster or bean beyond what the user typed
5. What other users' experiences are with the same beans

---

## Shared Infrastructure: In-App AI and MCP

Both the in-app AI advisor and the MCP `dialing_get_context` tool use the same underlying components. Changes to shared code benefit both paths equally.

### Shared Components

| Component | File | What it provides |
|-----------|------|-----------------|
| **System prompt** | `ShotSummarizer::shotAnalysisSystemPrompt()` | Espresso/filter system prompt + dial-in reference tables + profile KB section. Both paths call this — the in-app AI uses it directly as the conversation system prompt, the MCP sends it as the `profileKnowledge` field |
| **Shot summarizer** | `ShotSummarizer::buildUserPrompt()` | Phase metrics, anomaly detection, recipe description. The MCP sends this as the `shotAnalysis` field |
| **Profile Knowledge Base** | `resources/ai/profile_knowledge.md` | Per-profile curated knowledge (18 profiles). Loaded as Qt resource, injected via `shotAnalysisSystemPrompt()` |
| **Dial-in reference tables** | `resources/ai/espresso_dial_in_reference.md` | Structured variable→taste tables. Loaded as Qt resource, appended in `shotAnalysisSystemPrompt()` |
| **Profile KB matching** | `ShotSummarizer::matchProfileKey()` | Three-tier matching: direct KB ID → fuzzy title → editor type fallback |
| **Grinder context** | `ShotHistoryStorage::queryGrinderContext()` | Observed settings range, min/max, smallest step. Used by both MCP and in-app AI `requestRecentShotContext()` |
| **Dial-in history** | `ShotHistoryStorage::getRecentShotsByKbId()` | Last N shots with same profile family |

### What Differs Between Paths

| Aspect | In-App AI | MCP |
|--------|-----------|-----|
| **Multi-turn** | Yes (conversation history, shot-to-shot deltas) | No (single tool call, LLM manages context) |
| **Grinder context format** | Markdown text in user prompt | JSON in tool response |
| **Bean age** | Computed in `buildUserPrompt()` from shot's `roastDate` | Computed in tool response from `Settings::dyeRoastDate()` |
| **Current DYE settings** | Not included (shot data has what was saved) | Included as `currentBean` JSON block |
| **Current profile info** | Not included | Included as `currentProfile` JSON block |

---

## Lessons Learned: Profile Knowledge Doesn't Scale (March 2026)

### The Problem

A user's D-Flow / Q conversation exposed three failures where the AI gave bad advice despite having relevant information:

1. **Wrong pressure range in knowledge base**: We hand-wrote "Pressure peaks ~8-9 bar" in profile_knowledge.md. The profile author (Damian) specifies "between 6 and 9 bar" in the de1app. The AI flagged a 7.7 bar peak as too low and recommended grinding finer — wrong advice.

2. **Temperature misdiagnosis**: D-Flow deliberately uses a low fill temperature (84°C) stepping to 94°C. The actual basket temperature reaches ~88°C by design — the 94°C setpoint just drives the heater to push hot water that mixes with the cooler pool. The AI saw a 6-8°C gap between target and actual and diagnosed "heat soak the machine longer" — completely wrong.

3. **Misleading observation from the summarizer**: The app's own `temperatureUnstable` flag fired on every D-Flow shot (average deviation from target > 2°C is guaranteed by the profile's temperature stepping design). This actively pointed the AI at the wrong problem.

The AI had to be corrected **twice** by the user before giving useful advice. The profile knowledge base entry, which was supposed to prevent exactly this, had the wrong numbers.

### Why Hand-Curated Knowledge Doesn't Scale

Fixing D-Flow required:
- Finding the profile description in the de1app editor code (it was hardcoded in UI, not in the `profile_notes` field)
- Finding Damian's forum posts explaining the temperature stepping design
- Checking Damian's website (coffee.brakel.com.au) for the correct pressure range
- Understanding how the DE1's heater/mixing behavior creates the temperature gap

This level of research per profile is not sustainable for 18+ profiles, especially for profiles by community authors who may not have published detailed explanations.

### What We Fixed

1. **Profile notes populated**: Added Damian's descriptions to `d_flow_q.json` and `d_flow_la_pavoni.json` — these flow into the AI prompt as "Profile intent" lines automatically.
2. **Corrected pressure range**: `profile_knowledge.md` now says "between 6 and 9 bar" matching the author.
3. **Added temperature explanation**: Knowledge base now explains the intentional temperature stepping and why the gap is by design.
4. **Grind-dependent curve shape**: Added that finer grinds produce flat pressure (boiler-like) while coarser grinds produce declining pressure (lever-like) — both valid.
5. **Softened temperature observation**: Changed from "Temperature unstable" (which sounds like a problem) to "Temperature deviation" with guidance to check profile notes before flagging.

### The Path Forward: Three Scalable Improvements

Rather than trying to deeply research every profile, we should make the AI better at interpreting what it already has.

#### 1. Profile Notes as Primary Authority (cost: 0 tokens)

The profile author's own description is the most reliable source of truth. The `notes` field in the profile JSON already flows into the prompt as "**Profile intent**". We need to:

- **Audit all shipped profiles** for empty or sparse notes. The de1app has notes for most profiles — verify they've been copied over (done March 2026: only D-Flow/Q and La Pavoni were missing, now fixed).
- **Check de1app editor code** for profiles whose descriptions are hardcoded in the UI rather than in `profile_notes` (D-Flow was the only case found).
- **Instruct the AI to trust profile notes over knowledge base** when they conflict. The filter system prompt already does this ("Always read and respect" the profile intent), but the espresso prompt is weaker — it says "Profile Intent is the Reference Frame" but doesn't explicitly prioritize notes over knowledge base entries.

#### 2. Recipe-Aware System Prompt (cost: ~200-300 tokens, cacheable)

The profile recipe is already in every prompt. A shot using D-Flow / Q includes:

```
1. Filling (25s) PRESSURE 6.0bar 84°C exit:p>3.0
2. Infusing (1s) PRESSURE 6.0bar 94°C
3. Pouring (127s) FLOW 1.8ml/s 94°C lim:10.0bar
```

An AI that understands DE1 physics can derive expected behavior from this recipe alone:
- Frame 1 at 84°C, Frame 2 at 94°C → intentional temperature stepping, expect actual temp to lag
- Frame 3 is FLOW-controlled → pressure is passive, will decline as puck erodes
- Frame 3 has 10 bar limiter → pressure "target" of 10 bar is a ceiling, not a goal

But the AI repeatedly failed to make these inferences. We should add explicit recipe-interpretation rules to the system prompt:

```
## Reading the Recipe for Expected Behavior

The profile recipe tells you what SHOULD happen. Use it to set expectations
before looking at actual data:

**Temperature stepping**: If frames use different temperatures (e.g., 84°C fill
→ 94°C pour), the actual temperature will ALWAYS lag behind the target. This is
physics — the heater pumps hot water that mixes with the cooler pool above the
puck. A 5-8°C gap between target and actual during temperature transitions is
normal and expected. Only flag temperature issues if actual temp deviates from
target during a STABLE phase (same temperature across consecutive frames).

**Flow-controlled pour with pressure limiter**: When a pour frame controls FLOW
(e.g., 1.8 ml/s) with a high pressure limiter (e.g., 10 bar), pressure will
peak based on puck resistance and then decline as the puck erodes. The limiter
is a safety ceiling. Pressure anywhere from 4 to the limiter value is normal.
The peak depends on grind — do not assume a specific peak value unless the
profile notes or knowledge base states one.

**Pressure-controlled fill/infuse → Flow-controlled pour**: This is the
lever/flow hybrid pattern (D-Flow, Londinium family). Pressure builds during
fill/infuse, then the switch to flow control means pressure becomes passive.
A declining pressure curve after the switch is the expected signature, not a
problem.

**Exit conditions**: Frames with exit conditions like "exit:p>3.0" advance to
the next frame when the condition is met. Short phase durations (1-2s) after
exit conditions are normal — the machine transitions quickly.
```

This is profile-independent guidance that improves advice quality across ALL profiles, including custom ones with no knowledge base entry. It's fully cacheable and costs ~250 tokens.

Community sources confirm these interpretation principles:
- Scott Rao (scottrao.com): Pressure curve volatility indicates channeling from clumped grounds and poor puck prep, not profile issues. The slope of pressure dropoff is characteristic of the grinder/burrs.
- Decent "5 profiles for medium" blog: D-Flow adapts behavior by grind — mimics 8.5 bar for fine grinds, Londinium for optimal, Gentle & Sweet for coarse.
- Decent "4 mothers" theory: Each mother category (lever, blooming, allongé, flat-9-bar) has fundamentally different expected curves. Roast level is the most important factor in choosing a recipe.
- home-barista.com community: DE1 set-point temperatures are generally lower than conventional machines, with 80s°C being common. Preference for shots where flow stabilizes immediately after preinfusion.

#### 3. Smarter Observations from the Summarizer (cost: 0 tokens — less noise)

The summarizer currently generates dumb flags based on simple thresholds. These should be recipe-aware:

**Temperature deviation** (partially fixed March 2026):
- Current: Flags when average deviation from target > 2°C.
- Problem: Always fires for profiles with temperature stepping (D-Flow, 80s Espresso, any profile with different fill/pour temps).
- Better: Check if the profile recipe has different temperatures across frames. If so, only check deviation during frames with stable temperature (same temp as previous frame). Or suppress the flag entirely and let the AI interpret using the recipe.

**Channeling detection**:
- Current: Flags sudden flow spikes during flow-controlled phases.
- Better: Also note the pressure curve slope — Scott Rao's observation that pressure curve volatility during extraction correlates with channeling and puck prep quality is a useful diagnostic. The slope of pressure decline is characteristic of the grinder/burrs, so erratic slopes (not smooth decline) are more indicative of channeling than a specific peak value.

**Pressure behavior** (not currently flagged, but could help):
- If the profile is flow-controlled and pressure peaks well below typical levels (< 4 bar), note this as potentially too coarse — but only as an observation, not a diagnosis.

### Refocusing the Knowledge Base

With recipe-aware interpretation handling expected curves and temperature behavior, the profile knowledge base (`profile_knowledge.md`) should shift focus to things that **cannot be derived from the recipe**:

| Keep in KB (not derivable) | Remove from KB (derivable from recipe) |
|---|---|
| Roast suitability per profile | Expected pressure curve shape |
| Flavor character and what profiles emphasize | Temperature behavior during stepping |
| Community dial-in tips (e.g., "stop at pressure elbow") | Which variable is controlled vs passive |
| Grind sensitivity and range | What the limiter means |
| Profile author's design philosophy | Phase transition behavior |
| Cross-profile comparisons ("more body than X, less clarity than Y") | |
| Known pitfalls ("if pressure hits max, grind is too fine") | |

This makes the KB smaller, more accurate (less room for wrong numbers like "~8-9 bar"), and focused on genuinely curated wisdom that adds value beyond what the recipe encodes.

### What Information We Still Need

The D-Flow fix was possible because Damian published detailed explanations. Most profile authors haven't. Here's what's missing:

#### Profile-specific gaps in the knowledge base

| Profile | Quality | What's missing |
|---------|---------|----------------|
| **D-Flow** | Strong | Fixed March 2026. Author-sourced. April 2026: fill temps verified per-variant against JSON frames, Damian's LRv2/LRv3 temps documented. |
| **A-Flow** | Medium | No author (Janek) design philosophy. Does A-Flow use temperature stepping? Are the 9-10 bar pressure numbers from Janek or our guess? No expected pressure range from author. |
| **Adaptive v2** | Good | Has Gagné's Coffee ad Astra post. April 2026: pressure corrected to ~9 bar (8.8 bar exit, 9.5 bar extraction limiter) from JSON frames. |
| **Blooming Espresso** | Good | Has Rao's guidance. April 2026: added temperature line (97.5→90→92°C), corrected fill flow to 4 ml/s, added extraction limiter detail. |
| **Blooming Allongé** | Good | April 2026: corrected percolation flow to 3.5 ml/s from JSON, added temperature range (91-95°C), added pressure guidance for too-fine grind. |
| **Allongé / Rao Allongé** | Good | — |
| **Default** | Good | April 2026: added temperature (90→88°C), added DO NOT flag for declining pressure. |
| **Londinium/LRv3** | Good | April 2026: added disambiguation note vs Damian's LRv2/LRv3, documented flow-limit behavior from JSON frames. |
| **Turbo Shot** | Good | April 2026: corrected temperature to 90°C default from JSON, added frame-level detail (preinfusion flow, pressure limiter). |
| **Filter 2.0/2.1** | Good | April 2026: corrected preinfusion temp to 92°C from JSON, added Filter 2.1 variant note. |
| **Filter3** | Good | **New** April 2026. No-bypass filter profile. Full 7-phase flow data from JSON, setup/calibration from official Decent guide and Scott Rao blog, dialing-in and output targets from Diaspora community. |
| **Sprover** | Medium | April 2026: clarified dosing jargon. Still sparse on dial-in tips. |
| **Gentle & Sweet** | Good | April 2026: added temperature (88°C), added DO NOT flag for temperature increase recommendations. |
| **Extractamundo Dos** | Good | April 2026: documented dynamic bloom phase and temperature stepping (83.5→67.5→74.5°C) from JSON frames, added DO NOT flag. |
| **Flow Profile** | Good | April 2026: expanded with variant flow rates (2.0/1.2 ml/s), temperatures (92/88°C), grind guidance, two DO NOT flags. |
| **Cremina** | Good | April 2026: corrected temperature to 91.5-92°C from JSON. |
| **80s Espresso** | Good | April 2026: corrected temperature range to 82→72°C and pressure to 7.8→5 bar from JSON. |
| **Best Overall** | Good | April 2026: corrected pressure to 8.4 bar from JSON. |
| **E61** | Good | April 2026: quantified pressure decline threshold (avoid <4 bar). |

#### Systematic gaps (not profile-specific)

1. **No AI advice validation method.** We found the D-Flow problem because a user exported a bad conversation. We have no systematic way to test whether advice improved. Need: a set of test conversations (real or synthetic) that can be replayed against updated prompts.

2. **No user feedback loop for bad advice.** The enjoyment score rates the shot, not the advice. If the AI gives wrong recommendations, we only learn about it anecdotally. Could add: a simple "was this advice helpful?" signal after each AI response.

3. ~~**Dial-in reference tables are collected but unused.**~~ **Done** (April 2026, PR #635). Now loaded from Qt resource and included in system prompt.

4. ~~**Espresso system prompt doesn't prioritize profile notes.**~~ **Done** (April 2026, PR #635).

5. **Profile notes quality varies.** All shipped profiles have notes populated, but quality ranges from detailed (`blooming_espresso.json` — full paragraph explaining the technique) to minimal (`damian_s_q.json` — "A very popular profile made with D-Flow, spun out as its own profile"). The minimal ones don't help the AI much.

6. ~~**Temperature stepping profiles beyond D-Flow.**~~ **Done** (April 2026, PR #635). Recipe-aware system prompt rules now cover all temperature-stepping profiles generically. Additionally, the April 2026 KB quality pass (PR #646) documented exact temperature stepping values for Extractamundo Dos (83.5→67.5→74.5°C), 80s Espresso (82→72°C), Filter 2.0 (92→85°C), and Filter3 (94→92→90→88°C).

7. **A-Flow author knowledge.** A-Flow is the second most popular profile editor (after D-Flow) and the only other custom editor. Janek's design intent, expected ranges, and guidance are not well-documented in our KB. Source: check if Janek has published anything similar to Damian's coffee.brakel.com.au site.

### Priority (revised)

These improvements should be implemented before the other ideas in this doc (profile catalog, bean enrichment, cross-profile recommendations) because they improve advice quality for every shot at near-zero cost:

1. **Recipe-aware system prompt** — highest impact, addresses the root cause of the D-Flow failures. ~250 cacheable tokens. Should be implemented next.
2. **Espresso prompt: trust profile notes** — one sentence addition to espresso system prompt, matching what the filter prompt already does. 0 cost.
3. **Dial-in reference tables in system prompt** — move from Phase 1 to Phase 0. Data is already collected in ESPRESSO_DIAL_IN_REFERENCE.md. ~800-1000 cacheable tokens.
4. **Smarter summarizer observations** — reduces noise that actively misleads the AI. Code change only, no token cost.
5. **KB refocus** — lower priority, do incrementally. Start with A-Flow (seek author guidance) and thin entries (Default, Flow Profile).
6. **Test conversations** — collect 5-10 exported AI conversations covering different profiles and failure modes. Use to validate prompt changes before shipping.

---

## Ideas for Improvement

### 1. Profile Catalog in System Prompt

**Problem**: The AI only knows about the current shot's profile. It can't recommend switching profiles or compare approaches.

**Approach**: Generate a compact one-liner per profile and include in the system prompt. Example:

```
D-Flow / default (Damian) — lever-style: 3bar PI, 1.7ml/s flow pour, 88C, espresso
Blooming Espresso — bloom 30s then declining pressure, 92C, espresso
Best Practice Light Roast — designed for light/Nordic roasts, 94-96C
Filter 2.1 — high-flow pour-over style, 96C, 1:15 ratio, filter
```

**Token cost**: ~80-100 chars/profile x ~95 profiles = ~2-3K tokens added to system prompt.

**With caching**: Anthropic 90% discount, OpenAI 50% automatic. Minimal incremental cost per request.

**Status**: **Done** (April 2026). Implemented as a compact KB-only catalog (19 profiles) rather than all ~95 profiles. Built from `profile_knowledge.md` during KB loading via `buildProfileCatalog()`. Included in `shotAnalysisSystemPrompt()` as "Available Profiles with Curated Knowledge" section. ~500 cacheable tokens.

### 2. Curated Profile Knowledge Base

**Problem**: The profile catalog (idea #1) tells the AI what each profile *does* mechanically, but not what it's *for* — which roasts, which flavor goals, which skill level.

**Approach**: A curated resource file (text or JSON) encoding community wisdom per profile:

```
D-Flow / default:
  Best for: Medium to dark roasts
  Typical: 88C, 18g dose, 1:2.5-3 ratio
  Character: Forgiving puck prep, body-forward, classic espresso
  Not ideal for: Very light Nordic roasts (under-extracts)

Best Practice Light Roast:
  Best for: Light and Nordic roasts
  Typical: 94-96C, 18g dose, 1:2.5+ ratio
  Character: Clarity, brightness, needs precise puck prep

Blooming Espresso:
  Best for: All roasts, especially when seeking clarity
  Typical: 92C, bloom 30s, gentle extraction
  Character: Highlights origin flavors, lower body than traditional
```

**Who writes it**: Could be maintained by the developer/community, updated with app releases. Alternatively, seeded from community knowledge and refined over time.

**Status**: **Implemented.** Knowledge base lives in [`docs/PROFILE_KNOWLEDGE_BASE.md`](PROFILE_KNOWLEDGE_BASE.md) (human-readable, source-tagged) and [`resources/ai/profile_knowledge.md`](../resources/ai/profile_knowledge.md) (AI-optimized, loaded as Qt resource). 19 profiles documented with expected curve behavior, temperature, flow rates, grind guidance, and "DO NOT flag" instructions. Data sourced from Decent blog posts, the "4 mothers" theory, and transcripts from three Decent video tutorials (light/medium/dark roast profiles). Profile matching in `ShotSummarizer::shotAnalysisSystemPrompt()` with fuzzy title matching and alias support.

### 3. User History Summary

**Same-profile dial-in history** — **Implemented.** When analyzing a shot, up to 5 recent shots with the same `knowledge_base_id` are included in the user prompt via `ShotHistoryStorage::getRecentShotsByKbId()`. Each historical shot includes the full profile recipe, grind setting, temperature, dose/yield, score, and tasting notes. This lets the AI track dial-in progression and correlate changes with results (e.g., "you ground 2 clicks finer and the sourness resolved").

**Cross-profile history** — Not yet implemented. The AI doesn't know the user's track record *across* different profiles. A session-level summary would enable:

```
YOUR PROFILE HISTORY (last 90 days):
- D-Flow / default: 47 shots, avg rating 78, mostly medium-dark beans
  Best: 85 rating, Ethiopian Sidamo, 18g->38g, 88C
  Worst: 52 rating, Kenyan AA light, 18g->36g, 88C
- Blooming Espresso: 12 shots, avg rating 84, mostly light beans
  Best: 92 rating, Kenyan AA, 18g->42g, 94C
- Never tried: Turbo Shot, Rao Allonge, Adaptive v2...
```

**Token cost**: ~1-2K tokens. Changes per session, not per request — could be cached as a second block.

**Enables**: "You've been averaging 78 on D-Flow with light roasts but 84 on Blooming. Your data suggests Blooming works much better for your light roasts."

**Status**: Same-profile history done. Cross-profile summary needs aggregation query + summary generation.

### 4. Bean Data Enrichment

**Problem**: Users type "Onyx Southern Weather" but the AI may or may not know the roast level, processing, origin, or recommended recipe. Small/local roasters and seasonal lots are unknown. Today the BeanInfoPage only captures: roaster, coffee name, roast date, roast level (dropdown), grinder, setting, barista. No origin, processing, variety, altitude, or roaster's tasting notes.

#### Existing Bean Data Sources (Research, March 2026)

| Source | Data | Size | API | Decent Ecosystem |
|--------|------|------|-----|-----------------|
| **Loffee Labs Bean Base + Roasters Registry** | **Bean Base**: roaster, name, origin, processing, variety, roast level, tasting notes (categorized by flavor family), price, body, description. **Roasters Registry**: roaster name, region, country, type, linked bean-base stats. Separate but linked databases rebuilt together in V4.0 — beans are children of roasters. | Thousands of coffees, hundreds of roasters | Yes (contact loffeelabs@gmail.com for access) | Already integrated with visualizer.coffee |
| **RoastDB** | Origin, variety, process, roast level, tasting notes, price | 4,500+ coffees, 472+ roasters, 30 countries | No public API (Next.js + SQLite web app) | None |
| **Visualizer.coffee coffee_bags API** | roast_date, origin (country, region, farm, farmer), variety, elevation, processing, harvest time, tasting notes, roast level, quality score | User's own data only (authenticated) | Yes (existing auth in Decenza) | Core integration already exists |
| **Beanconqueror** | Origin, variety, processing, roast level per bean | Per-user local data | No public search API | Indirect (both integrate with visualizer) |
| **Coffee Quality Institute DB** | 1,300+ arabica reviews: processing, country, farm, mill, quality metrics | Static dataset | GitHub CSV | None |
| **AI training data** | Major roasters' flagships and long-running blends | Broad but unreliable for seasonal/local | Free (part of AI call) | N/A |

**Key finding**: Visualizer's `coffee_bags` API has all the right fields (origin, variety, processing, elevation, tasting notes) but only returns the **user's own data** — it's not a community-searchable database. The community search layer is **Loffee Labs Bean Base**, which visualizer.coffee already uses for its "Search Loffee Labs Bean Base" feature.

#### Recommended Approach: Two-Tier Lookup

**Tier 1: Visualizer coffee_bags (user's own data, no new API needed)**
- Decenza already has visualizer auth (`VisualizerUploader` with token)
- The `coffee_bags` API has excellent fields: origin (country, region, farm, farmer), variety, elevation, processing, harvest time, tasting notes, roast level, quality score
- But it's **per-user only** — only returns bags the user has already entered in visualizer
- Useful for: auto-filling fields the user already entered on the visualizer side, avoiding double entry
- Zero new dependencies — just add a GET to the existing visualizer integration

**Tier 2: Loffee Labs Bean Base + Roasters Registry (community search, needs API access)**
- Thousands of curated coffees from hundreds of roasters
- **Two linked databases**: Bean Base (beans with origin, processing, variety, tasting notes, etc.) and Roasters Registry (roaster directory with region, country, type). Beans are linked to their roasters. Rebuilt together in V4.0 on shared codebase.
- Search by roaster + bean name returns structured data
- Already integrated with visualizer.coffee (the "Search Loffee Labs Bean Base" field in visualizer's UI goes through their API)
- **Requires API access from Loffee Labs** — Keith from LoffeeLabs responded (March 2026): API is undergoing extensive modernization. New API keys paused until new system launches. Beta access offered (~1 month), full production ~2-3 months. Free usage including post-launch. New API won't require manual key approval.
- Fallback when the user's own visualizer data has no match
- **Open questions for Loffee Labs** (follow up when they respond):
  1. Can we search by roaster name to get all their current beans?
  2. Does the API expose roaster details (location, website, type)?
  3. What's the bean ID format — is it the UUID that visualizer uses as `canonical_coffee_bag_id`?

**Visualizer <-> Bean Base linking via `canonical_coffee_bag_id`:**
The visualizer API has a `canonical_coffee_bag_id` field (UUID, nullable) on both `coffee_bags` and shots. This is the mechanism for linking to external databases like Bean Base. This means Decenza can:
- **Read**: Pull the user's coffee bags from visualizer, which may already have Bean Base links from the visualizer web UI
- **Write**: When user selects a Bean Base result in Decenza, create/update a `coffee_bag` on visualizer with the `canonical_coffee_bag_id` set to the Bean Base UUID — keeping visualizer in sync
- **Link shots**: When uploading shots, include `coffee_bag_id` to link shot -> bag -> Bean Base

This creates a full round-trip: Bean Base -> Decenza -> Visualizer -> Bean Base, so bean data entered in any app stays connected.

**Why this two-tier approach:**
1. Tier 1 is free and immediate — no new API keys, uses existing auth
2. Tier 2 provides community-wide coverage for new beans
3. User's own data takes priority (they may have corrected/customized fields)
4. Writing back to visualizer with `canonical_coffee_bag_id` keeps the ecosystem in sync

**Proposed flow:**
1. User enters roaster + bean name on BeanInfoPage
2. After both fields have text (debounced), Decenza queries:
   a. First: visualizer `coffee_bags` API (user's own data, filtered by name match)
   b. If no match: Loffee Labs Bean Base API (community search)
3. Results shown as suggestions or auto-fill prompt
4. Auto-fills: origin, processing, variety, roast level, roaster's tasting notes
5. User can accept, edit, or dismiss the enriched data
6. Enriched data stored on bean preset and passed to AI with shot analysis
7. If Bean Base result was used, create/update a visualizer `coffee_bag` with `canonical_coffee_bag_id` so visualizer stays in sync

**New fields needed on bean preset:**
- `origin` — Country/region (e.g., "Ethiopia, Guji")
- `processing` — Washed, Natural, Honey, Anaerobic, etc.
- `variety` — Caturra, Gesha, SL28, etc.
- `altitude` — Growing altitude if available
- `roasterTastingNotes` — Roaster's description (distinct from user's post-shot notes)
- `roasterRecommendedRecipe` — Dose/yield/temp if roaster provides it
- `beanBaseId` — For future lookups and cache invalidation

**What this enables for the AI:**
```
Today:     "Coffee: Onyx - Southern Weather"
Enriched:  "Coffee: Onyx - Southern Weather (Colombia, Huila. Washed Caturra,
            1700-1900m. Medium-light roast. Roaster notes: milk chocolate,
            brown sugar, mandarin orange)"
```

The AI can now say: "Your Colombian washed beans have chocolate/citrus notes — D-Flow at 88C should bring out the chocolate, but if you want more of the mandarin brightness, try raising temp to 91C or switching to Blooming Espresso."

**Implementation steps:**
1. Add new fields to `Settings::addBeanPreset()` / `updateBeanPreset()` and `ShotMetadata`
2. Create `BeanLookupManager` in `src/network/` for both visualizer and Bean Base queries
3. Add lookup trigger in BeanInfoPage QML (debounced, after roaster+bean entered)
4. Show enriched data in UI with accept/edit/dismiss
5. Pass enriched fields through to `ShotSummarizer::buildUserPrompt()`
6. Contact Loffee Labs for Bean Base API access (required for Tier 2)

**Fallback options (when neither tier has a match):**

**B. Lean on AI training data (free, immediate)**
- Add to system prompt: "If you recognize the roaster and bean, share what you know about roast level, processing, and flavor profile. If you don't recognize them, say so and ask the user."
- Pro: Zero implementation cost, works for major roasters
- Con: Unreliable for seasonal/small roasters, may hallucinate
- Can be implemented immediately as a stopgap

**C. Web search fallback**
- If Bean Base returns no results, optionally search the web for the roaster's product page
- Use the configured AI provider to extract structured data from the page
- Pro: Works for any roaster with a website
- Con: Adds latency, unreliable parsing, may not find small roasters

**D. Community-contributed via visualizer.coffee**
- Users who enter bean details build up shared data via visualizer's `coffee_bags` API
- When someone enters the same roaster/bean, details auto-populate from community data
- Pro: Accurate, grows over time, already has API
- Con: Requires visualizer premium for writes, slow to build critical mass

**Status**: Not implemented. Tier 1 (visualizer) ready to build. Tier 2: **Loffee Labs responded (March 2026)** — their API is being modernized; beta access offered in ~1 month, free usage. See dependency notes below.

### 5. Gemini Context Caching

**Problem**: Anthropic caches for 5 minutes, OpenAI caches automatically. But users who pull shots throughout the day (e.g., 30 minutes apart) lose the cache between shots.

**Approach**: Use Gemini's explicit Context Caching API to create cached content resources with longer TTL (minutes to hours). The system prompt + profile catalog would be cached once per session.

**Status**: Not implemented. Would be valuable if the profile catalog (idea #1) or knowledge base (idea #2) significantly increases the system prompt size.

### 6. Cross-Profile Recommendations

**Problem**: Even with a profile catalog, the AI needs guidance on *when* to recommend switching profiles.

**Approach**: Add a section to the system prompt:

```
## When to Suggest a Profile Change

Consider recommending a different profile when:
- The user's roast level mismatches the profile's design temperature
  (e.g., light roast on an 88C profile)
- Repeated shots show the same issue that a different profile addresses
  (e.g., always under-extracting → try a profile with longer preinfusion)
- The user asks about a different brewing style
  (e.g., "I want more clarity" → suggest a blooming or turbo profile)

Never suggest a profile change after a single shot unless the mismatch
is severe. Give the current profile 2-3 shots to dial in first.
```

**Status**: **Done** (April 2026). Added "When to Suggest a Different Profile" section to espresso system prompt. Uses the profile catalog (idea #1, also done) for cross-profile awareness.

### 8. Structured Grinder + Burr Fields

**Problem**: The current `dyeGrinderModel` is a single free-text field (e.g. "DF83V"). This loses structure — the AI can't distinguish manufacturer from model, and has no idea what burrs are installed. For grinders with interchangeable burrs (Zerno Z1/Z2, DF64/83, EG-1, Lagom P64/P100, etc.), the burr set matters as much as the grinder for flavor advice.

**Approach**: Split the single "Grinder" field into three structured fields, following the same cascade pattern as beans (Roaster → Coffee):

```
Grinder Manufacturer → Grinder Model → Burrs
       "Turin"         →   "DF83V"    → "SSP HU 83mm"
       "Zerno"         →   "Z1"       → "SSP MPV2 64mm blind"
       "Eureka"        →   "Mignon Specialita" → ""  (no aftermarket burrs)
```

Each field cascades suggestions from history:
- **Manufacturer**: `getDistinctGrinderBrands()` — all brands the user has entered
- **Model**: `getDistinctGrinderModelsForBrand(brand)` — models for selected brand
- **Burrs**: `getDistinctGrinderBurrsForModel(brand, model)` — burr sets used with that grinder

**UI Layout** (BeanInfoPage):

```
Row 1: [Roaster]       [Coffee]    [Roast date]
Row 2: [Grinder brand] [Model]     [Burrs]
Row 3: [Roast level]   [Setting]   [Barista]
```

**Data Changes**:

| Component | Current | Proposed |
|-----------|---------|----------|
| `Settings` | `dyeGrinderModel` (single string) | `dyeGrinderBrand`, `dyeGrinderModel`, `dyeGrinderBurrs` |
| `ShotHistoryStorage` DB | `grinder_model TEXT` | `grinder_brand TEXT`, `grinder_model TEXT`, `grinder_burrs TEXT` |
| `ShotMetadata` struct | `grinderModel` | `grinderBrand`, `grinderModel`, `grinderBurrs` |
| Bean presets | `grinderModel` | `grinderBrand`, `grinderModel`, `grinderBurrs` |
| Visualizer upload | `grinderModel` in notes | `grinderBrand + " " + grinderModel` in notes (backward compat) |
| FTS index | includes `grinder_model` | includes `grinder_brand`, `grinder_model`, `grinder_burrs` |
| AI prompt | "Grinder: DF83V" | "Grinder: Turin DF83V with SSP HU 83mm burrs" |

**Migration**: DB migration uses `src/core/grinderaliases.h` — a lookup table of ~90 grinders with aliases that map common user-typed strings to structured brand/model/stock_burrs. The migration:
1. Lowercase-normalizes the existing `grinder_model` string
2. Matches against aliases (e.g. "niche" → Niche/Zero, "df83v" → Turin/DF83V, "comandante" → Comandante/C40 MK4)
3. On match: populates `grinder_brand`, `grinder_model`, and auto-fills `grinder_burrs` with `stock_burrs`
4. On no match: keeps entire string in `grinder_model`, leaves `grinder_brand` and `grinder_burrs` empty
5. Aliases prioritize longer matches first to avoid "niche" matching before "niche duo"

**Burr Auto-Fill**: When the user selects a grinder manufacturer + model from the autocomplete dropdown, the burr field auto-fills with the stock/most popular burr from `src/core/grinderaliases.h`. For grinders with `burr_swappable: true`, clicking the burr field shows a picker with:
- Common burr options for that grinder size (from GRINDER_DATABASE.md aftermarket burr sets)
- Previously used burr values from shot history
- Free-text entry for custom burrs
For non-swappable grinders (e.g. Eureka Mignon, Comandante), the field auto-fills and is less prominent since the user can't change burrs.

**Backward Compatibility**: `dyeGrinderModel` getter could return `brand + " " + model` for any code that still reads the old combined field. Old bean presets with just `grinderModel` continue to work (loaded into `grinderModel`, `grinderBrand` stays empty).

**AI Impact**: With manufacturer, model, and burrs, the AI can look up the grinder in `GRINDER_DATABASE.md` and know:
- Burr size and geometry → appropriate grind range and sensitivity
- Burr flavor profile → expected taste characteristics
- Whether the grinder accepts aftermarket burrs → can suggest burr changes
- Adjustment resolution → how much one click/turn changes the grind

**Status**: Implemented in PR #368.

### 10. Espresso Dial-In Reference Tables in System Prompt

**Problem**: The current system prompt has only 3 lines for roast considerations and generic common patterns (gusher, choker, sour, bitter). It lacks structured multi-variable relationships — e.g., "what does higher flow rate do to clarity vs body?" or "how should infusion length change for different roast levels?"

**Source**: Åbn Coffee "Espresso tabel oversigt" — a community reference mapping 7 espresso variables against taste/texture outcomes. See [`docs/ESPRESSO_DIAL_IN_REFERENCE.md`](ESPRESSO_DIAL_IN_REFERENCE.md).

**What it adds beyond the current system prompt**:
1. **5-tier roast level matrix** — maps light→dark against acidity, clarity, extractability, mouthfeel, puck risk, puck prep importance, and specific flavor notes (fruits → caramel → choc+fruit → milk choc → dark choc). Current prompt only has 3 tiers with rough temp ranges.
2. **Grind→flow→pressure dynamics** — how grind affects channelling risk, the body/sweetness curve (less → more → less), clarity, flow limit behavior, and pressure decline patterns. Directly relevant to DE1 curve interpretation.
3. **Preinfusion tuning** — drip weight targets (4-10g vs 0.5g), move-on timing (30s vs 5s), and texture/body/clarity tradeoffs. Current prompt has nothing on preinfusion dialing.
4. **Pressure sweet spot** — 6-9 bar range with taste descriptions (thin/watery → clear delicate → rich traditional → muddy). Current prompt says "don't assume 9 bar" but doesn't explain what different pressures taste like.
5. **Flow rate → flavor pairing** — high flow favors florals/fruits with clarity; low flow favors chocolate/nuts with body. Plus flow rate recommendations per roast level and use-case ranges (milk drinks vs straight espresso).
6. **Gagné's ratio-flow formula** — `Flow = 4.867 - (6.6 × ratio)` — a concrete relationship between ratio and recommended flow rate.
7. **Flavor targeting & correction tables** — full matrix of "want more X → adjust Y in direction Z" for acidity, sweetness, body, clarity, wine-like texture. Plus correction tables for sourness, bitterness, burnt taste, thin taste, muddiness.
8. **TDS vs EY% taste mapping** — bitterness increases with both, sourness increases with high TDS + low EY, dark chocolate at high EY + low TDS. Useful when user provides refractometer data.

**Approach**: Add a "Dial-In Reference" section to `espressoSystemPrompt()` in `shotsummarizer.cpp`, between "Roast Considerations" and "Forbidden Simplifications". Compact representation (~800-1000 tokens) would be cacheable with Anthropic's 90% discount.

**Key sections to add**:
- Roast level → temperature + expected flavor profile (5 tiers)
- Flow rate → flavor affinity by roast level
- Preinfusion tuning (drip weight, move-on time, tradeoffs)
- Pressure sweet spot with taste descriptions
- Flavor correction quick-reference (sour→do X, bitter→do Y)
- TDS/EY interpretation guidelines

**Status**: **Done** (April 2026, PR #635). Loaded from Qt resource `:/ai/espresso_dial_in_reference.md` into `shotAnalysisSystemPrompt()`. Shared between in-app AI and MCP.

### 9. Conversation Context Layers

Summary of the layered context approach:

| Layer | Content | Size | Changes | Caching | Status |
|-------|---------|------|---------|---------|--------|
| Static knowledge | System prompt + curated profile knowledge base (19 profiles) | ~2-2.5K tokens | Per app release | System prompt caching (Anthropic/OpenAI/Gemini) | **Done** |
| Recipe interpretation | Rules for deriving expected behavior from profile recipe (temp stepping, flow/pressure, limiters) | ~0.25K tokens | Per app release | System prompt caching | **Done** |
| Dial-in reference | Roast/grind/flow/pressure/ratio → taste tables, flavor correction guide | ~2.1K tokens | Per app release | System prompt caching | **Done** |
| Grinder context | Observed settings range, min/max, smallest step from shot history | ~0.1K tokens | Every request | Not cacheable | **Done** |
| Profile catalog | Compact one-liner per KB profile (19 profiles) for cross-profile awareness | ~0.5K tokens | Per app release | System prompt caching | **Done** |
| Bean enrichment | Origin, processing, variety, tasting notes from Bean Base/visualizer | ~0.5-1K tokens | Per bean preset | Included in user prompt | Not implemented |
| Dial-in history | Last 5 shots with same profile family (recipe, grind, temp, score, notes) | ~1-2.5K tokens | Every request | Not cacheable | **Done** |
| User history | Profile usage stats across all profiles, best/worst shots | ~1-2K tokens | Per session | Could be second cached block | Not implemented |
| Current shot | Shot data, phase breakdown, tasting notes | ~1-2K tokens | Every request | Not cacheable | **Done** |

Total context today: ~8-10K tokens. With all layers: ~14-18K tokens, with ~50-70% cacheable.

---

## Implementation Priority

### Phase 0: Fix interpretation quality (highest ROI, near-zero cost)
1. ~~**Recipe-aware system prompt**~~ — **Done** (April 2026, PR #635). Added "Reading the Recipe for Expected Behavior" section to `espressoSystemPrompt()` with rules for temperature stepping, flow-controlled pressure decline, pressure→flow transitions, and exit conditions. ~250 cacheable tokens.
2. ~~**Espresso prompt: trust profile notes**~~ — **Done** (April 2026, PR #635). Espresso system prompt now explicitly states profile intent takes priority over Profile Knowledge section when they conflict.
3. ~~**Dial-in reference tables in system prompt**~~ — **Done** (April 2026, PR #635). Full `ESPRESSO_DIAL_IN_REFERENCE.md` content loaded from Qt resource (`:/ai/espresso_dial_in_reference.md`) and appended in `shotAnalysisSystemPrompt()`. Shared between in-app AI and MCP — the MCP's separate `referenceGuide` field was removed to avoid duplication. ~2,100 cacheable tokens.
4. ~~**Smarter summarizer observations**~~ — **Done** (April 2026, PR #635). `temperatureUnstable` flag is now recipe-aware: suppressed when the temperature goal curve shows intentional stepping (range > 5°C). Eliminates false positives on D-Flow, 80s Espresso, and other temperature-stepping profiles.
5. ~~**Profile notes audit**~~ — **Done** (March 2026). D-Flow/Q and La Pavoni were the only empty ones; now fixed. All other profiles confirmed populated.
6. ~~**Grinder context in user prompt**~~ — **Done** (April 2026, PR #635). Grinder settings range query extracted to shared `ShotHistoryStorage::queryGrinderContext()`. In-app AI includes observed settings, range, and smallest step in the user context. MCP uses the same shared helper.
7. ~~**Bean age calculation**~~ — **Skipped.** The raw roast date is already in the user prompt. Pre-computing "days since roast" adds noise that can mislead the AI for users who freeze beans. The AI can do the math itself if relevant, and the "Forbidden Simplifications" section already prevents it from assuming old = stale.
8. ~~**Refocus knowledge base**~~ — **Partial** (April 2026, PR #646). Systematic quality pass: all 19 entries verified against profile JSON frame data, incorrect temperatures/pressures/flows corrected, missing temperatures added to 6 entries, DO NOT flags added to 4 entries, thin entries expanded (Default, Flow Profile), Extractamundo Dos bloom phase documented, Londinium naming disambiguated. New Filter3 entry added with data from JSON frames, official Decent guide, Scott Rao blog, and Diaspora community. Remaining: shift entries away from derivable curve behavior toward non-derivable wisdom, seek A-Flow author guidance.
9. **Test conversations** — Collect 5-10 exported AI conversations covering different profiles and failure modes. Use to validate prompt changes before shipping.

### Phase 1: Quick wins (no external dependencies)
1. ~~**System prompt bean guidance**~~ (idea #4 fallback B) — **Done.** `sharedBeanKnowledge()` in `shotsummarizer.cpp` includes proactive guidance about origin/processing characteristics, variety flavor profiles (Geisha, SL28, Caturra, Bourbon), roaster style recognition, and taste-to-bean mapping. Shared between espresso and filter prompts.
2. ~~**Profile catalog**~~ (idea #1) — **Done** (April 2026). `buildProfileCatalog()` generates a compact one-liner per KB profile (name, category, roast suitability) from `profile_knowledge.md` during KB loading. Included in `shotAnalysisSystemPrompt()` as "Available Profiles with Curated Knowledge" section (~500 cacheable tokens). Covers the 19 profiles with KB entries. Enables cross-profile recommendations (e.g., "your light roast may work better on Blooming Espresso").
3. ~~**Grinder knowledge base**~~ — **Done** (April 2026). Per-shot burr geometry (e.g. "83mm flat") was already in `buildUserPrompt()` via `GrinderAliases::burrGeometry()`. Added burr-swappable flag to the Grinder Context section so the AI knows when aftermarket burrs are an option. Full 150-grinder database is NOT loaded into the system prompt — only the user's own grinder specs from `GrinderAliases` are included, keeping tokens minimal.
4. ~~**Structured grinder + burr fields**~~ (idea #8) — **Done** in PR #368.
5. ~~**Espresso dial-in reference tables**~~ (idea #10) — **Done** (April 2026, PR #635). Moved to Phase 0 item 3.

### Phase 2: Bean enrichment
3. **Visualizer coffee_bags lookup** (idea #4 tier 1) — Query user's own visualizer data for bean details. No new API keys needed, uses existing auth. Avoids double-entry for visualizer users.
4. **Loffee Labs Bean Base integration** (idea #4 tier 2) — Contact Loffee Labs for API access. Community-wide search for beans not in user's visualizer data.
5. **New bean preset fields + AI integration** — Add origin, processing, variety, altitude, roaster tasting notes to bean presets. Extend `ShotSummarizer::buildUserPrompt()` to include enriched data.

### Phase 3: Personalization (app-side work)
6. ~~**Dial-in history per profile family** (idea #3 partial)~~ — **Done.** Up to 5 recent shots with the same `knowledge_base_id` are included in the user prompt with full recipe, grind, temp, dose, score, and tasting notes. Queried via `ShotHistoryStorage::getRecentShotsByKbId()`.
7. ~~**Curated profile knowledge base** (idea #2)~~ — **Done.** 19 profiles documented, integrated into system prompt via `shotAnalysisSystemPrompt()`.
8. **User history summary across profiles** (idea #3 remaining) — Aggregate shot history into per-session summary showing which profiles the user has tried, average ratings, best/worst combos. Would enable cross-profile recommendations.
9. ~~**Cross-profile recommendation guidance**~~ (idea #6) — **Done** (April 2026). Added "When to Suggest a Different Profile" section to espresso system prompt. Triggers: roast/profile mismatch, persistent issues across multiple shots, or user request. Guards against premature switching (2-3 shots minimum). Depends on profile catalog (Phase 1 item 2, also done).

### Phase 4: Optimization
9. **Gemini context caching** (idea #5) — Implement when context size grows from above additions.
10. **Web search fallback** (idea #4 fallback C) — For beans not in Bean Base.
11. **Community bean data via visualizer** (idea #4 fallback D) — Longer-term, needs critical mass.

### External Dependencies
- **Loffee Labs API access**: Keith responded (March 2026) — API undergoing modernization, new keys paused. Beta access in ~1 month, full production ~2-3 months. Free usage offered. No manual key approval on new system. Required for Phase 2, step 4
- ~~**Profile knowledge content**~~ — **Done.** 19 profiles documented from official Decent sources (quality pass April 2026, PR #646)

## Completed Data Collection
- **Profile Knowledge Base**: [`docs/PROFILE_KNOWLEDGE_BASE.md`](PROFILE_KNOWLEDGE_BASE.md) — 19 profiles with source-attributed guidance on roast suitability, temperature, ratio, grind, expected curve behavior, and dial-in tips. Enriched from three Decent video tutorials (light/medium/dark roast profiles).
- **AI Profile Knowledge Resource**: [`resources/ai/profile_knowledge.md`](../resources/ai/profile_knowledge.md) — AI-optimized extract loaded as Qt resource. Injected into system prompt per-profile via `ShotSummarizer::shotAnalysisSystemPrompt()`. 19 entries, all verified against profile JSON frames (April 2026, PR #646).
- **Grinder Database**: [`docs/GRINDER_DATABASE.md`](GRINDER_DATABASE.md) — ~150 grinders across premium, mid-range, budget, commercial, and hand grinder categories with burr specs, plus aftermarket burr sets and grind-setting guidance
- **Espresso Dial-In Reference Tables**: [`docs/ESPRESSO_DIAL_IN_REFERENCE.md`](ESPRESSO_DIAL_IN_REFERENCE.md) — Structured multi-variable reference from Åbn Coffee mapping roast level, temperature, grind size, infusion, peak pressure, flow rate, and ratio to their effects on taste, texture, and extraction. Includes flavor targeting tables ("how to get more acidity/sweetness/body/clarity"), flavor correction tables ("how to fix sourness/bitterness/thin taste"), TDS vs EY% taste relationships, Gagné's ratio-flow formula, flow rate recommendations by roast level, and preinfusion tuning guidance. Source: Åbn Coffee "Espresso tabel oversigt" (work in progress community reference).
## Related Internal Documentation

These docs in this repo contain information relevant to the AI advisor. Reference them when working on AI features:

### Machine & Profile Mechanics (inform what the AI should understand)
- [`docs/AUTO_FLOW_CALIBRATION.md`](AUTO_FLOW_CALIBRATION.md) — Flow sensor calibration algorithm using scale data as ground truth. Explains why flow readings can be ~20% off (D-Flow/Q conversation revealed this), the auto-correction mechanism, density correction (water at 93°C = 0.963 g/ml), and safety bounds [0.5, 1.8]. Relevant to AI advice about flow discrepancies between target and actual.
- [`docs/SIMPLE_PROFILE_EDITOR.md`](SIMPLE_PROFILE_EDITOR.md) — Three-step simple profile model (Preinfuse/Rise&Hold/Decline) vs frame-based advanced profiles. Explains `settings_2a` (pressure) and `settings_2b` (flow) profile types, time-based vs weight-based termination, and how simple profiles are converted to frames. Relevant for understanding profile structure differences.
- [`docs/profile-porting-guide.md`](profile-porting-guide.md) — Tcl-to-JSON profile conversion reference. Frame structure, exit conditions (`exit_if`, `exit_type`, `exit_pressure_over/under`, `exit_flow_over/under`), pump modes, sensor types. Essential reference for understanding how profile recipes map to machine behavior.
- [`docs/CLAUDE_MD/BLE_PROTOCOL.md`](CLAUDE_MD/BLE_PROTOCOL.md) — Shot sample rate (~5Hz), profile upload format, BLE command queue, retry mechanism. Explains data resolution limits (200ms between samples) that affect phase transition detection and curve smoothness.
- **Video transcripts**: [`docs/light_roast_profiles_transcript.txt`](light_roast_profiles_transcript.txt), [`docs/medium_roast_profiles_transcript.txt`](medium_roast_profiles_transcript.txt), [`docs/dark_roast_profiles_transcript.txt`](dark_roast_profiles_transcript.txt) — Full transcripts of three Decent video tutorials covering profile selection and dial-in by roast level. Primary source for profile-specific knowledge in PROFILE_KNOWLEDGE_BASE.md. Consult these when adding or updating profile knowledge entries.

### Data & Integration (inform what data the AI receives)
- [`docs/CLAUDE_MD/VISUALIZER.md`](CLAUDE_MD/VISUALIZER.md) — DYE metadata fields (`bean_brand`, `bean_type`, `roast_level`, `grinder_model`, `grinder_setting`, `drink_tds`, `drink_ey`, `espresso_enjoyment`), shot upload JSON structure, and Visualizer API integration. Defines the metadata fields available for AI analysis and the format of shot data uploads.

### Not AI-relevant (do not reference for AI work)
The following docs are about app infrastructure and are not relevant to AI advice quality: `ACCESSIBILITY.md`, `ANDROID_MEMORY_GC_PRESSURE.md`, `CPP_COMPLIANCE_AUDIT.md`, `DE1_BLE_PROTOCOL.md` (low-level UUIDs — use `CLAUDE_MD/BLE_PROTOCOL.md` instead), `headless-mode-investigation.md`, `HOME_AUTOMATION.md`, `IOS_CI_*.md`, `ISSUE_TRIAGE.md`, `layout.md`, `layout-server.md`, `MMR_WRITES_COMPARISON.md`, `simulation.md`, `text-widget.md`, `UI_COMPLIANCE_*.md`, `CLAUDE_MD/DATA_MIGRATION.md`, `CLAUDE_MD/PLATFORM_BUILD.md`, `accessibility/phase*.md`, `plans/`.

## External Sources (for future knowledge base updates)

These sources contain profile-specific advice that could inform future knowledge base or system prompt updates:

- **Damian Brakel's D-Flow site**: https://coffee.brakel.com.au/d-flow/ — D-Flow editor documentation, pour phase mechanics (flow goal + pressure limit interaction), grind-dependent behavior explanation
- **Scott Rao on DE1 pressure profiling**: https://www.scottrao.com/blog/2018/6/3/introduction-to-the-decent-espresso-machine — Pressure curve volatility = channeling (clumped grounds + poor puck prep). Better burrs = smoother curves. Prefers shots where flow stabilizes immediately after preinfusion.
- **Decent blog "5 profiles for medium"**: https://decentespresso.com/blog/5_profiles_for_medium_roasted_beans — Comparison of Default, Flow, Londinium, Adaptive, D-Flow for medium roasts. D-Flow adapts by grind (8.5 bar boiler-like for fine, Londinium-like for optimal, G&S-like for coarse).
- **Decent "4 mothers" theory**: https://decentespresso.com/docs/the_4_mothers_a_unified_theory_of_espresso_making_recipes — Unified framework: lever, blooming, allongé, flat-9-bar. Roast level is the most important factor in recipe choice.
- **Coffee ad Astra (Gagné) Adaptive profile**: https://coffeeadastra.com/2020/12/31/an-espresso-profile-that-adapts-to-your-grind-size/ — Adaptive v2 design rationale and expected behavior.
- **home-barista.com DE1 threads**: Temperature profiling discussion (t59650), DE1 extraction recipes (t57755), pressure/temperature findings (t62508) — community experiences with DE1 profiling. Note: forum requires JavaScript rendering, not directly scrapable.
