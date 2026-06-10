## 0. Prerequisites & schema groundwork (cross-change coordination)

- [ ] 0.1 Confirm with the `add-shot-metadata-capture` change owner whether that change will own the DYE schema additions (`origin`, `region`, `variety`, `process`, `producer`, `minElevationM`, `maxElevationM`, `tastingTags`, `tastingNotes`, `productUrl`, `imageUrl`, `roasterWebsite`, `beanType`, `generalTags`) plus `beanBaseId` + `beanBaseRoasterId`. If yes, mark Tier 2.4 as blocked-on; if no, absorb those tasks into this change under a new section 2A. **(Still open — needs the change owner; blocks Section 4.)**
- [~] 0.2 Bean Base API base URL + auth confirmed empirically (June 2026): base `https://loffeelabs.com/api/v2`, `Authorization: Bearer <key>` (or `?api_key=`), `/beans` 401s without a key, `/roasters|/origins|/varieties|/processes` public. Recorded in `design.md` § Context (item 5). **id format still unverified** — needs one authenticated `GET /beans?id=…` with a real key (no key available this session); user guide says "numerical". Store as opaque string until confirmed (already the plan in Section 4).
- [ ] 0.3 Confirm the Visualizer `coffee_bag` upsert endpoint and method. Field names confirmed from Visualizer's `coffee_bag_form_controller` (`coffee_bag[canonical_coffee_bag_id]`, `[canonical_roaster_id]`, etc.) and recorded in `design.md`; the create/update HTTP endpoint itself still needs probing against `visualizeruploader.cpp` + an authenticated request. Blocks Section 7.

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
- [x] 2.5 Signup link added (`Qt.openUrlExternally("https://loffeelabs.com/bean-base")`).
- [x] 2.6 RESOLVED without a Flickable: the sibling right card (upload settings) carries ~6 comparable rows and ships with no Flickable, so target devices accommodate cards of this height. Matching that avoids inconsistency and a ~270-line re-indent. The `Item { Layout.fillHeight: true }` spacer still pins the Visualizer signup link to the card bottom. Flag for on-device check on the very shortest tablets if a regression is reported.
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

- [ ] 5.1 Create `qml/components/BeanBaseSearchBar.qml` as a reusable widget exposing:
  - `property string label: "Search Loffee Labs Bean Base"` (default; not translated to preserve the verbatim Loffee Labs branding the way Visualizer renders it)
  - `property bool linked: false`
  - `property string linkedLabel: ""` (e.g., "Buenos Aires Caturra · Prodigal Coffee")
  - `property string linkedUrl: ""` (the bean's `link` field)
  - `signal entrySelected(var entry)` — fires with the full `BeanBaseEntry` JSON.
  - `signal unlinkRequested()`
- [ ] 5.2 Render layout:
  - Section header label
  - Search `StyledTextField`
  - Dropdown `ListView` rendering each result as "RoastName · RoasterName" (Visualizer's exact format)
  - When `linked`, hide the dropdown; show a small "✓ Linked" inline indicator + `[Unlink]` button + `🔗` link icon that opens `linkedUrl` via `Qt.openUrlExternally`.
- [ ] 5.3 Wire keystrokes to `BeanBaseClient.searchBeans()` via the debounce timer. Show a small spinner overlay when a request is in flight or queued.
- [ ] 5.4 Tapping a dropdown row emits `entrySelected(entry)` and collapses the dropdown.
- [ ] 5.5 Typing while in `linked` state automatically transitions to "search mode" (emits `unlinkRequested()` and re-opens the dropdown). This matches Visualizer's `canonical_selector_controller` behavior.
- [ ] 5.6 Accessibility: search field is `Accessible.role: TextField` with name "Search Loffee Labs Bean Base"; dropdown rows are `AccessibleButton`-equivalent with `accessibleName` of the row text.
- [ ] 5.7 Component test (Qt Quick test or screenshot) verifying empty / typing / dropdown / linked states.

## 6. Tier 2 — Integrate `BeanBaseSearchBar` into BeanInfoPage

- [ ] 6.1 Add `BeanBaseSearchBar` to the top of the **Bean** section in the right-hand `fieldsGrid` of `qml/pages/BeanInfoPage.qml`, above the Roaster + Coffee row.
- [ ] 6.2 Bind `visible: Settings.beanbase.beanBaseApiKey.length > 0 || hasBeanBaseLink` where `hasBeanBaseLink := Settings.dye.beanBaseId.length > 0`. (If both false, render nothing — matches today.)
- [ ] 6.3 Render mode logic per the three-state matrix (no-key/no-link → nothing; no-key/link → static "✓ Linked" pill; key/no-link → live search; key/link → search + linked indicator).
- [ ] 6.4 On `entrySelected(entry)`:
  - Set `Settings.dye.dyeBeanBrand = entry.roasterName`, `dyeBeanType = entry.roastName`, and `dyeRoastLevel = entry.degree` only when `degree` is non-empty (empty pulled values leave the field unchanged and editable).
  - Set `Settings.dye.beanBaseId = entry.id`, `beanBaseRoasterId = entry.roasterId`, plus the cached attribute fields.
  - Do **not** touch `dyeRoastDate`.
  - Trigger the `↑` suggestion arrows on Roaster + Coffee to hide (e.g., `Settings.dye.beanBaseId` non-empty → `suggestionArrow.visible = false`).
- [ ] 6.5 Render Roaster, Coffee, AND Roast level controls locked with a subtle "verified" tint when linked AND the matched entry supplied a non-empty value for that field (lock condition: `linked && pulledValueNonEmpty` — e.g. an entry with no `degree` leaves Roast level editable so the user can fill the gap). Unlinked beans keep today's fully-editable free-text experience with zero added friction (most beans are not in Bean Base).
- [ ] 6.6 On `unlinkRequested()`: clear `Settings.dye.beanBaseId` + `beanBaseRoasterId` + cached attribute fields. Roaster + Coffee field values are *retained* (do not clear) so the user can edit them freely.
- [ ] 6.7 If a Bean Base entry has `image`, show its thumbnail in the bean preset list (left column) for matching presets. (See task 4.x for storing `imageUrl` on the preset.)
- [ ] 6.8 In edit mode (`isEditMode`), keep the search bar visible — retro-linking a historical shot is a deliberate use case, and so is FIXING a wrong link ("forgot to change the bean"). Re-link/unlink in edit mode updates the edited shot's `beanbase_json` snapshot (and visible bean fields) for that shot only; current DYE session state is untouched. The shot-metadata update path (`requestUpdateShotMetadata`) must carry the snapshot.
- [ ] 6.9 Translation keys: `beaninfo.beanbase.linked`, `beaninfo.beanbase.unlink`, `beaninfo.beanbase.openUrl`, `beaninfo.accessibility.searchBar`.
- [ ] 6.10 Accessibility focus order: search bar comes before Roaster field; when linked, "Unlink" button is reachable before Roaster.

## 7. Tier 3 — Visualizer bag linkage

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

## 8. Tier 4 — AI advisor bean attribute enrichment

- [ ] 8.1 Extend `DialingBlocks::buildCurrentBeanBlock()` (in `src/mcp/mcptools_dialing.cpp`) to include a `beanBaseAttributes` sub-object when the active preset has a `beanBaseId`: `{ origin, region, variety, process, degree, beanType, elevationM, tastingTags, generalTags, tastingNotes, productUrl }`.
- [ ] 8.2 Verify the new block survives `enrichUserPromptObject` and is rendered into the system prompt as structured data (not stringified).
- [ ] 8.3 Add a `bean_base_search` MCP tool in `src/mcp/mcptools_ai.cpp` (or a new `mcptools_beanbase.cpp` file):
  - Args: `query` (string, required), `limit` (int, default 10, max 25), `roaster` (optional anchored filter).
  - Returns: `QList<BeanBaseEntry>` JSON-encoded.
  - Uses the same `BeanBaseClient` instance (with same rate-limit harness).
  - Rejects gracefully with a structured error if `Settings.beanbase.beanBaseApiKey` is empty.
- [ ] 8.4 Document `bean_base_search` in `docs/CLAUDE_MD/MCP_SERVER.md` under the read-tools section. Note the rate-limit characteristics so advisor prompts don't spam it.
- [ ] 8.5 Integration test: with a mock Bean Base server, the advisor receives the attribute block for a linked preset and free-text fallback otherwise.

## 9. Documentation & cross-references

- [ ] 9.1 Update `docs/CLAUDE_MD/AI_ADVISOR.md` § "Bean Data Enrichment" (lines 384–432): mark Bean Base integration as in-progress (with reference to this change), keep the Visualizer-linkage paragraph accurate (correct the "ids match" assumption — they don't; Visualizer stores Bean Base ids verbatim in its `canonical_coffee_bag_id` column).
- [ ] 9.2 Add a new `docs/CLAUDE_MD/BEAN_BASE.md` covering: API endpoints, free-tier rate limits, the `BeanBaseClient` rate-limit + cache harness, the field-mapping table, the linked-state matrix, and the Visualizer upload linkage shape.
- [ ] 9.3 Link the new doc from the table in `CLAUDE.md` ("Reference Documents" section).
- [ ] 9.4 If the tab is renamed to "Cloud" in a future change, this doc gets updated then — not now.

## 10. Verification

- [ ] 10.1 Manual: with no key → BeanInfoPage looks identical to today; Settings shows the new section with a working "Get free key" link.
- [ ] 10.2 Manual: with a valid key → typing "prodigal espress" in the search bar yields a Visualizer-like dropdown; tapping a result populates Roaster + Coffee + Roast level and shows "✓ Linked".
- [ ] 10.3 Manual: with an invalid key → Test Key shows "Invalid API key"; search bar still visible but every search shows a graceful error toast.
- [ ] 10.4 Manual: after linking → upload to Visualizer; verify in the rendered shot page that the coffee_bag shows the canonical-bag verified state with the right roaster + name + URL.
- [ ] 10.5 Manual: clear the API key while a preset is linked → the linked-state indicator persists, search bar disappears. Re-enter the key → search bar returns; existing link unchanged.
- [ ] 10.6 Manual: AI advisor with a linked preset → confirm in MCP debug logs that the `beanBaseAttributes` block is included in the prompt.
- [ ] 10.7 Manual: AI advisor calls `bean_base_search` → response comes back within rate-limit budget, results are usable.
- [ ] 10.8 Manual: backup/restore round-trip → API key, beanBaseId on presets, and cached attributes all survive.
