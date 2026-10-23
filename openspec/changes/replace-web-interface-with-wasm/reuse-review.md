# Independent reuse and simplification review — 2026-10-22

Two fresh agents reviewed the current working tree independently of the implementation authors. One traced services and remaining HTTP adapters; the other traced shared QML, shell policy, appearance and test seams. The parent cross-checked the last-shot/layout callers and ordinary navigation differences. This is a source review, not runtime acceptance. No review-driven implementation edits, builds or tests were performed. The preceding navigation/draft-protection batch remains awaiting its paired build and verification.

Estimates below are net production-line reductions after replacement wiring, not guaranteed savings. Test changes are separate unless stated. Preserve the existing HTML interface, HTTP endpoints and serializers; deleting a Wasm consumer does not authorize retiring a server contract.

## First cleanup batch: use services that already exist

### 1. Replace the last-shot background's private HTTP backend

`qml/components/LastShotChartSource.qml:112` is the only shared QML reader of `MainController.shotHistory`. It retains `src/webclient/remoteshothistorybackend.cpp` (451 lines) and its header (86 lines), plus ownership in `RemoteMainController`. That adapter duplicates fetching, parsing, cancellation, cache, revision and identity handling.

Use `HistoryService.requestMostRecentShotId()` and `requestShot()` with correlated completions, readiness and history-change notifications. `qml/components/layout/items/LastShotItem.qml:32` already uses this shared service. Delete the browser backend and its wiring; retain the HTTP endpoint and serializer.

Estimated net reduction: **480–530 production lines**. Validate empty history, deletion during loading, post-shot background refresh, disconnect/reconnect and concurrent shot-detail requests. Keep the background's existing no-samples/empty behavior and render-cache invalidation.

### 2. Finish layout migration and delete RemoteSettingsNetwork

Five shared QML references keep a separate layout HTTP adapter alive:

- `qml/pages/PostShotReviewPage.qml:227`: `Settings.network.layoutConfiguration`.
- `qml/pages/settings/LayoutEditorZone.qml:201`, `:474`, `:481`, `:640`: layout catalog functions.

All APIs already exist on `src/remote/layoutservice.h`. Change these references to `LayoutService`, then delete `src/webclient/remotesettingsnetwork.cpp` (206 lines), its header (78 lines), and browser settings ownership/wiring. Native `SettingsNetwork` has other legitimate callers and remains intact.

Estimated net reduction: **280–295 production lines**. Validate review-field ordering, editor catalogs, shared layout updates and reconnect. Adjust `tst_remotesettings` rather than retaining an obsolete production adapter to satisfy old tests.

### 3. Remove unused Steam settings facade paths

`src/webclient/remotesettings.cpp:197` and `:242` retain Steam graph/view/coaching properties and HTTP setters. Shipped Steam QML now reads/writes `SteamService` (`qml/pages/SteamPage.qml:803`, `:2151`, `:2165`; `qml/components/SteamGraph.qml:18`).

Remove unused declarations/implementations, `RemoteSessionActions::sendSteamGraphTrace` at `src/webclient/remotesessionactions.cpp:133`, and Steam-only branches in `sendUiPreference`. Keep the still-used `advancedMode` and `flowMultiplier` paths.

Estimated net reduction: **120–160 production lines**. Verify Steam view/coaching preferences and both graph dialogs. Preserve server routes and tests for retained behavior.

### 4. Delete obsolete appearance projections

`qml/components/SessionAppearancePublisher.qml:35` still publishes resolved geometry, colors and equipment labels. `src/network/sessionappearance.h:273` requires them and validates them at `:396`, `:438`, and `:480`. `qml/components/EquipmentDialogText.qml:68` maintains an additional property-to-map projection exclusively for this publisher.

The review found no production consumer of these three projected fields beyond transport/schema validation. Both targets now compile canonical `Theme.qml`; browser settings supply its inputs, and equipment labels use the ordinary translation catalog. Delete the unused projections, key lists and validation branches. Preserve validation of consumed theme inputs, fonts, units, background and graph settings. The same-package rule means an old-client compatibility shim is unnecessary.

Estimated net reduction: **280–330 production lines**, plus **100–150 obsolete fixture/assertion lines**. This also removes a bootstrap refusal caused by an unused equipment translation exceeding the projection's 512-byte limit. Verify custom colors, font sizes, units, backgrounds and language changes against the matched native/Wasm package.

These four disjoint items total approximately **1,160–1,315 production lines**, excluding test changes. Their common approach is deletion of superseded paths, not a new abstraction layer.

## Follow-up improvements

### Share ordinary navigation decisions

Browser `qml/webclient/Main.qml:647` and nearby handlers push Community, Favorites, Recipes, Beans and Equipment unconditionally. Native `qml/main.qml:3767` and `:3831` use `pushUnlessCurrent`. The duplicated code permits repeated browser pages where native retains the current page. Browser Settings handling at `Main.qml:680` also requests Connections when no tab is specified; native `main.qml:3652` retains the current tab.

Share current-page detection, in-place Settings/History updates and destination policy, supplying push/replace hooks from the shells. Keep browser history serialization/restoration and native lifecycle/hardware handling at their existing boundaries. Estimated reduction: **100–180 net production lines**. Validate repeated destination actions, Settings with/without a tab, filtered History, editor entry paths and guarded Back/Forward.

The phase handlers also differ: browser `Main.qml:964` lacks native Descaling/Transport and machine-disconnection presentation branches (`main.qml:3369`, `:3450`). Review those against the accepted independent-navigation/workflow rules before sharing the phase-to-page decision in `OperationNavigationPolicy.js`; do not copy native hardware/settings side effects. Potential saving: **30–60 lines**, with parity as the main benefit.

### Declare shared CMake inputs once

`CMakeLists.txt:874` and `src/webclient/CMakeLists.txt:285` repeat shared facade source lists. Replica generation is duplicated at `CMakeLists.txt:1508` and `src/webclient/CMakeLists.txt:527`. Use one explicit CMake include for shared facade inputs and replica names; keep native sources/browser adapters separate. No globbing or runtime registry is needed.

Estimated reduction: **140–200 CMake lines**. Both Qt Creator builds must still collect the same QML registration metadata and replica outputs.

### Finish a smaller shared control migration

`PostShotReviewPage.qml:1541` still calls `MainController.selectSteamPitcher`, retaining browser-specific code in `remotemaincontroller.cpp:28` and `remotesessionactions.cpp:103`. `HomePresetActions.qml:89` already uses `MachineControlService.selectSteamPitcher` and its host implementation calls the same controller with host-owned captured milk.

Potential reduction: **35–55 production lines**. Verify selected-preset conflict checks versus the former whole-list revision rule, captured milk and removed/empty pitchers before changing this caller.

### Remove unused session fields

`RemoteSessionBackend` parses, stores and clears `steamHealth` (`remotesessionbackend.cpp:284`, `:730`, `:804`) but its accessor has no production caller; shared SteamService owns warning presentation. Removing this browser-only field/requirement saves **15–25 lines** and per-snapshot work. Preserve server fields if keeping their HTTP contract. `steamGraphRevision` is also unused; its availability flag becomes removable after the unused Steam facade cleanup.

## Keep useful boundaries

- Pure helpers such as `SteamCalculations`, `TemperatureDisplay`, `ProfileDialInText`, `BackgroundPresets`, `ThemeFonts`, `LayoutCatalog` and `PreparationState` are already shared. Another wrapper would not improve reuse.
- Keep QtRO connection/transport, browser history, file/download adapters, browser accessibility and translated-catalog transport where platform behavior requires them.
- Style/interaction injection is not proven dead. `tst_sharedformcontrols.qml:148` supplies alternate styles and exercises font/scale changes in a service-free test module. Although both production builds use the same `Native*` adapters, removing roughly 527 gross definition lines would require replacement fixtures; estimated net savings are only 200–350 lines with substantial churn. Defer this behind the proven deletions.
- `HttpShotGraphBackend` is used by `tools/webclient/DiagnosticsWindow.qml:24`, not the production UI. Its 866 implementation/header lines could move behind an optional diagnostics target, reducing shipped code, but that is not repository-line savings and must preserve the existing diagnostic reproductions/evidence.

## Execution recommendation

Complete the already-written navigation batch's verification, then implement the four first-batch deletions with disjoint file ownership. Inspect shared behavior changes before one paired Wasm/native build. Run the affected suites and grouped live checks; run the full suite once at the integration boundary. Use the existing route/evidence ledger, and do not recreate already-wired routes or services from historical feasibility tasks.
