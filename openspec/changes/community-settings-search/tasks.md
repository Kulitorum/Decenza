## 1. Card (Phase 1)

- [x] 1.1 `qml/components/CommunitySuggestionsCard.qml` — parametrized (`beanBrand`/`beanType`/`roastLevel`/`isCoffee`), modelled on `ShotResultsCard.qml`, `pragma ComponentBehavior: Bound` + `required property` on the profile delegate.
- [x] 1.2 Own-history wiring: `refresh()` fires `requestRankedProfilesForBean` / `requestLatestGrindForBean` on `MainController.shotHistory`; one `Connections` consumes both `*ForBeanReady` signals with the **echoed-query stale-reply guard**.
- [x] 1.3 Display: up to 3 `withBean` profiles with epoch-seconds `lastUsed` formatted as a locale date; grind line honours `matchLevel` (`similarRoast` labelled, never shown as this bean's); empty-state caption; `similar` deliberately not surfaced.
- [x] 1.4 Deep link: `encodeURIComponent`'d bean_brand/bean_type, `min_enjoyment=70`, `Qt.openUrlExternally`.
- [x] 1.5 Visibility gate: `isCoffee && beanBrand !== "" && beanType !== ""` (collapses for tea / no bean).

## 2. Host + build

- [x] 2.1 `BeanInfoPage.qml`: card as first child of `contentColumn`, bound to `Settings.dye.*`; `activeBagIsCoffee` derivation (match active bag id in `inventoryBags`, read `kind`).
- [x] 2.2 `CMakeLists.txt`: add `qml/components/CommunitySuggestionsCard.qml` to `qt_add_qml_module`.

## 3. Upstream-clean discipline

- [x] 3.1 No `src/barista/*` dependency; edits to shared files (`BeanInfoPage.qml`, `CMakeLists.txt`) additive and minimal.

## 4. Verification

- [ ] 4.1 `ENABLE_TSNET=OFF ./build.sh --target OSX --debug --dev` green (includes the qmllint gate; a green build means lint passed).
- [ ] 4.2 Owner word-gate on the 8 PROPOSED strings (`beaninfo.community.*`); strip the `// PROPOSED` markers on approval.
- [ ] 4.3 Owner-visual on device: open Beans with a real coffee bag active → card shows your profiles/grind; the button opens the filtered Community page; a tea bag / no bean collapses the card.
- [ ] 4.4 Commit to the `backup` remote (not origin); DECISIONS.md ADR entry travels with the commit.
- [ ] 4.5 Archive this change as the PR's final commit (`openspec archive community-settings-search --yes`) — deferred until the feature's phases close or the owner elects to archive Phase 1 standalone.

## 5. Aggregator (Phase 2)

- [x] 5.1 `src/core/communityaggregator.{h,cpp}` — pure static `summarize(shots, Params{myGrinder, nowEpochSec, minEnjoyment})` → summary `QVariantMap`; Qt-core only, no `QNetwork`, no `Q_OBJECT`, no `src/barista`.
- [x] 5.2 Cleaning: leading-number parse (numeric-or-free-text, comma→dot) of dose/yield/duration; drop dose/yield=0, ratio outside [1,6], time outside [10,120] s; enjoyment >100 dropped, ≤0 unrated; `start_time` pinned to epoch seconds.
- [x] 5.3 Profile-family grouping: trim → collapse whitespace → case-fold → strip only explicit trailing version markers (NOT a bare integer — "Blooming 88" ≠ "Blooming 93"); display title = modal raw title.
- [x] 5.4 Ranking: Normal lower credible bound `mean − 1.64·sd/√n` over rated-and-≥floor shots per family, n≥2 to rank; deterministic tie-break (score, then ratedCount, then title). **Supersedes the design doc's Bayesian-average sketch** — the Bayesian mean cannot satisfy "one 100 can't beat ten 80s" (a confidence-bound property); see the DECISIONS ADR + the Fable reconcile.
- [x] 5.5 Display stats over ALL kept family shots (unrated included): interpolated medians + min/max of dose/yield/ratio/time.
- [x] 5.6 Grinder: match via `GrinderAliases::lookup` (both strings resolve, same brand+model); grind = observed median of `parseGrinderSetting` over matched rows, ≥3 parseable to quote, formatted via `formatGrinderSetting`; compound dials ("1+4") handled.
- [x] 5.7 12-month window hard filter + all-time fallback flagged `isAllTime`; ≥3-distinct-users gate drives `hasSuggestion`.
- [x] 5.8 `tests/tst_communityaggregator.cpp` (19 slots: cleaning, grouping, credible-bound ranking incl. the two-table noisy-small-n proof, window edges/fallback, grinder match/compound/threshold, min-users boundary, median parity, minEnjoyment floor, zero-rated exclusion) + `decenza_communitylib` narrow lib in `tests/CMakeLists.txt`.
- [x] 5.9 VERIFIED: `ENABLE_TSNET=OFF ./build.sh --target OSX --debug --dev` green; `tst_communityaggregator` 19/19 slots; full suite 135/135 (incl. `fork_index_fresh`, `failonwarning_lint`); no network linked.
- [ ] 5.10 Fable pre-commit review GO; commit to `backup` with the DECISIONS ADR entry (Phase 2 has no model-visible strings → no owner word-gate).

## 6. Client + card (Phase 4)

- [x] 6.1 `src/network/communitysettingsclient.{h,cpp}` — `CommunitySettingsClient` (QML_ELEMENT/UNCREATABLE, via `MainController.communitySettings`); NO VisualizerUploader dep (own request vs `communityApiBaseUrl` + `basicAuthHeader` only when `visualizerConnected`); NO debounce (latest-wins + session cache); `searchKey()` '\n'-joined; two-leg (bean authoritative, canonical enrichment/degrade, merge by id, detach-then-abort, emit-after-abort); `apiAvailable` 404-latch reset on base/setting change; status map 404/429/parse/superseded; `resultsReady(key, summary)`.
- [x] 6.2 DEVIATION (Fable-ruled correct): no `min_enjoyment` in the GET — the aggregator floors ranking only; the floor is client-side (summarize + deep link). Pinned by a test.
- [x] 6.3 `SettingsVisualizer` props (h/cpp only): `communitySearchEnabled` (true), `communityMinEnjoyment` (70), `communityApiBaseUrl` (`https://visualizer.coffee`).
- [x] 6.4 MainController wiring: `Q_PROPERTY communitySettings` CONSTANT + getter + member + ctor construction beside `m_beanbase`; `CMakeLists.txt` app SOURCES.
- [x] 6.5 Card community section (`CommunitySuggestionsCard.qml`): honest states (disabled→loading→summary→shots-only→empty→network+retry→unsupported-silent), re-title to "Community" only with data, `required property var modelData` delegates, deep link consolidated into `communityPageUrl()` (local `communityUrl()` deleted); `BeanInfoPage.qml` passes `beanBaseId`.
- [x] 6.6 `tests/tst_communitysettingsclient.cpp` (fake `QTcpServer`, 15 cases) + `decenza_communitysettingslib` in `tests/CMakeLists.txt`.
- [x] 6.7 Owner word-gate on the 13 `beaninfo.community.*` strings — APPROVED 2026-10-19, `// PROPOSED` markers stripped; `→` glyph coverage confirmed.
- [x] 6.8 VERIFIED: OSX debug/dev build green; qmllint 281/281; `tst_communitysettingsclient` 15/15; full suite 136/136; no `src/barista`. Fable design + pre-commit both GO-WITH-CHANGES (all folded).
- [ ] 6.9 OWED (owner): deploy the Phase-3 endpoint (Docker) + point `communityApiBaseUrl` at it → owner-visual on device. Phases 5 (Apply + temperature) and 6 (web research + parity) remain.

## 7. Apply (Phase 5)

- [x] 7.1 Aggregator: per profile row `family` + `topShotId` (best rated-and-≥floor shot: enjoyment, then latest, then smallest id); always-present keys. Test `topShotId_bestRatedAboveFloorWithTiebreaks`.
- [x] 7.2 `CommunitySettingsClient::fetchShotProfile(shotId)` — `GET /api/shots/:id/profile?format=json`, auth when connected, latest-wins, session-cached; `shotProfileReady(shotId, {temperatureC, profileJson, title})` (via `Profile::fromJson`) / `shotProfileFailed(shotId, status)`; non-200 → network (never the apiAvailable latch). `resetForNewServer` clears the profile cache too. 4 fake-server tests.
- [x] 7.3 MainController: 2 byte-identical micro-helpers extracted from the replay tail (`queueDoseWrite`, `armTemperatureOverrideIfDiffers`) + `applyCommunitySuggestion(QVariantMap)` + `communitySuggestionApplied(key, ok)`. Profile mandatory (checked bool returns; else write NOTHING + false); queued dose → queued ratio anchor → sync temp guard → grind (matched-only). `VisualizerImporter` NOT used.
- [x] 7.4 Card: Apply button (gated on `_canApply` incl. `topShotId`) → lazy `fetchShotProfile` → inline `DecenzaDialog` confirmation sheet (old→new from EFFECTIVE current values) → `applyCommunitySuggestion`; fetch-failed-but-installed → sheet without temp; fetch-failed-and-absent → fail; `refresh()` resets apply state + closes the sheet. Look-up/import by the fetched REAL title (no duplicate re-import).
- [x] 7.5 Owner word-gate on the 13 `beaninfo.community.*` Apply strings — APPROVED 2026-10-19; markers stripped; confirm/cancel reuse `common.button.*`; `°C`/`→` glyph coverage confirmed.
- [x] 7.6 VERIFIED: OSX debug/dev build green; qmllint 281/281; aggregator 22/22; client 19/19; full suite 136/136 (replay tests green); no `src/barista`. Fable design + pre-commit both GO-WITH-CHANGES (all folded).
- [ ] 7.7 OWED (owner): owner-visual on device once the Phase-3 endpoint is deployed.

## 8. Web research + parity (Phase 6 — final)

- [x] 8.1 (6a) `AIManager::researchBean(token, roaster, coffee, productUrl)` → `beanResearchReady/beanResearchFailed` (dedicated signals), mirroring `findProductPage`; static `beanResearchUserPrompt` scopes to roaster host + decentespresso.com; single `searchWeb` call, prose + inline citations. `m_isBeanResearch` reset at every group-set site (incl. coach/session).
- [x] 8.2 (6a) Card "From the web" section: shown only when the chosen provider `supportsWebSearch`; cost-hinted button → `researchBean` → advice via `Theme.replaceEmojiWithImg(MarkdownRenderer.toHtml())` + `Text.RichText` + `onLinkActivated` (safe AI-text path) + disclaimer; NEVER applied/parsed/mixed into the numbers. `refresh()` resets web state.
- [x] 8.3 (6b) MCP `bean_search` `community` action (optional `CommunitySettingsClient*`, `action` enum default `search`, Gather+`searchKey`, `communitySummaryToJson`, nullptr-degrade); desc <500; `McpSurfaceVersion` 1.20.0; budget+fingerprint pass.
- [x] 8.4 (6c) Shot-server `GET /api/beans/community` mirroring `/api/beans/search` (async + `searchKey` echo + timeout).
- [x] 8.5 Superseded hardening: card re-issues once (`_commRetried`) when another shared-client consumer displaces it; MCP + shot-server answer superseded immediately (retry) instead of stalling to the deadline.
- [x] 8.6 Owner word-gate on the 6 `beaninfo.community.web.*` strings — APPROVED 2026-10-19; marker stripped; glyph gate exit 0.
- [x] 8.7 VERIFIED: OSX debug/dev build green; qmllint 281/281; `tst_beanbaseclient` 85/85; `tst_aimanager` 168/168; full suite 136/136; MCP budget exit 0; no `src/barista`. Fable design + pre-commit both GO-WITH-CHANGES (all folded).
- [ ] 8.8 OWED (owner): on-device owner-visual of Phases 4–6 once the Phase-3 endpoint is live.
