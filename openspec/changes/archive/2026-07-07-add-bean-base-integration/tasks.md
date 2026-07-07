## 0. Prerequisites & schema groundwork (cross-change coordination)

- [x] 0.1 OBSOLETE (2026-07-07): the snapshot-blob design (shots.beanbase_json, migration 18) replaced per-field DYE columns; add-shot-metadata-capture never started. Original: Confirm with the `add-shot-metadata-capture` change owner whether that change will own the DYE schema additions (`origin`, `region`, `variety`, `process`, `producer`, `minElevationM`, `maxElevationM`, `tastingTags`, `tastingNotes`, `productUrl`, `imageUrl`, `roasterWebsite`, `beanType`, `generalTags`) plus `beanBaseId` + `beanBaseRoasterId`. If yes, mark Tier 2.4 as blocked-on; if no, absorb those tasks into this change under a new section 2A. **(Still open — needs the change owner; blocks Section 4.)**
- [x] 0.2 RESOLVED with the user's real key (June 2026): `id` is a JSON number (stored as opaque string, as planned). Live payload wraps results as `{"meta":{…},"beans":[…]}` — NOT `data` — which exposed and fixed a parser bug. Free tier does NOT expose `image`/`tasting-tag`/`general-tag`/`soldout`/`available` (silently dropped even via `fields=`): bag photos unavailable from the API today (UI collapses gracefully; ask Loffee Labs about tier gating), tag chips dormant, `tasting` comma-string carries the flavor list. Full shape in `design.md` § Context item 6.
- [x] 0.3 RESOLVED from the open-source `miharekar/visualizer` repo (design.md § Context 9): full CRUD at `/api/coffee_bags` (+`/api/roasters`), Basic auth, bag WRITES are premium-gated; `canonical_coffee_bags.id` is a Visualizer UUID with the Bean Base integer in `loffee_labs_id` (no API lookup exists — canonical linkage needs a small upstream addition); shot PATCH accepts `shot[canonical_coffee_bag_id]` for all users and `shot[coffee_bag_id]` with coffee management enabled (server auto-fills bean fields from the bag). Section 7 re-scoped accordingly.

## 1. Tier 1 — Settings: `SettingsBeanBase` domain

- [x] 1.1 Create `src/core/settings_beanbase.h` with `SettingsBeanBase : QObject`. Add `Q_PROPERTY(QString beanBaseApiKey READ beanBaseApiKey WRITE setBeanBaseApiKey NOTIFY beanBaseApiKeyChanged)`. Keep the include footprint tight (no `settings.h` cross-include) per CLAUDE.md.
- [x] 1.2 Implement `src/core/settings_beanbase.cpp` with QSettings-backed get/set using key `"beanbase/apiKey"`. Empty string when unset.
- [x] 1.3 Add `SettingsBeanBase* beanbase()` accessor to `Settings` and expose as `Q_PROPERTY(QObject* beanbase READ beanbaseQObject CONSTANT)` (QObject* return like the other domains, per settings.h convention). Settings owns the instance. Also added to CMakeLists (main, saw_parity, tests CORE_SOURCES).
- [x] 1.4 Register `qmlRegisterUncreatableType<SettingsBeanBase>(...)` in `src/main.cpp` so QML reads `Settings.beanbase.beanBaseApiKey` without resolving to `undefined`.
- [x] 1.5 Serialize / deserialize `beanBaseApiKey` in `src/core/settingsserializer.cpp`. Added to `sensitiveKeys()` and gated behind `includeSensitive` (like the Visualizer password and AI keys), so the key is only in backups that include sensitive data.
- [x] 1.6 ~~Expose via MCP~~ — DEVIATION: the established convention for sensitive credentials (Visualizer password, MQTT password, all AI API keys) is to **exclude** them from `settings_get`/`settings_set` entirely. `settings_set`'s own description says "API keys and passwords are excluded (sensitive)." Followed that convention: the Bean Base key is NOT exposed via MCP. No log statements reference the key value, satisfying the masking requirement vacuously.
- [x] 1.7 Factory reset: `beanbase/apiKey` lives in the primary `("DecentEspresso","DE1Qt")` store, which `Settings::factoryReset()` already wipes via `m_settings.clear()`. `SettingsBeanBase` holds no in-memory cache (reads QSettings live), so no `invalidateCache()` hook is needed.
- [x] 1.8 Unit test: added `beanBaseApiKeyDefaultIsEmpty`, `beanBaseApiKeyRoundTrip`, `beanBaseApiKeySignalEmitted`, and `beanBaseApiKeyExcludedFromNonSensitiveExport` to `tests/tst_settings.cpp`.

## 2. Tier 1 — Settings UI: Bean Base section

- [x] 2.1 Added a `Theme.borderColor` divider (the project convention, used in SettingsAITab/SettingsCalibrationTab — there is no `Theme.dividerColor`) and a "Bean Base" section header in the left card, above the `Item { Layout.fillHeight: true }` spacer.
- [x] 2.2 Added the description Tr (`settings.beanbase.description`).
- [x] 2.3 API key `StyledTextField` bound to `Settings.beanbase.beanBaseApiKey`, `echoMode` toggled by `showBeanBaseKey`. DEVIATION: used a text "Show/Hide" toggle instead of a `[👁]` glyph — CLAUDE.md forbids Unicode glyphs as icons. Toggle is an `AccessibleMouseArea` with proper accessible name; field has `accessibleName`.
- [x] 2.4 `[Test Key]` button calls `MainController.beanbase.testApiKey()`; a `Connections { onApiKeyTestResult }` handler maps status→localized message colored success/error. Used distinct `beanBaseTestMessage`/`beanBaseTestSuccess` page properties. The `testApiKey()` backend is the minimal `BeanBaseClient` (see Section 3 note).
- [x] 2.5 Signup link added (`Qt.openUrlExternally("https://loffeelabs.com/developers")`).
- [x] 2.6 Fixed after on-device report: the left card DID overflow (API key unreachable) — wrapped the card column in a Flickable (SettingsAITab pattern) with `targetFlickable` registered on the KeyboardAwareContainer; Visualizer sign-up link moved up beside its account block since the fillHeight spacer collapses inside a Flickable column. (Initial "no Flickable needed" call was wrong on hardware.)
- [x] 2.7 Translation fallbacks are inline (the project has no separate English JSON — `Tr`/`TranslationManager.translate` carry the source string as the fallback). All keys used: `settings.beanbase.section`, `.description`, `.apiKey`, `.apiKeyPlaceholder`, `.showKey`/`.hideKey`, `.testKey`, `.testing`, `.testSuccess`, `.testInvalid`, `.testRateLimited`, `.testNetworkError`, `.signupLink`, plus `common.button.show`/`common.button.hide`. Also indexed the section in `SettingsSearchIndex.js` so the key is findable via settings search.

## 3. Tier 2 — Bean Base HTTP client

> **Partial — built during Tier 1.** `src/network/beanbaseclient.{h,cpp}` exists with the constructor + `testApiKey()` (used by the Settings UI Test Key button), wired into `MainController` as `MainController.beanbase` (Q_PROPERTY), and registered in CMakeLists. The base URL constant (`https://loffeelabs.com/api/v2`), `Authorization: Bearer` header, and live key read from `Settings.beanbase` are in place. Remaining below: `searchBeans`/`lookupBean`, the debounce + 3 s rate-limit queue, the response cache, and the `BeanBaseEntry` struct.

- [x] 3.1 `src/network/beanbaseclient.h/.cpp` complete. DEVIATION from the callback signature: QML can't pass `std::function`, so the API is `Q_INVOKABLE search(QString)` + `searchResults(query, entries)` / `searchFailed(query, status)` signals (query echoed back so consumers drop stale results). `lookupBean(id)` deferred to when Tier 2 UI needs rehydration — `search()` covers the search bar. `setBaseUrl()` added as the test seam.
  - `QNetworkAccessManager`-backed (use existing `MainController` instance via DI or its own).
  - Methods: `searchBeans(QString query, std::function<void(QList<BeanBaseEntry>)> callback)`, `lookupBean(QString id, ...)`, `testApiKey(std::function<void(bool, QString)>)`.
  - Authorization header `Authorization: Bearer <key>` pulled from `Settings.beanbase.beanBaseApiKey`.
  - Base URL constant (config file or constexpr).
- [x] 3.2 Debounce (800 ms single-shot) + 3 s sent-gap cooldown implemented; queued query is latest-wins (replaced, never accumulated); in-flight reply aborted when superseded so stale responses can't land out of order. Timer use justified in a header comment: the API's wall-clock rate window is genuinely temporal (like polling/heartbeats), not an event-suppression guard.
- [x] 3.3 Session cache `QHash<QString, QVariantList>` keyed by trimmed+lowercased query. App-session lifetime, no persistence.
- [x] 3.4 DEVIATION: entries are `QVariantMap`s (QML-friendly, what the search bar binds to) rather than a C++ struct — same field set: id (opaque string), roasterName, roastName, degree, beanType, link, image, origin, region, producer, variety, process, minElevationM, maxElevationM, tastingTags, tastingNotes, generalTags, roasterRegion, roasterCountry, harvest, description, soldout, available. `roasterId` omitted: not present in the documented bean export fields (design.md open question on canonical_roaster_id sourcing stands).
- [x] 3.5 `parseBeans()` (static, public, unit-testable): tolerates `{"data":[…]}` or bare array; id as number or string; elevations numeric or string; tags as JSON array or comma-joined string; non-object entries skipped; garbage → empty list.
- [x] 3.6 `testApiKey()` → `GET /beans?limit=1`; 200→success, 401→invalid, 429→ratelimited, transport→network, empty key→missing (synchronous, no request).
- [x] 3.7 Wired as `MainController.beanbase` Q_PROPERTY. Gotcha hit + fixed: Qt 6.11 moc requires the complete type for pointer Q_PROPERTYs — header is included in maincontroller.h (like the other clients), not forward-declared.
- [x] 3.8 `tests/tst_beanbaseclient.cpp` with a canned-response `FakeBeanBaseServer` (QTcpServer): debounce coalesces rapid keystrokes into one request (final query only); second search inside the 3 s window is parked then sent after the window clears; case-insensitive cache hit emits synchronously with zero new requests; parse robustness (full entry, missing-field defaults, bare array, garbage). NOTE for future test authors: no raw string literals in moc'd test files — moc miscounts braces inside `R"(...)"` and silently drops classes declared after one (vtable link error).
- [x] 3.9 `testApiKey()` tests: 200→success, 401→invalid, connection-refused→network (instant, no slow timeout wait), empty key→missing with zero requests sent. All 13 tests green via Qt Creator run_tests (44/44 with tst_Settings).

## 4. Tier 2 — DYE schema additions for Bean Base attributes

> **RESOLVED 0.1 by design change (user decision, June 2026): single-blob schema in THIS change** — `add-shot-metadata-capture` keeps its scope for user-editable fields. The cached Bean Base payload is one compact-JSON blob (`beanBaseData`), not 14 individual fields: both consumers (shot snapshot → Visualizer upload, MCP/advisor) read it as a unit, the blob survives payload-shape surprises, and preset rows already flow to QML as variant maps so nested blob fields are QML-readable for free.

- [x] 4.1 Preset rows gain `beanBaseId` (opaque string), `beanBaseRoasterId`, `beanBaseData` (compact-JSON blob with origin/variety/process/image/link/tags/etc. inside). Additive to the existing presets JSON array — no migration needed; missing = unlinked.
- [x] 4.2 BLOB INSTEAD OF PER-FIELD: `SettingsDye` gains `dyeBeanBaseId` / `dyeBeanBaseRoasterId` / `dyeBeanBaseData` (sticky QSettings-backed Q_PROPERTYs) + `clearBeanBaseLink()` invokable. Serialized in settingsserializer (dye block). PLUS the per-shot snapshot path: `ShotMetadata.beanBaseJson` → `ShotSaveData.beanBaseJson` → new `shots.beanbase_json` column (migration 18) → `ShotRecord.beanBaseJson` → `ShotProjection.beanBaseJson` (sparse-emit). All three MainController save sites wired. `updateShotMetadataStatic` fieldMap carries `beanBaseJson` so edit mode can fix/clear a historical shot's snapshot.
- [x] 4.3 `addBeanPreset`/`updateBeanPreset` mirror the live DYE link when the saved identity matches the active bean (including clearing after Unlink); `updateBeanPreset` preserves the row's existing link otherwise (rename-of-inactive-preset case). `applyBeanPreset` copies the preset's link into DYE state — an unlinked preset clears any leftover link. `recomputeBeansModified` counts a link change as modified (id compared; data travels with id).
- [x] 4.4 Added `findBeanPresetByBeanBaseId()` (new Q_INVOKABLE — kept `findBeanPresetByContent` signature unchanged; UI tries id first, falls back to brand+type).
- [x] 4.5 Unit tests: `dyeBeanBaseLinkRoundTripAndClear`, `beanPresetCarriesAndAppliesBeanBaseLink`, `updateBeanPresetPreservesLinkWhenNotActiveBean` in tst_settings.cpp (with init/cleanup save-restore of the new sticky fields).

## 5. Tier 2 — `BeanBaseSearchBar` QML component

- [x] 5.1 Create `qml/components/BeanBaseSearchBar.qml` as a reusable widget exposing:
  - `property string label: "Search Loffee Labs Bean Base"` (default; not translated to preserve the verbatim Loffee Labs branding the way Visualizer renders it)
  - `property bool linked: false`
  - `property string linkedLabel: ""` (e.g., "Buenos Aires Caturra · Prodigal Coffee")
  - `property string linkedUrl: ""` (the bean's `link` field)
  - `signal entrySelected(var entry)` — fires with the full `BeanBaseEntry` JSON.
  - `signal unlinkRequested()`
- [x] 5.2 Render layout:
  - Section header label
  - Search `StyledTextField`
  - Dropdown `ListView` rendering each result as "RoastName · RoasterName" (Visualizer's exact format)
  - When `linked`, hide the dropdown; show a small "✓ Linked" inline indicator + `[Unlink]` button + `🔗` link icon that opens `linkedUrl` via `Qt.openUrlExternally`.
- [x] 5.3 Wire keystrokes to `BeanBaseClient.searchBeans()` via the debounce timer. Show a small spinner overlay when a request is in flight or queued.
- [x] 5.4 Tapping a dropdown row emits `entrySelected(entry)` and collapses the dropdown.
- [x] 5.5 Typing while in `linked` state automatically transitions to "search mode" (emits `unlinkRequested()` and re-opens the dropdown). This matches Visualizer's `canonical_selector_controller` behavior.
- [x] 5.6 Accessibility: search field is `Accessible.role: TextField` with name "Search Loffee Labs Bean Base"; dropdown rows are `AccessibleButton`-equivalent with `accessibleName` of the row text.
- [x] 5.7 CLOSED won't-do (2026-07-07): no QML component-test harness exists; covered by manual verification. Original: Component test — DEFERRED: no QML component-test harness exists in tests/ today; covered by manual verification (Section 10) until one is introduced.

## 6. Tier 2 — Integrate `BeanBaseSearchBar` into BeanInfoPage

- [x] 6.1 Add `BeanBaseSearchBar` to the top of the **Bean** section in the right-hand `fieldsGrid` of `qml/pages/BeanInfoPage.qml`, above the Roaster + Coffee row.
- [x] 6.2 Bind `visible: Settings.beanbase.beanBaseApiKey.length > 0 || hasBeanBaseLink` where `hasBeanBaseLink := Settings.dye.beanBaseId.length > 0`. (If both false, render nothing — matches today.)
- [x] 6.3 Render mode logic per the three-state matrix (no-key/no-link → nothing; no-key/link → static "✓ Linked" pill; key/no-link → live search; key/link → search + linked indicator).
- [x] 6.4 On `entrySelected(entry)`:
  - Set `Settings.dye.dyeBeanBrand = entry.roasterName`, `dyeBeanType = entry.roastName`, and `dyeRoastLevel = entry.degree` only when `degree` is non-empty (empty pulled values leave the field unchanged and editable).
  - Set `Settings.dye.beanBaseId = entry.id`, `beanBaseRoasterId = entry.roasterId`, plus the cached attribute fields.
  - Do **not** touch `dyeRoastDate`.
  - Trigger the `↑` suggestion arrows on Roaster + Coffee to hide (e.g., `Settings.dye.beanBaseId` non-empty → `suggestionArrow.visible = false`).
- [x] 6.5 Render Roaster, Coffee, AND Roast level controls locked with a subtle "verified" tint when linked AND the matched entry supplied a non-empty value for that field (lock condition: `linked && pulledValueNonEmpty` — e.g. an entry with no `degree` leaves Roast level editable so the user can fill the gap). Unlinked beans keep today's fully-editable free-text experience with zero added friction (most beans are not in Bean Base).
- [x] 6.6 On `unlinkRequested()`: clear `Settings.dye.beanBaseId` + `beanBaseRoasterId` + cached attribute fields. Roaster + Coffee field values are *retained* (do not clear) so the user can edit them freely.
- [x] 6.7 DONE (2026-07-07, this archive PR): BagCard now shows a 44px bag-photo thumbnail from the snapshot blob with a beans-icon placeholder (pattern from BeanBaseDetailsRow). Original: Preset-list thumbnails (left column rows) — NOT YET: FavoritesListView rows are text-based; the bag photo IS shown in the details row + popup (6B). Revisit as polish if wanted.
- [x] 6.8 In edit mode (`isEditMode`), keep the search bar visible — retro-linking a historical shot is a deliberate use case, and so is FIXING a wrong link ("forgot to change the bean"). Re-link/unlink in edit mode updates the edited shot's `beanbase_json` snapshot (and visible bean fields) for that shot only; current DYE session state is untouched. The shot-metadata update path (`requestUpdateShotMetadata`) must carry the snapshot.
- [x] 6.9 Translation keys: `beaninfo.beanbase.linked`, `beaninfo.beanbase.unlink`, `beaninfo.beanbase.openUrl`, `beaninfo.accessibility.searchBar`.
- [x] 6.10 Accessibility focus order: search bar comes before Roaster field; when linked, "Unlink" button is reachable before Roaster.

## 5B. Tier 2 — Search quality follow-up (from live-API probing)

- [x] 5B.1 OBSOLETE (2026-07-07): loffeelabs whole-word `search=` was replaced by Visualizer canonical search (substring + multi-word native). Original: Multi-word zero-result fallback: `search=` is contiguous-substring only ("prodigal washed" → 0). When a multi-word query returns 0 results, retry with `roaster=<leading word(s)>` anchoring or longest-single-word fallback before showing "No matches". (See design.md § Context 7.)

## 5C. Tier 2/3 — Visualizer-powered lookup (from § Context 10; user-requested direction)

- [x] 5C.1 Add `visualizerCanonicalSearch(q)` to the client layer: GET `visualizer.coffee/canonical/autocomplete_coffee_bags?q=`, parse `<li>` attrs (`data-autocomplete-value` UUID, `data-roaster`, `data-coffee-bag`). No auth, substring + multi-word. Defensive parsing (internal endpoint).
- [x] 5C.2 Use it as the search bar's multi-word / no-key path (supersedes 5B.1). CONFIRMED LIVE: the two-stage flow (`autocomplete_roasters` → `autocomplete_coffee_bags?require_roaster=true&canonical_roaster_id=<uuid>`) embeds a JSON payload per result (roast_level, country, region, farmer, variety, elevation, processing, harvest_time, tasting_notes) — so Visualizer alone supplies most attributes keylessly. No loffee_labs_id is exposed (no precise Bean Base cross-lookup); Bean Base adds only link/type/description/image, via fuzzy `roaster=`+`search=` when a key exists.
- [x] 5C.3 Shot PATCH linkage: send `shot[canonical_coffee_bag_id] = visualizerCanonicalId` on uploads/updates (accepted for ALL users — no premium needed). This delivers Tier 3's cross-user clustering without the bag-CRUD premium gate.
- [x] 5C.4 SATISFIED (2026-07-07): Visualizer's official /api/canonical_coffee_bags JSON endpoint adopted (#1336); per-user Bean Base keys removed entirely. Original: Upstream ask (miharekar): JSON canonical endpoint incl. `loffee_labs_id` + display attributes — would obsolete per-user Bean Base keys entirely.

> 5C IMPLEMENTED (June 2026): `search()` is now the canonical path (keyless, 350 ms debounce, no rate floor, session cache + roaster-UUID cache); `searchBeanBase()` keeps the Bean Base contract for optional enrichment; `fetchCanonicalDetails()` does the two-stage payload fetch (emitted via `canonicalDetails`, merged into the blob by BeanInfoPage); uploader PATCH sends `shot[canonical_coffee_bag_id]` from the blob's `visualizerCanonicalId` (emit-only, never nulled — the user may have linked in Visualizer's UI). MCP tool RENAMED `bean_base_search`→`bean_search` and switched to the canonical path with per-result enrichment (top 5, 4 s grace). Search bar is now ALWAYS visible (spec revised). Caught by tests: const-ref aliasing bug (doSendCanonicalSearch cleared the member its `query` param aliased — results cached under ""). 51/51 green.

## 6B. Tier 2 — Bean details visibility (all three surfaces)

> Added per user direction (June 2026): the blob fields must be USER-VISIBLE, not
> just plumbing. One shared component, three mounts; each page passes its own
> blob source (live DYE state vs. per-shot snapshot).

- [x] 6B.1 Create `qml/components/BeanBaseDetailsPopup.qml`: takes a `beanBaseJson` string property, parses with `JSON.parse` (guarded), renders: bag image (graceful collapse on load failure), origin, region, producer, variety, process, elevation range ("1700–1850 m"), bean type (Filter/Espresso/Omni), tasting-tag chips, roaster tasting notes/description, harvest, soldout indicator when true, "View at roaster" link (opens `link` externally). All Theme-styled, translated labels, accessible (dialog role, dismiss action).
- [x] 6B.2 Create a compact `BeanBaseDetailsRow.qml` (or inline pattern): small bag-image thumbnail + one-line summary (origin · variety · process) + tap affordance opening the popup. Reused by all three pages.
- [x] 6B.3 BeanInfoPage: when linked, show the details row (with image) inside the Bean section under the search bar; tap → popup. Register new QML files in CMakeLists.txt.
- [x] 6B.4 Post-shot review page: when the shot's `beanBaseJson` is non-empty, show the details row next to the bean information; tap → popup fed from the SHOT's snapshot (not live DYE state).
- [x] 6B.5 Shot history / shot detail page: same pattern as 6B.4, fed from the loaded shot record's `beanBaseJson`.
- [x] 6B.6 All three mounts: zero footprint when blob empty (unlinked/legacy shots look exactly as today).

> Implementation notes (June 2026): components are `BeanBaseSearchBar.qml`, `BeanBaseDetailsPopup.qml`, `BeanBaseDetailsRow.qml` (all registered in CMakeLists). BeanInfoPage drives lock state via `beanBaseLocks(fieldKey)` = linked && non-empty pulled value, with `activeBeanBaseJson` switching between live DYE state and `editBeanBaseJson` in edit mode (saved through `requestUpdateShotMetadata`). Review + detail pages mount the row read-only from the shot's `beanBaseJson`. DEVIATION from spec draft: no-key+linked shows the search bar DISABLED with Unlink still available (removability never requires credentials) — spec scenario updated. KNOWN EDGE: Bean Base `degree` values "Ultra Light"/"Nordic Light" aren't in the roast-level combo model, and stored values are English while the combo model is translated — the field is locked when supplied so this only affects combo display; revisit if it surfaces.

## 7. Tier 3 — Visualizer bag linkage

> Premium detection (prerequisite, designed June 2026): `POST /api/coffee_bags` with an empty body — 401 = bad creds, 403 "must be a premium user" = not premium, 400 ParameterMissing = PREMIUM (params rejected before any write; zero side effects). Note premium ≠ coffee_management_enabled (a separate user preference gating shot[coffee_bag_id]); verify by reading the PATCH response back. Upstream nicety: ask for `premium` in `/api/me`.

> **OBSOLETE (2026-07-07).** The blocker dissolved when search moved to Visualizer canonical UUIDs natively (#1336) — no loffee_labs_id→UUID lookup needed. Bag payloads carry canonical_coffee_bag_id/canonical_roaster_id + cached attributes (visualizeruploader.cpp), and shots PATCH the canonical id directly; the fetch-user-bags dedup flow below was keyed to the removed API-key configuration trigger and is superseded by the bag-sync architecture (#1334, migration 16 queue). Tasks left unchecked as a record of the unbuilt original plan.
>
> Original re-scope: Blocked on: (a) an upstream loffee_labs_id→canonical-UUID lookup (propose/contribute to miharekar/visualizer — it's open source), and (b) bag writes being premium-only. Implementable once (a) lands: ensure-roaster → ensure-bag (full Bean Base attributes) → PATCH shot with coffee_bag_id / canonical_coffee_bag_id. Original tasks below kept for the field mapping; revise against the confirmed params when implementing.

- [ ] 7.1 Extend `VisualizerUploader::buildCoffeeBagJson()` (or whichever method serializes the bag payload — verify in `src/network/visualizeruploader.cpp` lines around the existing bag block) to include:
  - `coffee_bag[canonical_coffee_bag_id]` = `dyeBeanBaseId` if non-empty
  - `coffee_bag[canonical_roaster_id]` = `dyeBeanBaseRoasterId` if non-empty
  - `coffee_bag[url]` = `dyeProductUrl`
  - `coffee_bag[country]` = `dyeOrigin`
  - `coffee_bag[region]` = `dyeRegion`
  - `coffee_bag[variety]` = `dyeVariety`
  - `coffee_bag[process]` = `dyeProcess`
  - `coffee_bag[producer]` (or `farmer` — confirm Visualizer field name) = `dyeProducer`
  - `coffee_bag[tasting]` = `dyeTastingNotes`
- [ ] 7.2 Implement `VisualizerUploader::fetchUserCoffeeBags()` calling `GET /api/coffee_bags` with the user's session/auth, returning a `QList<VisualizerCoffeeBag>` containing `id` (UUID), `canonical_coffee_bag_id` (string), `roaster`, `name`. First call after a fresh login or fresh Bean Base key configuration.
- [ ] 7.3 Implement bag-id resolution before shot upload:
  - If `dyeBeanBaseId` is non-empty AND a fetched Visualizer bag has matching `canonical_coffee_bag_id` → use that Visualizer bag id, skip create.
  - Else if a fetched bag matches by roaster+name → use that id (warn at log level that no canonical match was found, fall back to free-text match).
  - Else POST a new coffee_bag with all the fields from 7.1 and use the returned id.
- [ ] 7.4 Persist resolved bag UUID locally on the preset so subsequent uploads skip the resolution step.
- [ ] 7.5 Integration test (`tests/`): with a mock Visualizer server, simulate (a) bag exists with matching canonical id → reused; (b) bag exists by name only → reused with log warning; (c) no bag exists → created with full canonical fields. Each path must result in exactly one POST or zero POSTs to `/api/coffee_bags`.
- [ ] 7.6 Backfill: existing presets without `beanBaseId` should NOT trigger a fetch-coffee-bags loop. Only when a Bean Base match is made for the first time do we run resolution.

## 7B. Tier 3 — MCP read/write of per-shot Bean Base snapshots

> Added per user direction (June 2026): MCP must be able to update Bean Base
> entries on historical shots. Snapshot semantics confirmed: we SAVE the data
> per shot, never just reference Bean Base's id — their `historical=true`
> Dev-tier gating implies entries get delisted/mutated, so history must not
> depend on their retention. (Bag images remain URLs into their CDN — text
> data survives deletion, photos may not; local image caching would be a
> separate deliberate feature.)

- [x] 7B.1 `shots_get_detail` / `shots_compare`: emit the snapshot as a parsed `beanBase` object (`reshapeBeanBase()` — LLMs can't reliably read double-encoded JSON strings; sparse, omitted when unlinked).
- [x] 7B.2 `shots_update`: new `beanBase` object arg — full entry replaces the shot's snapshot, `{}` clears it. Enables "fix shots recorded against the wrong bean" via MCP (copy the `beanBase` block from a correctly-linked shot, or from `bean_base_search` once Section 8 lands). Stored compact via the existing `updateShotMetadataStatic` beanBaseJson field-map entry.
- [x] 7B.3 Done in the Section 9 docs pass (MCP_SERVER.md Bean Base section).

## 8. Tier 4 — AI advisor bean attribute enrichment

- [x] 8.1 DONE (expanded per user direction to include the roaster's tasting expectations + everything advice-relevant: roasterTastingNotes, description, origin, region, producer, variety, process, roastLevel (Bean Base's richer degree string), roastedFor (Espresso/Filter/Omni), harvest, min/maxElevationM). Extend `DialingBlocks::buildCurrentBeanBlock()` (in `src/mcp/mcptools_dialing.cpp`) to include a `beanBaseAttributes` sub-object when the active preset has a `beanBaseId`: `{ origin, region, variety, process, degree, beanType, elevationM, tastingTags, generalTags, tastingNotes, productUrl }`.
- [x] 8.2 Both surfaces wired: mcptools_dialing passes sd.beanBaseJson; ShotSummary carries beanBaseJson through both summarize() (live ShotMetadata) and summarizeFromHistory() (ShotProjection), so the in-app advisor and dialing_get_context emit byte-equivalent currentBean.beanBase. Verify the new block survives `enrichUserPromptObject` and is rendered into the system prompt as structured data (not stringified).
- [x] 8.3 DONE in mcptools_ai.cpp (registerAITools already receives MainController → beanbase()). One-shot signal bridge with a guard object; query echoed for stale-filtering; status tokens mapped to user-readable errors (missing/invalid/quota/ratelimited); tool description teaches whole-word matching + the shots_update beanBase pairing + rate budget. KNOWN EDGE: a concurrent Beans-page search can supersede a pending MCP query (latest-wins) and the MCP call relies on the client-side timeout. Original: Add a `bean_base_search` MCP tool in `src/mcp/mcptools_ai.cpp` (or a new `mcptools_beanbase.cpp` file):
  - Args: `query` (string, required), `limit` (int, default 10, max 25), `roaster` (optional anchored filter).
  - Returns: `QList<BeanBaseEntry>` JSON-encoded.
  - Uses the same `BeanBaseClient` instance (with same rate-limit harness).
  - Rejects gracefully with a structured error if `Settings.beanbase.beanBaseApiKey` is empty.
- [x] 8.4 Documented in MCP_SERVER.md (new Bean Base section: bean_base_search, beanBase read/write on shot tools, key not exposed via MCP).
- [x] 8.5 OBSOLETE (2026-07-07): the advisor reads the local snapshot blob (no live Bean Base server exists to mock). Original: Integration test: with a mock Bean Base server, the advisor receives the attribute block for a linked preset and free-text fallback otherwise.

## 9. Documentation & cross-references

- [x] 9.1 Update `docs/CLAUDE_MD/AI_ADVISOR.md` § "Bean Data Enrichment" (lines 384–432): mark Bean Base integration as in-progress (with reference to this change), keep the Visualizer-linkage paragraph accurate (correct the "ids match" assumption — they don't; Visualizer stores Bean Base ids verbatim in its `canonical_coffee_bag_id` column).
- [x] 9.2 Add a new `docs/CLAUDE_MD/BEAN_BASE.md` covering: API endpoints, free-tier rate limits, the `BeanBaseClient` rate-limit + cache harness, the field-mapping table, the linked-state matrix, and the Visualizer upload linkage shape.
- [x] 9.3 Link the new doc from the table in `CLAUDE.md` ("Reference Documents" section).
- [x] 9.4 CLOSED (2026-07-07): explicit not-now by design. Original: If the tab is renamed to "Cloud" in a future change, this doc gets updated then — not now.

## 10. Verification

- [x] 10.1 OBSOLETE (key removed; search is keyless). Original: with no key → BeanInfoPage looks identical to today; Settings shows the new section with a working "Get free key" link.
- [x] 10.2 VERIFIED in production use (canonical search dropdown). Original: with a valid key → typing "prodigal espress" in the search bar yields a Visualizer-like dropdown; tapping a result populates Roaster + Coffee + Roast level and shows "✓ Linked".
- [x] 10.3 OBSOLETE (key removed). Original: with an invalid key → Test Key shows "Invalid API key"; search bar still visible but every search shows a graceful error toast.
- [x] 10.4 VERIFIED in production use (canonical id lands on Visualizer). Original: after linking → upload to Visualizer; verify in the rendered shot page that the coffee_bag shows the canonical-bag verified state with the right roaster + name + URL.
- [x] 10.5 OBSOLETE (key removed). Original: clear the API key while a preset is linked → the linked-state indicator persists, search bar disappears. Re-enter the key → search bar returns; existing link unchanged.
- [x] 10.6 VERIFIED (advisor currentBean.beanBase block ships from the snapshot). Original: AI advisor with a linked preset → confirm in MCP debug logs that the `beanBaseAttributes` block is included in the prompt.
- [x] 10.7 OBSOLETE (bean_base_search replaced by keyless bean_search MCP tool). Original: AI advisor calls `bean_base_search` → response comes back within rate-limit budget, results are usable.
- [x] 10.8 VERIFIED (backup/restore covers dye/bag storage incl. blob; key no longer exists). Original: backup/restore round-trip → API key, beanBaseId on presets, and cached attributes all survive.
