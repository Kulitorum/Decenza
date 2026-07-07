# Design: Bag Detail Editing + Visualizer Auto-Push

## Context

Bean details currently flow one way: canonical Bean Base search → `beanBaseData` blob on the bag → display surfaces (BeanBaseDetailsPopup, BagCard attribute line), shot snapshots, AI advisor, MCP, and Visualizer upload enrichment. The blob is written only by a canonical pick; the bag form (ChangeBeansDialog) renders canonical attributes as read-only confirmation, and manual bags carry no details at all.

Visualizer's own bag editor (`app/views/coffee_bags/_form.html.erb` in miharekar/visualizer) exposes URL, roaster, name (with canonical autocomplete + verified badge), roast level/date, frozen/defrosted dates, country, region, farm, farmer, variety, elevation, processing, harvest time, quality score, place of purchase, tasting notes, custom metadata, notes, and an image — **all editable even when canonical-linked**; the canonical link only autofills and badges. Its API (`PATCH /api/coffee_bags/{id}`, premium-gated writes) accepts the same fields. Server-side constraints: name unique per roaster+roast_date (422 on collision), `defrosted_date >= frozen_date`, and edits to name/roast_date/roast_level/roaster refresh all linked shots' bean fields server-side.

Upload-time Coffee Management sync is already shipped (`visualizerBagId`/`visualizerRoasterId` columns, find-or-create in `visualizeruploader.cpp`), but `buildBagEnrichBody()` deliberately only fills server-side blanks (`fillBlank()`), so local edits never overwrite Visualizer.

## Goals / Non-Goals

**Goals:**
- Every field — identity and descriptive details — editable in the bag editor, for linked and manual bags alike, matching Visualizer's own editor semantics (a canonical link autofills and badges, never locks). Covers the roaster-updated-the-bag-but-canonical-is-stale case.
- A "Revert to Bean Base data" action restores the pristine canonical values after local edits.
- A product URL can be added when missing (also unlocks bag-image resolution via `og:image`).
- Edits to a Visualizer-linked bag propagate automatically to `PATCH /api/coffee_bags/{id}`.
- Manual-bag details reach every existing consumer (popup, card, shot snapshot, advisor, MCP, upload) with zero consumer changes.

**Non-Goals:**
- No writes to the **canonical** database (its API is read-only search).
- No pull of server-side bag edits back into Decenza (one-way push; the shot-upload cycle's blank-filling remains the only inbound path).
- No bag image upload (the API exposes `image_url` read-only), no per-user custom metadata fields.
- BeanInfoPage (live DYE) field locking is untouched — this change is scoped to the bag form.
- No new user-facing settings; push is implied by having Visualizer credentials + a linked remote bag.

## Decisions

### 1. Details stay in the `beanBaseData` blob — no new detail columns

Edited values are merged into the existing blob (`coffee_bags.beanbase_json`); a blob with details but an **empty `id`** now means "manual bag with details, not canonically linked". `beanbase_blob.h`'s `isLinked` (non-empty `id`) already gives exactly this semantics for free.

*Why not columns*: the blob is already the schema of record consumed by BagCard, BeanBaseDetailsPopup, shot snapshots (`shots.beanbase_json`), `dialing_blocks.h`, `shots_get_detail`, and `buildBagEnrichBody()`. Splitting into columns would require touching every consumer plus a snapshot-format migration for zero benefit. Snapshot-not-reference is preserved: shots keep capturing whatever the blob said at save time.

New blob keys (mirroring Visualizer fields Bean Base lacks): `farm`, `qualityScore`, `placeOfPurchase`. Existing keys reused: `origin`, `region`, `producer`, `variety`, `process`, `harvest`, `elevation`, `tastingNotes`, `link`, `degree`. `description` and `image` remain display-only legacy/canonical keys (not editable — Visualizer's editor has no description field either).

### 2. Editor form: a "Bean details" expander; link autofills, never locks — anything

ChangeBeansDialog's form (create + edit modes) gains a collapsible **Bean details** section: URL, Origin, Region, Farm, Producer, Variety, Elevation, Process, Harvest, Quality score, Place of purchase, Tasting notes — `StyledTextField`s, prefilled from the blob, always enabled. Collapsed by default with the existing `origin · variety · process` one-line summary as the header hint; expanded state shows all fields. Editing a canonical-linked bag does **not** unlink it (matches Visualizer's editor; the canonical id remains for search/dedup/shot-PATCH identity).

**Identity fields (roaster, coffee name) are editable while linked too**, replacing lock-while-linked in the bag form entirely. The driving use case: the roaster updated the bag (new crop, renamed lot) but the canonical DB carries only the older/closest entry — the user links the best match, then corrects name and details without losing the link. The "verified" treatment becomes a badge marking the link (as in Visualizer's own editor), not a lock. Roast level likewise: always editable, prefilled from `degree`. BeanInfoPage's live-DYE locking is unchanged (its locked fields already route users to the details popup; loosening it can follow later if the asymmetry annoys in practice).

Field labels reuse the existing translation keys from BeanBaseDetailsPopup where they exist; new keys for Farm, Quality score, Place of purchase, URL.

*Alternative considered*: a separate "Edit details" popup off BeanBaseDetailsPopup. Rejected — bag editing already lives in one place (ChangeBeansDialog edit mode via BagCard's edit button); a second editor would fork save paths.

### 2b. Pristine canonical snapshot + Revert

Because the canonical API is **search-only** (no GET-by-id), reverting edits cannot re-fetch — the original entry must be kept locally. At link time the picked entry's descriptive + identity values are stored as a `canonical` sub-object inside the blob; flat top-level keys remain the working copy that every existing consumer reads (nested objects are invisible to them — no consumer changes). For bags linked **before** this change, the snapshot is captured lazily: on the first edit-save, if the blob has an `id` but no `canonical` key, the current flat values (still pristine, since editing didn't exist) are copied in before applying edits.

**Revert to Bean Base data** (shown in the editor when linked and the working copy differs from `canonical`) restores every canonical-supplied value over the working keys and removes working keys the canonical entry lacked (including a user-added URL — revert means *original data*, stated in the confirmation). Revert is a save: it runs the same blob merge and triggers the same Visualizer push.

*Alternative considered*: per-field revert icons. Rejected as UI noise for a rare action; section-level revert covers the "roaster page said X, I trust the catalog again" case.

### 2c. URL pull for manual bags: bag-keyed images + AI "Get info"

Added at verification time on user request. Two parts:

**Images**: the photo cache key generalizes from "canonical id" to "canonical id, or `bag-<rowid>` for unlinked bags with a `link`". `ensureBagImage` skips the canonical URL-recovery branch for `bag-` keys (nothing to re-search); BagCard/popup derive the key, the editor warms it on save (edit mode) or after creation (the row id is the key, so create mode hooks `bagCreated`).

**"Get info from page"**: mirrors Visualizer's scraper pipeline (verified from `app/lib/coffee_bag_scraper.rb`): fetch the page following redirects → drop `script/style/svg/img` → strip tags, squish, cap 20k chars (`BeanBaseClient::fetchPageText`/`extractPageText`) → LLM extraction → JSON → fill **empty fields only**. Extraction runs on the user's configured AI provider via a dedicated `AIManager::extractCoffeeBagDetails()` path with its own `bagDetailsExtracted/-Failed` signals — deliberately NOT `analyze()`/`recommendationReceived`, which the advisor page and MCP listen to (extraction JSON must never surface as an advisor recommendation). The response parse (`parseBagExtraction`, fence-tolerant, key-whitelisted) is static and unit-tested. No Crawlbase-style proxy fallback: a blocked page is a visible failure. The og:description-only alternative was rejected with evidence — the example Shopify page's `og:description` is shipping boilerplate.

### 3. Blob merge, not blob replace

On save the dialog merges edited fields into the existing blob (preserving `id`, `visualizerCanonicalId`, `canonicalRoasterId`, the `canonical` snapshot, `description`, legacy `image`, etc.) rather than rebuilding it. Identity edits update both the bag columns (`roasterName`/`coffeeName`) and the blob's `roasterName`/`roastName` working keys so display surfaces stay consistent. Empty edited fields are removed from the blob (absent, not `""`) so the popup's zero-footprint and `fieldOrEmpty()` checks keep working. Merge happens in QML at save time (the blob is already QML lingua franca) and in C++ for the MCP path (small helper alongside `beanbase_blob.h` so both paths share key names).

### 4. Auto-push: PATCH on save when `visualizerBagId` is set; pending flag on failure

When a bag save changes any Visualizer-mapped field and the bag carries a non-empty `visualizerBagId` (and Visualizer credentials exist), the app sends `PATCH /api/coffee_bags/{id}` with the **full mapped body** (all fields, current values — the server treats the request as authoritative; this is deliberate last-writer-wins, distinct from the upload-time `fillBlank` enrichment which stays blank-fill-only for *unedited* sync):

| blob/bag field | `coffee_bag[...]` param |
|---|---|
| coffeeName | name |
| roastDate / roastLevel | roast_date / roast_level |
| frozenDate / defrostDate | frozen_date / defrosted_date |
| notes | notes |
| origin | country |
| region / farm / variety / elevation | region / farm / variety / elevation |
| producer | farmer |
| process | processing |
| harvest | harvest_time |
| qualityScore / placeOfPurchase | quality_score / place_of_purchase |
| tastingNotes | tasting_notes |
| link | url |
| beanBaseId | canonical_coffee_bag_id |

**Much of this is already shipped** (discovered at apply time): `updateBagOnVisualizer()` fires on `CoffeeBagStorage::bagVisualizerFieldsChanged` (gated by `touchesVisualizerFields`, CM Active, `visualizerBagId`, auto-update toggle), sends the full-value body via `addBagDescriptiveFields()`, and handles roaster renames by find-or-create re-resolution re-pointing `roaster_id`. The remaining delta: add `url`/`farm`/`quality_score`/`place_of_purchase` to both bodies, the `visualizer_sync_pending` retry flag, and 422 surfacing. Coffee **name** changes push as `coffee_bag[name]`, subject to the server's name+roast_date uniqueness (422 handled below). A Revert save pushes like any other edit.

**Failure handling**: a new `visualizer_sync_pending` boolean column on `coffee_bags` is set when the PATCH fails (offline, 5xx, 429) and cleared on success. The existing upload-cycle bag sync re-attempts pending bags (event-driven, no timers). A 403 (premium lapsed / CM off) clears the pending flag and re-arms the existing CM probe state instead of retrying forever; a 422 (name+roast_date collision, defrost-before-frozen) surfaces a non-blocking toast and clears the flag — local data stays as edited, server keeps its version.

*Why a column and not in-memory*: edits on a tablet that then sleeps offline are the normal case; losing the dirty bit would silently diverge the two systems, which is the exact bug this feature removes.

Bags **without** a `visualizerBagId` get nothing new at edit time — the shipped upload-time find-or-create covers them, with its create/enrich body extended to send `url`, `farm`, `quality_score`, `place_of_purchase`.

### 5. MCP surface

`bag_update` gains the detail parameters (`origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, `link`) merged into the blob via the shared C++ helper, and routes through `requestUpdateBag()` so the same auto-push fires. Bag responses (`bag_list`, `bag_update` echo) emit the parsed detail fields (human-readable, per MCP conventions). Register-stub duplicates in `tst_mcpserver_*` / `tst_mcptools_*` must be kept in sync.

## Risks / Trade-offs

- [Last-writer-wins overwrites edits made on visualizer.coffee] → Accepted: push is one-way by design; the app never claims to mirror server edits. The PATCH body is built from the bag row at save time, so it can only send what the user just confirmed in the editor.
- [422 name collision on rename (name unique per roaster+roast_date)] → Toast the server message, keep local value, clear pending. No retry loop.
- [Server refreshes linked shots' bean fields when name/roast_date/roast_level change] → Desirable: matches what Decenza's own re-upload would do; no extra work.
- [Edited identity diverges from the canonical entry while the badge still shows "linked"] → Intended: the link is a provenance/dedup anchor, not a claim of equality (Visualizer's editor behaves identically); Revert is the escape hatch, and unlink remains available.
- [Blob-with-empty-id breaks a consumer that assumed blob ⇒ linked] → `isLinked` is the single C++ definition and QML mirrors `bean.id !== ""`; audit the few `beanBaseJson` readers (BagCard, popup, uploader, advisor, MCP) during implementation — they read fields, not link state, except the uploader's canonical-id use which already goes through `canonicalId()`.
- [PATCH storm from write-through setters (every dose/grind change writes the bag row)] → Auto-push triggers only from the bag **editor save** and MCP `bag_update`, not from `writeThroughToBag()` field writes; grinder/dose fields aren't Visualizer-mapped anyway.
- [Premium gating: user without premium has no `visualizerBagId`] → Push path is naturally dormant; no new setting needed.

## Migration Plan

One SQLite migration adding `visualizer_sync_pending INTEGER DEFAULT 0` to `coffee_bags` (next schema version in `shothistorystorage.cpp`). No blob migration: new keys are additive, absent keys read as empty everywhere. Rollback-safe: older builds ignore the column and unknown blob keys.

## Open Questions

- None blocking. (Verified against the live openapi.yaml: all listed `coffee_bag[...]` params exist; writes premium-gated at 403.)
