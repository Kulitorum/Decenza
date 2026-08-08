# canonical-bean-search Specification

## Purpose
Specifies `BeanBaseClient`'s use of Visualizer's documented, public JSON API (`/api/canonical_coffee_bags`, `/api/canonical_roasters`) for bean/roaster lookups in place of the undocumented autocomplete HTML endpoints — covering the single-request full-attribute response, type-ahead debounce with latest-wins and session caching, bounded rate-limit-aware failure handling, and keyless canonical identity round-tripping to Visualizer.
## Requirements
### Requirement: Canonical search uses the official Visualizer JSON API

`BeanBaseClient` SHALL perform canonical bean and roaster lookups against Visualizer's documented, public JSON API — `GET /api/canonical_coffee_bags` and `GET /api/canonical_roasters` — and SHALL parse the documented `{ "data": [...], "paging": {...} }` response. It MUST NOT depend on the undocumented `/canonical/autocomplete_*` HTML endpoints.

#### Scenario: Search issues a JSON API request
- **WHEN** `search(query)` fires after debounce
- **THEN** the client issues `GET /api/canonical_coffee_bags?q=<query>`
- **AND** parses the response as JSON, reading the `data` array

#### Scenario: Non-JSON or error response degrades cleanly
- **WHEN** the endpoint returns a non-200 status or an unparseable body
- **THEN** the client emits `searchFailed(query, status)` with a status token and never throws

### Requirement: A single search request returns full descriptive attributes

Because the API returns each canonical bag's full descriptive block in the search response, each entry emitted via `searchResults` SHALL carry both the identity fields (`id`, `visualizerCanonicalId`, `source = "visualizer"`, `roasterName`, `roastName`) and the descriptive blob (`degree`, `origin`, `region`, `producer`, `variety`, `process`, `harvest`, `tastingNotes`, `elevation`) plus `canonicalRoasterId`, remapped from the API's column names. No second or third request SHALL be required to obtain attributes.

#### Scenario: Search entries include descriptive attributes
- **WHEN** a search returns a canonical coffee bag with origin data
- **THEN** the emitted entry contains the remapped blob keys (e.g. `origin` from `country`, `producer` from `farmer`, `process` from `processing`, `degree` from `roast_level`) and `canonicalRoasterId` from `canonical_roaster_id`

#### Scenario: Enrichment requires no additional network call
- **WHEN** `fetchCanonicalDetails(entry)` is called for an entry produced by `search()`
- **THEN** the client emits `canonicalDetails(canonicalId, attrs)` built from the attributes already present on the entry
- **AND** issues no network request for the enrichment

#### Scenario: Enrichment delivery stays asynchronous
- **WHEN** `fetchCanonicalDetails(entry)` re-emits attributes from the entry
- **THEN** `canonicalDetails` is delivered on a later event-loop turn (deferred), so a consumer that connects after invoking it still receives the signal

### Requirement: Type-ahead debounce, latest-wins, and session caching

The canonical path SHALL debounce input (350 ms, single-shot), coalescing rapid keystrokes into at most one in-flight request, apply latest-wins (a superseded pending or in-flight query notifies its consumer rather than leaving it waiting), and serve a repeated normalized query from a session cache without a network request.

#### Scenario: Rapid keystrokes coalesce to one request
- **WHEN** `search()` is called multiple times within the debounce window
- **THEN** exactly one request is sent, for the latest query

#### Scenario: Repeated query is served from cache
- **WHEN** a query that previously returned results is searched again
- **THEN** `searchResults` is emitted from the session cache with no new request

#### Scenario: Superseded query is reported
- **WHEN** a pending or in-flight query is displaced by a newer one
- **THEN** the displaced query's consumer receives `searchFailed(query, "superseded")`

### Requirement: Rate-limit-aware and bounded failure vocabulary

The client SHALL respect the API's documented rate limit (50 requests/minute per IP, 200/10 min) through its debounce + cache, and SHALL classify outcomes using a bounded `searchFailed` status vocabulary that the UI maps to localized text. An HTTP 429 SHALL be classified as a reach failure rather than surfaced as a missing-result ("no matches") state.

#### Scenario: Rate-limited response is not mistaken for empty results
- **WHEN** the API returns HTTP 429
- **THEN** the client emits `searchFailed(query, ...)` (a reach failure), not an empty `searchResults`

### Requirement: Keyless canonical identity round-trips to Visualizer

The search path SHALL remain keyless (no account or API key), and the `id` / `visualizerCanonicalId` of an entry SHALL be the Visualizer canonical UUID that Decenza stores locally and sends back on shot upload as `shot[canonical_coffee_bag_id]`, so the same canonical id links the bean in both systems.

That round-trip carries a precondition, because the id is an IDENTITY claim and not a details pointer: a canonical record is a ROASTER'S PRODUCT, and visualizer.coffee rewrites a shot's `bean_brand` and `bean_type` from the linked record. Decenza SHALL store and export the id only while the local bag's own roaster and coffee still name that record. When they do not — the **borrowed record** case, where the same coffee scraped from another roaster was the only match because the user's roaster is absent from the canonical database — the link SHALL be dropped rather than corrected: the canonical endpoints are read-only, so no correct id exists for such a bag and unlinked is the only correct state.

A shot's stored snapshot is a historical record and SHALL NOT be rewritten to match, so the export path SHALL apply the same check independently and withhold a borrowed id at upload time.

#### Scenario: Canonical UUID is the stored and uploaded identity

- **WHEN** a user links a bean from a canonical search entry
- **THEN** the stored snapshot's `id`/`visualizerCanonicalId` equals the API's `id`
- **AND** that id is sent on shot upload as `shot[canonical_coffee_bag_id]`

#### Scenario: A borrowed record is never stored as a link

- **WHEN** a bag is saved whose roaster or coffee names a different coffee than the record it points at — whether by picking a near-match and typing over the prefilled roaster, or by renaming a bag that was already linked
- **THEN** the canonical id SHALL NOT be stored
- **AND** the descriptive values and product URL from that record SHALL be kept as the user's own data

#### Scenario: A borrowed id already in a shot snapshot is withheld from export

- **WHEN** a shot whose stored snapshot carries a canonical id is uploaded or its metadata is updated, and that snapshot's record names a different coffee than the shot's own bean fields
- **THEN** `canonical_coffee_bag_id` SHALL be omitted from the payload
- **AND** the shot's stored snapshot SHALL be left unchanged

#### Scenario: An unverifiable snapshot is treated as borrowed

- **WHEN** the stored snapshot cannot be parsed
- **THEN** the link SHALL be treated as conflicted and withheld, because the permissive answer exports an unverified identity claim into the user's cloud history

