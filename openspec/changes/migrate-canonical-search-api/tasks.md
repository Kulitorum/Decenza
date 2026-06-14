## 1. Client: endpoints + JSON entry builder

- [ ] 1.1 Add a static JSON entry-builder (e.g. `entryFromApiBag(QJsonObject) -> QVariantMap`) that maps one `/api/canonical_coffee_bags` `data` object to one entry: `id`, `visualizerCanonicalId` (= id), `source = "visualizer"`, `roasterName` (from `canonical_roaster_name`), `roastName` (from `name`), `canonicalRoasterId` (from `canonical_roaster_id`), and the descriptive blob (`degree`←`roast_level`, `origin`←`country`, `region`, `producer`←`farmer`, `variety`, `process`←`processing`, `harvest`←`harvest_time`, `tastingNotes`←`tasting_notes`, `elevation`), dropping empty/null values. Keep it static + public for tests.
- [ ] 1.2 Repoint `doSendCanonicalSearch` to `GET /api/canonical_coffee_bags?q=`, parse `{data:[...]}` JSON, build entries via 1.1, cache + emit `searchResults`. Remove the HTML `<li>` tripwire; treat a JSON-parse failure as `searchFailed(query, "parse")`.
- [ ] 1.3 Classify HTTP 429 (and other non-200) as `searchFailed(query, "network")`; keep the `superseded` and transfer-timeout handling intact.

## 2. Client: single-call enrichment

- [ ] 2.1 Rewrite `fetchCanonicalDetails(entry)` to build the attrs map (`degree/origin/region/producer/variety/process/harvest/tastingNotes/elevation` + `canonicalRoasterId`) from the keys already present on `entry`, and emit `canonicalDetails(canonicalId, attrs)` **deferred** (queued/zero-delay) so it stays asynchronous. Emit nothing when the entry carries no descriptive values (preserves the gather grace-timer path).
- [ ] 2.2 Remove the now-dead second stage: `fetchCanonicalPayload`, `m_roasterUuidCache`, and the HTML scrapers `parseCanonicalAutocomplete` / `parseCanonicalPayload`. Update `beanbaseclient.h` declarations + doc comments (endpoints, single-call flow, rate-limit note).

## 3. Tests

- [ ] 3.1 Update `FakeBeanBaseServer` usage: canned responses are JSON (`{"data":[...]}`); update `canonicalSearchFlow` to assert the request hits `/api/canonical_coffee_bags`, debounce coalescing, cache hit, and `visualizerCanonicalId`.
- [ ] 3.2 Replace the HTML parse tests with a JSON entry-builder test: assert identity + remapped blob keys + `canonicalRoasterId`, and that nulls/empties are dropped; garbage degrades to empty.
- [ ] 3.3 Rewrite the enrichment test: an entry carrying descriptive keys emits `canonicalDetails` with **zero** additional requests; assert the deferred (async) delivery.
- [ ] 3.4 Update the MCP gather tests: the stall/grace case now uses a search result whose descriptive fields are empty (so no `canonicalDetails`), and the superseded case uses `{"data":[]}`.

## 4. Verify + document

- [ ] 4.1 Build clean (Qt Creator) and run `tst_beanbaseclient` (all green); run `tst_coffeebags` to confirm no regression in the bean/bag path.
- [ ] 4.2 On-device smoke test: Beans-page search + link, unlink, post-shot re-link, and MCP `bean_search`.
- [ ] 4.3 Update `docs/CLAUDE_MD/BEAN_BASE.md` "Canonical search" section to the new `/api/canonical_*` endpoints and the single-call flow; update the `beanbaseclient.h` header comment likewise.
