## 1. Client: endpoints + JSON entry builder

- [x] 1.1 Add a static JSON entry-builder (`parseCanonicalCoffeeBags`) that maps one `/api/canonical_coffee_bags` `data` object to one entry: `id`, `visualizerCanonicalId` (= id), `source = "visualizer"`, `roasterName` (from `canonical_roaster_name`), `roastName` (from `name`), `canonicalRoasterId` (from `canonical_roaster_id`), and the descriptive blob (`degree`←`roast_level`, `origin`←`country`, `region`, `producer`←`farmer`, `variety`, `process`←`processing`, `harvest`←`harvest_time`, `tastingNotes`←`tasting_notes`, `elevation`), dropping empty/null values. Static + public for tests, with a `parsedOk` out-param.
- [x] 1.2 Repoint `doSendCanonicalSearch` to `GET /api/canonical_coffee_bags?q=`, parse `{data:[...]}` JSON, build entries via 1.1, cache + emit `searchResults`. JSON-parse failure → `searchFailed(query, "parse")`.
- [x] 1.3 Classify HTTP 429 (and other non-200) as `searchFailed(query, "network")`; keep the `superseded` and transfer-timeout handling intact.

## 2. Client: single-call enrichment

- [x] 2.1 Rewrite `fetchCanonicalDetails(entry)` to build the attrs map (descriptive blob + `canonicalRoasterId`) from the keys already present on `entry`, and emit `canonicalDetails(canonicalId, attrs)` **deferred** (QueuedConnection invoke) so it stays asynchronous. Emit nothing when the entry carries no descriptive values (preserves the gather grace-timer path).
- [x] 2.2 Remove the now-dead second stage: `fetchCanonicalPayload`, `m_roasterUuidCache`, and the HTML scrapers `parseCanonicalAutocomplete` / `parseCanonicalPayload`. Update `beanbaseclient.h` declarations + doc comments (endpoints, single-call flow, rate-limit note).

## 3. Tests

- [x] 3.1 Update `canonicalSearchFlow`: canned JSON response; assert the request hits `/api/canonical_coffee_bags`, debounce coalescing, cache hit, and `visualizerCanonicalId`.
- [x] 3.2 Replace the HTML parse tests with `parseCanonicalCoffeeBagsJson`: identity + remapped blob keys + `canonicalRoasterId`, nulls/empties dropped, malformed→empty (parsedOk false), valid-empty→empty (parsedOk true).
- [x] 3.3 `fetchCanonicalDetailsFromEntryNoNetwork`: an entry carrying descriptive keys emits `canonicalDetails` with **zero** requests, delivered async; a descriptive-less entry emits nothing.
- [x] 3.4 Update the MCP gather tests: the stall/grace case uses a search result whose descriptive fields are empty (no `canonicalDetails`); the superseded case uses `{"data":[]}`.

## 4. Verify + document

- [x] 4.1 Build clean (Qt Creator, 0 warnings) and run `tst_beanbaseclient` (8/8) + `tst_coffeebags` (30/30) — all green.
- [ ] 4.2 On-device smoke test: Beans-page search + link, unlink, post-shot re-link, and MCP `bean_search` (requires running the new build).
- [x] 4.3 Update `docs/CLAUDE_MD/BEAN_BASE.md` "Canonical search" + "Testing" sections and the architecture diagram to the new `/api/canonical_*` endpoint and single-call flow; `beanbaseclient.h` header comment updated.
