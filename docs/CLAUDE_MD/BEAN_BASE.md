# Bean Base Integration (Visualizer canonical)

Canonical coffee-database integration: users link beans to Visualizer canonical coffee-bag entries, and the snapshot enriches shot history, Visualizer uploads, and the AI advisor. OpenSpec change: `add-bean-base-integration` (design.md §§ Context 5–9 hold the historical empirical API findings).

> **Note (June 2026):** The Loffee Labs Bean Base API path (`searchBeanBase()`, `testApiKey()`, the per-user `beanBaseApiKey` setting, and the whole `SettingsBeanBase` domain) was **removed** — it was never wired into production. Search is now 100% Visualizer canonical search (keyless). The sections below that describe the loffeelabs API, its rate/quota contract, and `searchBeanBase()` are kept only as historical context for the design doc; none of that code exists anymore.

## Architecture

```
BeanBaseSearchBar (BeanInfoPage + PostShotReviewPage)
   └─ MainController.beanbase
        BeanBaseClient
          └─ search() → visualizer.coffee
               canonical search API (keyless)
   user picks entry → DYE link state (SettingsDye: dyeBeanBaseId/RoasterId/Data)
        ├─ bean presets carry the link (apply/save/rename handled)
        └─ shot save snapshots the blob → shots.beanbase_json (migration 18)
              ├─ ShotRecord/ShotProjection.beanBaseJson (sparse-emit)
              ├─ MCP: shots_get_detail emits parsed `beanBase`; shots_update accepts it
              └─ advisor: currentBean.beanBase via DialingBlocks::buildCurrentBeanBlock
```

**Snapshot, not reference**: every shot stores the full bean JSON at link time. Bean Base delists/mutates entries (`historical=true` is Dev-tier-gated), so history must never depend on their retention. The snapshot never preserves the photo — legacy pre-removal blobs carry a CDN `image` URL, canonical blobs nothing; locally displayed pixels live in an evictable file cache (see 'Bag images' below).

## Historical: the Loffee Labs Bean Base API (removed June 2026)

The notes below describe the loffeelabs API that the integration originally shipped against. **None of this code exists anymore** (`searchBeanBase()`, `testApiKey()`, `parseBeans()`, `classify429()`, the API key setting). Kept only so the design doc's empirical findings stay readable.

- Base `https://loffeelabs.com/api/v2`, auth `Authorization: Bearer <key>` (per-user free keys from loffeelabs.com/developers). Public: `/roasters /origins /varieties /processes` (`{"data":[…]}`).
- `GET /beans` returned `{"meta":{…,"remainingQuota"},"beans":[…]}` — wrapper key was `beans`, NOT `data`. `id` was a JSON number (stored as an opaque string).
- **Search matched whole words only** — no prefix matching, no wildcards, no term-AND.
- **Free tier omitted `image`, `tasting-tag`, `general-tag`, `soldout`, `available`** (supporter-tier-gated per the query-builder source).
- Quota counted beans returned (2,000/day free). Rate limit 1 req/3 s. Both returned 429.

## Canonical search (the only path, June 2026)

`BeanBaseClient::search()` hits Visualizer's **official** `GET /api/canonical_coffee_bags?q=` (keyless, substring + multi-word; documented in `miharekar/visualizer` openapi.yaml, returns `{data:[…],paging:{…}}`). It is debounced 350 ms (latest-wins) with a session cache by normalized query; together they keep usage under the endpoint's rate limit (50 req/min per IP, 200/10 min). `searchFailed` emits only `network`, `parse`, or `superseded` (HTTP 429 → `network`, never an empty "no matches"). Entries carry `visualizerCanonicalId` (UUID) = the identity stored locally and sent on shot PATCH (`shot[canonical_coffee_bag_id]`, accepted for all users) so the same bag id lands in both systems. The search response already includes every descriptive field, so it is a **single call**: each entry carries the descriptive blob + `canonicalRoasterId`, and `fetchCanonicalDetails()` is a local **deferred re-emit** of `canonicalDetails` with no network round-trip (silent when the entry has no descriptive values). MCP tool: `bean_search` (canonical search; enrichment from the same response). The old `/canonical/autocomplete_*` HTML endpoints are no longer used (migrate-canonical-search-api).

## The entry / blob vocabulary (schema of record)

Entries are QVariantMaps (QML lingua franca; deliberately not a C++ value type). Keys, by producer (the "Bean Base" column documents the removed `searchBeanBase()` shape — no live producer now, but consumers still tolerate these keys if a snapshot from before the removal carries them):

**User-editable working keys (add-bag-detail-editing):** the bag editor (ChangeBeansDialog) and MCP `bag_update` edit the blob's descriptive keys through `BeanBaseBlob::mergeBeanDetails()` (`src/network/beanbase_blob.h`; QML bridge on `BeanBaseClient`). Editable list: `roasterName, roastName, degree, origin, region, farm, producer, variety, elevation, process, harvest, qualityScore, placeOfPurchase, tastingNotes, link` (`farm`/`qualityScore`/`placeOfPurchase` are user-input-only — the canonical DB has no such columns). An empty edit REMOVES the key (absent-not-empty). **A blob without `id` is valid**: a manual bag carrying user-entered details stays unlinked (`isLinked` keys solely off a non-empty `id`). On the first edit of a linked blob, the pre-edit values are snapshotted into a `canonical` sub-object (never touched by later edits) — `revertToCanonical()` restores them and `differsFromCanonical()` drives the editor's "Revert to Bean Base data" affordance. Consumers of flat keys ignore `canonical`; shot snapshots carry it along unchanged.

| Key | canonical (`search()`) | Bean Base (removed `searchBeanBase()`) | enrichment (`canonicalDetails`) |
|---|---|---|---|
| `id` (opaque string; **non-empty = linked**) | UUID | integer-as-string | — |
| `visualizerCanonicalId` | = id | — | — |
| `source` | "visualizer" | "beanbase" | — |
| `roasterName`, `roastName` | ✓ | ✓ | — |
| `degree, origin, region, producer, variety, process, harvest, tastingNotes` | ✓ (remapped from Visualizer columns via `kAttrMap`) | ✓ | ✓ (same values; `canonicalDetails` is a local re-emit) |
| `elevation` (display string) | ✓ | — | ✓ |
| `link` (roaster product page) | ✓ (from `url`) | ✓ | — |
| `minElevationM/maxElevationM` (int), `image, beanType, description, tastingTags, generalTags, soldout, available, roasterRegion, roasterCountry` | — | ✓ | — |

The blob = one entry JSON-compacted. `src/network/beanbase_blob.h` is the C++ definition of "linked" (`isLinked`: parses + non-empty `id`) and of the uploader's canonical id (`canonicalId`); QML mirrors it via `bean.id !== ""` checks. A misspelled key reads as `undefined`/`""` with no diagnostics — check this table before adding readers.

## Bag images (file cache, never in the DB)

The canonical DB has **no image column**, so canonical blobs carry no `image` (only legacy pre-removal blobs do). Bag photos are resolved best-effort by `BeanBaseClient::ensureBagImage()`: the entry's `link` (roaster product page) is fetched once and its `og:image` meta tag downloaded to a **file cache** at `CacheLocation/bagimages/<key>` — key = canonical id for linked bags, `bag-<rowid>` for manual bags with a user-entered URL (add-bag-detail-editing; `bag-` keys skip the canonical URL-recovery fallback) — size-capped (30 MB), oldest-written evicted first, re-resolvable; pixels never enter the database or the blob (writes are atomic: temp file + rename). Blobs linked before `link` was captured recover the URL by re-searching the canonical API by name; the recovered `link` is backfilled into the bag blob (`bagLinkRecovered` → BagCard) so the details popup can offer the reorder URL. One attempt per canonical id per session; expected misses are silent (consumers keep their placeholder), local disk faults `qWarning`. Consumers: `BagCard` (`Theme.scaled(44)` thumbnail + beans-icon placeholder) and `BeanBaseDetailsPopup` (large photo) — both fall back to a legacy blob `image` URL; `BeanBaseDetailsRow` still renders only the legacy blob URL and does not use the cache. File writes and eviction run off the main thread.

## UI rules

- Search bar (`BeanBaseSearchBar.qml`) is always visible — search is keyless since the canonical switch. Label is the verbatim branding "Search Loffee Labs Bean Base" (untranslated).
- **Lock-follows-data is retired** (add-bag-detail-editing): no surface locks bean fields anymore. BeanInfoPage has had no editable bean text fields since the bag-inventory switch (edits go through the dialog), and the bag editor (ChangeBeansDialog) keeps identity, roast level, and the whole Bean details section editable while linked — the canonical link is a badge, not a lock (matching Visualizer's own bag editor), and editing never breaks the link. A non-combo canonical degree ("Light To Medium-light") shows as the roast-level combo's displayText until the user picks a level.
- The link is always correctable: Unlink works without a key; typing while linked re-enters search; edit mode rewrites the *shot's* snapshot (`requestUpdateShotMetadata` carries `beanBaseJson`).
- **"Get info from page"** (add-bag-detail-editing, hardened by add-recipe-wizard-tea): with a product URL + a configured AI provider, the bag editor offers Visualizer-style extraction in **two stages**. Stage 1: `BeanBaseClient::fetchPageText()` (HTML→squished text, **48k cap** — the old 20k truncated menu-heavy Shopify pages from the wrong end) → `AIManager::extractCoffeeBagDetails(token, text, kind)` (dedicated `bagDetailsExtracted/-Failed` signals — never `recommendationReceived`, which the advisor consumes) → fills **empty** detail fields only. Stage 2 (on `emptyPage` — the JS-rendered-SPA signature, e.g. fortnumandmason.com): `AIManager::extractCoffeeBagDetailsFromUrl()` has the PROVIDER fetch the URL itself via its server-side web-fetch tool (`AIProvider::supportsUrlAnalysis`/`analyzeUrl`; Anthropic `web_fetch_20250910` with max_uses 2 + 20k max_content_tokens, OpenAI via the **Responses API** `web_search` tool at reasoning effort low — chat/completions has no general web tool and gpt-5 rejects web_search at minimal, Gemini `url_context`; Ollama/OpenRouter emit `urlFetchUnsupported` and the stage-1 error shows). Stage 2's JSON adds `imageUrl` (the main product photo — SPA pages have no og:image), consumed by `BeanBaseClient::cacheBagImageFromUrl()` exactly like an og:image hit (create mode stashes it until the row id exists). Caveat: Anthropic's server fetch does not execute JS either — SPA success depends on the shop serving prerendered content to crawlers. The `kind` selects the extraction vocabulary: coffee (origin…roastLevel) or **tea** (teaType/garden/cultivar/flush/tastingNotes + structured brewing numbers: `brewTempC` normalized to Celsius, `leafGramsPer100Ml` normalized from per-cup dosing, `steepTime` display string) — the tea keys are in `BeanBaseBlob::editableKeys()` and seed the recipe wizard (see RECIPES.md). Manual bags with a URL also resolve their photo (see Bag images).
- **Tea bags** (add-recipe-wizard-tea): `coffee_bags.kind` ("coffee" default / "tea", migration 28, stamped at creation via BeanInfoPage's Add Coffee / Add Tea buttons, never edited — a mis-created zero-shot bag is deleted and recreated). Tea mode in ChangeBeansDialog is subtraction: Brand/Tea labels, roast level + grind/rpm + ALL canonical-link affordances hidden (the canonical DB is coffee-only; tea searches return coffees with tea-flavored tasting notes — verified live), tea detail fields shown. `UnifiedBeanSearchModel.bagKind = "tea"` suppresses the canonical lane and filters inventory/history lanes to known tea bags (`queryHistoryStatic`'s kind EXISTS clause). Visualizer shot upload/bag sync is unchanged for tea.
- Details surfaces: `BeanBaseDetailsRow`/`BeanBaseDetailsPopup` on BeanInfoPage (live DYE state), PostShotReviewPage + ShotDetailPage (per-shot snapshot). Zero footprint when the blob is empty.
- PostShotReviewPage also hosts the full search/link/unlink flow: a pick rewrites the SHOT's snapshot via the page's autosave (undoable); the sticky DYE link follows only for the MOST RECENT shot (gate on `lastSavedShotId`, which is seeded from MAX(id) at startup so the rule holds across restarts) — historic edits never touch the bean dialog or brew settings. The sticky sync runs only after the DB write is confirmed.

## Visualizer linkage (shot PATCH + CM bag sync + edit-push shipped)

From the open-source `miharekar/visualizer` repo: `canonical_coffee_bags.id` is a Visualizer UUID; the Bean Base integer lives in `loffee_labs_id` — **no API resolves one to the other today**, so canonical linkage needs a small upstream addition. Bag CRUD is at `/api/coffee_bags` (writes premium-gated); shot PATCH accepts `shot[canonical_coffee_bag_id]` (all users) and `shot[coffee_bag_id]` (coffee management enabled, auto-fills bean fields server-side).

**Bag edit-push (add-bag-detail-editing):** editing a bag (editor save or MCP `bag_update`) that changed a Visualizer-stored field (`CoffeeBagStorage::touchesVisualizerFields` → `bagVisualizerFieldsChanged` → `updateBagOnVisualizer`, gated on visualizerAutoUpdate + CM Active + `visualizerBagId`) PATCHes the remote bag with the FULL current field set (`addBagDescriptiveFields`, static + unit-tested; empty locals omitted, never nulled — a local clear can't wipe a server value). Roaster renames re-resolve via find-or-create and re-point `roaster_id`. Failure handling is **park-first**: `coffee_bags.visualizer_sync_pending` (migration 24) is set before any network I/O (and when CM is still Unknown) and cleared only by an actual outcome — so a push that dies anywhere (roaster GET, bag load, PATCH transport/429/5xx) stays parked and is drained by `retrySyncPendingBags()` when the next upload's read-back confirms CM Active; 403 → NoCoffeeManagement; 404 → cleared, next upload recreates; 422 → cleared + `bagPushRejected(bagId, bagName, message)` → one-shot toast in Main.qml, local values kept. The upload-time `buildBagEnrichBody` path stays fill-blanks-only and now also carries `farm`/`quality_score`/`place_of_purchase`/`url`.

## Testing

`tests/tst_beanbaseclient.cpp` runs against a canned-response local HTTP server (`FakeBeanBaseServer`): canonical debounce coalescing, cache hits, `/api/canonical_coffee_bags` JSON parsing (`parseCanonicalCoffeeBags`), single-call enrichment (re-emit from the entry, zero requests), and the `bean_search` gather bridge (grace window + superseded). Gotcha: **no raw string literals in moc'd test files** — moc miscounts braces inside `R"(...)"` and silently drops subsequent classes (vtable link error).

## Coffee bag model (bean-bag-inventory)

Bags replaced bean presets entirely — one concept, no live-state divergence. Key invariants:

- **The active bag IS the bean state.** `SettingsDye`'s dye/* keys are a synchronous write-through cache of the active bag: selecting a bag (`Settings.dye.activeBagId`) copies its fields in (`applyActiveBag`); every bean/grinder/dose setter writes through to the bag row on a background thread. There is no `beansModified`, no save prompt. `setActiveBagKeepFields()` selects WITHOUT applying — used when the caller is about to set its own values (loading a historical shot's setup).
- **Storage**: `coffee_bags` table in the shot history DB (migration 19, `shothistorystorage.cpp`); CRUD via `src/history/coffeebagstorage.{h,cpp}` (async + `withTempDb()` statics). Shots snapshot `bag_id`, `frozen_date`, `defrost_date` at save time and populate the indexed `shots.beanbase_id` column (history search lane; backfilled from `beanbase_json` in migration 19).
- **Legacy presets**: `bean/presets` + `bean/selectedPreset` QSettings are merge-imported into bags by `CoffeeBagStorage::convertLegacyPresetSettings()` — version-independent, runs at launch AND from `SettingsSerializer` legacy imports; keys cleared only after commit. Guarded out of test builds (`DECENZA_TESTING`) so unit tests don't consume the developer's real presets.
- **Transfer**: `importDatabaseStatic` migrates bags with `shots.bag_id` id-remap; `dye/activeBagId` is excluded from settings export (device-local row id).
- **Freeze lifecycle**: `frozenDate`/`defrostDate` on the bag describe the CURRENT portion only; per-shot snapshots are the permanent thermal history. "Thaw" on the bag card opens a calendar and writes `defrostDate` via `requestUpdateBag`.
- **Search**: `UnifiedBeanSearchModel` (`src/history/unifiedbeansearchmodel.{h,cpp}`) merges inventory (Tier 0) + canonical search + shot history into the Change Beans dialog's ranked list; a history/canonical result matching an inventory bag is absorbed into its Tier 0 row.
- **canonicalRoasterId** rides along on each canonical search entry (mapped from the API's `canonical_roaster_id` by `parseCanonicalCoffeeBags`) and is persisted in the beanBaseData blob for future Visualizer Coffee Management roaster linking.
- **MCP**: `bag_list` / `bag_select` / `bag_update` tools; `shots_get_detail` carries the bag snapshot.
- Visualizer Coffee Management sync (CM detection, bag CRUD at upload time) is specced in `openspec/changes/bean-bag-inventory/specs/visualizer-coffee-management/spec.md` but blocked on a live-API verification spike.
