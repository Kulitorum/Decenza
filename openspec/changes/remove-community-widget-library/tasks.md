# Tasks

Builds and test runs go through the Qt Creator MCP (`mcp__qtcreator__build` / `mcp__qtcreator__run_tests`), asked for before each call. Delete by file and symbol, never by `grep community`: the Visualizer bean-suggestions feature (`CommunitySettingsClient`, `CommunityAggregator`, `CommunitySuggestionsCard`, `decenza_communitylib`, `decenza_communitysettingslib`) stays.

## 1. In-app community UI and the Go to Community action

- [x] 1.1 Remove the Community tab, the share-to-community / delete-from-server / browse-all controls and the `communityEntries` state from `qml/components/library/LibraryPanel.qml`, and the `communityEntry` / server-thumbnail path from `LibraryItemCard.qml`. Reduce the empty-state `Text` to the local `library.emptyState.local` string and delete `previewsUnavailable`. Verify: the in-app layout editor's library panel lists, applies, renames and deletes local entries in the running app.
- [x] 1.2 Delete `qml/pages/CommunityBrowserPage.qml` and `qml/components/CommunityBrowseNavigation.js`. Remove them from `CMakeLists.txt` and `src/webclient/CMakeLists.txt`. Remove the `community` route from `BrowserRouteHistory.js` and `SharedAppNavigation.qml`, and `communityBrowserRequested` from `AppShell.qml`. Verify: the build is green (the qmllint gate passes), and back/forward through browser route history works in the running app.
- [x] 1.3 Remove the `navigate:community` row from `layoutActionTable()` in `src/core/layoutcatalog.cpp` and its `case "community"` arm from `LayoutActions.execute()`. Verify: neither action picker offers "Go to Community", and `tst_customwidgethtml` (`everyCatalogActionHasADispatchArm`, `everyAppShellRequestReachesTheShell`) passes.
- [x] 1.4 Drop `CommunityBrowserPage.qml` as a source in `tst_customwidgethtml::neitherEditorHardCodesItsOwnActionList` (keep the two remaining editors). Remove `communityRouteComponent` from `tests/qml/tst_operationcompletionoverlay.qml` and `CommunityBrowseNavigation.js` from `_completion_qml` in `tests/CMakeLists.txt`. Verify: both tests pass, and break the in-app picker's list to confirm the trimmed assertion still goes red.

## 2. Web editors and ShotServer routes

- [x] 2.1 Remove the Community tab, filters, upload/delete buttons and community JS from the web layout editor in `src/network/shotserver_layout.cpp`, and the `/api/community/{thumbnail,browse,download,upload,delete}` handlers from `handleLayoutApi()`. Verify: the web layout editor's library section shows only local entries and applies one, checked in the built-in browser.
- [x] 2.2 Remove the "Library & Community" community half from the web theme editor (`webtemplates/theme_html.h`, `theme_js.h`, any `theme_css.h` rules only it uses). Verify: the web theme editor saves and applies a local theme, checked in the built-in browser.
- [x] 2.3 Remove the `/api/community` prefix from the ShotServer dispatch (`shotserver.cpp`) and `/api/community/thumbnail` from `remotewebroute.h`'s allowlist. Update the community rows in `tests/tst_remoteaccess.cpp`. Verify: `GET /api/community/browse` gets the generic not-found answer, and `tst_remoteaccess` passes.
- [x] 2.4 Remove the "Async community endpoints" section from `docs/CLAUDE_MD/SHOTSERVER.md`. Verify: `grep -n "LibrarySharing\|/api/community" docs/CLAUDE_MD/SHOTSERVER.md` is empty.

## 3. Library service operations and the sharing client

- [x] 3.1 Remove `browseCommunity`, `browseMyUploads`, `featured`, `upload`, `download`, `delete` and `flag` from the allowlist, validation and dispatch in `src/remote/libraryservicesource.cpp`. Drop the `LibrarySharing*` parameter and `completeSharing`. Remove `sameOriginCommunityThumbnails` from `libraryservice.cpp`. `libraryservice.rep` is unchanged. Verify: the build is green and a removed operation name completes with failure through the existing unknown-operation path.
- [x] 3.2 Delete `src/network/librarysharing.{h,cpp}` and `src/network/communitythumbnail.h`. Remove them from `CMakeLists.txt`, remove `LibrarySharingForeign` from `contextsingletons_qml.h`, and remove the construction, `setLibrarySharing`, singleton publication and `clearConnectionCache` call from `main.cpp`, plus `m_librarySharing` / `setLibrarySharing` from `shotserver.h`. Verify: the build is green and `grep -rn "LibrarySharing\|communitythumbnail" src qml` is empty.
- [x] 3.3 Rename `tests/tst_librarysharing.cpp` to `tst_libraryservice.cpp` (target and the `"tst_librarysharing|libraryservice"` pair in `tests/CMakeLists.txt`). Keep the library-service admission and argument-validation cases, delete the sharing-client cases, and drop `librarysharing.cpp` from its sources. Verify: break the admission check each kept case covers and watch it go red, then restore it and watch the target pass.
- [x] 3.4 Update `docs/CLAUDE_MD/PROJECT_STRUCTURE.md` (drop the `librarysharing.*` line) and remove the "Widget / layout Library sharing" bullet from `PRIVACY.md`. Verify: `grep -n "librar" PRIVACY.md` no longer mentions `/v1/library`.

## 4. Translations, manual and integration

- [x] 4.1 For each community-library key (`library.tab.community`, `library.accessibility.shareToCommunity`, `library.accessibility.browseAllCommunity`, `library.dialog.deleteFromServer`, `library.emptyState.community`, `community.empty.previews`, `customaction.navigate.community`, the `CommunityBrowserPage` keys), grep for remaining uses, then let the translation scan settle what is unreferenced. Verify: `tst_translationscan` and `tst_translationsourcedrift` pass, and `grep -rn "community.empty.previews" qml src` is empty.
- [x] 4.2 Update the wiki (`~/Development/GitHub/Decenza.wiki`): delete `Manual.md`'s "Community Widget Library" section and its row in the web-interface table, and the "browse and share community widgets" phrase in `FAQ.md`. Do not add text explaining the removal. Verify: `grep -n "Community Widget Library\|share community widgets" Manual.md FAQ.md` is empty.
- [x] 4.3 Run the build-free per-PR checks locally (`scripts/check_log_markers.py`, `scripts/check_test_source_duplication.py`, `scripts/settings_search_index.py`, `scripts/check_mcp_tool_budget.py`). Verify: each exits 0.
- [x] 4.4 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) on a clean build. Verify: all tests pass with no new warnings.
- [x] 4.5 With the user running the app: open the in-app library panel, the web layout editor and the web theme editor, apply a local entry from each, and tap a Custom widget whose stored action is `navigate:community`. Verify: local entries work, no Community tab appears anywhere, the widget tap opens nothing, the log shows the `unknown navigate target` warning, and there are no QML `ReferenceError`/`TypeError` lines.

## Workflow follow-up

- Archive with `openspec archive remove-community-widget-library --yes` as the last commit on the feature branch, then read the PR's `text-invariants` run before merging.
- Shutting down the `/v1/library` service is the maintainer's call and is independent of this release. The rest of `api.decenza.coffee` stays.
