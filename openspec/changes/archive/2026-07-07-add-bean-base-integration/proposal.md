## Why

Decenza captures bean information today only as free-text fields the user types (roaster, bean name, roast level, roast date). The AI advisor sees this as opaque strings, Visualizer has to fuzzy-match strings to cluster shots across users, and rich attributes — origin, processing, variety, elevation, roaster tasting notes, bag image, product URL — are unavailable. The advisor documentation has carried a "Tier-2: Bean Base" placeholder since March 2026 specifically blocked on getting API access.

Loffee Labs Bean Base is the community coffee database that visualizer.coffee already uses for its "Search Loffee Labs Bean Base" autocomplete on the bag form. Each user can self-serve a free API key (1 req/3 s, 2,000 beans/day) from loffeelabs.com. With that key, Decenza can resolve every typed bean to a canonical Bean Base entry — unlocking:

- **Rich bean attributes** for the advisor prompt without expanding user data entry.
- **Stable cross-user identity** on Visualizer uploads via `canonical_coffee_bag_id` and `canonical_roaster_id`, so shots on the same bean cluster across the global community.
- **Pre-populated fields** (origin, variety, processing, tasting tags, roaster URL, bag image) that today have no entry path at all.
- **A foundation for per-bean profile recommendations** ("users brewing this exact bean rate D-Flow/Q highest") — the dial-in coaching north-star.

The schema for the bean identity, the field-population behavior, the disabled-when-verified pattern, and the upload-side field names are all observable from Visualizer's `canonical_selector_controller`, `coffee_bag_form_controller`, and `coffee_bag` form schema. We're following an integration shape Visualizer has already proven on the web side.

## What Changes

### Tier 1 — Settings: Bean Base credentials

- New `SettingsBeanBase` domain class owning `beanBaseApiKey` (and forward-room for cache TTLs, last-tested-at).
- New **Bean Base** section in the left card of `SettingsVisualizerTab.qml`, below the existing Visualizer.coffee account block (separated by a thin divider).
  - API key field (password-masked, show/hide toggle, paste-friendly).
  - **Test Key** button: hits `GET /beans?limit=1` and shows ✓ / 401 / network-error.
  - "Get a free API key from loffeelabs.com →" link (opens external browser).
  - Short pitch copy explaining what Bean Base does (since this is now the only discovery surface).
- Tab rename to "Cloud" is **out of scope** for this change — defer until a third cloud service exists or until we have evidence the current "Visualizer" tab label is confusing users.

### Tier 2 — BeanInfoPage: "Search Loffee Labs Bean Base"

- New search bar at the top of the **Bean** section in the right-hand `fieldsGrid` of `BeanInfoPage.qml`, above the existing Roaster + Coffee fields.
- Label: verbatim **"Search Loffee Labs Bean Base"**.
- **Visibility:** rendered only when `Settings.beanbase.beanBaseApiKey` is non-empty. No empty-state nudge on the Beans page — discovery happens entirely in Settings.
- Three states governed by `(hasKey, hasLink)`:

  | hasKey | hasLink | What renders |
  |--------|---------|--------------|
  | no | no | Nothing — page looks like today. |
  | no | yes | "✓ Linked to Bean Base" pill above Roaster, no actions. Preserves the linkage value for upload + advisor even after key removal. |
  | yes | no | Search input + dropdown autocomplete. |
  | yes | yes | Search input + inline ✓ Linked indicator + Unlink/Replace + `🔗` open-link affordance. |

- Typing triggers debounced `GET /beans?search=…&limit=25` (800 ms debounce, 3 s sent-request floor per free-tier rate limit). Session-cache by query string.
- Picking a result applies Bean Base fields to the DYE schema (see field-mapping table in `design.md`), caches the full bag payload on the in-progress preset, and stores `beanBaseId` + `beanBaseRoasterId`.
- After applying, Roaster + Coffee fields are disabled with a "verified" visual treatment matching Visualizer's pattern; Roast level remains editable (user's specific bag may differ); Roast date is untouched (Bean Base `date` is release date, not roast-batch date).
- The `↑` distinct-values suggestion arrow is hidden on disabled (verified) fields.

### Tier 3 — Visualizer upload: canonical id linkage

- Extend `VisualizerUploader::buildCoffeeBagJson()` (or equivalent bag-create/upsert path) to set:
  - `coffee_bag[canonical_coffee_bag_id]` — Bean Base bean `id`
  - `coffee_bag[canonical_roaster_id]` — Bean Base roaster `id`
  - `coffee_bag[url]` — Bean Base `link` field (roaster product URL)
  - `coffee_bag[country]`, `region`, `variety`, `process`, `producer`, `tasting` (notes) — populated from cached Bean Base attributes when present.
- Shot upload references the bag's local Visualizer UUID (existing pattern) — the linkage lives on the bag, not the shot.
- On first sync after a new key is configured, pull `GET /api/coffee_bags` from Visualizer and reuse existing bag UUIDs (matched by Bean Base id or by free-text roaster+name) instead of creating duplicates.

### Tier 4 — AI advisor: bean attribute enrichment

- When the active preset has a `beanBaseId`, inject the cached Bean Base attribute block into the dialing/advisor prompt instead of just the free-text roaster + bean name strings.
- New MCP tool `bean_base_search` (read-only) so the LLM can resolve beans the user mentions in chat that weren't pre-resolved on BeanInfoPage. Bounded by the same free-tier rate budget.

### DYE schema additions (overlap with `add-shot-metadata-capture`)

The bean preset and per-shot bean record gain optional fields populated from Bean Base:

- `beanBaseId` (integer per Loffee Labs user guide — "The exact numerical ID of the coffee")
- `beanBaseRoasterId` (integer)
- `origin`, `region`, `producer`, `variety`, `process`, `minElevationM`, `maxElevationM`, `tastingTags` (array), `tastingNotes` (string), `productUrl`, `imageUrl`, `roasterWebsite`, `beanType` (Filter/Espresso/Omni), `generalTags` (array — COE, Single Origin, etc.)
- The existing `add-shot-metadata-capture` change (0/35 tasks) should absorb the DYE schema portion of this work to avoid race conditions; the Bean Base integration depends on those fields existing.

## Capabilities

### New Capabilities

- **`settings-cloud-credentials`** — `SettingsBeanBase` domain + the Bean Base section of the Visualizer settings tab, including API key validation via `GET /beans?limit=1`.
- **`bean-base-search`** — The BeanInfoPage search bar, autocomplete behavior, three-state visibility rule, field-mapping on selection, linked-state UI, and the debounce/cache/rate-limit harness shared with the advisor MCP tool.
- **`visualizer-bag-linkage`** — Setting `canonical_coffee_bag_id` and `canonical_roaster_id` on coffee_bag uploads, pulling the user's existing `coffee_bags` to avoid duplicates, and the upload-side url/country/variety/process/tasting field expansion.

### Modified Capabilities

- **`ai-advisor`** — Prompt enrichment when the active preset carries a `beanBaseId`; new `bean_base_search` MCP tool.
- **`settings-ui`** — New Bean Base section in the Visualizer settings tab. (Tab rename deferred.)
- **`bean-info-page`** — Search bar added to the right-hand fields grid; Roaster + Coffee fields gain a "verified/disabled" state; new linked-state indicator above the Bean section.
- **DYE schema** (covered by `add-shot-metadata-capture` — absorb the field additions there).

## Impact

- **`src/core/settings_beanbase.h/.cpp`** — new domain class.
- **`src/core/settings.h/.cpp`** — own and expose `Settings.beanbase` sub-object.
- **`src/core/settingsserializer.cpp`** — serialize the new domain.
- **`src/main.cpp`** — `qmlRegisterUncreatableType<SettingsBeanBase>(...)` so QML can read `Settings.beanbase.beanBaseApiKey`.
- **`src/mcp/mcptools_settings.cpp`** — expose the API key setting via MCP (read + write).
- **`src/network/beanbaseclient.h/.cpp`** — new HTTP client wrapping the 4 Bean Base endpoints, with response caching and the 3 s sent-request rate-limit queue.
- **`src/network/visualizeruploader.cpp`** — extend bag upload schema with the four `coffee_bag[...]` canonical/url/varietal fields; add a "pull existing bags on first sync" path.
- **`src/core/settings_dye.h/.cpp`** — extend preset schema with `beanBaseId` + cached attributes (or via `add-shot-metadata-capture` if that change ships first).
- **`qml/pages/settings/SettingsVisualizerTab.qml`** — new Bean Base section in the left card.
- **`qml/pages/BeanInfoPage.qml`** — new search bar at the top of the Bean section, linked-state indicator, disabled state for Roaster + Coffee when verified.
- **`qml/components/BeanBaseSearchBar.qml`** — new reusable component (search input + dropdown + state management).
- **`src/mcp/mcptools_ai.cpp` / `mcptools_dialing.cpp`** — inject Bean Base attribute block when preset has a `beanBaseId`.
- **`src/mcp/mcptools_*.cpp` (new file or addition)** — `bean_base_search` read-only MCP tool.
- Translation keys: `settings.beanbase.*`, `beaninfo.beanbase.*`.
- No BLE impact. No DB migration required if `beanBaseId` is stored in the existing preset JSON blob; if stored as a column, a non-destructive `ALTER TABLE` is needed.
