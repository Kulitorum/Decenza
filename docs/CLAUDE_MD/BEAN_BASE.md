# Bean Base Integration (Loffee Labs)

Canonical coffee-database integration: users link beans to Loffee Labs Bean Base entries, and the snapshot enriches shot history, Visualizer uploads, and the AI advisor. OpenSpec change: `add-bean-base-integration` (design.md §§ Context 5–9 hold the full empirical API findings).

## Architecture

```
BeanBaseSearchBar (BeanInfoPage)            Settings → Visualizer → Bean Base
   └─ MainController.beanbase                  SettingsBeanBase (beanbase/apiKey
        BeanBaseClient                           — gates ONLY testApiKey/
          ├─ search() → visualizer.coffee          searchBeanBase, not the bar)
          │    canonical autocomplete (keyless)
          └─ searchBeanBase() → loffeelabs.com (key; unused in production)
   user picks entry → DYE link state (SettingsDye: dyeBeanBaseId/RoasterId/Data)
        ├─ bean presets carry the link (apply/save/rename handled)
        └─ shot save snapshots the blob → shots.beanbase_json (migration 18)
              ├─ ShotRecord/ShotProjection.beanBaseJson (sparse-emit)
              ├─ MCP: shots_get_detail emits parsed `beanBase`; shots_update accepts it
              └─ advisor: currentBean.beanBase via DialingBlocks::buildCurrentBeanBlock
```

**Snapshot, not reference**: every shot stores the full bean JSON at link time. Bean Base delists/mutates entries (`historical=true` is Dev-tier-gated), so history must never depend on their retention. Bag images are CDN URLs, not stored pixels.

## The API (confirmed live, June 2026)

- Base `https://loffeelabs.com/api/v2`, auth `Authorization: Bearer <key>` (per-user free keys from loffeelabs.com/developers). Public: `/roasters /origins /varieties /processes` (`{"data":[…]}`).
- `GET /beans` returns `{"meta":{…,"remainingQuota"},"beans":[…]}` — wrapper key is `beans`, NOT `data`. `id` is a JSON number (we store it as an opaque string).
- **Search matches whole words only** — no prefix matching, no wildcards, no term-AND. (Relevant only to `searchBeanBase()`; the UI's canonical search is substring, so nothing needs to teach it.)
- **Free tier omits `image`, `tasting-tag`, `general-tag`, `soldout`, `available`** (supporter-tier-gated per the query-builder source). UI photo slots collapse gracefully and light up with no code changes if the tier exposes them.
- Quota counts beans returned (2,000/day free; our 25-result searches ≈ 80 fresh searches/day). Rate limit 1 req/3 s. Both return 429 — `classify429()` splits them by body text so quota exhaustion says "resets tomorrow", not "try again shortly".
- Client enforces the budget: 800 ms debounce, 3 s sent-floor (latest-wins queue), session cache by normalized query, in-flight abort on supersede.

## Canonical search (the primary path, June 2026)

`BeanBaseClient::search()` hits Visualizer's open `GET /canonical/autocomplete_coffee_bags?q=` (keyless, substring + multi-word; internal endpoint — parse defensively, expect drift). Entries carry `visualizerCanonicalId` (UUID) = the identity stored locally and sent on shot PATCH (`shot[canonical_coffee_bag_id]`, accepted for all users) so the same bag id lands in both systems. `fetchCanonicalDetails()` runs the two-stage flow (`autocomplete_roasters` → `require_roaster=true&canonical_roaster_id=`) for the attribute payload. `searchBeanBase()` keeps the Bean Base API contract (whole words, key, quota) for optional extras. MCP tool: `bean_search` (canonical + per-result enrichment).

## The entry / blob vocabulary (schema of record)

Entries are QVariantMaps (QML lingua franca; deliberately not a C++ value type). Keys, by producer:

| Key | canonical (`search()`) | Bean Base (`searchBeanBase()`) | enrichment (`canonicalDetails`) |
|---|---|---|---|
| `id` (opaque string; **non-empty = linked**) | UUID | integer-as-string | — |
| `visualizerCanonicalId` | = id | — | — |
| `source` | "visualizer" | "beanbase" | — |
| `roasterName`, `roastName` | ✓ | ✓ | — |
| `degree, origin, region, producer, variety, process, harvest, tastingNotes` | — | ✓ | ✓ (remapped from Visualizer columns) |
| `elevation` (display string) | — | — | ✓ |
| `minElevationM/maxElevationM` (int), `link, image, beanType, description, tastingTags, generalTags, soldout, available, roasterRegion, roasterCountry` | — | ✓ | — |

The blob = one entry JSON-compacted. `src/network/beanbase_blob.h` is the C++ definition of "linked" (`isLinked`: parses + non-empty `id`) and of the uploader's canonical id (`canonicalId`); QML mirrors it via `bean.id !== ""` checks. A misspelled key reads as `undefined`/`""` with no diagnostics — check this table before adding readers.

## UI rules

- Search bar (`BeanBaseSearchBar.qml`) is always visible — search is keyless since the canonical switch. Label is the verbatim branding "Search Loffee Labs Bean Base" (untranslated).
- **Lock follows the data**: a field (Roaster/Coffee/Roast level) locks iff linked AND the entry supplied a non-empty value. Locked roast level renders as read-only text (Bean Base degree strings like "Light To Medium-light" don't fit the combo model). Tapping any locked field opens the details popup.
- The link is always correctable: Unlink works without a key; typing while linked re-enters search; edit mode rewrites the *shot's* snapshot (`requestUpdateShotMetadata` carries `beanBaseJson`).
- Details surfaces: `BeanBaseDetailsRow`/`BeanBaseDetailsPopup` on BeanInfoPage (live DYE state), PostShotReviewPage + ShotDetailPage (per-shot snapshot). Zero footprint when the blob is empty.

## Visualizer linkage (shot PATCH shipped; bag CRUD / id-resolution pending — design.md § Context 9)

From the open-source `miharekar/visualizer` repo: `canonical_coffee_bags.id` is a Visualizer UUID; the Bean Base integer lives in `loffee_labs_id` — **no API resolves one to the other today**, so canonical linkage needs a small upstream addition. Bag CRUD is at `/api/coffee_bags` (writes premium-gated); shot PATCH accepts `shot[canonical_coffee_bag_id]` (all users) and `shot[coffee_bag_id]` (coffee management enabled, auto-fills bean fields server-side).

## Testing

`tests/tst_beanbaseclient.cpp` runs against a canned-response local HTTP server (`FakeBeanBaseServer`): debounce coalescing, 3 s gap, cache hits, 429 classification, parse robustness. Gotcha: **no raw string literals in moc'd test files** — moc miscounts braces inside `R"(...)"` and silently drops subsequent classes (vtable link error).
