# Shared-service adapter cleanup — Mac evidence, 2026-10-22

This implements task 30 following the independent [reuse review](reuse-review.md). These results establish the bounded Mac cleanup, not full application or remote/mobile acceptance.

## Architecture and deletions

Native and Wasm now use the same PreparationService facade and host-owned PreparationState/controller, plus a shared HostSessionService for validated atomic machine, appearance and accessibility facts. LanguageService delivers bounded, revision-consistent catalog pages from the canonical native TranslationManager. HistoryService and LayoutService replace private last-shot and layout clients. Shared preferences/control services handle graph options, pitcher selection, tare and portal reconnect.

Deleted obsolete browser implementations: `remoteshothistorybackend`, `remotesettingsnetwork`, `remotepreparation`, `remotesessionactions`, `httpshotgraphbackend`, and the diagnostic `httpdiagnosticsbackend` (headers and implementations). Deleted the old development `serve.py`, `test_serve.py`, `probe.html` and the graph-adapter-only test. Diagnostics retain shared production form controls and the recorded production graph. Current docs and CLAUDE.md now assign native/Wasm parity to shared QML and canonical services; legacy HTML gains no new sharing abstractions.

The browser session adapter retains lifecycle, stale state and exact serving-origin/package/host-epoch checks. The verified entry document supplies the bootstrap identity; authentication/origin checks and bounded binary QtRO delivery remain. Ordinary service replicas attach only after HostSessionService validates identity. Reentrant disconnects cannot restore fresh state, and old transport teardown cannot retire a new connection. The independent final source review found no remaining actionable issue in those guards.

An audit of `src/webclient` and developer C++/QML found no QNetworkAccessManager/QNetworkRequest domain client. Its remaining API URLs are the QtRO WebSocket and selected background-image resource. HTTP remains appropriate for documents/assets, files and existing authentication/legacy/API contracts. Browser lifecycle/history, downloads/file selection/clipboard/accessibility and native hardware/OS/persistence remain platform boundaries.

## Builds and tests

- Wasm Release build **3469** passed in 43.644 s, zero errors/warnings.
- Native Debug build **3471** passed, zero errors/warnings; QML gate **349/349**. Both kits and packaged assets identify **23b935d7d4b0dbb0a4e992f8eb9ef586f7ffc21c0ee4ac2d5bcde5876ae67968**.
- Focused run **6087** passed 14/15 suites. The remaining preparation fixture immediately reacquired an in-process replica before deferred release and missed signal wiring. It now uses fresh TCP nodes, matching browser reconnect. This changed the test only; rerun **6088** passed.
- Full Mac suite **6089** passed **203/203**, zero failures, skips or warnings, in **30.740 s**. Includes the live opt-in localhost ShotServer route probe, bootstrap/integrity fixtures, malformed/stale session/catalog tests, correlated background loading, preparation capture/reconnect baselines, graph preference conflicts and existing domain regressions.

Earlier build corrections removed `final` from a creatable QML element, shared generated test contracts to avoid duplicate CMake outputs, corrected test moc ownership/protected fixture setters, and made the catalog's bounded size conversion explicit. No compatibility fallback was added.

## Live Mac observations

Qt Creator stopped the old app before launching the matched native build. The listener was verified against the original checkout executable; only one process owned port 8888. Session data confirmed Simulation Mode and Simulated Scale. No physical-machine operation was started.

- Both `/wasm/` and root `/` rendered the common Home UI through the new QtRO bootstrap, with no browser warning/error during the normal connection checks.
- Browser Steam selection changed the host preparation mode without starting flow and appeared selected on native Home. While the browser stayed on Recipes, native Hot Water selection left its route intact; returning browser Home showed the shared selection.
- A running document's `#recipes` change now opens the shared Recipe list. This corrects the failure recorded in the earlier navigation evidence; complete task 16.4 remains separately open.
- Graph options on the October 22, 8:18 PM shot changed flow scale from 3x to 2x through shared preferences; native state confirmed 2x. Restored **3x**.
- Applied Last Shot (Advanced) temporarily. The same latest-shot curves rendered as the native and browser wallpaper through HistoryService ([browser capture](evidence/qtro-root-shot-background-23b935d7.jpg)). Restored background **none**, retaining the original advanced flag.
- Browser language selection changed native and browser text to Danish after complete catalog delivery. Restored **English**.
- `/legacy/` still served the 1,307-shot HTML history; typing Sublunar returned **72 shots**. The full-suite route probe also exercised API/file and bookmark/legacy route contracts.
- Stopping host PID 79747 through Qt Creator immediately removed usable browser state. After port 8888 was free, Qt Creator launched one new process, PID 81175, with a fresh epoch. The old tab's explicit Reconnect was refused and offered **Reload interface** ([capture](evidence/qtro-host-restart-rejected-23b935d7.jpg)); using it restored Settings against the new epoch. Original background/graph preferences remained restored. The short wiki manual now instructs reloading after native restart.

## Measured reductions and limits

The retired proxy/wrapper files total **2,041 lines** (1,392 developer-source + 649 test lines), excluding small documentation/build-wiring reductions. Separate disjoint implementation slices removed about **295 appearance-production lines**, and **1,223 diagnostic source lines plus 478 adapter-test lines**. The new preparation facade/source adds 308 lines while replacing a 74-line adapter; it intentionally establishes one canonical native/browser service. These are slice measurements, not a claimed total net reduction across the entire working tree or all new services.

The production graph/form diagnostics and prior benchmarks remain available as fixtures/evidence. Actual screen-reader, phone/tablet, remote HTTPS, real-machine and multi-client host-impact acceptance remain open in the main tasks. Full parity is not inferred from these 203 test results.

## Bounded review follow-up — shared build inputs and navigation

Task 31 is implemented on matching package **7c918d2b7584ab83d16bb0c25911facde83b9cf5cf1ce1ecdd8fe873844343c2**. The explicit `cmake/shared_remote_inputs.cmake` replaces duplicate native/browser declarations: **135 source/header paths**, including all **41 QML metadata headers**, and **42 replica names**. Native source implementations and source-rep generation remain target-owned. Before/after concrete source multisets and replica lists match; a second agent independently checked scope, registration and platform boundaries. The persisted [audit](evidence/shared-remote-inputs-audit.json) records the exact sets. Net reduction is **157 CMake lines**.

`OperationNavigationPolicy.js` now shares ordinary push/stay and Settings/History request decisions. Both shells retain the current Recipes/Beans/Equipment/Favorites/Community instance; browser Settings no longer resets the current tab for an unspecified/invalid request. Explicit valid tabs update in place. History keeps a current search for null/undefined requests and clears an explicit empty filter. Native stack mutation and tracked browser history remain shell callbacks; drafts, guarded Back/Forward, editors, screensaver and hardware paths were not extracted. Independent peer review found no actionable regression.

Wasm build **3477** passed in **56.378 s** with no warnings. Native **3478** passed in **49.154 s**, QML gate **349/349**. Its five toolchain warnings were the two Qt private-module notices, the existing local OpenSSL macOS deployment-version warnings, and the large debug unwind section; no compiler error occurred. Focused run **6090** passed **7/7** (StackView policy, QML registration/inventory/resource closure, packaging and live routes). Integration run **6091** passed **203/203**, no failures/skips/warnings, in **30.640 s**.

The real StackView harness verifies retained page instance/local selection, depth, tracked-entry count, current/fresh Settings requests and null/empty/concrete History filters. Live smoke checks verified both native and browser Home→Settings→tab selection→Back and browser Home→History→Back against one native process, **16255**, from the original checkout. The first browser refresh preceded listener startup and reached a connection-error document; a fresh tab loaded successfully after listener ownership was confirmed. These smoke checks do not claim that every repeated custom-widget request was manually exercised; complete parent-workflow coverage remains open in tasks 15/22/26.

The temporary `DECENZA_SHOTSERVER_ROUTE_TEST_URL` override was removed through Qt Creator after the full run. Its Build Environment summary is back to **Use System Environment**. The root browser is left at Home; original preferences remain restored.

## MCP shared-port verification — 2026-10-22

Verified the running original-checkout native process **16255** on **port 8888**, package `7c918d2b`, without restarting it or changing settings. `POST /mcp` completed the 2025-06-18 initialize/initialized exchange (200/202), advertised MCP surface **1.19.0**, and successfully served ping, **67 tools**, **24 resources**, `machine_get_state` and the matching machine-state resource. State reported connected/Ready, 93°C and zero flow, matching the existing Wasm simulator view. The temporary legacy session was explicitly closed afterward; other sessions were not touched.

Also verified the 2026-07-28 path: server discovery, the same 67-tool catalogue and machine-state call succeeded. Legacy `GET /mcp` returned SSE headers and retry/event-ID framing; modern `subscriptions/listen` returned its subscription acknowledgment. The streams were intentionally bounded to five/three seconds and closed by the probe, so curl's timeout exit was expected rather than a server failure. No live state-change notification was induced; protocol fixtures cover notification delivery and filtering.

Asynchronous `shots_list` (limit one) and `recipe_list` also returned **200**, with one history result and 15 recipes. Only counts were retained in the verification summary.

While the legacy event stream was open, machine/resource reads, root Wasm delivery and legacy HTML delivery all returned **200** on the same listener. The already-open Wasm client retained live Ready/time display through its QtRO connection. A request with an unrelated Origin returned **403**. No machine command, data edit, credential change or remote exposure was performed; live authenticated remote access remains a separate gate.

Focused Qt Creator run **6092** passed **5/5**, zero failures/skips/warnings: `tst_mcpserver_session`, `tst_mcpserver_protocol`, `tst_mcpremoteaccess`, `tst_webrequestguard` and `tst_remotesessionserver`. No runtime fix or rebuild was needed for these checks.
