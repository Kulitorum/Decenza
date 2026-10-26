# Proposal

## Why

The community widget library — browse, share, download and delete layouts, widgets and themes through `api.decenza.coffee/v1/library` — is not being used, and its AWS service is being retired. Shipping a network feature against a retired service leaves users with a tab that fails or shows stale content, and keeps ~4,000 lines of client code, a privacy-policy disclosure and a test target alive for nothing.

## What Changes

- **BREAKING (user-visible):** Remove the community library from every surface that offers it. The **local** widget library (save, apply, rename, delete, import/export of entries on this device) is kept unchanged.
  - In-app: the **Community** tab and its **Share to community** / **Delete from server** / **Browse all** controls in the library panel; the full-page Community browser.
  - Web: the Community tab in the web layout editor and in the web theme editor, and the ShotServer `/api/community/*` endpoints (`browse`, `download`, `upload`, `delete`, `thumbnail`).
  - Remote UI: the `browseCommunity`, `browseMyUploads`, `featured`, `upload`, `download`, `delete` and `flag` operations of the library service.
- **BREAKING (layouts):** Remove the **Go to Community** Custom-widget action (`navigate:community`) from the action catalog. A layout that already stores it keeps the stored value (no rewrite of user data); the action no longer navigates anywhere.
- Delete the `LibrarySharing` client, its QML singleton registration and its construction in `main.cpp`, plus the community thumbnail proxy.
- Remove the "Widget / layout Library sharing" disclosure from `PRIVACY.md`, and the community library from the wiki manual and FAQ.

**Not changing:**
- The Visualizer-backed **bean community suggestions** (`CommunitySettingsClient`, `CommunityAggregator`, `CommunitySuggestionsCard`). It is a different feature on a different, maintained backend. It shares only the word "community".
- The other `api.decenza.coffee` users: crash reporting (`/v1/crash-report`), the Shot Map (`/v1/shots`) and translation upload/download. Only the layout library's `/v1/library` service is being retired; the rest of that backend stays in service.
- The installation `deviceId` setting. MQTT, the relay client and ShotServer still use it.

## Capabilities

### New Capabilities
- `widget-library`: the device-local widget library and the fact that it has no network sharing surface. No existing spec covers the library, so this capability is introduced to hold the "local-only" contract. Future local-library requirements belong here too.

### Modified Capabilities
None. No existing spec states a requirement about community sharing. `layout-action-catalog` and `layout-editor-usability` mention the library panel and the action table only generically, and those requirements still hold after this change.

## Impact

- **C++:** `src/network/librarysharing.{h,cpp}`, `src/network/communitythumbnail.h` (deleted); `src/core/contextsingletons_qml.h`, `src/main.cpp`, `src/network/shotserver.{h,cpp}`, `src/network/shotserver_layout.cpp`, `src/network/remotewebroute.h`, `src/network/webtemplates/theme_{html,js,css}.h`, `src/remote/libraryservice.cpp`, `src/remote/libraryservicesource.{h,cpp}`, `src/core/layoutcatalog.cpp`, `src/core/widgetlibrary.cpp` (comments only).
- **QML:** `qml/pages/CommunityBrowserPage.qml`, `qml/components/CommunityBrowseNavigation.js` (deleted); `qml/components/library/LibraryPanel.qml`, `qml/components/library/LibraryItemCard.qml`, `qml/components/SharedAppNavigation.qml`, `qml/components/BrowserRouteHistory.js`, `qml/AppShell.qml`, `qml/components/layout/LayoutActions.qml`.
- **Build:** `CMakeLists.txt`, `src/webclient/CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Tests:** `tests/tst_librarysharing.cpp` loses its sharing cases. It keeps the library-service admission cases and is renamed to match. `tests/tst_customwidgethtml.cpp` and `tests/qml/tst_operationcompletionoverlay.qml` lose their community-browser assertions.
- **Docs:** `PRIVACY.md`, `docs/CLAUDE_MD/PROJECT_STRUCTURE.md`, `docs/CLAUDE_MD/SHOTSERVER.md`, wiki `Manual.md` and `FAQ.md`.
- **Translations:** community-only keys (`library.tab.community`, `library.accessibility.shareToCommunity`, `customaction.navigate.community`, `community.*`, …) become unused. The translation scan determines which ones are removed.
- **External:** No server change is needed. The client stops calling `/v1/library`. Previously uploaded entries are not deleted from the backend by this change.
