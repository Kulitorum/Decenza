# Bean Base Integration (Loffee Labs)

Canonical coffee-database integration: users link beans to Loffee Labs Bean Base entries, and the snapshot enriches shot history, Visualizer uploads, and the AI advisor. OpenSpec change: `add-bean-base-integration` (design.md §§ Context 5–9 hold the full empirical API findings).

## Architecture

```
BeanBaseSearchBar (BeanInfoPage)            Settings → Visualizer → Bean Base
   └─ MainController.beanbase ──────────────── SettingsBeanBase (beanbase/apiKey)
        BeanBaseClient (src/network/beanbaseclient.{h,cpp})
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
- **Search matches whole words only** — no prefix matching, no wildcards, no term-AND. The empty state teaches this.
- **Free tier omits `image`, `tasting-tag`, `general-tag`, `soldout`, `available`** (supporter-tier-gated per the query-builder source). UI photo slots collapse gracefully and light up with no code changes if the tier exposes them.
- Quota counts beans returned (2,000/day free; our 25-result searches ≈ 80 fresh searches/day). Rate limit 1 req/3 s. Both return 429 — `classify429()` splits them by body text so quota exhaustion says "resets tomorrow", not "try again shortly".
- Client enforces the budget: 800 ms debounce, 3 s sent-floor (latest-wins queue), session cache by normalized query, in-flight abort on supersede.

## Canonical search (the primary path, June 2026)

`BeanBaseClient::search()` hits Visualizer's open `GET /canonical/autocomplete_coffee_bags?q=` (keyless, substring + multi-word; internal endpoint — parse defensively, expect drift). Entries carry `visualizerCanonicalId` (UUID) = the identity stored locally and sent on shot PATCH (`shot[canonical_coffee_bag_id]`, accepted for all users) so the same bag id lands in both systems. `fetchCanonicalDetails()` runs the two-stage flow (`autocomplete_roasters` → `require_roaster=true&canonical_roaster_id=`) for the attribute payload. `searchBeanBase()` keeps the Bean Base API contract (whole words, key, quota) for optional extras. MCP tool: `bean_search` (canonical + per-result enrichment).

## UI rules

- Search bar (`BeanBaseSearchBar.qml`) is always visible — search is keyless since the canonical switch. Label is the verbatim branding "Search Loffee Labs Bean Base" (untranslated).
- **Lock follows the data**: a field (Roaster/Coffee/Roast level) locks iff linked AND the entry supplied a non-empty value. Locked roast level renders as read-only text (Bean Base degree strings like "Light To Medium-light" don't fit the combo model). Tapping any locked field opens the details popup.
- The link is always correctable: Unlink works without a key; typing while linked re-enters search; edit mode rewrites the *shot's* snapshot (`requestUpdateShotMetadata` carries `beanBaseJson`).
- Details surfaces: `BeanBaseDetailsRow`/`BeanBaseDetailsPopup` on BeanInfoPage (live DYE state), PostShotReviewPage + ShotDetailPage (per-shot snapshot). Zero footprint when the blob is empty.

## Visualizer linkage (Tier 3 — pending, see design.md § Context 9)

From the open-source `miharekar/visualizer` repo: `canonical_coffee_bags.id` is a Visualizer UUID; the Bean Base integer lives in `loffee_labs_id` — **no API resolves one to the other today**, so canonical linkage needs a small upstream addition. Bag CRUD is at `/api/coffee_bags` (writes premium-gated); shot PATCH accepts `shot[canonical_coffee_bag_id]` (all users) and `shot[coffee_bag_id]` (coffee management enabled, auto-fills bean fields server-side).

## Testing

`tests/tst_beanbaseclient.cpp` runs against a canned-response local HTTP server (`FakeBeanBaseServer`): debounce coalescing, 3 s gap, cache hits, 429 classification, parse robustness. Gotcha: **no raw string literals in moc'd test files** — moc miscounts braces inside `R"(...)"` and silently drops subsequent classes (vtable link error).
