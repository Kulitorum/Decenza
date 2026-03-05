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

**System prompt** (~3-4K tokens): Espresso or filter-specific guidance covering DE1 machine behavior, pressure/flow interpretation, common shot patterns, roast considerations, grinder/burr geometry, and response guidelines.

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

**Multi-shot conversations**: Previous shots are summarized and compressed. Older messages get trimmed to manage token count.

### What the AI Does NOT Know Today

1. What other profiles are available on the machine
2. What profiles might work better for the user's beans/roast
3. Community recommendations for bean/profile pairings
4. The user's history with other profiles
5. Details about the roaster or bean beyond what the user typed
6. What other users' experiences are with the same beans

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

**Status**: Not implemented. Ready to build.

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

**Status**: Initial content started in [`docs/PROFILE_KNOWLEDGE_BASE.md`](PROFILE_KNOWLEDGE_BASE.md), seeded from Decent's official blog posts on profiles for light/medium/dark roasts and the "4 mothers" theory. Needs expansion with specific temperatures, ratios, and community feedback.

### 3. User History Summary

**Problem**: The AI doesn't know the user's track record — which profiles they've tried, what worked, what didn't.

**Approach**: At conversation start, query `ShotHistoryStorage` and generate a summary:

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

**Status**: Not implemented. Data is available in `ShotHistoryStorage`. Needs aggregation query + summary generation.

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

**Status**: Not implemented. Depends on ideas #1 or #2 being in place first.

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
- **Burrs**: `getDistinctBurrsForGrinder(brand, model)` — burr sets used with that grinder

**UI Change** (BeanInfoPage): Row 2 currently has `[Roast level] [Grinder] [Setting]`. Change to:

```
Row 2: [Roast level] [Grinder Mfg] [Grinder Model]
Row 3: [Burrs]       [Setting]     [Barista]
```

**Data Changes**:

| Component | Current | Proposed |
|-----------|---------|----------|
| `Settings` | `dyeGrinderModel` (single string) | `dyeGrinderBrand`, `dyeGrinderModel`, `dyeGrinderBurrs` |
| `ShotHistoryStorage` DB | `grinder_model TEXT` | `grinder_brand TEXT`, `grinder_model TEXT`, `grinder_burrs TEXT` |
| `ShotMetadata` struct | `grinderModel` | `grinderBrand`, `grinderModel`, `grinderBurrs` |
| Bean presets | `grinderModel` | `grinderBrand`, `grinderModel`, `grinderBurrs` |
| Visualizer upload | `grinderModel` in notes | `grinderBrand + " " + grinderModel` in notes (backward compat) |
| FTS index | includes `grinder_model` | includes `grinder_brand`, `grinder_model` |
| AI prompt | "Grinder: DF83V" | "Grinder: Turin DF83V with SSP HU 83mm burrs" |

**Migration**: DB migration uses `resources/grinder_aliases.json` — a lookup table of ~90 grinders with aliases that map common user-typed strings to structured brand/model/stock_burrs. The migration:
1. Lowercase-normalizes the existing `grinder_model` string
2. Matches against aliases (e.g. "niche" → Niche/Zero, "df83v" → Turin/DF83V, "comandante" → Comandante/C40 MK4)
3. On match: populates `grinder_brand`, `grinder_model`, and auto-fills `grinder_burrs` with `stock_burrs`
4. On no match: keeps entire string in `grinder_model`, leaves `grinder_brand` and `grinder_burrs` empty
5. Aliases prioritize longer matches first to avoid "niche" matching before "niche duo"

**Burr Auto-Fill**: When the user selects a grinder manufacturer + model (whether from autocomplete or typed), the burr field auto-fills with the stock/most popular burr from `grinder_aliases.json`. For grinders with `burr_swappable: true`, clicking the burr field shows a picker with:
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

**Status**: Proposed. Depends on grinder database (done) and BeanInfoPage layout changes.

### 9. Conversation Context Layers

Summary of the layered context approach:

| Layer | Content | Size | Changes | Caching |
|-------|---------|------|---------|---------|
| Static knowledge | System prompt + profile catalog + curated knowledge base | ~8-10K tokens | Per app release | System prompt caching (Anthropic/OpenAI/Gemini) |
| Bean enrichment | Origin, processing, variety, tasting notes from Bean Base/visualizer | ~0.5-1K tokens | Per bean preset | Included in user prompt |
| User history | Profile usage stats, best/worst shots, preferences | ~1-2K tokens | Per session | Could be second cached block |
| Current shot | Shot data, phase breakdown, tasting notes | ~1-2K tokens | Every request | Not cacheable |

Total context: ~12-16K tokens, with ~70-80% cacheable.

---

## Implementation Priority

### Phase 1: Quick wins (no external dependencies)
1. **System prompt bean guidance** (idea #4 fallback B) — Add two sentences to system prompt telling the AI to share what it knows about recognized roasters/beans. Zero cost, immediate value.
2. **Profile catalog** (idea #1) — Generate compact profile summaries at build time, include in system prompt. Enables basic cross-profile awareness.
3. **Grinder knowledge base** — Curated database of ~150 grinders with burr size, type, material, adjustment sensitivity, and espresso suitability. See [`docs/GRINDER_DATABASE.md`](GRINDER_DATABASE.md). When user enters grinder model, AI can provide grind-setting guidance and explain grind characteristics for their specific burr geometry.
4. **Structured grinder + burr fields** (idea #8) — Split single "Grinder" field into Manufacturer / Model / Burrs with cascading autocomplete from history. Enables AI to look up exact grinder specs and burr flavor profile. See idea #8 for full design.

### Phase 2: Bean enrichment
3. **Visualizer coffee_bags lookup** (idea #4 tier 1) — Query user's own visualizer data for bean details. No new API keys needed, uses existing auth. Avoids double-entry for visualizer users.
4. **Loffee Labs Bean Base integration** (idea #4 tier 2) — Contact Loffee Labs for API access. Community-wide search for beans not in user's visualizer data.
5. **New bean preset fields + AI integration** — Add origin, processing, variety, altitude, roaster tasting notes to bean presets. Extend `ShotSummarizer::buildUserPrompt()` to include enriched data.

### Phase 3: Personalization (app-side work)
6. **User history summary** (idea #3) — Aggregate shot history into per-session summary. Data already exists in `ShotHistoryStorage`.
7. **Curated profile knowledge base** (idea #2) — Needs content authoring, biggest impact on profile recommendation quality.
8. **Cross-profile recommendation guidance** (idea #6) — Depends on #1/#2 being in place.

### Phase 4: Optimization
9. **Gemini context caching** (idea #5) — Implement when context size grows from above additions.
10. **Web search fallback** (idea #4 fallback C) — For beans not in Bean Base.
11. **Community bean data via visualizer** (idea #4 fallback D) — Longer-term, needs critical mass.

### External Dependencies
- **Loffee Labs API access**: Keith responded (March 2026) — API undergoing modernization, new keys paused. Beta access in ~1 month, full production ~2-3 months. Free usage offered. No manual key approval on new system. Required for Phase 2, step 4
- **Profile knowledge content**: Needs someone with Decent community expertise — required for Phase 3

## Completed Data Collection
- **Profile Knowledge Base**: [`docs/PROFILE_KNOWLEDGE_BASE.md`](PROFILE_KNOWLEDGE_BASE.md) — ~16 profiles with source-attributed guidance on roast suitability, temperature, ratio, and grind
- **Grinder Database**: [`docs/GRINDER_DATABASE.md`](GRINDER_DATABASE.md) — ~150 grinders across premium, mid-range, budget, commercial, and hand grinder categories with burr specs, plus aftermarket burr sets and grind-setting guidance
