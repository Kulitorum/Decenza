# Design

## Context

See proposal.md for why. The community library reaches the user through four layers, and all four sit on one client object:

- `LibrarySharing` (`src/network/librarysharing.*`) is the only code that talks to `/v1/library`. It is published to QML as a `QML_FOREIGN` singleton (`contextsingletons_qml.h`), constructed in `main.cpp`, and handed to ShotServer and to `NativeLibraryServiceSource`.
- The remote library service has a generic contract: `execute(requestId, operation, arguments)` in `libraryservice.rep`. Community traffic is a set of operation names (`browseCommunity`, `browseMyUploads`, `featured`, `upload`, `download`, `delete`, `flag`) that `libraryservicesource.cpp` allowlists and dispatches to `LibrarySharing`. Local operations go through the same entry point.
- ShotServer forwards every `/api/community*` path to `handleLayoutApi()` (`shotserver.cpp:2938`). `remotewebroute.h` allowlists `/api/community/thumbnail` for the public relay host.
- QML has a Community tab in `LibraryPanel.qml`, a full page (`CommunityBrowserPage.qml` with `CommunityBrowseNavigation.js`), a `community` route in `BrowserRouteHistory.js` / `SharedAppNavigation.qml`, an `AppShell.communityBrowserRequested` signal, and the `navigate:community` catalog action. The web layout editor (`shotserver_layout.cpp`) and the web theme editor (`webtemplates/theme_*.h`) each have their own Community tab.

## Goals / Non-Goals

**Goals:**
- Delete the community library completely. Nothing should be left behind a flag: no dormant client, no route that 404s only by luck.
- Leave every local library operation behaving exactly as it does today.

**Non-Goals:**
- The other `api.decenza.coffee` features and the Visualizer-backed bean suggestions (see proposal, "Not changing").
- Deleting user uploads on the backend.
- Rewriting stored layouts.

## Decisions

### Delete the code; don't disable it

Remove `LibrarySharing`, its singleton, routes and UI, instead of hiding the tab behind a setting or a compile flag. A disabled client still gets compiled, linted and tested. It would also need a reason to exist, and it doesn't have one. The memory rule "prefer fewer settings" points the same way.

### Keep the library-service contract; shrink its operation set

`libraryservice.rep` stays as it is. Its operations are strings inside one generic `execute` slot, so removing community traffic means removing the operation names from the allowlist and dispatch in `libraryservicesource.cpp`. Once a name is gone, the existing unknown-operation path already rejects it with a failed completion. Changing the `.rep` would force a Wasm/native replica re-sync for nothing.

`NativeLibraryServiceSource` drops its `LibrarySharing*` constructor parameter and the `completeSharing` plumbing. `libraryservice.cpp` drops `sameOriginCommunityThumbnails`. `communitythumbnail.h` has no other user, so it is deleted.

### `/api/community` becomes an unknown path, not a stub

Remove the `/api/community` prefix from the ShotServer dispatch and the `/api/community/thumbnail` entry from `remotewebroute.h`'s allowlist. A former endpoint then gets the generic not-found answer (spec: *web server exposes no community library endpoints*). A stub returning "feature removed" was considered and rejected: no client is left to read the message. The bundled web pages are served by the same binary, so they stop calling these routes in the same build.

### Drop `navigate:community` from the catalog and leave stored values alone

Remove the catalog row from `layoutActionTable()` and the `case "community"` arm from `LayoutActions.execute()`, and delete `AppShell.communityBrowserRequested`. A layout that already stores `navigate:community` keeps it; we don't retro-rewrite user-set data. Tapping it reaches the existing `unknown navigate target` warning in `LayoutActions.execute()`, which does nothing.

Alternatives rejected:
- *Keep a legacy `inPicker=false` row so the editor shows a label.* Its dispatch arm would have to do nothing. That is the "button that does nothing" the catalog rule forbids, and `everyCatalogActionHasADispatchArm` exists to stop it.
- *Remap it to another page,* such as the layout editor's library. That would invent a destination the user never chose.

Cost: the Custom-widget editor's label lookup misses, so it shows the raw id `navigate:community` for such a widget, and the user can reassign it. That is acceptable because the proposal's premise is that the feature is unused.

### Fold the surviving tests into a renamed file, don't add one

`tst_librarysharing.cpp` mixes two things. Sharing-client cases go with the code: browse, upload/download/delete replies, thumbnail validation, malformed responses. Library-service admission cases stay: `queuedReplyFromRetiredReplicaCannotCompleteAReusedId`, `failedAdmissionCannotReplayAReusedIdOnTheReplacementNode`, `requestAdmissionSurvivesSynchronousObservers`, `libraryServiceValidatesArgumentsAndChecksLongLayoutBaseline`. Rename the file and target to `tst_libraryservice` and update the `"tst_librarysharing|libraryservice"` pair in `tests/CMakeLists.txt`. That keeps one TU instead of adding one (TESTING.md: a new test file costs ~1.4 s of build forever). Before keeping each surviving case, confirm it still constructs and fails red without `LibrarySharing`. A case that only tested sharing admission is deleted.

`tst_customwidgethtml`'s community-browser assertion (`neitherEditorHardCodesItsOwnActionList`) loses its third source, `CommunityBrowserPage.qml`. The other two editors keep the invariant. `tst_operationcompletionoverlay.qml` loses its `communityRouteComponent` fixture, and `tst_remoteaccess` flips or removes its community-thumbnail rows.

### Translations go through the existing scan

Remove the community-only keys from the source. Then let the translation scan and `tst_translationsourcedrift` / `tst_translationscan` say what is now unreferenced, instead of hand-editing language files. Grep each key before removing it: the bean-suggestions card owns `community.*` keys of its own. `community.empty.previews` goes with this feature. It is shown only on the Community tab and the Community page, because `previewsUnavailable` is set only by the community handler. The local tab has its own `library.emptyState.local`.

## Risks / Trade-offs

- [A surviving local feature quietly depended on `LibrarySharing`, for example the theme thumbnail generation in `widgetlibrary.cpp` that is commented "community downloads".] → The compile will fail loudly if code still calls it. For comment-only references, re-read the code path and keep the behaviour if a local import still uses it.
- [The bean-suggestions feature shares the word "community" in names, translation keys and the `decenza_communitylib` / `decenza_communitysettingslib` test libraries.] → Delete by file and symbol, never by `grep community`. The Impact list in proposal.md is the deletion set.
- [A screen loses a delegate or a route and fails only at runtime. QML route and delegate breaks are invisible to the compiler and qmllint.] → Open the layout editor library panel (in-app and web), the web theme editor, and back/forward navigation through the browser route history in the running app.
- [Users with a stored "Go to Community" action see a raw id in the editor.] → Accepted; see Decisions.

## Migration Plan

No data migration. The change ships in a normal release. A stored layout keeps working, and only the removed action becomes inert. Rollback is a revert of the PR. The `/v1/library` service can be shut down whenever the maintainer chooses, independently of this release; the rest of `api.decenza.coffee` stays up. Older app versions in the field will then see their Community tab fail, which is the outcome retiring the service implies anyway.
