# Navigation, Language and resource closure follow-up

## Scope and baseline (2026-10-22)

This follows the accepted adapter cleanup on package `7c918d2b` and keeps the
same application QML and legacy HTML routes. Three independent audits cover
task 16.4 route context/draft guards, tasks 24.1/24.4 Language actions, and tasks
26.1/26.4 shared input/resource closure. Findings below are not acceptance of
unexercised parent workflows or deferred device gates.
Community widget/theme browsing recorded below was subsequently removed by
main's #2117; its route-specific evidence is historical.

## Language action inventory

| Shared action | Canonical owner | Evidence / remaining check |
|---|---|---|
| Select an installed language; current language and coverage | LanguageService → TranslationManager | Prior Danish/English native/browser switch accepted on `23b935d7`; English restored. |
| Scan registered strings; load paged editor catalog | Same service and manager | Both native and browser String Customizer displayed 4,839 grouped entries on `7c918d2b`. |
| Search strings, All/Custom filters, clear search | Same StringBrowserPage model | Keyboard input `Steam` yielded 100 rows in both clients; browser Custom yielded zero, All restored 100, Clear restored 4,839. No translation was changed. Accessibility set-value alone does not simulate the text-edit signal; actual keyboard entry was used. |
| Add language | Correlated `addLanguage` operation | Shared AddLanguagePage exists; visible refusal and pending-state coverage being completed. No real language created/deleted for this check. |
| Edit final/custom text; accept/clear AI suggestion; clear all AI | Canonical manager writes | Audit found language/revision target and in-progress draft-refresh gaps. Fix and isolated regression coverage are required before acceptance. |
| Download community language/list | Canonical manager network operations | Opening the existing Language tab invokes its normal read-only list refresh. Network failures and request correlation require fixture coverage. |
| Translate missing strings; Stop; provider errors | Host-selected provider and model | Must use isolated fixtures. No paid call was initiated. |
| Submit / translate-and-upload | Host community operations | Must use fixtures unless explicitly authorized publishing. No real submission was made. |
| Import/export | Shared LanguageFileActions → LanguageService → TranslationManager, using the common local-file boundary | Implemented in both targets on `f58f1937`; worker preparation, guarded commit, cancellation, malformed/mismatched files and bounded transfer fixtures pass. Browser export produced valid English JSON. Native picker and browser import runtime acceptance remain open. |

Native and browser navigation remained independent during the search check.
Both clients used the installed English catalog, without saved preference or
data edits. Browser pointer clicks on a clipped control can be ineffective;
the native accessibility press opened the intended shared destination.

## Confirmed route defects corrected

- Manually selected Settings tabs were not copied into the browser route entry.
- History search/filter edits were not copied into its browser route entry;
  restoring `searchText` also targeted a nonexistent initial page property.
- Shot review next/previous changed the displayed record without changing the
  tracked route record.
- An editor draft retained underneath another page was omitted from the
  before-unload warning.

These are browser history boundary fixes plus shared page context properties;
host navigation, data ownership and editor commits remain unchanged.

## Shared input audit

Package `df398f8e` generated 350 QML/script files per target: 348 canonical shared
files, two native lifecycle files, and two browser lifecycle files. All 194
group-26 inventory rows and all 16 common JavaScript helpers resolve to the
same source files. The inventory now fails when a declared input disappears;
resource closure rejects stale generated manifests outside the active target.
The subsequent `LanguageFileActions.qml` addition passed paired builds on `f58f1937`: 351 QML/script files per target, 349 shared canonical sources, and 195 group-26 rows in the 353-file inventory.

## Integrated verification

Package `df398f8e9ac525e699ac052f36691ff4fb60f2f7d119694bd2ff7956f4af4dcc`:

- Qt Creator Wasm build 1792683623494 passed with zero warnings; native build
  1792683623495 passed with three existing linker warnings. Native build
  1792683623489 ran the QML diagnostics gate, 350/350 clean.
- Focused run 1792683626100 passed both previously failing suites. Full run
  1792683626101 passed 203/203 CTest suites in 34.650 s without warnings. The
  opt-in live HTTP route case was not enabled in this run; suite totals do not
  imply it executed.
- Regression coverage caught and corrected real route validation failure for
  Qt sequence wrappers: bounded array-like IDs are copied to JS arrays before
  validation. Resource-fixture expectations now resolve macOS `/var` symlinks.
- Browser Settings tab reload restored Language & Access on the intermediate
  package `44ead`; the final package also exposes the Add Language empty state
  in the accessibility tree.
- On the final package, typing `D-Flow` in History survived reload and browser
  Back from a reloaded shot detail. Selecting the latest shot, stepping older,
  and reloading restored **Oct 22, 9:04 AM**, its 17.8 g output, and Shot 2 of 50
  neighbor navigation. No shot metadata was edited.
- The two selected comparison records and their base survived reload; changing the base to Oct 22, 8:18 PM survived browser Back/Forward. Recipe query `Sublunar` and its expanded archived shelf also survived reload. A temporary unsaved Recipe draft stayed intact after a reload attempt while Profile Info covered it; the agent then explicitly discarded only that local draft without saving. The IAB did not expose a browser warning dialog, so its wording is not accepted by this check.
- Shared LanguageStringEditor tests cover retained draft/context, stale target,
  request correlation, two editors, retry/discard, and row ownership. Canonical
  service fixtures use intercepted network responses, including stale AI
  context refusal before any provider request. No paid translation or public
  submission was initiated.

The language import/export follow-up is covered by the next integrated batch below. Remaining route/component runtime checks and deferred device gates are still open. The maintainer confirmed concurrent native language changes were
their testing; no language preference was forced or restored by the agent.

## File operations and remaining route context — integrated 2026-10-23

Matched package `f58f1937bb7e5882509e752d892d0ca222958e9da0376cb7cecb503ddd26995f`:

- Qt Creator Wasm build 1792683623512 passed with zero warnings; native build 1792683623513 passed with four existing linker warnings and QML gate 351/351 clean.
- Focused run 1792683626104 passed 12/12 suites with no warnings. Full run 1792683626105 passed 203/203 suites in 35.680 seconds, with no failures, warnings or skips. The opt-in live HTTP route case was not enabled; this total does not imply that inner case executed.
- These results supersede the new-fixture failures in runs 6102/6103: the native save fixture now uses QApplication and an explicitly installed offscreen font, and the isolated Language fixture no longer discovers real profile directories. No warnings were suppressed.
- Language import/export uses the canonical manager preparation/commit path, an off-thread bounded file worker, exact source/language/revision checks and a machine-operation check immediately before commit. Shared file transfer and opaque download bundles are reused with Debug Conversion; host paths do not cross the service boundary. Native save selection is asynchronous.
- Full-run measurements with populated translation registries: whole main-thread import callbacks were median/worst 4.081/4.743 ms at 4,000 entries and 8.839/12.821 ms at 10,000; export callbacks were 0.227/0.231 ms and 0.228/0.242 ms. Import preparation took 149.285/154.178 ms and 390.554/403.775 ms off-thread. Fixtures cover stale/busy/cancel/destruction, partial metadata outcomes and reentrant commit cancellation/registry changes.
- The sole simulator host was restarted through Qt Creator and its executable path and `/api/session` package identity were verified. A fresh copied `#recipeWizard/69` link loaded the expected saved Recipe; opening its profile used `#profileInfo/d_flow_q`. The unmodified editor was cancelled without saving.
- Browser export produced a valid 133-byte `translations-en.json` containing English metadata and an empty custom-translations object. Browser import has not yet passed runtime acceptance: pointer/accessibility activation did not trigger the action, keyboard activation reached the pending picker state, and automation did not capture a chooser. This is an unresolved test outcome, not proof of a working import or a confirmed application defect. No translations were changed.

The route follow-up bounds long History neighbor lists around the displayed record, restores Community query filters/sort, Profile Picker search/chips/sort and String Browser query/filter, and adds missing Visualizer/Add Language unload guards. Compact copied detail links now carry bounded record IDs or public profile basenames; editor type remains canonical host-derived, never serialized metadata. Existing focused fixtures pass. Community pagination, viewport and selected-entry recovery remain open under task 16.4. Drafts and server result payloads are not serialized.

### Route acceptance at this stage (historical)

The later checks below supersede the Community, Profile Picker, String Browser,
Beans and Home Back items in this snapshot. Current unresolved items are tracked
in task 16.4 and the latest `evaluation.md` entries.

Fresh runtime checks are still needed for Community filters/sort, Profile Picker search/chips/sort, String Browser search/filter, and Beans query/finished shelf. Check direct hash changes and refusals under protected drafts, real browser unload wording, Quit-ignore, and native-navigation independence. Fixtures cover the bounded long-shot neighbor window; the live shot check had only 50 neighbors.

Viewport/loaded-window/selection recovery was not complete for all lists at this
stage. Later checks below cover historical Community recovery. History now restores
its selected shots, loaded row window and scroll position as well as its query.
Retained Back instances can preserve local state. Full route/query
context is stored in `history.state`; copied Favorite Details links carry an
anchor shot and grouping mode and reload the group through HistoryService.
Remaining route-context obligations under 16.4 stay open.

On 2026-10-26, the rebased paired build and focused History/navigation suites
passed through Qt Creator. In Edge against the Mac simulator, scrolling into a
150-shot loaded window and reloading restored that window and the same nearby
rows; a subsequent search reset to the beginning. The position and selection
stay browser-local and are restored only for the same host. This is one History
check, not acceptance of all routes, browsers or devices.

### Focused closure checks and import correction — 2026-10-23

Read-only live probes on the running `f58f1937` host returned 200 for root Wasm, `/wasm/`, `/legacy/` and `/api/session`. MCP initialization negotiated 2025-06-18 with surface version 1.19.0; its initialized notification returned 202, tools/resources discovery returned 67/24 entries, and `machine_get_state` succeeded. Only this validation session was terminated afterward. These checks exercise port sharing; they do not replace authorization/SSE/device acceptance.

A fresh `#profileInfo/d_flow_q` tab rendered D-Flow / Q by Damian, its three-frame pressure/flow/temperature graph and settings. Native navigation stayed independent.

Native import selected the isolated `qab` JSON fixture while English was selected, but remained pending. Reading the current-session log through MCP identified `LocalFileTransfer.qml:153: Cannot assign [undefined] to int`: the common upload component used `data.length` for a QByteArray signal argument, which Qt exposes as an ArrayBuffer. The shared correction uses bounded `byteLength`, and a typed-byte end-to-end transfer regression replaces the inaccurate picker mock. This is a confirmed common import defect; correction acceptance awaits the next paired build/test/runtime result. No translation data was written by the failed mismatch check.

### Typed-byte transfer correction result

Package `576f22d7f306d17bbfd2cbcb461ecc5de4d3fbd909978f78c1fc40828acc396c` passed Wasm/native Qt Creator builds 1792683623517/3518 (39.708/58.603 seconds; zero Wasm warnings, four existing native linker warnings; QML 351/351 clean). Full run 1792683626106 passed 202/203 suites, including the new typed-byte transfer regression; its only failure was a stale inventory row for the removed QML picker mock. Removing that obsolete row passed the focused inventory rerun 1792683626107 (1/1). This is a full run plus one corrected-documentation rerun, not a second 203/203 full-run claim.

The sole native host was stopped and restarted through Qt Creator; port ownership, executable and package identity were verified (PID 91196). Native import now completes with a visible generic failure and enabled controls, but the expected specific language-mismatch result has not yet been accepted.

A fresh Edge tab rendered the matching shared Settings UI and opened its native file chooser. Selecting the isolated fixture aborted the Wasm client. Its console identified the primary error as “Suspending the main thread requires asyncify or JSPI”; later memory/table/binding messages occurred after that abort and are not independent findings. The pinned Qt BlobIODevice reader uses synchronous arrayBuffer suspension. A narrow correction is being validated in the existing local-file adapter: asynchronous bounded Blob slices, retaining the same common transfer/service/QML path and single-thread kit. Native metadata/read behavior and shared upload chunk acknowledgement remain canonical. No new feature, task or architecture is introduced.

### Bounded async browser-file reader — build/fixture result

Package `9759e816b27cb8b52ac41ac16fa981c8de235c4ac5ed6fea9540c56a5612bcb2` passed Qt Creator Wasm build 1792683623522 (53.352 s, no warnings) and native build 1792683623523 (19.965 s, three existing linker warnings). Focused run 1792683626108 passed all six affected suites without warnings: LocalFileTransfer, LocalFileSave, LanguageServiceSource, TranslationSourceDrift, ProfileImportServiceSource and QML inventory. The transfer fixture now also passes its arguments through the real QML-to-QVariantMap conversion. No full-suite rerun is claimed for this follow-up.

The old simulator host was stopped through Qt Creator, the port was verified free, and the matching replacement was started once (PID 1203, exact original-workspace executable and `/api/session` package verified). The crashed Edge test tab was closed; a fresh tab successfully loaded the matching shared Settings UI. Runtime import acceptance is still being checked.

On `9759e816`, both native and Edge completed the isolated mismatch import and re-enabled their controls. MCP logs confirm both reached the canonical Language importer and returned `languageMismatch` (rather than a transport refusal). Edge remained responsive without the earlier suspension abort. A second generic Language toast covered the requester-owned file-result message; the shared caller now disables that generic subscriber during its file operation, preserving one correlated outcome. This final message-only follow-up is in the next paired build. The short wiki Language entry now documents Export JSON, merge behavior and matching-language refusal.

## Language task 24.1 acceptance — 2026-10-23

Final package `a6a22f94dac5e95b4ce6bbafe19ca85137487cfa734bbdb4a1b069cecdcc8984` passed Qt Creator Wasm/native builds 1792683623526/3527 (40.779/42.775 s; zero Wasm warnings, three existing native linker warnings). Its host was restarted only through Qt Creator, with the previous listener stopped first; sole PID 9316, original-workspace executable and package identity were verified.

In a fresh Edge client, importing the isolated wrong-language JSON displayed exactly one specific refusal: “Select the file's language before importing it.” Controls returned to ready and the client did not abort. Importing the previously exported English JSON then displayed “Translations imported.” The exported document had no custom entries and the merge did not change custom translation text. The shared Add Language page showed its explicit all-offered-languages-already-available state and returned to Settings; no language was created or deleted. Native navigation remained independent. Native Export opened the asynchronous “Save language translation” picker; Cancel closed it and re-enabled both file actions without creating a file.

Together with the earlier paired language-switch/String Browser search checks and the passing canonical edit, retained-draft, stale-context, import/export, selected-provider/failure and cancellation fixtures (`tst_languageservicesource`, `tst_translationsourcedrift`), this closes task 24.1's Mac shared-language scope. The six affected suites passed in run 1792683626108 before the final notification-only QML adjustment; that adjustment passed both builds and the live refusal/success checks. Paid translation and public submissions were never invoked. Task 24.4's wider support/focus inventory and deferred device/accessibility gates remain open.

## Shared navigation closure — 2026-10-23

Matched package `734aca78ca68e253db8df6095a467e4b146de517b27ac9e14a45a2d3725e8fc3` replaces both roots' page catalogs and ordinary AppShell handlers with `SharedAppNavigation.qml`. The shared stack owns the 33 destinations, status/background composition, canonical instant transitions and navigation guard. Native window/device lifecycle and browser history/session wiring remain at their platform boundary. Relative to the frozen pre-extraction roots (5,894 lines), the two roots plus shared component contain 5,518 lines: **376 lines removed overall**, without a second UI.

- Qt Creator Wasm build 1792683623547 passed in 38.860 s with zero warnings; native 1792683623548 passed in 43.141 s with three existing linker warnings. The shared-shell native QML gate is 352/352 clean. Full run 1792683626111 passed 203/203 suites in 31.890 s without warnings before the seven-line popup cleanup; final focused run 1792683626112 passed shared controls, equipment service, remote session backend and QML inventory (4/4, 3.040 s, no warnings).
- Cold browser startup initially registered zero domain services after introducing a readonly StackView reference. Restoring a direct `pageStack` alias in both roots restored all 41 services and cold Home rendering. The precise Qt initialization cause is not established. The existing remote-session suite now also compiles a QML list-binding fixture and verifies that both listed services attach only to a verified session.
- Native and browser Home, persistent status/bottom bars, Settings entry, Equipment inventory, its full cards/actions and shared edit form rendered. Edit cancellation preserved data. Native Escape returned Home. Browser Back, Forward and in-app Back traversed Home/Settings; Equipment deep-link reload returned its inventory. Browser navigation left native on its own page. Earlier equipment mutation/restore evidence in `evaluation.md` remains applicable.
- Live testing reproduced Home's equipment picker remaining visually above Settings. The shared stack now closes popups under its covered page through the existing `PopupCloser`; repeating the Home-picker-to-Settings sequence in both final clients removed the picker visually. Wasm accessibility nodes can remain after a popup closes, so screenshots, not stale nodes alone, determined this result.
- The exact sole native executable and port owner were checked after stopping the previous process and running once through Qt Creator (PID 38780). `/api/session` matched the packaged identity above. `/`, `/wasm/` and `/legacy/` returned HTTP 200. A fresh MCP session on the same port initialized, listed 67 tools and 24 resources, and completed read-only `machine_get_state` without an error; only that temporary session was then closed.

This closes tasks **2.2 and 2.27** with the prior shared equipment layout/action/editor fixtures. No checklist tasks were added. Task 16.1 remains open for resize/orientation and full focus acceptance; the attempted native resize did not change the window, so it is not evidence of a pass. Full route/draft/operation acceptance stays under its existing tasks; these checks do not claim remote/mobile or screen-reader acceptance.

### Root failure/recovery acceptance on the same package

The remaining task 29.1 check used the actual running HTTP router, with the built `Contents/Resources/webclient` directory temporarily renamed and restored in `finally` (all requests completed in 0.232 s). Enabled and restored `/` and `/wasm/` returned 200 with 2,713-byte entry documents. While unavailable both returned bounded 503 responses (34 bytes, `Cache-Control: no-store`); `/index.html?route_probe=closure` returned 302 to `/legacy/?route_probe=closure` in every state. `/legacy/` and `/api/session` stayed available (200) while the bundle was absent. The package identity was unchanged after restoration. This closes 29.1 without code changes or another build. It does not close 29.3's legacy upload-form/XHR acceptance.

### Legacy namespace acceptance

The generated `/legacy/upload` page's real file-picker/XHR flow sent the deliberately invalid 51-byte `closure-invalid.apk` and displayed **Upload failed: Not an APK file**. The streaming handler rejects those bytes before save/install and cleans its temporary upload. This proves actual POST body dispatch from the nested legacy page without changing user data or installing anything. The browser file-picker automation took about 3 h 44 min to return; that automation path will not be repeated as routine validation. The host stayed on PID 38780 and the package directory was fully restored.

Independent source audit found page navigation under `/legacy/`, uploads under `/legacy/upload` and `/legacy/upload/media`, and intentionally unchanged absolute `/api/`, `/auth/`, download and `/icons/` contracts. No relative static URL or wrong nested POST destination was found. A live `/icons/espresso.svg` GET returned 200 with SVG content. Together with the existing route/body-dispatch classification and API/download tests, this closes task 29.3's namespace contract. It does not accept media processing, firmware installation, or every domain API action; those retain their own workflow tasks.

Native Home also rendered after maximizing and restoring its window; Edge Settings remained usable after half-screen resizing and restoration. These are desktop resize checks, not portrait/mobile orientation acceptance.

### Profile selection and new-draft acceptance on the same package

Both native and Wasm opened all five shared new-profile entry paths (Pressure, Flow, D-Flow, A-Flow, Advanced), displayed the intended editor, then discarded each unsaved local draft and returned to Profile Picker. Every return retained the host's active **D-Flow / Q**; new drafts showed their own defaults without applying them to the host. No profile was saved, loaded or deleted. The five native paths were exercised in one bounded UI batch (12.487 s), and the four remaining browser paths in another (8.131 s) after the Pressure check.

Native **Set as auto-load profile** for the already active D-Flow / Q appeared in the browser's shared strip. Browser **Disable auto-load** removed the native badge/strip again, restoring the original unpinned preference. Browser favorites-order opened and cancelled without saving; its D-Flow query with Favorites off and Decent Built-in on survived reload, while native retained its independent default Favorites view. No favorites or profile ordering were changed. Existing canonical profile/draft/service fixtures and paired builds apply. This closes task 18.1; full import/edit/delete/concurrency action acceptance remains assigned to 18.2–18.5.

### Remaining route-context checks

On `734aca78`, Beans query **Gran Bar** and its expanded finished shelf survived reload: both matching finished bags remained visible and the shelf still offered Hide finished (2). No bag was edited or restored. Profile Picker query/category restoration and new-draft guards are recorded above. Community selection loss on reload was reproduced visually and remains an existing task 16.4 defect; its correction is the only new implementation in the next bounded batch.

String Browser query **Steam** and Custom filter survived reload. Switching back to All returned 100 matching entries. No language text was edited and no provider request was sent.

Browser Back at Home was checked with a fresh Wasm document reached from `/legacy/`: Back left Wasm and returned to that previous HTML document. A 1.3-second physical hold on Home's Sleep/Quit gesture left the browser open on Home and the simulator Ready. No machine operation or profile change occurred.

## Community correction — final Mac acceptance

Package `3d757ba3fcac257cc1e7f8b9290a8b5bb49fa4e58ba78df41cbc830af0ce38ee` fixes the reproduced selection/position loss in the existing shared Community page. Browser history carries only bounded selected-entry ID, page count and scroll offset; shared page logic restores pages sequentially, stops at the current catalog size, clamps the viewport, and yields to filter changes or a new selection. The 11-line `CommunityBrowseNavigation.js` helper is shared by native/Wasm and the existing operation-navigation fixture; the shared page does not import the browser-only history adapter. There is no new service, UI, test target or checklist task.

Qt Creator Wasm build 1792683623551 passed (80.716 s, zero warnings); native build 1792683623552 passed (69.942 s, three existing linker warnings, QML gate 353/353 clean). An interrupted run supplied no result while the other checkout was active. After selecting the original workspace as startup project, full run **1792763853090 passed 203/203 suites in 30.630 s, with no failures, skips or warnings**. Port 8888 was free before Run was pressed once through Qt Creator. The sole replacement listener was PID 96139, at the original workspace's `build/Qt_6_12_0_for_macOS_Debug/Decenza.app/Contents/MacOS/Decenza`; `/api/session` matched the package above and confirmed Simulation Mode.

Community's later-page **Kulitorum Golden** selection and scroll position survived reload and browser Back/Forward. Changing Type to Items reset the selection and scroll, displayed the six-item result set and disabled Add to Library. Nothing was downloaded or installed. Browser navigation left native Home unchanged. `/`, `/wasm/` and `/legacy/` returned 200; a fresh MCP session initialized, listed 67 tools and 24 resources and completed `machine_get_state` without error, then was closed.

The browser shell rendered Home, Settings and Community at 768×1024 portrait and 1024×768 landscape, then returned to its default 1280×720 viewport. Status/bottom bars, Back and popup controls remained visible and usable. Settings search accepted keyboard input; Escape dismissed it visually without leaving Settings. Native search accepted the same input, Escape returned focus to Search settings, and a second Escape returned Home. Together with the prior native maximize/restore, Edge resize, covered-popup and navigation checks, this closes **16.1**. The known Wasm accessibility tree can retain closed-popup nodes; these checks do not close the separate screen-reader/device gates.

The checklist remains frozen at 190 tasks: **94 complete**. Existing tasks 2.2, 2.27, 16.1, 18.1, 29.1 and 29.3 were closed against recorded implementation and validation; deferred requirements were not removed. Task 16.4's remaining route/draft acceptance is still open.
