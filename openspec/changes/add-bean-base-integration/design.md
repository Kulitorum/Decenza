## Context

The Decenza Beans page (`qml/pages/BeanInfoPage.qml`) captures only free-text bean fields. Visualizer uploads send those strings verbatim. The AI advisor sees them as unattributed text. Bean Base (Loffee Labs) is a community-maintained coffee database — Visualizer already uses it on their bag-edit form via three Stimulus controllers (`canonical_selector_controller`, `coffee_bag_form_controller`, `coffee_bag_scraper_controller`).

After investigation we have:

1. **Bean Base API** — Read-only. Authenticated `GET /beans` (paginated search, full-text + 14 string filters + range filters); public unauthenticated `GET /roasters | /origins | /varieties | /processes`. Bean ids are integers per the user guide. Each bean carries `id, roaster, roast-name, link, image, type, degree, origin, region, producer, min-elev, max-elev, variety, process, description, tasting, harvest, price-low/high, gram-low/high, tasting-tag, general-tag, roaster-region, roaster-country, soldout, available`. Free tier: 1 req / 3 s, 2,000 beans/day, 50 per call.

2. **Visualizer's coffee_bag schema** — `id` is Visualizer's local UUID; `canonical_coffee_bag_id` (the field name observed in `coffee_bag_form_controller.field("canonical_coffee_bag_id")`) stores the Bean Base bean id; `canonical_roaster_id` stores the Bean Base roaster id. When `canonical_coffee_bag_id` is set, Visualizer's UI disables the free-text Roaster + Name fields and renders a "verified" indicator. The bag also has `url, country, region, farmer (producer), variety, process, tasting` fields that Visualizer auto-populates from the Bean Base autocomplete payload.

3. **The advisor doc's prior assumption** that `canonical_coffee_bag_id` is a UUID was wrong — it's an opaque foreign reference, format determined by Bean Base (integer). No translation layer is needed; the integer passes through Visualizer's column verbatim.

4. **Per-user keys** — Loffee Labs confirmed each Decenza user should sign up for their own free key. Bundled-key model is not on offer.

5. **API base URL + auth (confirmed empirically, June 2026)** — Base is `https://loffeelabs.com/api/v2`. Endpoints: `GET /beans` (auth required → 401 `{"error":"Missing API Key. Provide it in the Authorization header or as ?api_key=..."}` without a key); `GET /roasters`, `GET /origins`, `GET /varieties`, `GET /processes` are public and return `{"data":[...]}`. `/roasters` rows carry `roaster, country, link, locality, region, last`. Auth accepted either as `Authorization: Bearer <key>` or `?api_key=<key>` query param. The `/api` (no version) and `/api/v1` paths return `{"message":"Route not found"}` — v2 is current.

6. **Live authenticated payload shape (confirmed with a real Free-tier key, June 2026)** — `GET /beans` returns `{"meta":{…},"beans":[…]}` — note the wrapper key is `beans`, NOT `data` (only the public endpoints use `data`). `meta` carries `total, page, limit, totalPages, tier ("Free Access"), beansExportedToday, remainingQuota, historicalIncluded` — `remainingQuota` could power a quota display later. **`id` is a JSON number** (e.g. `31754`) — open question 0.2 RESOLVED; we store it as an opaque string. Fields present in the default Free-tier set: `id, date, roaster, roast-name, link, type, degree, origin, region, producer, min-elev, max-elev, variety, process, description, tasting, harvest, price-low/-high, gram-low/-high, roaster-region, roaster-country, roast-type, roast-degree`, plus derived `price-per-cup-(low/high)`. Nullable: `variety, description, harvest, price-high`. **NOT present at Free tier (silently dropped even when requested via `fields=`): `image`, `tasting-tag`, `general-tag`, `soldout`, `available`.** Consequences: bag photos cannot be fed from the API today (the UI collapses gracefully; ask Loffee Labs whether `image` is tier-gated), tag chips won't render (the `tasting` comma-string carries the flavor list and is displayed as notes), and the sold-out advisory stays dormant. `fields=` works as a response filter but cannot summon fields the tier doesn't expose.

7. **Deeper live-API probing (June 2026, Free-tier key, ~30 beans of quota):**
   - `?id=<n>` works as an exact single-bean lookup (`total: 1`) — our rehydration path is confirmed. REST-style `GET /beans/<id>` is 404 ("Route not found").
   - `format=csv` returns the **identical column set** as JSON — no hidden image/tag columns anywhere in the export.
   - `historical=true` on Free tier is **silently ignored** (`historicalIncluded` stays `false`) — no error, graceful degrade.
   - `roaster=` is an exact-match filter; `exclude_<filter>` and `<range>_min/_max` work as documented; `fields=` trims the response payload.
   - `sort=`/`order=` params are accepted without error but results stay date-descending (default ordering; no evidence sorting is implemented).
   - **`search=` matches WHOLE WORDS only — no prefix matching**: `search=prodi` → 0, `search=prodigal` → 10, `search=prodigal washed` → 0 (so also not term-AND). Confirmed by the user in-app: results appear only once a complete word is typed. No client-side workaround exists for server matching; the no-matches empty state teaches the behavior. Worth asking Loffee Labs for last-token prefix matching (standard autocomplete semantics) — until then autocomplete is effectively per-completed-word. Multi-word fallback idea (task 5B.1) still applies for the roaster+bean-fragment case.
   - `tasting-profile=Chocolate` filtering WORKS at Free tier even though the `tasting-tag` field is never returned — the filter evaluates server-side. The `tasting` string can be very long (30+ comma-joined descriptors observed).
   - **Quota counts beans returned, not requests**: `beansExportedToday` increments by the result count of each non-cached response. At our 25-result search limit that's ~80 fresh searches/day on the 2,000-bean Free quota — fine for dial-in use; the session cache prevents repeat spend.

## Goals / Non-Goals

**Goals:**

- Single, opt-in surface (Settings → Visualizer → Bean Base section) for credentialing.
- One search affordance on BeanInfoPage that resolves typed beans to canonical entries.
- Stable bag identity on Visualizer uploads via `canonical_coffee_bag_id` + `canonical_roaster_id`.
- AI advisor sees structured bean attributes when available; falls back gracefully to free-text otherwise.
- All four tiers ship independently — Tier 1 alone is shippable as "credentials exist"; Tier 2 needs Tier 1; Tiers 3 & 4 each need Tier 2.

**Non-Goals:**

- **Bundled key.** Not on offer from Loffee Labs.
- **URL scraper.** Visualizer has one (`coffee_bag_scraper_controller` via ActionCable). Beneficial side effect: when a Bean Base entry has a `link` field, our upload populates `coffee_bag[url]` and Visualizer's UI already lets the user re-scrape from that URL if needed. We don't build a scraper ourselves.
- **Roaster/origin/variety/process autocompletes** as a separate feature. The Bean Base search bar makes these mostly redundant — once a bean is matched, those fields are populated. If we later want per-field autocompletes (for unmatched beans), they're a follow-on change using the same public endpoints.
- **Tab rename** of Settings → "Cloud". Deferred until a third cloud service is on the roadmap.
- **Writing to Bean Base.** API is read-only; we cannot contribute beans the user types that aren't in the DB.
- **A Decenza-hosted proxy.** Each user holds their own key; no backend service to build.
- **Migration of the existing `add-shot-metadata-capture` DYE schema fields** — that change owns the field additions; we depend on it but don't duplicate.

## Decisions

### Decision: New `SettingsBeanBase` domain, not properties on `SettingsVisualizer`

**Rationale:** CLAUDE.md is firm — "Settings go in their domain sub-object, not on `Settings` directly." Bean Base is a distinct external service with separate auth, separate quota, and obvious room to grow (cache TTLs, last-test-at, future preferences for tier display). Even starting with one property, a new domain keeps the rebuild-blast story clean — touching the API key field shouldn't recompile every consumer of `SettingsVisualizer`.

**Alternative considered:** `beanBaseApiKey` on `SettingsVisualizer`. Rejected — couples two unrelated services and violates the domain convention.

### Decision: Search bar only renders when `beanBaseApiKey` is non-empty

**Rationale:** Users without a key see a clean BeanInfoPage identical to today. No empty input, no nudge banner, no popup interrupting bean entry. Discovery happens entirely in Settings → Visualizer → Bean Base section, where the pitch copy and "Get free key" CTA can do real onboarding work.

**Consequence:** the Settings section's copy is now the *only* discovery surface for Bean Base. Worth investing in. (Alternative for the future: a one-time tooltip on the Beans page header — but not in this change.)

**Alternative considered:** Always render the bar; show "Add API key in Settings →" when empty. Rejected — visual clutter for users who'll never use it, and easier for screenshots/marketing to look "noisy" than the cleaner gated approach.

### Decision: Bean preset linkage persists even if API key is later removed

**Rationale:** A linked preset's `beanBaseId` still has value for Visualizer uploads (we still send `canonical_coffee_bag_id`) and for the cached attribute block on advisor prompts. Removing the key only disables *new* matching and *replacement* of existing links. The linked-state indicator above the Bean section stays visible without the search bar when this state is reached.

### Decision: Field-mapping on selection

When a Bean Base entry is applied, fields are partitioned into three buckets:

| Bean Base field | DYE field | Behavior |
|------------------|-----------|----------|
| `roaster` | `dyeBeanBrand` | Filled, **disabled (verified)** |
| `roast-name` | `dyeBeanType` | Filled, **disabled (verified)** |
| `degree` | `dyeRoastLevel` | Filled, **disabled (verified)** |
| `link` | preset `productUrl` (cached) + `coffee_bag[url]` on upload | Cached, not user-visible on BeanInfoPage |
| `image` | cached in `beanBaseData` | Displayed on BeanInfoPage when a linked bean is active (bag photo beside the Bean section) and as a thumbnail on linked preset rows. QML `Image` loads the URL directly; if it fails to load, the layout collapses gracefully (no broken-image placeholder). |
| `origin`, `region`, `variety`, `process`, `producer`, `min-elev`, `max-elev`, `tasting`, `tasting-tag`, `general-tag`, `type` | preset attribute cache | Cached; sent to Visualizer + injected into advisor prompt |
| `harvest` | (none) | Cached; available for future use |
| (Bean Base) `date` | `dyeRoastDate` | **NOT touched.** Bean Base `date` is the release date for a bean; the user's bag may have been roasted on a different day. Roast date remains user-entered. |
| `soldout`, `available` | (none) | Cached; could power a "marked sold out" advisory but not in this change. |

**Rationale (updated per user decision, June 2026):** Every field Bean Base supplies a non-empty value for is read-only while linked — canonical data wins where we have it, and divergent user edits can't pollute the canonical shot record. To change a pulled value, unlink first. The lock follows the data, not the link: if the matched entry lacks a value (e.g. no `degree`), that field stays editable so the user can fill the gap themselves (lock condition: `linked && pulledValueNonEmpty`). Decenza-specific fields (dose, grinder, barista, roast date) remain fully editable — they describe the user's bag and shot, not the bean's identity.

**Free-text is the primary path, not a fallback.** Many beans aren't in Bean Base. The unlinked experience must remain exactly as frictionless as today: no nag styling, no "unverified" warnings, the search bar stays out of the way when it has nothing to offer. The cached blob is optional enrichment; nothing requires it.

### Decision: Debounce 800 ms + 3 s sent-request floor; cache aggressively

**Rationale:** Free-tier rate limit (1 req / 3 s) is the only thing a user might feel during typing. An 800 ms debounce after the last keystroke covers normal typing without sending mid-word. A 3 s minimum gap between *sent* requests respects the API. Cached query strings short-circuit instantly within a session. Once a bean is picked, its full payload is cached on the preset forever — never re-fetched unless the user explicitly refreshes.

**Worst case:** user types very fast on a brand-new query, feels a perceptible 2–3 s lag before results appear. Acceptable for free tier. Upgrade-tier or self-bundled-key futures eliminate it.

**Quota math:** 50 beans/call × 25-result limit + ~5 calls per "search and pick" cycle ≈ 5 beans/day for a moderately active user. The 2,000/day cap is comfortably unreachable in normal use.

### Decision: "Verified" UX matches Visualizer's pattern but is rendered Decenza-style

**Rationale:** Visualizer disables fields and shows a small "verified" SVG icon when `canonical_coffee_bag_id` is set. We follow the same pattern (disabled fields + indicator) but render it in Decenza's `Theme` system — no shared assets. The pattern is what matters, not pixel parity.

### Decision: "Search Loffee Labs Bean Base" label verbatim

**Rationale:** Matches Visualizer exactly. Reinforces Loffee Labs' brand attribution (relevant given their per-user-key model and free service). Avoids future confusion if a user moves between Decenza and Visualizer.

**Alternative considered:** "Search Bean Base" (shorter). Rejected — too generic; attribution to Loffee Labs is worth the extra two words.

### Decision: Tier 4 advisor enrichment in same change

**Rationale:** Once `beanBaseId` + cached attributes exist on presets (Tier 2), wiring them into the advisor prompt is trivial (~50 lines in `mcptools_dialing.cpp` and `mcptools_ai.cpp`). Splitting it into a follow-up change creates artificial delay for a high-value low-effort win. The `bean_base_search` MCP tool is slightly more work but reuses the Tier 2 client and rate-limit harness.

**Alternative considered:** Defer Tier 4 to its own change. Rejected — code paths are too intertwined to split cleanly.

### Decision: First-sync pull of existing `coffee_bags` from Visualizer

**Rationale:** Without this, the first upload after a Bean Base key is configured creates a duplicate coffee_bag on Visualizer even when the user already has a bag for that bean (matched manually in Visualizer's UI). Pulling `/api/coffee_bags` once and matching by `canonical_coffee_bag_id` (best) or by free-text roaster+name (fallback) prevents duplicates.

**Alternative considered:** Always upsert by free-text. Rejected — Visualizer's matching is fuzzy and creates near-duplicates with slightly different roaster/name strings.

### Decision: Linked-state indicator persists when key removed; search bar does not

**Rationale:** Two different concerns. The link is data (carries upload + advisor value); the search bar is a tool (needs credentials). Keeping the indicator without the bar makes the state machine clearer: "your bean is linked; you currently can't change the link."

## Open Questions

- **Tier 3 — exact `coffee_bag` upsert endpoint.** We've observed the form fields (`coffee_bag[canonical_coffee_bag_id]`, etc.) but not the Visualizer API endpoint and method for first-time bag creation. Worth confirming with a Visualizer API request inspection (or a 5-minute conversation with visualizer.coffee maintainers) before implementing Tier 3 — adds at most a half-day if it turns out to be a multi-step flow.
- **`canonical_roaster_id` source.** Visualizer's `canonical_selector_controller` carries a `roasterCanonicalMapValue` preloaded into the page — meaning Visualizer maintains the mapping internally between *its* roaster ids and Bean Base roaster ids. When Decenza creates a bag, do we send Bean Base's roaster id (and trust Visualizer to map), or send Visualizer's roaster id (and require Visualizer to expose it via their `/roasters` endpoint)? Best guess: send Bean Base's id directly into `canonical_roaster_id` since the field name carries that semantic. Confirm during Tier 3.
- **Bean Base id format ambiguity** — user guide says "numerical ID" but the API spec doesn't show example values. Implementation should treat the id as an opaque string (preserving leading zeros, etc.) and parse only when comparing. One sample API call resolves this.
