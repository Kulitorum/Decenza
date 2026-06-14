## Context

`BeanBaseClient` (`src/network/beanbaseclient.{h,cpp}`) drives the only canonical bean search path (the Loffee Labs Bean Base direct API was removed June 2026 — see `BEAN_BASE.md`). It currently scrapes Visualizer's internal HTML autocomplete endpoints and runs a three-request enrichment flow per pick:

1. `GET /canonical/autocomplete_coffee_bags?q=` → parse `<li>` rows for `{id, roasterName, roastName}`.
2. On pick: `GET /canonical/autocomplete_roasters?q=<roasterName>` → scrape to resolve the roaster UUID (cached).
3. `GET /canonical/autocomplete_coffee_bags?q=<roastName>&require_roaster=true&canonical_roaster_id=<uuid>` → scrape the embedded `data-coffee-bag-payload-value` JSON for the descriptive fields.

The maintainer added official JSON endpoints ([miharekar/visualizer@c649b972](https://github.com/miharekar/visualizer/commit/c649b9722cecbd3da4dd45caa4844391afe2caeb)) and their **search response already contains every descriptive field plus `canonical_roaster_id`**, verified live:

```
GET /api/canonical_coffee_bags?q=intelligentsia
{ "data": [ { "id","canonical_roaster_id","canonical_roaster_name","name","url",
              "roast_level","country","region","farmer","variety","elevation",
              "processing","harvest_time","tasting_notes" } ],
  "paging": { "count":121,"page":1,"limit":10,"pages":13 } }
```

Consumers depend only on the `searchResults(query, entries)` / `canonicalDetails(canonicalId, attrs)` signals and the entry/blob vocabulary (`BEAN_BASE.md`): `UnifiedBeanSearchModel`, `ChangeBeansDialog.qml`, `PostShotReviewPage.qml`, and the `bean_search` MCP gather (`mcptools_beansearch.cpp`).

## Goals / Non-Goals

**Goals:**
- Use the official `/api/canonical_*` endpoints + documented JSON, eliminating HTML scraping.
- Collapse the three-request flow to a single search request (enrichment becomes a local re-emit).
- Preserve the exact consumer contract (signals, entry keys, blob vocabulary, snapshot-not-reference).
- Stay within the new rate limit via the existing debounce + cache; classify 429 honestly.

**Non-Goals:**
- No consumer changes (`UnifiedBeanSearchModel`, QML, MCP gather keep their signal usage).
- No new settings, no auth/key (the path stays keyless).
- Not making Visualizer surface canonical attributes on personal bags (separate concern; the client enrich shipped in #1334 already covers display there).
- Not removing the old endpoints from anywhere we don't control.

## Decisions

**1. Single-call enrichment, attributes carried on each search entry.**
Each `searchResults` entry carries the descriptive blob + `canonicalRoasterId` directly (the API returns them). `fetchCanonicalDetails(entry)` re-emits `canonicalDetails` from the entry with no network round-trip.
- *Why over keeping 3 calls:* the data is already in hand; extra calls are pure waste and eat the 50/min budget (the MCP gather alone fired up to 5 enrichment calls per search). One call also removes the fragile roaster-name → UUID exact-match step.
- *Alternative considered:* keep enrichment as one fresh API call (match by `id`). Rejected — still a needless round-trip when the search already returned the row.

**2. Deferred (asynchronous) `canonicalDetails` emit.**
The re-emit is posted to a later event-loop turn (queued invoke / zero-delay) rather than emitted synchronously inside `fetchCanonicalDetails`.
- *Why:* preserves the existing async contract so a consumer that connects to `canonicalDetails` after calling `fetchCanonicalDetails` still receives it, and keeps the MCP gather's grace-timer accounting intact.

**3. Bounded failure vocabulary unchanged; 429 → reach failure.**
Keep `searchFailed` statuses to `network | parse | superseded` (the QML maps these). Classify HTTP 429 into the reach-failure bucket (`network`) so a rate-limited query never renders as "No matches."
- *Why over a new `rate_limited` token:* avoids a QML/localization change for a state that is rare given debounce+cache; the user-facing copy ("couldn't reach the bean database") stays honest. Revisit only if 429s prove common.

**4. Keep the 350 ms debounce + session cache; no extra throttle.**
Debounce fires one request after typing pauses, and the cache serves repeats. That keeps steady-state well under 50/min for a single user.
- *Why over adding a token-bucket:* the existing coalescing is sufficient; adding a limiter is speculative complexity (no evidence of hitting the cap).

**5. Replace the two HTML scrapers with one JSON entry-builder.**
Remove `parseCanonicalAutocomplete` / `parseCanonicalPayload` (HTML) and `fetchCanonicalPayload` / `m_roasterUuidCache`. Add a static JSON-based builder (kept static + public for tests) that maps one API bag object → one entry (identity + blob + `canonicalRoasterId`).

## Risks / Trade-offs

- **[New API not yet relied upon in prod by us]** → Verified live before coding; keep parsing defensive (missing fields tolerated, never throw). The old endpoints remain as an emergency manual fallback if the API regresses.
- **[Rate limit (50/min) is stricter than the old "no documented limit"]** → Debounce + cache + dropping enrichment calls *reduces* total request volume vs today; net safer. 429 handled as a reach failure.
- **[Behavioral change: enrichment can no longer "stall"]** → Since search carries the data, the MCP gather's enrichment grace path rarely triggers; a bag whose descriptive fields are all empty yields an empty attrs map and (as today) no `canonicalDetails` emit, still covered by the grace timer. The stall test is rewritten to that case.
- **[Pagination default 10/page]** → For type-ahead, page 1 is sufficient (exact/leading matches rank high); we take the first page and do not walk pages.

## Migration Plan

1. Land the client rewrite + test-harness update behind no flag (pure substitution; consumer contract unchanged).
2. Verify on-device: search, link, unlink, post-shot re-link, MCP `bean_search`.
3. Update `BEAN_BASE.md` "Canonical search" section to the new endpoints and single-call flow.
- **Rollback:** revert the change; the old `/canonical/autocomplete_*` endpoints still exist upstream, so reverting restores the prior working behavior with no data migration.

## Open Questions

- Does `/api/canonical_coffee_bags` accept an `items=`/`limit=` page-size param (to fetch >10 in one call)? Default 10 is fine for type-ahead, so not blocking; confirm during implementation if a larger result set is ever wanted.
- Should we eventually drop the keyless assumption if Visualizer ever gates these endpoints? Out of scope now (they are public today).
