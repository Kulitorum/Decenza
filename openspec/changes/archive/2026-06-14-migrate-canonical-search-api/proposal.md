## Why

Decenza's canonical bean search (`BeanBaseClient`) scrapes Visualizer's **undocumented internal** HTML autocomplete endpoints (`GET /canonical/autocomplete_coffee_bags`, `/canonical/autocomplete_roasters`). The Visualizer maintainer has now published an **official, documented JSON API** for the same data ([miharekar/visualizer@c649b972](https://github.com/miharekar/visualizer/commit/c649b9722cecbd3da4dd45caa4844391afe2caeb), live on production) — explicitly as the supported path for clients like us. Migrating de-risks our bean search against the internal endpoints changing or being retired, and lets us drop fragile HTML scraping for a stable contract.

The new search response **already includes the full descriptive block** per result, so the current three-request enrichment dance collapses to a single search request — a meaningful simplification and a large reduction in calls against the new API's rate limit.

## What Changes

- Repoint `BeanBaseClient` from `/canonical/autocomplete_*` to `GET /api/canonical_coffee_bags` and `GET /api/canonical_roasters`, parsing the documented JSON (`{data, paging}`) instead of scraping HTML `<li>` fragments.
- Collapse the three-call enrichment flow (search → resolve roaster UUID via `autocomplete_roasters` → re-search with `require_roaster=true` to scrape `data-coffee-bag-payload-value`) into a **single search call**: each search result carries the descriptive blob (`degree/origin/region/producer/variety/process/harvest/tastingNotes/elevation`) and `canonicalRoasterId` directly. `fetchCanonicalDetails(entry)` becomes a local, deferred re-emit of `canonicalDetails` with **no network round-trip**.
- Remove the HTML-scraping helpers (`parseCanonicalAutocomplete`, `parseCanonicalPayload`), the roaster-UUID cache, and the `fetchCanonicalPayload` second stage.
- Respect the new API's documented rate limit (50 req/min per IP, 200/10min): keep the 350 ms type-ahead debounce + session cache, and classify HTTP 429 distinctly from a generic reach failure.
- Preserve the consumer contract exactly: `searchResults(query, entries)` / `canonicalDetails(canonicalId, attrs)` signals, the entry/blob vocabulary in `BEAN_BASE.md`, and the snapshot-not-reference rule.

Not breaking for users — the old endpoints still work today (not yet deprecated), so this is a de-risking/cleanup change, not urgent.

## Capabilities

### New Capabilities
- `canonical-bean-search`: The behavioral contract of Decenza's canonical bean lookup — which Visualizer endpoints are used, the shape of search-result entries and enrichment attributes, debounce/cache/latest-wins semantics, rate-limit handling, and the keyless identity (`visualizerCanonicalId`) round-tripped on shot upload.

### Modified Capabilities
<!-- None: there is no pre-existing canonical-bean-search spec in openspec/specs/ (the original bean-base work shipped via the add-bean-base-integration and bean-bag-inventory changes, not a promoted spec). This change captures the contract as a new spec while migrating the implementation. -->

## Impact

- **Code**: `src/network/beanbaseclient.{h,cpp}` (endpoints, parsing, enrichment flow, caches). No change required at consumers — `UnifiedBeanSearchModel`, `ChangeBeansDialog.qml`, `PostShotReviewPage.qml`, and the `bean_search` MCP gather (`mcptools_beansearch.cpp`) keep their existing signal contract.
- **Tests**: `tests/tst_beanbaseclient.cpp` — `FakeBeanBaseServer` canned responses (HTML → JSON), the parse tests, the two-stage → single-call enrichment test, and the enrichment-stall/grace case (which changes shape now that search carries the descriptive data).
- **External dependency**: Visualizer's `/api/canonical_*` endpoints (public, keyless, rate-limited). The old `/canonical/autocomplete_*` endpoints remain as an undocumented fallback only if needed.
- **Docs**: `docs/CLAUDE_MD/BEAN_BASE.md` — update the "Canonical search" section to the new endpoints and single-call flow.
