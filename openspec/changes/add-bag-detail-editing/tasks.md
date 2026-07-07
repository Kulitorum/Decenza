# Tasks: add-bag-detail-editing

## 1. Storage & blob foundation

- [x] 1.1 Add `visualizer_sync_pending` column to `coffee_bags`: bump schema version in `src/history/shothistorystorage.cpp` (new migration), add the field to the `CoffeeBag` struct and `kCols` in `src/history/coffeebagstorage.{h,cpp}` (insert/load/update), verify backup restore + device transfer carry it
- [x] 1.2 Add the new blob keys and a shared merge helper next to `src/network/beanbase_blob.h`: `mergeBeanDetails(blobJson, QVariantMap edits)` — preserves link keys (`id`, `visualizerCanonicalId`, `canonicalRoasterId`, `canonical`, `description`, `image`), applies identity edits to working `roasterName`/`roastName`, removes keys for empty values; editable key list: `roasterName, roastName, degree, origin, region, farm, producer, variety, elevation, process, harvest, qualityScore, placeOfPurchase, tastingNotes, link`
- [x] 1.3 Pristine snapshot support: store the picked entry as a `canonical` sub-object at link time; lazy-capture on first edit-save for legacy linked blobs (`id` present, no `canonical`); `revertToCanonical(blobJson)` helper restoring canonical-supplied values and removing working detail keys canonical lacked
- [x] 1.4 Unit tests: merge helper (preserve link keys, remove-on-empty, new keys, identity edit), snapshot lazy-capture, revert (restore + removal of user-added URL), migration adds column with default 0, blob-without-`id` keeps `isLinked` false

## 2. Bag editor UI (ChangeBeansDialog)

- [x] 2.1 Add a collapsible "Bean details" section to the form (create + edit modes) in `qml/components/ChangeBeansDialog.qml`: `StyledTextField`s for URL, Origin, Region, Farm, Producer, Variety, Elevation, Process, Harvest, Quality score, Place of purchase, Tasting notes — always enabled, prefilled from the blob; collapsed header shows the `origin · variety · process` summary when values exist; new translation keys (`Tr` pattern) for Farm/Quality score/Place of purchase/URL, reuse existing detail-field keys
- [x] 2.2 Remove lock-while-linked from the bag form: identity (roaster, coffee name), roast level, and all detail fields editable while linked; replace the read-only "canonical attributes confirmation" with prefilled editable fields; keep the "verified" treatment as a badge marking the link
- [x] 2.3 On save, merge edited fields into `fBeanBaseData` (QML-side merge mirroring the C++ helper's rules, incl. lazy `canonical` snapshot capture) before `requestUpdateBag()`/create; keep the link intact on any edit
- [x] 2.4 Add "Revert to Bean Base data" action, visible only when linked AND working values differ from the `canonical` snapshot, with a confirmation stating local edits (incl. user-added URL) are discarded; revert persists like a save and triggers the Visualizer push
- [x] 2.5 When a URL was added/changed, trigger `ensureBagImage()` re-resolution for the bag (clear the once-per-session guard for that canonical id / bag)
- [x] 2.6 Render the new keys (`farm`, `qualityScore`, `placeOfPurchase`) in `qml/components/BeanBaseDetailsPopup.qml` grid (zero footprint when absent); confirm `BagCard` attribute line + popup work for an unlinked bag with details
- [x] 2.7 Accessibility pass on the new section (roles, names, focusable, onPressAction per ACCESSIBILITY.md) and fix any pre-existing violations touched

## 3. Visualizer edit-push

- [x] 3.1 In `src/network/visualizeruploader.cpp`, add `pushBagEdit(const CoffeeBag&)`: full-value `PATCH /api/coffee_bags/{visualizerBagId}` body per the design mapping table (blob→`coffee_bag[...]`, incl. `country`, `farmer`, `processing`, `harvest_time`, `quality_score`, `place_of_purchase`, `tasting_notes`, `url`, `canonical_coffee_bag_id`; no roaster name)
- [x] 3.2 Wire triggers: bag editor save and MCP `bag_update` (via `requestUpdateBag()` completion) fire the push when `visualizerBagId` non-empty and credentials exist; write-through setter path (`writeThroughToBag`) must NOT fire it
- [x] 3.3 Failure handling: set `visualizer_sync_pending` on network/429/5xx; clear + reset CM state to `UNKNOWN` on 403; clear + one non-blocking toast with server message on 422; clear on 200
- [x] 3.4 Retry: upload cycle re-pushes bags with `visualizer_sync_pending` set (same full-body PATCH) before/alongside the existing bag sync; clears the flag on success
- [x] 3.5 Extend the upload-time find-or-create/enrich body (`buildBagEnrichBody()` / create POST) with `url`, `farm`, `quality_score`, `place_of_purchase` (fillBlank semantics unchanged there)
- [x] 3.6 Tests: PATCH body mapping (all fields, empty-omission) via the now-static `addBagDescriptiveFields` + enrich-body coverage for the new fields; pending-flag column round-trip covered in migration tests. (The 403/422/retry network outcomes have no automated harness — the uploader's CM endpoints hard-code visualizer.coffee with no test seam; verified by code review + manual test instead.)

## 4. MCP

- [x] 4.1 Extend `bag_update` in `src/mcp/mcptools_write.cpp` with the detail params (merge via the shared C++ helper; empty string clears a key); route through `requestUpdateBag()` so the edit-push fires; echo details in the response
- [x] 4.2 Emit stored detail fields in `bag_list` responses
- [x] 4.3 Register-stub duplicates unchanged (registerWriteTools signature untouched); added `bagUpdateMergesDetailFieldsIntoBlob` in tst_mcptools_write (update+echo, clear-key, identity mirror, no-blob-conjuring); built `--target all`

## 5. Verification & docs

- [x] 5.1 Full test suite green (61/61 via ctest); compile check via Qt Creator MCP
- [x] 5.2 Update `docs/CLAUDE_MD/BEAN_BASE.md` (blob vocabulary table: new keys + "blob valid without id"; UI rules: details editable, lock scope narrowed to identity; edit-push section) and `docs/CLAUDE_MD/VISUALIZER.md` if touched
- [ ] 5.3 Ask Jeff to launch the app and verify: edit linked bag details, add URL to a manual bag (image resolves), confirm the Visualizer bag updates on visualizer.coffee, offline edit syncs after next upload

## 6. URL pull for manual bags (scope added during verification)

- [x] 6.1 Bag-keyed image cache: `bag-<rowid>` key for unlinked bags in BagCard/BeanBaseDetailsPopup/ChangeBeansDialog (edit-save refresh + post-create warm); `ensureBagImage` skips canonical URL recovery for `bag-` keys; colliding test id renamed
- [x] 6.2 `BeanBaseClient::fetchPageText`/`extractPageText` (Visualizer-scraper-equivalent HTML→text reduction, redirects, 20k cap) + unit test
- [x] 6.3 `AIManager::extractCoffeeBagDetails` with dedicated `bagDetailsExtracted/-Failed` signals (no advisor cross-talk) + fence-tolerant whitelisted `parseBagExtraction` + unit test
- [x] 6.4 "Get info from page" button + status line in the Bean details section; fills empty fields only; a11y announcements; hidden without URL/AI
