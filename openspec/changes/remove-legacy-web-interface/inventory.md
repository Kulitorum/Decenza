# Legacy interface inventory (in progress)

## Page routes and ownership

Before this change, `shotserver.cpp` dispatched `/`, `/index.html`, `/shots`, `/compare/<ids>`, `/shot/<id>`, `/debug`, `/remote`, `/settings`, `/upload`, `/upload/media`, `/restore`, `/recipes`, `/beans`, `/equipment`, `/theme`, `/layout` and `/ai-conversations` to the old HTML interface. `/legacy/` was stripped before dispatch and `/index.html` redirected to the old interface; `/` served Wasm. The `/upload` and `/upload/media` POSTs, `/api/backup/restore`, and the shot JSON/profile file routes were used by the old interface and were removed under the user’s direction. `/mcp/setup`, auth pages, browser assets and icon routes are independent and remain.

The corresponding `generate*Page` methods are distributed among `shotserver_shots.cpp`, `_settings`, `_upload`, `_backup`, `_recipes`, `_bags`, `_equipment`, `_theme`, `_layout` and `_ai`. Their declarations live in `shotserver.h`. Removed in this work so far: debug, settings, restore, recipes, beans, equipment, theme, layout, AI conversations and `/remote`. Shot list, detail, comparison and upload pages were also removed. The `shotserver_*.cpp` files also contain APIs; retain them unless every remaining definition is proven unused.

## Shared paths and simplification candidates

- `shotserver_webroute.h` formerly normalized `/legacy/`; streaming and request dispatch used that normalization. The alias and the old upload routes were removed; migration backup downloads remain. `iconResource` and `wasmRootTarget` remain used by authentication and Wasm.
- `webtemplates/auth_page.h` remains for sign-in; its explicit `/legacy/auth/login` compatibility branch was removed. All other legacy templates, including theme/shader and `remote_page.h`, have been deleted. Preserve MCP setup HTML and scripts in `shotserver.cpp`.
- Preserve Wasm routes, migration, MCP, documented home automation, Pocket, origin guards and the public route policy. The JSON session WebSocket was removed; the shared Qt Remote Objects transport and its existing security checks remain.
- `tst_webclientassets.cpp` asserts legacy aliases and live old pages; update or remove only obsolete assertions while retaining Wasm, auth and file coverage. `tst_recipesearch.cpp`, `tst_customwidgethtml.cpp` and `tst_shotreviewactions.cpp` read old page code directly; remove legacy-only comparisons while retaining independent native/shared tests. `tests/CMakeLists.txt` documents those tests. Check `CMakeLists.txt` if any complete source file is removed.
- `docs/CLAUDE_MD/SHOTSERVER.md`, `docs/CLAUDE_MD/SHARED_UI.md`, `tools/webclient/README.md`, the manual and release notes contain current-facing legacy references. Historical archived plans and changes are evidence, not live instructions.

## Open checks

Check remaining helper callers, particularly `queryShotList` and login, and validate the retained transport. Confirm any remaining API is independently supported before removing it. New code and tests require user verification before being written.

## User decisions during implementation

The user directed removal of endpoints that the Wasm interface does not use, including legacy upload and restore routes and legacy shot-file downloads. Explicit exceptions are MCP, device migration, documented home-automation endpoints (including the documented shot list/detail) and Pocket integration. MQTT talks directly to its broker and does not use the REST home-automation endpoints. Wasm uses `/api/session/objects`, authenticated sign-in, `/api/library/thumbnail`, `/api/debug/file/zip`, screensaver media and session image routes. The device-migration client uses `/api/backup/manifest`, `/api/backup/full`, `/api/backup/ai-conversations`, `/api/backup/settings`, `/api/backup/extra-settings`, `/api/backup/profiles`, `/api/backup/profile/...`, `/api/backup/shots`, `/api/backup/media` and `/api/backup/media/...`.

The old HTML recipe, bag, equipment, settings and theme handlers have no in-tree Wasm caller; their pages and handlers were removed. The library thumbnail remains in the former layout handler for the current interface. The APK header check remains used by the shared update service. The user questioned the value of long-term tests for removal; do not add tests without their verification.

## Manual and release notes

The local wiki checkout at `../Decenza.wiki` has updates to `Manual.md` and `Installation.md`; `wiki-update.patch` records the exact changes. The wiki changes have not been published. To publish them, review and commit those two wiki files, then push the wiki repository's branch. `release-note.md` contains the proposed release note; it has not been published to a release.

The user authorized adapting existing connection tests, without adding a new test suite; the snapshot-only tests were removed.

## Remaining shared code

The library thumbnail handler is retained for Wasm; the former layout and theme HTTP editors and their notifications were removed. The layout catalog still supplies QML data; five JSON-only projections for the old editor were removed, along with their HTML parity checks. The existing Sleep-widget check now reads the shared defaults. The APK signature check remains in the shared update policy, while its old upload-route helper and duplicate test were removed. Large HTTP request buffering remains because MCP still shares this listener. Session appearance, image delivery, package identity, origin checks and periodic revalidation remain because Wasm's Qt Remote Objects service uses them. The state, telemetry and shot-list/detail APIs remain for documented home automation; Pocket and device-migration routes remain for their independent clients.

The user approved narrowing the existing remote allowlist to the Qt Remote Objects connection and the bean/background image paths. The existing remote-access test passed after that change. The user also directed keeping the minimum necessary tests; the old HTML parity checks and duplicate APK signature test were deleted, while the Sleep behavior check and Qt Remote Objects authentication, origin and lifecycle checks remain.

## Validation and diff

Qt Creator build `1794066139919` passed, including the matching Wasm package build. The last complete suite before the final session cleanup passed 203 tests; after that cleanup the existing connection and remote-access checks passed, followed by the targeted layout, settings, APK policy and connection checks. No new test suite was added. Live host and browser results are recorded below.

The current tracked diff has 22,872 application/build lines removed and 55 added; tests remove 1,754 and add 42; documentation removes 249 and adds 20. These physical-line counts include comments and blank lines and exclude this untracked OpenSpec change and the separate local wiki diff. Recount if the source changes further.

## Live local verification

The user started the build from this checkout; the listener was verified as its `Decenza.app` on port 8888. On that host, `/` and `/wasm/` returned 200; `/index.html`, `/legacy/`, `/legacy/recipes`, `/shots`, `/recipes`, `/equipment`, `/shot/123`, its old JSON download, `/api/session/ws`, `/api/recipes`, and POSTs to `/upload`, `/upload/media`, `/api/backup/restore` and retired session actions returned 404. The migration manifest, settings, extra-settings, profiles and media endpoints, as well as `/api/state`, `/api/telemetry`, `/api/pocket/status`, `/api/shots` and `/mcp/setup`, returned 200. The browser displayed the connected shared interface, navigated to Settings, and reported a successful backup download. With the user's approval, a public Funnel request to the access link and browser root returned 200, and `/shots` returned 404 in that session. The remote browser also displayed the live interface and explicitly reported a Remote Web Access connection. The existing remote-access test passes its deny-by-default checks. Authentication was disabled on the running instance; its retained connection checks passed in the existing tests. Loader-failure behavior is covered by the existing web-client asset tests. A live Wasm file import remains unverified: clicking the shot ZIP import control did not produce a file chooser in the in-app browser, so no file was sent or imported.

The user subsequently selected a valid test ZIP containing no shot files in the remote browser. The app displayed the generic “The import could not be completed” error. The running app's `debug_get_log` recorded `commitTransfer shotimporter.error.noShotsInArchive` for the attempts: the archive was transferred and inspected, and the empty archive was rejected as expected. At the user's request, the missing specific message was added; Qt Creator build `1794066139920` passed. After restarting the app and repeating the import, the remote browser displayed “No .shot files were found in the archive.”
