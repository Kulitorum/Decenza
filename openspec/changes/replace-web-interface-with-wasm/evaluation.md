# WebAssembly feasibility evaluation

Routine screenshots were removed from the source tree on 2026-10-24. Recorded
observations, measurements, test/build results and upstream reproduction links
remain; do not repeat passed checks to regenerate the capture archive.

> **Routing-history note (2026-10-22):** The historical entries below record evaluations performed while Wasm was additive at `/wasm/` and HTML was the default. Do not rewrite those observations. Their route/default statements are superseded by the current decision: Wasm serves `/`, `/wasm/` remains an alias, and the retained HTML implementation moves under `/legacy/`. Task 29 tracks that cutover and its API/bookmark/recovery checks; these earlier records do not establish its completion.

## Qt Remote Objects transport preflight — 2026-10-21

The maintainer authorized building QtRO for Wasm and proving compatibility before changing the architecture, reusing the discovered SynQt project where possible. **Mac transport GO:** unmodified Qt 6.12.0 QtRO built and installed into a workspace-local prefix through Qt Creator. The isolated native host and Wasm client passed typed properties, signals, queued method results, model insert/change/remove, and fresh initialization after forced disconnect in both the in-app browser and Safari 27.2. No watcher polling workaround, Asyncify flag, or Qt patch was required. A deliberately stale initial source correctly failed the harness.

The probe adapts the Apache-2.0 SynQt transport spike. [Full evidence and repeat instructions](../../../tools/qtremoteobjects-probe/results.md) distinguish this isolated transport result from Decenza integration. Existing HTML and production transport code were not changed. HTTPS/authentication, paired package enforcement, shared QML service integration and deferred remote/mobile acceptance still need validation before broad adoption.

## Scope and decision

Started 2026-10-20 at source commit `5431bd5683e60855e3d4a55df4b7c0415391dbf1`, in the original `Decenza` checkout on `main`. The experiment uses browser Wasm → HTTP → a separate, user-started native Qt Decenza process → DE1 simulator on this Mac.

**Recommendation: continue as a bounded, opt-in second interface; keep HTML intact.** Native-to-browser read-only transport, the shared production graph, and the shared text/suggestion control now work on this Mac. The form extraction has focused native regression coverage. Browser popup accessibility remains an explicit issue, and full equipment forms, host writes, remote clients and mobile behavior remain unproved.

**Follow-up decision, 2026-10-20:** after reviewing the Mac graph evidence, the maintainer authorized a bounded implementation attempt for one shared production text/suggestion control as the next additive Wasm slice. Task 2.8 records its native and Wasm build, native regression, and Mac-browser typing/selection/cancel checks. This supersedes the earlier stop decision for the bounded slice. Its implementation and evidence are recorded below; it does not complete task 2.2 or pass Gate 0 or Gate 1.

The maintainer explicitly requested keeping the current HTML UI. `/wasm/` is an additive destination; existing HTML pages, routes, and defaults remain in place. This experiment changes no ShotServer route. Any eventual default switch or HTML retirement requires a separately approved change.

This is an early Mac feasibility decision, **not a passed Phase 1–3 gate or approval for broad implementation**. Real remote clients, phones/tablets, authentication, writes, host-impact budgets, and mobile accessibility remain unverified. The plan and its unresolved tasks are retained honestly.

## Environment and reproducibility

| Item | Observed value |
|---|---|
| Machine | MacBook Pro Mac17,6; Apple M5 Max, 18 cores, 48 GB RAM |
| OS | macOS 27.2, build 26B5112a |
| Stock Qt / toolchain | Qt 6.12.0 native macOS and `wasm_singlethread`; Emscripten 5.0.5 |
| Build environment | Qt Creator 21.0.0-beta2, same root CMake project with separate native Debug / Wasm Release build directories |
| Browser exercised | Codex in-app Chromium; page-reported UA Chrome/155.0.0.0, crossOriginIsolated=false |
| Viewport | 1100×900; graph also resized to 800×900 during replay |
| Native host | User-restarted PID 96729 verified at original `build/Qt_6_12_0_for_macOS_Debug/Decenza.app/Contents/MacOS/Decenza`; TCP 8888 |
| Development browser origin | `http://127.0.0.1:8080/wasm/probe.html` |
| Dataset | `tests/data/shots/blooming_aborted.json`: 253 timestamps through 62.992 s, six measured columns and three goal curves; ignore the extra untimestamped trailing column value |
| Current inventory/history | Four equipment packages; one `/api/shots` response returned 1000 rows / 524,967 bytes (not a claim about total database size) |

[Run guide](../../../tools/wasm-feasibility/README.md) documents Qt Creator kit selection, the serving command, replay controls, instrumentation and limits. All builds and the targeted test were run through Qt Creator. Native Decenza was started/restarted by the maintainer. No separate checkout was created and no browser database or device connection was introduced.

## Implementation and observed behavior

- Root CMake selects an isolated Wasm target before native services are configured. The browser links Quick/QML/Controls/Network/Graphs, the actual `FastLineRenderer`, typed `HttpDiagnosticsBackend`/`RecordedTrace`, shared QML, and bundled fonts. It does not link BLE, native controllers, storage or simulator logic.
- Native `ShotGraph.qml` is now an adapter to shared `ShotGraphView`, `ShotGraphData`, `ShotGraphOptions`, `ShotGraphStyle`, `FastLineTraceLayer`, right-axis labels and end-marker hit target. The shared view includes axes, goal curves, trace geometry, markers and pump-mode bars. Native portal presentation remains an injected native overlay; portal behavior was not exercised in Wasm.
- Native `EquipmentSummary.qml` and the browser use the same `EquipmentSummaryRenderer.qml`. Native formatting, translated labels and puck-prep lookup remain native responsibilities. This establishes shared summary presentation, not a complete shared equipment card or form.
- The browser now consumes the same `SharedTextField` and `SharedSuggestionField` as native `StyledTextField`/`SuggestionField`. A local equipment-name/grinder fixture exercises edits and Apply/Cancel without saving. The native wrapper retains its app-specific `SelectionDialog`; the browser fixture supplies its own Qt dialog. No browser mutation or machine command was sent.
- Live browser labels matched native Idle/Ready, then Sleep after the maintainer's native action. Two independent tabs read the same host state. Stopping/restarting the temporary proxy produced explicit stale values then automatic recovery without reloading. These were short functional checks, not a soak or live-subscription latency test.
- Native build `1792509381465` passed 271/271 QML files after initial trace/summary extraction; the maintainer completed the requested restart and graph/card check. Final full-graph native build and restart are recorded below; detailed final visual comparison remains user-observed rather than instrumented.
- The existing HTML equipment form was opened, its name edited locally, then Cancel used; the original name remained. No save was performed. This records functional form behavior, not input latency.

## Build and regression evidence

| Check | Result |
|---|---|
| Initial typed transport Wasm build | `1792509381462`: succeeded, 0 errors/warnings |
| Final full-graph Wasm build | `1792509381471`: succeeded in 19.815 s, 0 errors/warnings; generated qmllint target included |
| Final full-graph native Debug build | `1792509381473`: succeeded in 29.262 s, QML gate 277/277 clean; 3 existing linker warnings |
| Native restart | Maintainer reported “new build running”; original checkout process and Idle/Ready API verified |
| QML registration regression | Qt Creator run `1792509383772`: `tst_qmlregistration` passed, 0 failures/skips/warnings |
| Full unit suite / other platforms | Not run for this bounded experiment; required before a requested PR |

The first full-graph builds exposed unqualified QML accesses and an id shadow; these were corrected without weakening the diagnostic baseline. Browser console checks after the final load found the Qt QML-debugging notice, with no QML runtime errors. This remains a local development build; production packaging must disable debugging.

## Loading and memory measurements

The server used `--gzip --mbps 20 --latency-ms 30`: aggregate response-body throttling and 30 ms added per response. It sends `Cache-Control: no-store`. This is **synthetic shaping on loopback, not a real 30 ms RTT link, cleared browser compilation cache, or warm-cache test**. The first run also fills the server's gzip cache; native compilation overlapped some runs. Results are useful early estimates, not acceptance passes.

| Measurement | Five runs (ms) | Median / maximum |
|---|---|---|
| Wasm first presented frame since navigation | 6743.4, 5681.5, 5808.5, 5828.1, 5664.9 | 5808.5 / 6743.4 |
| HTML equipment inventory inserted into DOM | 104.5, 101.3, 103.3, 108.2, 104.9 | 104.5 / 108.2 |

Wasm and HTML measure different milestones: the actual Qt window's first frame versus four inventory cards inserted in the DOM. Neither is end-to-end editing usability. The HTML baseline injects a MutationObserver into a development-proxied copy of the existing native page. An unrelated shared-menu power-status request is outside the proxy allowlist; this does not change the inventory result. The original HTML service remains directly available at port 8888.

| Graph-slice asset (before form extraction) | Raw bytes | gzip bytes (server algorithm) |
|---|---:|---:|
| Wasm | 37,853,391 | 13,537,285 |
| Application JavaScript | 243,791 | 55,631 |
| Qt loader | 13,775 | 4,536 |

The page reported 52,428,800 bytes of allocated Wasm linear memory at first frame. That is not resident memory, total browser/GPU usage, or a peak. The roughly 13.5 MB compressed Wasm payload alone needs about 5.4 seconds at 20 Mb/s; caching and narrower dependency/resource packaging matter for an optional UI. The 8 s cold / 3 s warm acceptance budgets remain unpassed because their specified conditions were not tested.

## Graph evidence

**Final full shared graph replay: 3779 presented-frame intervals, p95 17.7 ms over 62.992 s.** The right axis was changed from temperature to flow to weight, and the viewport resized from 1100×900 to 800×900 and back during the run. Traces, goal curves, tick labels and start/end markers remained visible. This is below the 33 ms project threshold for this Mac run, without establishing the full device gate. Desktop typing updated the shared summary locally; this does not verify the production input control or input-latency budget. The harness requests continuous window updates every 16 ms during replay, appends recorded data at its actual timestamps, and measures intervals between QQuickWindow frameSwapped signals over 62.992 s. This avoids misclassifying idle gaps between the roughly 4 Hz recorded samples as slow rendering. It measures whole-window presentation cadence, not CPU/GPU render duration.

An earlier trace-only probe measured p95 18.6 ms over 3748 intervals. It is superseded by the final shared-graph run. An initial attempt without continuous repaint measured sparse update cadence and is explicitly discarded as a performance result.

## Shared form-control slice (2026-10-20)

[Dependency audit](dependency-audit.md) records the exact production input chain. `SuggestionField` depends on `StyledTextField`, keyboard composition/commit/show/hide, keyboard-aware container focus, accessibility exploration and focus suppression, suggestion popup keyboard navigation, and app styling/translations. A generic browser TextField does not prove those native behaviors survive extraction.

The graph extraction demonstrates a practical typed presentation boundary, but required several shared files plus native adapters; it was not simply adding the existing native module to a browser target. The small equipment summary was straightforward; the actual input control crosses a much wider dependency boundary. No reliable developer-hour cost was tracked, so elapsed session time is not presented as an engineering estimate.

The bounded extraction adds `FormControlStyle`, `NativeFormControlStyle`, `SharedTextField`, and `SharedSuggestionField`. Native caller APIs, translated labels, inline clear/arrow buttons and the accessibility action row remain in thin native adapters. The portable existing `Keyboard` class is compiled into the Wasm module. The browser fixture supplies suggestion values and a local draft/preview; it does not fetch or save an equipment registry. `KeyboardAwareContainer`, the native browse dialog, production equipment editing, and host command/storage behavior were not refactored.

The extraction revealed two concrete issues and fixed them: whole-font bindings conflicted with existing callers' nested overrides, and ListView's automatic initial selection bypassed the intended exact-text Return behavior. Font subproperty bindings preserve caller overrides; a changed suggestion model now clears its keyboard highlight. Tests cover these behaviors.

| Shared-control check | Result |
|---|---|
| Wasm Release build | Qt Creator `1792509381483`: succeeded in 21.947 s, zero errors/warnings, generated QML lint included |
| Native Debug build | Qt Creator `1792509381482`: succeeded in 33.455 s, QML gate 281/281 clean; 3 existing linker warnings |
| Focused tests | Qt Creator run `1792509383775`: both CTest entries passed in 1.820 s (`tst_sharedformcontrols`, `tst_qmlregistration`), no failures/skips/warnings |
| Runtime control regressions | 11 substantive Quick Test cases plus init/cleanup passed: filtering, selection, external binding after selection, Return variants, arrows/Escape, accessible labels/password masking, exploration-vs-activation keyboard requests, actual QInputMethodEvent preedit/commit, nested font overrides |
| Native live check | Maintainer reported “running”; PID 15666 verified in the original checkout, owning TCP 8888. Browser reads returned Idle/Ready. The reply confirms restart; detailed native visual/OS-keyboard checks were not independently observed. |
| Existing HTML continuity | Direct native `/equipment`, `/beans`, `/recipes` each returned HTTP 200 `text/html`; no ShotServer source or route changes |

The headless IME test sends actual Qt input-method events to the shared TextField: `displayText` filters while committed `text` is empty, then a commit updates the external binding. It does not test a phone's predictive keyboard, OS focus behavior, or an actual screen reader.

Final-build Mac Chromium checks at 1100×1100 exercised typing/filtering `Niche`, arrow/Return selection, editing the equipment name, applying to the local summary, changing/clearing drafts and cancelling back to the applied values, browse-dialog cancellation, and free-text/Escape. API labels continued to display native simulator Idle/Ready. Cancel → Tab returned focus to the name field. These are functional checks, not input-latency measurements. Console inspection found only the existing Qt debugging notice, with no QML runtime errors.

The final form build is 38,033,674 raw / 13,591,952 gzip bytes of Wasm (54,667 gzip bytes above the graph-only slice). One shaped reload reached its first frame in 6801.3 ms and allocated 62,914,560 bytes of Wasm linear memory. This single observation is not a new startup distribution, RSS or peak-memory measurement; the graph-slice results above remain separately labelled.

**Accessibility issue and workaround:** Qt 6.12's Wasm accessibility bridge creates HTML `<dialog>` shadows for accessible dialogs but mirrors Qt visibility only with `inert`, leaving an otherwise-visible closed HTML dialog out of the browser accessibility tree. The app-owned `qt-dialog-accessibility.js` scoped to Qt's shadow root mirrors `inert` to the dialog's `open` attribute and resets browser dialog geometry to match Qt. With this workaround, browser accessibility-tree inspection exposes the browse rows and Cancel. Activating the `Niche Duo` row updates the visible QML field and status, closes the browse dialog, and returns focus to Browse; the shadow input still reports the old value (`Varia VS6`). Editable-field names are also absent from the Wasm shadow input. Source findings and upstream status are recorded in [dependency-audit.md](dependency-audit.md). The workaround is not a Qt fix or full accessibility acceptance: actual screen-reader behavior remains pending, as do real phone/tablet keyboard, touch, visual and accessibility checks. Task 2.9 remains open.

The work stayed within two shared controls, explicit style inputs, native adapters, one local fixture and focused runtime tests. No reliable developer-hour total was tracked; the prior **1–2 developer-day** figure remains an estimate. The remaining cost includes full native form integration, browser popup accessibility, mobile validation and remote write semantics, each still separately gated.

## Popup repair and live inventory continuation — 2026-10-20

The app-owned dialog workaround now exposes both autocomplete and browse choices in the browser accessibility tree. Selecting Niche Zero through the autocomplete accessibility action closes suggestions and returns focus to the field; browsing and selecting a row closes the dialog. Explicit `focus: true` and focus return in the browser host’s Dialog also fix cancellation returning to the document rather than Browse. Reopen/Cancel now returns browser focus to Browse. Qt shadow dialogs have `open` while visible and zero default margin/padding. This establishes browser-tree actions, not actual VoiceOver/TalkBack acceptance.

The typed `HttpEquipmentBackend` loads only the existing `GET /api/equipment`. The read-only panel uses `EquipmentSummaryRenderer`, escapes host text, distinguishes loaded/empty/error/stale states, and clears old rows when the origin changes. A failed same-origin refresh retains stale rows; a successful refresh clears the warning. Response schemas, duplicate IDs and a 2 MiB accepted-body cap are checked. Browser networking may buffer independently of that cap. Full puck-prep presentation and editing remain unimplemented.

The live panel displayed all four packages from native Decenza’s API with matching grind settings, baskets and inventory state. None was active in the native response. A disposable localhost fixture separately verified an active badge and literal `<b>Escaped & visible</b>` text, then stale rows after HTTP 503, successful recovery, an empty inventory, and a first-load-style error without cached rows. No native data was modified; fixture services were stopped. Direct `/equipment`, `/beans` and `/recipes` checks still returned HTTP 200 HTML.

Qt Creator native build `1792509381488` passed in 6.798 s with no warnings. Focused run `1792509383777` passed all three CTest entries (`tst_httpequipmentbackend`, `tst_sharedformcontrols`, `tst_qmlregistration`) with no reported warnings. The first HTTP test run caught inconsistent HTTP error reporting and reading an aborted reply; both were fixed before the passing run. Final Wasm build `1792509381493` passed in 24.801 s with zero warnings, including QML diagnostics. The current native kit was restored. Browser console inspection showed only Qt’s existing QML-debugging notice, with no new QML runtime errors.

A plain Qt-only `AccessibilityRepro.qml` reproduced missing input names and stale programmatic values. With explicit maintainer authorization, filed [QTBUG-151710](https://bugreports.qt.io/browse/QTBUG-151710) and [QTBUG-151711](https://bugreports.qt.io/browse/QTBUG-151711), including minimal QML, expected/actual results, environment and exact Qt source references. The existing dialog issue remains covered by [QTBUG-144330](https://bugreports.qt.io/browse/QTBUG-144330) and pending [review 707847](https://codereview.qt-project.org/c/qt/qtbase/+/707847). Details and duplicate-check results are in `tools/wasm-feasibility/upstream-accessibility.md`.

The maintainer accepts accessibility as a tracked development limitation and wants confirmed new defects reported upstream after duplicate checks. This does not mark actual screen-reader/mobile testing passed. Remote Wasm deployment now targets trusted HTTPS. Single-thread remains the measured working baseline; multithreading should be considered only for an evidenced benefit, not selected merely because HTTPS permits it. No threading comparison or trusted remote certificate setup was performed in this slice.

## Gate record

- Mac read-only transport preflight: passed in its narrow scope.
- Mac shared graph: built and rendered; p95 frame interval 17.7 ms in the scoped replay above. Equipment summary and production text/suggestion controls are shared. Full production forms remain unimplemented; task 2.8 evidence is recorded above.
- Gate 0: partially evidenced; HTML baseline and datasets recorded, but native replay without a browser and host-impact baseline not measured.
- Gates 1–3: held, not passed. Real mobile/remote checks were deferred by the maintainer; input latency, writes, auth, subscriptions, conflicts, accessibility and soak remain unproved.
- The maintainer authorized continued additive work after task 2.8, including popup investigation and the read-only inventory slice (2.10). Remaining gates stay unpassed; mobile/remote checks remain deferred. Existing HTML remains available and default. No automatic cutover or retirement is planned.

Acceptance budgets remain startup ≤8 s cold/≤3 s warm, input p95 ≤100 ms, bounded writes p95 ≤1 s, graph p95 ≤33 ms, live state p95 ≤500 ms, added native main-thread p99 delay ≤10 ms with unchanged stop decisions and no lost samples, plus a 30-minute two-client soak. The current 2-second polling probe intentionally does not meet the subscription latency budget.

## Historical environment repairs

The user reported a native build failure. Qt Creator build `1792509381455` failed during CMake generation because cached OpenSSL 3.6.5 paths no longer existed. Both `OpenSSL::SSL` and `OpenSSL::Crypto` were consequently absent. Saved the previous cache to `/tmp/decenza-native-cmake-cache-before-openssl-reset.txt`; Qt Creator's reset/reconfigure succeeded with zero errors and the existing two private-Qt-module warnings. It discovered installed OpenSSL 4.0.3. Relevant feature settings and Debug/tests configuration were unchanged.

Native rebuild `1792509381456` succeeded in 54.5 seconds, zero errors, four linker warnings (three macOS minimum-version warnings for OpenSSL 4 and one unwind-section-size warning). The native QML diagnostic gate passed 269/269 files. Tests were not run. This build preceded the root CMake Wasm branch.

**Native launch blocker:** the user's launch was blocked by macOS and the app moved to Trash. At 09:48:44 local time, `amfid` recorded a certificate trust failure with `CSSMERR_TP_CERT_REVOKED`, followed by an invalid-signature failure. An independent `security verify-cert` check using the `codeSign` policy with required OCSP confirmed that the existing Apple Development leaf certificate was revoked. The configured identity was identical before and after the cache reset, and the build output confirms the existing signing step ran. No certificate or trust settings were changed. The cause/time of revocation is unknown. This historical blocker was resolved below. No security protections were bypassed.


### Replacement identity and successful native launch

The maintainer created a replacement Apple Development certificate. Updated the existing macOS kit and this checkout's native CMake cache from `DFA23C5DAFA64BEC7CA9D9D1DFA1746CE0E1C560` to `8C80F01D6928A9C0D4B8DD8DD7E7BDC9B8D0E0F1`; no source identity or unrelated checkout was changed. Qt Creator build `1792509381459` succeeded in 7.239 seconds with zero errors and three existing linker warnings. `codesign --verify --deep --strict` reported valid on disk and satisfaction of the designated requirement. A separate strict online OCSP check did not complete successfully; do not describe it as a passed revocation check.

The maintainer then successfully launched the rebuilt native app. Verified PID 73286 owns port 8888 and runs the exact executable in the original checkout. Its API returned connected Idle/Ready, later Sleep after the maintainer changed the simulator. The previous launch block no longer prevented this local run.



## Mac equipment editing continuation — 2026-10-20

Maintainer authorized continued Mac-only implementation with HTML intact. Added Equipment,
Machine and Preview navigation; the equipment name editor uses the same `SharedTextField`
compiled into the native app. Native form layout remains native-owned; this is not a full
shared equipment editor or completion of Gate 1/2.

The native host advertises `capabilities.equipmentNamePrecondition`. The browser sends only
`name` and the original raw `expectedName`; an older host stays read-only. Storage checks the
name and uniqueness and writes under `DbWriteTxn`, including serialization against separate
SQLite writers. HTTP completion is tied to its own request, including copy-on-write result IDs.
Global native completion signals cannot accidentally finish a native dialog for an HTTP save;
`packagesChanged` and `packageSuperseded` still refresh/repoint native consumers. Existing HTML
clients keep their update contract and do not need a precondition.

Verification on the same Mac:
- Native Qt Creator build 1792509381494 passed (40.545 s). Its four linker warnings concern the
  local OpenSSL dylib deployment target and large exception-unwind table; no code errors.
- CTest proxy suite run 1792509383778 passed (4.710 s). Focused run 1792509383779 passed equipment
  storage, HTTP adapter, shared form controls, QML registration and web request guard (5/5,
  2.080 s, no test warnings). After the final client error/trim/stale adjustment, adapter run
  1792509383780 passed (1.710 s, no warnings).
- Final Wasm build 1792509381499 passed (17.980 s, zero warnings). macOS Debug kit restored.
- User restarted native host PID 56991 from this checkout's macOS Debug app, listening on 8888.
  Browser origin 127.0.0.1:8080 proxies to that host. A temporary equipment name saved in Wasm
  appeared in native `/equipment` HTML; it was then restored through Wasm. Comparing the whole
  GET inventory response before/after confirmed exact restoration, not merely matching names.
- Switching Equipment/Machine preserved an unsaved draft. Pointer navigation worked initially;
  AX activation needed the same explicit PageTab Press action already used by native
  `StyledTabButton` (Qt 6.12 qaccessiblequickitem.cpp:517–550,
  qquickaccessibleattached.cpp:548–551). Final AX tab activation worked. This was corrected app
  usage, not a new confirmed upstream defect.
- Disposable loopback fixture browser checks verified duplicate-name rejection, blank-name
  fallback, cancel without saving, stale-name rejection and draft retention, explicit
  Keep-my-edit followed by successful save, and 503 uncertainty retaining the draft. The UI
  shows the refreshed current name before permitting a deliberate conflict override.
- `/equipment`, `/beans`, `/recipes` still return 200 text/html from the native host. No HTML
  templates or page routes changed. The dev proxy allows only the exact bounded rename JSON
  schema with same-origin checks; the native baseline HTML cannot issue its broader writes
  through that proxy.

The temporary live edit was restored; refusal checks used a disposable fixture. Browser build remains stock Qt
6.12.0 singlethread. Existing text-input accessible-name/value Qt issues remain; actual screen
reader/mobile acceptance is unverified. Session subscriptions, remote HTTPS packaging,
real-device checks, and recipe editing remain pending. This adds a persisted-edit
proof without declaring those gates complete.

## Mac read-only session continuation — 2026-10-20

Added `/api/session` preflight and `/api/session/ws` on the existing native listener, plus
a typed `RemoteSessionBackend` and loopback proxy tunnel. Each connection receives complete
state/telemetry snapshots at 250 ms intervals with protocol version, server-instance UUID,
and sequence. The client remains stale until its first valid stream snapshot, rejects
identity/order/schema failures, and requires an explicit reconnect after disconnect or five
seconds without a snapshot. Equipment drafts survive disconnection and writes stay disabled
until a current session and inventory have been obtained. Machine commands and live graph
history are not part of this slice.

The source audit found important Qt boundaries: `QWebSocketServer::handleConnection()` needs
the unread HTTP request; Qt removes raw socket callbacks during WebSocket cleanup, so raw
ownership tracking must use `QPointer` and explicit WebSocket completion; and Qt's Wasm
message callback bypasses its native frame parser. Consequently the 128 KiB client limit
is an acceptance limit after browser allocation, while native snapshots are limited to
4 KiB and queued output to 64 KiB. The proxy uses nonblocking sockets with a 256 KiB buffer
per direction. Authentication uses the existing host policy and is rechecked on publication.

Verification:
- Final Wasm build 1792509381506 passed in 21.939 s with zero warnings, including explicit
  client teardown while callbacks and member state are still valid.
- Native build 1792509381503 passed in 30.810 s with the three existing OpenSSL/deployment
  and exception-unwind linker warnings.
- Initial focused run 1792509383781 found a raw socket lifetime failure and a test-fixture
  teardown failure under ASan. Both were corrected. Run 1792509383782 passed all six suites
  (session server, session backend, proxy, equipment adapter, request guard, QML registration)
  in 8.250 s with no warnings. Final server run 1792509383783 passed in 1.420 s with no
  warnings after correcting closing-state connection accounting and adding its regression.
- Browser inspection against the earlier native build correctly showed HTTP 404 for the
  unavailable session endpoint and disabled equipment edits.
- The user restarted native Decenza (PID 80254, original workspace, port 8888) after native
  build 1792509381508 passed in 7.544 s. `/api/session` returned protocol 1 and host instance
  `77e0c9cb-eee9-4ed7-b1e1-1557c5cc0e7b`. The browser connected through the loopback proxy and
  displayed Ready, Idle, 93 °C, and zero flow/pressure/shot time matching the native API.
  Disconnect marked readings stale and disabled saving an unsaved equipment draft.
  Reconnect obtained fresh session/inventory data, retained the draft, and enabled Save.
  The draft was cancelled without a mutation. Existing `/equipment`, `/beans`, `/recipes`
  remained HTTP 200 text/html.
- New bundle first-frame marker was 6,824.6 ms
  from navigation with the existing 20 Mbps/30 ms proxy settings; allocated Wasm linear
  memory remained 50 MiB. This is one observation, not a new startup benchmark matrix.

Task 2.15 host-restart recovery was verified against the subsequent graph build (see below).
This does not establish remote HTTPS,
actual authenticated-browser login, real-device background/resume, live graph synchronization,
or multi-client host-impact acceptance.

## Mac live graph continuation — 2026-10-20 (in progress)

The second interface now has a typed adapter for complete snapshots of the native
`ShotDataModel`, using the shared production graph view. The small WebSocket envelope
announces graph revisions; the browser fetches the exact `/api/session/graph` route with
one request in flight and coalesces newer revisions. Each curve is capped at 6,000 entries
(including goal separators), markers at 512, and the response at 2 MiB. A changed host clears
the graph; disconnect retains it visibly stale. Tare and post-shot trimming notify the
revision publisher, including corrections that do not add a new sample.

Native Debug/ASan serializer measurements from Qt Creator run 1792509383787 use 32 runs per
size, nine series with long round-trip doubles, and representative goal curves:

| Points per series | JSON bytes | Median CPU ms | p95 CPU ms | Worst CPU ms |
|---|---:|---:|---:|---:|
| 253 | 65,977 | 1.821 | 1.943 | 2.141 |
| 2,530 | 657,376 | 16.733 | 18.748 | 19.950 |
| 6,000 | 1,558,708 | 39.736 | 41.655 | 43.604 |

The long-graph cost is too high for repeated work on the native control thread. Serialization
now runs on one background worker with a bounded queue of eight pending requests; current
waiters share a captured revision. These numbers measure JSON
serialization, not model capture, HTTP transfer, browser parsing, rendering, or host jitter.
Browser diagnostics separately measure accepted snapshot parse/apply CPU cost, including
synchronous QML notifications, over at most 256 samples; they do not measure GPU frame cost.

Wasm build 1792509381513 passed in 33.869 s with no warnings. Native build 1792509381511
passed all 281 QML checks. Initial graph test runs exposed fixture errors and a closed-reply
read warning, which were corrected. Native worker build 1792509381515 passed in 9.493 s
with the three existing linker warnings. Focused run 1792509383789 passed all seven suites
(graph serializer, HTTP graph backend, settling, session backend/server, proxy and QML
registration) in 8.240 s with no test warnings. Run 1792509383790 passed the graph backend
again in 1.660 s after adding a regression that accepted data stays fresh during a healthy
background refresh and becomes stale on failure.

Live simulator evidence and final browser measurements remain pending. No full graph or
remote/mobile acceptance is claimed yet.

Final Wasm build 1792509381517 passed in 32.090 s without warnings. The reloaded browser
reached its first frame at 7,411.3 ms with 50 MiB allocated linear memory under the same
synthetic proxy shaping. It connected to the still-running older native host, displayed its
Ready state and four equipment packages, and correctly disabled graph refresh because that
host does not advertise graph support. The only new console error-level line was the known
Qt QML-debugging notice.
Qt Creator was restored to the macOS Debug kit. The requested native restart and simulator
shot are the outstanding live-validation dependency.


### Native graph host restart verified

The maintainer restarted the original workspace macOS Debug executable, PID 12272 on port
8888. The server UUID changed from `77e0c9cb-eee9-4ed7-b1e1-1557c5cc0e7b` to
`5e45dc1f-8b7f-4dc6-b195-0ebb1afda205`. The existing browser tab showed the closed connection
and stale readings; explicit Connect obtained the new preflight and restored Ready/Idle
readings. The host advertised `graphSnapshot: true`, revision 1, and the browser successfully
loaded the empty pre-shot graph with both native target weights at 42 g. No browser reload
was needed for restart recovery. Task 2.15 is complete within its Mac-only scope; live shot
and performance evidence for 2.16 remains pending.

Eight concurrent read-only graph requests against the restarted native host returned HTTP
200, matching host/revision and 457-byte empty snapshots, with observed round-trip durations
31.08–31.64 ms. This is a small idle smoke check, not queue saturation or a soak test.
`/equipment`, `/beans`, and `/recipes` each remained HTTP 200 text/html. The browser connected. A simulator shot has been
requested from the maintainer; live traces, mid-shot rejoin and graph CPU measurements await it.

### Two simulator shots and recovery evidence

The first simulator shot rendered live in the browser from 8.2 s onward with the native
Filling, Infusing [P], Pouring [T] and End markers, goals and sensor traces. At 22.4 s the
browser was explicitly disconnected: the graph and readings remained visible but stale.
The native simulator finished independently at 25.6 s; reconnect recovered its full graph.
The endpoint returned revision 394, 30,573 bytes, nine sensor series (129 samples each for
pressure/flow/temperatures/resistance variants; weight 119, weight flow 118). Stop/final
graph weight was 35.454 g. Native shot review exposed the same phases and approximately
35.6 g outcome. This is semantic/endpoint agreement, not a captured native pixel comparison.

Across 94 accepted browser updates, parse/apply CPU was 1.8 ms median, 3.4 ms p95 and
19.3 ms maximum. The latest request took 60.9 ms. These are snapshot processing and
request timings, not GPU/frame cadence or synchronized emission-to-visible latency.
Native snapshot: `evidence/mac-native-graph-shot.json`.

A second simulator shot completed while the browser remained disconnected. Reconnect
replaced the first graph with the second (25.4 s graph data, 35.3 g stop weight); it did
not append the second shot to the first. The smaller 800 × 900 browser viewport retained
the scrollable graph and controls. Native snapshot: `evidence/mac-native-graph-second-shot.json`. The viewport override was then reset.

Neither reconnect happened before the shot ended, so mid-operation join remains
unverified. Native UI automation stalled during a read, and further live checks use
browser/API evidence instead. Task 2.16 remains open for mid-operation join and the
remaining native visual agreement; mobile, paired host-impact and soak gates remain
unclaimed. The measured short-shot browser CPU does not currently justify incremental
transport or a multithreaded Wasm build.

### Mid-shot join verified

After the recipe host restart, the browser was disconnected with an empty graph. The
maintainer started another simulator espresso. Connecting at 15.7 s showed Espresso /
Pouring and immediately loaded 15.6 s of accumulated graph data, including samples from
the start. It then followed the shot to Ready at 25.3 s, with 25.2 s of graph data and a
35.4 g end marker (35.5 g live scale reading). The final native snapshot was revision 385,
with 127 pressure samples spanning 0–25.202 s and the matching Infusing, Pouring [T] and End
markers. This closes the mid-operation join gap above.

Browser accessibility text recorded the connected Pouring state and 15.6 s of
accumulated graph data; the graph was below the narrow viewport. The full curve
was inspected after completion. `evidence/mac-graph-midshot-native.json`
preserves the native snapshot. The temporary viewport
override was reset. Task 2.16 remains open only for the outstanding native visual
comparison; remote/mobile, paired host-impact and soak gates are still unclaimed.

## Mac recipe-name continuation — 2026-10-20

Task 2.17 adds a bounded recipe-name workflow, using the same `RecipeNameEditor.qml` in
the native wizard and Wasm client. Native styling, translated labels, keyboard composition,
focus/select-all and accessibility-mode behavior remain supplied by the wizard. The browser
uses a typed `HttpRecipeBackend` and the existing recipe storage; it does not carry its own
recipe database, validation rules, activation logic or full wizard.

The host advertises `recipeNamePrecondition` and compares `expectedName` inside the database
write transaction. Request-scoped completion keeps concurrent saves correlated; success
refreshes native active-recipe/auto-load consumers without consuming their legacy self-write
counters. Writes without the precondition retain their existing HTTP contract. The loopback
proxy permits only the exact recipe list route and name-only rename route, with the same
origin/auth/framing/size checks as equipment edits.

The browser retains drafts through navigation, refresh, disconnect, conflicting edits and
uncertain saves. It cancels old requests on host/session changes and never replays a POST.
A host-instance change additionally blocks a retained edit until explicit review. Archived
recipes are listed but cannot be renamed in this bounded slice. The recipe list uses a
virtualized ListView rather than eagerly constructing every row.

Native build 1792509381519 found an ID/member naming collision in the shared control; child
ID `recipeInput` now differs from its public `inputField` alias. Build 1792509381520 passed
in 55.723 s with all 282 QML files clean and the three existing linker warnings. Focused run
1792509383791 passed recipe storage and QML registration; three fixture failures were fixed
(a hidden visual ancestor, an old expected name after switching fixture hosts, and a fake
upstream missing its new GET route). Run 1792509383792 passed HTTP recipe, shared-form and
proxy suites in 10.210 s with no warnings. All five selected suites therefore pass.

Wasm build 1792509381522 passed in 36.384 s without warnings. At first load the browser
reached its first frame at 7,519.1 ms with 50 MiB allocated linear memory under the existing
synthetic shaping. It displayed the native list (20 recipes including five archived) and
correctly disabled renames against the older running native host without the new capability.
The only console error-level entry was the known QML-debugging notice. Existing `/recipes`,
`/equipment` and `/beans` remained HTTP 200 HTML.

The current host UUID is still `5e45dc1f-8b7f-4dc6-b195-0ebb1afda205`. Native restart and
manual wizard checks were requested; live browser rename/restore and draft/recovery UI
checks remain pending. Task 2.17 stays open. This is not full recipe workflow parity,
a remote-device gate, or permission to replace any HTML route.

Final Wasm wording build 1792509381523 passed in 33.581 s with no warnings. Qt Creator was restored to the macOS Debug kit for the requested native restart.

### Live recipe-name round trip and conflicts verified

The maintainer restarted native Decenza; the new process owns port 8888 (PID 49346) and
reports host UUID `20e69d2a-3e3e-4ecc-9f01-19bcfacddfdf`. GET /api/recipes now advertises
`recipeNamePrecondition: true`. The existing browser reconnected without reloading, fetched
the new capability/list, and enabled recipe editing. It still lists 20 recipes, five archived.

Browser checks rejected a blank name and an existing active name. A draft survived switching
to Machine, disconnecting, returning to Recipes, and reconnecting. Saving was disabled
while disconnected and enabled only after the fresh session/list. Renaming “Gran Bar ·
D-Flow / Q” with a temporary suffix succeeded; a separately opened existing HTML Recipes
page showed the new name. Comparing the entire native list showed only that name changed.
Restoring the name through Wasm returned the complete list exactly to its pre-test JSON.

For the concurrent-edit check, the browser held an unsent draft while a second HTTP client
performed a conditional name-only update. Saving the stale browser draft returned a conflict:
the form retained the draft, displayed the current host name and disabled Save until an
explicit Keep my edit action. The browser then restored the original name. A second complete
list comparison again matched the original exactly. No active recipe selection or other
recipe fields changed. No unexpected browser errors were observed.

Task 2.17 is complete
within its bounded Mac scope. Actual screen-reader/mobile acceptance and detailed native
visual observations remain outside this result.

### Native visual reuse follow-up

The current browser shell/list are deliberately temporary. The desired visual direction is
shared native QML layout and theme with responsive browser sizing. `RecipeDrinkCard` still
depends on Theme, DrinkType, ProfileManager, translations, BeanThumbnail and ShotPlanText.
The next bounded seam is a service-free shared card view/style, with native and browser
adapters supplying data; no native singleton stubs or copied page should be introduced.
The recipe endpoint already contains most card facts but omits profile frame temperatures,
so exact shot-plan temperature display needs a bounded derived-data API addition. This audit
is not implementation or visual parity evidence.

### Shared recipe-card implementation and build blocker

Task 2.18 now has a shared `RecipeDrinkCardView`, typed `RecipeDrinkCardStyle`, and
`RecipeCardText.js` formatter. Native `RecipeDrinkCard` supplies its existing Theme,
translations, BeanThumbnail and ShotPlanText through adapters/slots. The browser recipe
list consumes the same layout with validated HTTP display facts, active/archived/stale/
missing-profile states, shared drink labels/icons and its existing rename action. Native
callers do not rely on the old card root's Rectangle-specific properties. Text from the
host is explicitly plain text; the stale native action also supports Space/Return.

The browser uses explicit light palette inputs; it does not yet synchronize the native
theme. Cached photos and complete shot-plan display remain deferred because their host
transport is not present. The existing HTML routes and handlers were not changed by this
slice. A narrow-window header overlap and diagnostic column wrapping were also corrected
in the Wasm shell source, pending browser verification.

Native build 1792509381525 caught unbound outer IDs in the new native Components. Adding
`pragma ComponentBehavior: Bound` fixed those diagnostics. Build 1792509381526 passed in
27.652 s with 286/286 QML files clean and the three existing linker warnings. Focused run
1792509383793 passed shared controls/cards, HTTP recipe adapter and QML registration in
2.730 s with no warnings. Checks cover literal text, narrow wrapping, slot heights,
active/stale/missing states, keyboard/accessibility actions, display-field validation and
shared label/icon mappings.

The first Wasm configuration found the espresso icon's physical filename differs from its
resource alias. The bundle now uses the existing `espresso 8mm.svg` with alias `espresso.svg`;
reconfiguration passed. Build 1792509381529 then stalled before launching any compiler:
Qt Creator reports running, Compile Output only says “Running steps for project Decenza…”,
and process inspection found no compiler/build process. The maintainer was asked to inspect
Qt Creator for a blocking prompt. No shell build was substituted. Task 2.18 remains open
for the Wasm build, live browser validation and the requested native visual check. The
active Qt Creator kit is still Wasm while that build is pending; restore macOS after it.

### Build recovered and live shared cards verified

The stalled Wasm build 1792509381529 subsequently succeeded with zero warnings; its
25-minute elapsed time includes the blockage and is not a compilation benchmark. The
latest Wasm check, 1792509381532, also passed (0.471 s, no changes, zero warnings).
The maintainer's Run attempted Chrome via emrun because the Wasm kit was still selected;
Chrome failed to create its profile SingletonLock with “Operation not permitted”. This
was a browser-launch failure, not a native app or signing failure. The macOS kit was
restored, and the maintainer launched the correct original-workspace Debug executable,
PID 71574 on port 8888, host UUID `02e4c212-6451-4ca1-b42a-65e5d69f7d9b`.

Reloading the existing loopback probe reached first frame at 6,899.5 ms with 50 MiB
allocated linear memory under the existing synthetic shaping. It connected to the new
host and loaded 20 recipes. Desktop 1100 × 900 and narrow 390 × 900 checks showed the
shared card layout, icons, active border/badge, wrapped bean/profile text and finished-bag
warning. Scrolling reached missing-profile warnings and archived cards with disabled
Rename actions. Rename from a card opened the shared field, typing enabled Save, and
Cancel discarded the draft without writing. The browser error log and captured warning/
error console entries were empty. `/recipes` still returned HTTP 200 HTML. The temporary
viewport override was reset.

The browser build/live blocker is resolved. Task
2.18 remains open only for the requested native list/wizard visual and interaction
comparison; a “running” reply confirms launch, not those observations. Task 2.16 likewise
retains its outstanding native graph visual comparison. No remote/mobile acceptance or
complete theme/photo/shot-plan parity is claimed.

The maintainer clarified that the “difference” response was an accidental selection and
that no difference was observed. With permission, the assistant inspected the already-running
native app directly: recipe-list photos, active border, finished-bag warning, wrapped plan
text and footer controls rendered correctly; Edit opened the existing recipe's wizard
summary, whose shared hero card retained its photo and full 18.1 g / 36.0 g / 84 · 94°C /
grind 12 plan. Cancel returned to the list. The finished-bag action opened its bean chooser,
which was dismissed without selecting anything. No recipe save, activation or bag change
was made. Together with the maintainer's observation, this
completes bounded task 2.18; full recipe workflow and remote/mobile gates remain separate.

The native saved review of the October 20, 6:32 PM simulator shot was also inspected. Its Infusing/Pouring [T] timing, pressure
decline/plateau, weight ramp and 35.5 g saved outcome agree with the earlier browser
capture and endpoint evidence. Presentation settings differ: native Graph options showed
3× flow scaling and the Weight right axis, while the browser uses its explicit default
1× scale and had the Temperature axis selected. This is visual/data agreement with those
known display differences, not pixel equality or host preference synchronization. No
graph settings were changed; the native app was returned to idle. Together with the
earlier reset, correction tests, reconnect, host restart, mid-shot join and timing
evidence, bounded task 2.16 is complete. Host display-setting synchronization remains a
presentation follow-up; remote/mobile, host-impact and soak gates remain unpassed.

## Mac shared recipe shot-plan continuation — 2026-10-20

Task 2.19 extracts the native shot-plan formatter and layout behind explicit inputs. `ShotPlanText` retains native services and defaults; the Wasm adapter uses the same shared formatter/view with passive accessibility text. The host adds a separate `shotPlan` object to recipe reads, preserving existing HTML fields. Profile data is captured from the catalog cache and frozen fallback parsing occurs on the recipe worker. Temperatures use the existing native formatter and host unit choice.

Validation is in progress. Initial focused tests caught two formatter expectations that are being investigated; no live or native acceptance is claimed yet. Theme, photos, RPM capability and full translation synchronization remain outside this bounded slice.

### Matched-package contract

The maintainer clarified that the browser bundle and native app are built and shipped together and may communicate only as that pair. The implementation now requires a matching deterministic package identity on session preflight and every stream snapshot, restricts browser API origin to the asset origin, and treats missing required recipe/equipment/graph features as protocol errors. This supersedes the earlier experimental old-host read-only behavior. The current local proxy remains a development harness; release assembly and cache/manifest delivery are still task 9.1, not claimed complete.

### Build and focused-test evidence

Native Qt Creator build `1792509381541` passed with the three previously recorded linker warnings. Wasm build `1792509381544` passed with zero warnings. Both generated package identities equal `8778bbb14808dc01c62e376f0cac06de47418cf49da609ed520b5cc6456cced2`. The eight focused suites passed across runs `1792509383796` (seven passing, one stale fixture expectation) and corrected equipment rerun `1792509383797`: shared form controls, recipe/equipment adapters, temperature display, profile manager, QML registration, session adapter and session server. The first five shot-plan suites had also passed in run `1792509383795`.

Live acceptance awaits the maintainer restarting the native process, which predates package identity and shot-plan fields. Qt Creator is restored to the macOS kit. Tasks 2.19 and 2.20 remain unchecked until matched live operation and native rendering are verified.

The browser loaded the new bundle in 6,956.4 ms to its first-frame event under the existing synthetic proxy shaping (50 MiB Wasm linear allocation). Its initial connection to the still-running previous native process was rejected with the package-mismatch message; equipment loading, recipes and graph refresh remained disabled.  This verifies the rejection side, not the pending matched-package/live-rendering check.

### Matched-package live verification completed

After the maintainer restart, the browser connected to native host `b9146262-3437-4d41-adf9-0fbb05974455` with the expected package ID, received Ready/Idle and loaded all 20 recipes. Sublunar displayed 18.1g in, 36.0g, 84 · 94°C, grind 12; Gran Bar displayed 18.0g in, 54.0g (1:3.0), 84 · 94°C, grind 10. Native recipe cards and the wizard summary showed matching values. The wizard was cancelled without saving. Desktop 1100×900 and narrow 390×900 rendering and browser accessibility text were inspected.  Existing `/recipes`, `/equipment`, `/beans` each returned HTTP 200 HTML. Tasks 2.19 and 2.20 pass their bounded Mac checks; no mobile, remote deployment, theme or photo parity is inferred.

The maintainer now authorizes native app start/stop through Qt Creator only. This replaces the earlier per-restart handoff for this session; shell app launches/stops remain prohibited.


## Mac recipe search, sort and thumbnails — 2026-10-20

Task 2.21 is complete for the bounded Mac slice. Native and Wasm use `RecipeSearch.js` for normalized AND-token search, sort keys, blanks-last ordering and ID tie breaks. The browser groups active and archived matches, keeps rename state outside the filtered list, and distinguishes no matches from an empty inventory. `SharedBeanThumbnail` owns image/placeholder presentation; native retains its cache adapter and the browser uses the existing bag-image GET route. Native recipe cards also opt in to exposing their shared shot-plan text to accessibility.

Qt Creator native build `1792509381546` passed. Focused run `1792509383798` passed shared controls, recipe backend, QML registration and proxy tests; a new search test failed because its callback referenced an unexported JS symbol. Corrected search and strengthened image load/clear checks passed in `1792509383799`. Proxy run `1792509383800` passed the separate 8 MiB image/2 MiB JSON limits, including a 3 MiB accepted image and rejected oversized image. Final native build `1792509381557` and Wasm build `1792509381558` passed; Wasm has zero warnings. Package identity is `76b2e72d98b1aa11176e03f2202ab42eaf0975e81acf58c93a35a22c7a06af30` in both outputs.

The user authorized native lifecycle operations through Qt Creator. Before each Run, the existing Qt Creator process was stopped and a process-list check confirmed no Decenza remained. The final live host is `4e361d03-c5fb-4ac7-bb0b-3ba4ecfd098f`, PID 91681, with one process owning port 8888. No native process was launched/stopped from a shell.

Live Chromium 155 checks loaded 15 active and 5 archived recipes. `sublunar df` matched one active and one archived recipe; archived names stayed read-only. A nonsense query produced the no-match message, and Clear restored all results. Name ascending/descending reversed the expected visible ordering. An unsaved Sublunar rename survived filtering to Gran Bar and was cancelled without saving. The replacement sort picker exposes named buttons; selection and Cancel return focus to its trigger. Native photos and plans were inspected, including accessible plan text. The existing HTML `/recipes`, `/equipment`, `/beans` routes each returned 200 text/html.

A live image was 5,301,439 bytes and exceeded the proxy's initial 2 MiB JSON cap. Image GETs now have their own 8 MiB bound; the photo loads successfully. Missing photos (404) retain the bean placeholder. This does not yet optimize source-image bandwidth or refresh images that finish native async backfill after an initial 404. Finished/deleted bag records can also lack a browser photo even when the native recipe has cached image metadata. Those remain delivery/parity follow-ups, not a claim of complete photo parity.

The stock Qt ComboBox has no Wasm accessibility-role mapping. A Qt-only reproduction also exposes only a disabled `First` text field instead of the named ComboBox. Existing QTBUG-144330 and pending Gerrit 680803 cover this, so no duplicate issue was filed. The browser uses an accessible button/dialog selector. Existing editable-input name/value defects and actual screen-reader/device checks remain open; browser AX inspection is not VoiceOver acceptance.

 Desktop 1100×900 and narrow 390×900 layouts were inspected; the latter remains a desktop viewport check. Final first-frame marker: 6,957.8 ms with synthetic 20 Mb/s / 30 ms proxy shaping, 50 MiB linear-memory allocation, not a fresh performance gate. No recipe data or machine operation was changed.

## Mac shared native palette and temperature units — 2026-10-20

Bounded task 2.22 is complete. Native QML publishes an explicit, whitelisted appearance
snapshot containing dark-mode and Fahrenheit choices plus the resolved named color tokens.
The browser consumes the same colors for shared cards, forms, chrome and graph styles;
browser geometry remains local. The matched-session object is 1,703 bytes, below its
existing 4 KiB bound. Missing or malformed appearance is rejected, and a new streamed
appearance replaces the prior map. Dynamic updates and resets were verified with backend
fixtures; the live theme and temperature-unit preferences were not toggled during this
check.

Qt Creator native builds `1560` and `1564` passed with three known linker warnings. The
focused run `1792509383801` passed `tst_remotesessionbackend`, `tst_remotesessionserver`,
`tst_qmlregistration`, and `tst_sharedformcontrols`. Wasm build `1563` passed with zero
warnings. Both builds use package identity
`aad5b38f58ed44d5435bfb72fde8bb33b960ec88a6134596f023710d7a0ba030`.

The browser accepted and rendered the current native palette from the running original-
workspace app (PID `96614`, host `6e943b4b-b3b8-4246-b498-aafd92697f82`), then reconnected
successfully.  This verifies the
current matched Mac palette and snapshot, not a live user preference toggle, mobile use,
full theme editing, fonts, background media, or remote deployment. Existing HTML routes
remain unchanged.


## Shared native theme font roles — 2026-10-20

Task 2.23 extends the matched appearance object with all eight effective native font-size roles, including user overrides. Native remains authoritative. The browser uses the same bundled Decenza Sans and Noto Sans Math families and role choices for its shell, forms, shared recipe/equipment cards and graph. Native viewport scale is not transmitted; browser geometry remains local. The synthetic Preview keeps its diagnostic styling. Background images/pattern delivery and full theme editing remain open.

Native Qt Creator build `1792509381566` passed the QML gate (291/291). Focused run `1792509383802` passed all three suites: session backend, shared controls and QML registration. Schema fixtures reject missing/extra roles, strings, zero, fractional, non-finite and oversized sizes; changed font-role snapshots notify and identical appearance does not. Wasm build `1792509381568` passed without warnings; final native build `1792509381569` passed with the three known linker warnings. Both package identities are `98ebbcd4b15fdc48cc309ef741757b84aa2d023d0917a0384e48fd3a3b6bd341`.

The previous native process was stopped through Qt Creator; process inspection confirmed none remained before Run. The new original-workspace process is PID 6268, host `4aaa85a0-5fb8-4aa4-874d-be9ae13b5bd6`. Its full session is 1,843 bytes, below the 4 KiB bound, and contains the eight native font sizes. The browser connected and rendered equipment/recipe typography at 1100×900 and 390×900; narrow shot-plan wrapping was inspected after scrolling settled.  Existing `/recipes`, `/equipment` and `/beans` still returned HTTP 200 HTML. No native preference, recipe or machine operation was changed. Dynamic preference updates were checked with backend fixtures; a live user preference toggle has not been claimed. Desktop viewport checks do not establish mobile-device acceptance.


## Permanent web-client home and shared backgrounds — 2026-10-20

The maintainer authorized moving beyond feasibility naming while continuing the additive Mac implementation. Task 2.25 is complete: production adapters and build definition now live in `src/webclient`, production QML in `qml/webclient`, and bootstrap assets in `resources/webclient`. The executable is `decenza-web`, and its QML module now uses the native `Decenza` URI so the Wasm target can compile existing Decenza QML against target-specific facades; the generated bundle is under the Wasm build's `src/webclient` directory. Development proxy, recorded fixtures and accessibility reproduction remain in `tools/webclient`. Historical evidence paths above describe the builds actually measured and have not been rewritten. Old local build/Qt Creator metadata was not removed.

The default development entry is `/wasm/index.html`, also reached from `/` and `/wasm/`. It provides a loading message and error/retry shell, then gives the application the full viewport. Production navigation contains Equipment, Machine and Recipes. `/wasm/probe.html?diagnostics` explicitly loads the recorded graph/form probes; `/wasm/probe.html?accessibility-repro` retains the standalone Qt reproduction. Diagnostic source is still compiled into this development bundle. Both kits consume the same source-package identity, updated to include the new source/resource locations.

Task 2.24 is implemented but remains unchecked pending live applied-selection verification. Native and browser consume `SharedBackgroundSurface.qml` for base fill, tiled/tinted SVG patterns and cropped photos. Native retains its distinct last-shot wallpaper adapter. The appearance schema carries only source kind, catalog pattern ID, resolved ink color and an opaque revision. `/api/session/background-image/<revision>` uses existing authentication and delivers only the currently selected media-cache PNG/JPEG, with bounded off-thread decoding/resizing, no-store, and a second revision check before sending. Native paths never enter the session payload. Source bytes are capped at 32 MiB, decoded dimensions at 32 million pixels, output at 8 MiB and 2560 pixels per edge; at most two jobs run concurrently. These are delivery limits, not mobile performance acceptance.

Validation:

- Native Qt Creator build 1792509381579 succeeded with the three previously recorded linker warnings; the changed QML passed the 292-file native gate. The five browser-only QML files are explicitly excluded from the native module-registration check and linted in the Wasm target instead.
- Wasm Qt Creator build 1792509381580 succeeded without warnings. Both generated headers and the running host report package `57a9efb2b9fc5093adc951f1b1594644cda87aebc424da70418c391f6331f561`.
- Focused run 1792509383803 initially passed seven of nine suites; the new QML error-message matcher and appearance signal-count assertions were corrected. Run 1792509383804 executed the broader 130-entry suite: 129 passed, with only the warning-guard lint failing on seven newly added test files. Those files now call `QTest::failOnWarning`; run 1792509383805 passed all seven affected suites plus the lint (8/8). The known deliberate oversized-preflight abort warning is explicitly expected. No sanitizer error was reported by these runs; macOS does not establish leak freedom.
- Image-loader tests cover original PNG/alpha preservation, bounded JPEG resizing, missing/corrupt/outside-root files, symlink escape and oversized input. Shared QML tests cover pattern configuration, photo readiness, error/base-fill behavior and clearing. Proxy tests cover the new default entry, exact background route, byte/MIME/cookie forwarding, method/query/path/origin rejection and response bounds.
- The previous native process was stopped through Qt Creator and its absence checked before launching the rebuilt original-workspace macOS Debug target through Qt Creator. Live host ID: `4a52ea66-e077-48c9-98dd-97d677ed845e`. The browser's clean entry connected and displayed Ready, four equipment packages, 15 active and five archived recipe matches. Desktop 1100×900 and the narrower default viewport rendered the relocated interface. The browser reported one missing cached bean thumbnail; no new background or QML-instantiation error was observed.
- Live `/recipes`, `/equipment` and `/beans` each returned 200 HTML. With native background source `none`, the current image revision returned 404/no-store; a stale revision returned 409/no-store. The live session contained no background path.
- The native background chooser rendered Grain, its preview and the other pattern tiles through the extracted component without applying a preference.

A request to temporarily apply a pattern/photo and restore the original preference remains pending. No background preference was applied. The Mac locked during the subsequent Cancel action, so cancellation could not be confirmed and further UI checks require manual unlock. The temporary browser viewport override was reset successfully after the lock. This is an environmental verification blocker; live pattern/photo synchronization is not marked passed. Shot-chart wallpaper, remote/mobile/VoiceOver acceptance and production packaging remain separate work. Existing HTML remains intact; its eventual removal is a separate follow-up.


### Background live-check completion

After unlocking the Mac, the maintainer explicitly authorized temporary appearance changes and restoration. The native Grain/Porcelain selection immediately appeared in the connected browser with dark ink over the light background; selecting cached photo 1 of 139 then replaced the pattern without browser reload. Native and proxied image routes both returned 91,978 bytes of JPEG with `Cache-Control: no-store`; the previous revision returned 409 through both routes. The photo cropped correctly at desktop 1100×900 and narrow 390×900 viewports.

The chooser was then set to None for both source and pattern and applied. The session again reports source `none`, empty pattern ID, `#1a1a2e` background and white text, matching the recorded starting state. The former photo revision now returns 409; HTML recipes/equipment/beans still return 200. The browser viewport override was reset. This completes task 2.24's Mac photo/pattern selection check; shot-chart wallpaper and remote/mobile acceptance remain separate. The earlier lock blocker is resolved.

## Complete shared equipment controls — in progress, 2026-10-21

The production browser entry now instantiates the same equipment inventory, complete cards, editor, suggestion controls and selection dialogs as native Decenza. The earlier browser-specific Equipment/Machine/Recipes navigation and name-only editors are no longer the production entry. Typed adapters supply native services or matching-host HTTP operations; presentation and action controls have one QML implementation. Full navigation and the remaining app pages are still pending, so task 2.27 remains incomplete.

The equipment contract now supports create, full edit, activation, removal and deletion with request-specific completion. Full edits carry the original editable fields for atomic conflict checks; refreshing inventory must not overwrite an open draft. The existing HTML API contracts remain supported. A shared bottom-bar extraction preserves the native component API as groundwork for shared navigation; compiling it for Wasm does not by itself establish a working browser navigation flow.

The matching Wasm bundle is staged before the native build and installed in `Decenza.app/Contents/Resources/webclient` before signing. The native server served the browser directly at `http://127.0.0.1:8888/wasm/`, without the development proxy. Package `749a37825729082a5497eb71c3527e9a6c9d643c9a9f7283084c1064195729f8` loaded four equipment cards with their edit/remove/delete controls and prep labels; its editor opened with the native fields and five prep switches. Visual inspection found misplaced suggestion icons despite a successful build; the anchor fix and subsequent validation are pending below. No live equipment mutation was performed during that inspection.

Intermediate Qt Creator runs: `1792509383809` passed session client/server, proxy, asset serving, packaging and QML registration; `1792509383810` passed equipment storage and the expanded HTTP backend. Shared-control tests exposed fixture lookup/geometry issues and are not yet recorded as passing. Final matched builds, clean runtime rendering, live writes and navigation acceptance remain outstanding. These Mac checks do not establish remote HTTPS, phone/tablet or actual screen-reader acceptance.

Validation follow-up, 2026-10-21: matched package `f67364f56a131394e8accd66e189f476b7d19c97a2b304e2c9c6e928851455e4` passed Wasm build `1792509381616` (zero warnings) and native build `1792509381617` (three existing linker warnings). Qt Creator shared-control run `1792509383816` passed; the other eight focused suites passed in runs 3812/3813. Suggestion actions are now siblings of their text input with explicit geometry: desktop browser icons render at each field's right edge and all twelve Clear/Open actions are exposed in the accessibility tree. The grinder-brand picker opens and cancels. A reversible browser name edit reached native inventory; restoring `Varia VS6` through the native editor updated browser inventory without reloading. Native Back returned to Idle. No new browser warnings were captured after this load. The 390-pixel desktop viewport kept controls reachable but truncated the editor title and prep labels; this remains a shared responsive-layout issue, not phone acceptance. Screenshots: `/tmp/decenza-shared-equipment-editor-2026-10-21.png` and `/tmp/decenza-equipment-narrow-2026-10-21.png`.

The next implementation step compiles the original `qml/Theme.qml` unchanged in the Wasm target under the same `Decenza` module URI, using typed remote Settings inputs. This replaces selected-color projection as the source of theme calculations; it does not yet complete whole-page/navigation parity.


Shared-theme acceptance, 2026-10-21: Wasm build `1792509381630` and native build `1792509381631` produced matching package `0edab0b916774d9416419851bb66d29c6e446cd97ac99429e3ec114279718398`. The browser now runs the original `qml/Theme.qml` under module `Decenza`, with live typed settings and the same bundled font/emoji assets. Font-role defaults were extracted into `src/core/themefonts.h` for both native SettingsTheme and its remote adapter; this removed zero-size font warnings during startup. The desktop equipment editor rendered correctly with no fresh browser warnings. Qt Creator runs 3818 and 3819 passed the affected session, settings, HTTP equipment and module-registration tests. Native lint passed 331 QML units; Wasm emitted no build warnings, and native retained the previously recorded linker warnings. Screenshot: `/tmp/decenza-native-theme-shared-editor-2026-10-21.png`. Whole-page/navigation parity remains pending.


Shared-service and equipment acceptance, 2026-10-21: the browser now instantiates the original `EquipmentCard.qml`, native form style/text adapters and shared Theme viewport scaling. Typed remote Settings, MainController equipment storage, TranslationManager and AccessibilityManager facades support those exact files. Translation catalogs are revisioned and bounded; the live host returned 4,630 catalog entries with matching package/host identity. Existing native and Wasm style copies were removed. Section headers now stack actions when needed, form field pairs reflow, and prep labels wrap. Button insets scale with the control so narrow desktop layouts no longer squash the painted background.

Matching package `3528be406abe0511483fa303b6de2889627b3b1587485dcf253182aaf0e4fc00` passed Wasm build 1792509381645 and native build 1792509381646. Focused run 1792509383821 passed seven affected service suites without warnings; shared-control run 1792509383825 passed after correcting the test's text-item lookup. The browser loaded the native package directly from `/wasm/`, displayed all four equipment packages and opened the full editor. Native equipment cards/editor were inspected and cancelled without saving. The smaller 390-pixel desktop viewport verified button paint bounds; it is not phone acceptance. Evidence: `/tmp/decenza-shared-button-scale-fixed.png`, `/tmp/decenza-native-shared-editor-3528.png`. Full native page/navigation reuse remains incomplete.

The next slice replaces the remaining browser background adapter with `ThemedPageBackground` and `LastShotChartRenderer`. It adds a bounded latest-saved-shot snapshot, shared graph preference metadata, and a platform-neutral PORTAL overlay. Paired builds and live wallpaper checks are pending; these changes are not yet accepted.


Shared wallpaper validation, 2026-10-21: matching package `ab802d2121a0c01c6931a30811cbf90314886cb9e3c4782e0c0b6c8263b67007` passed Wasm build 1792509381660 and native build 1792509381661. Native QML gate covered 335 files; Wasm had no warnings, native retained four existing linker warnings. Qt Creator runs 3826–3828 passed the nine affected service/control/package suites plus warning lint after fixing JSON integer validation, a test label leak, and the saved-shot fixture's omitted water curve. The final session suite passed 3828 without warnings.

The native app was launched and stopped through Qt Creator. Live host `3a8e59e1-582d-45b6-88a1-c7453bc5936f` served the packaged browser at `/wasm/`. Selecting Last Shot (Advanced) in native settings rendered the saved-shot graph in the browser without reload, through the same `LastShotChartRenderer`/`HistoryShotGraph` sources. Cold Brew plus Dot Grid then replaced it live; clearing source and pattern restored the original plain background. No browser warnings/errors were captured. `/equipment` remained 200 `text/html` with its existing HTML title. Evidence: `/tmp/decenza-wasm-shared-shot-wallpaper.png`, `/tmp/decenza-native-shared-shot-wallpaper.png`, `/tmp/decenza-wasm-shared-pattern.png`.

The comparison exposed one remaining chrome mismatch: the browser root did not publish whether the displayed page actually painted the shot chart, so browser cards remained opaque. The shared-page build below fixes this through `CurrentPageBackground`, consumed by both shells.

Actual equipment-page acceptance, 2026-10-21: matching package `1c4b688628b38b62cc9839a9ce21f097d0d0456d4f63ed81f00290c10fb1c134` passed Wasm build 1792509381665 (zero warnings), native build 1792509381666 (existing Qt-private-module/linker warnings), and all five focused suites in Qt Creator run 1792509383829. Both targets now compile `qml/pages/EquipmentPage.qml`; the separate browser `EquipmentInventory.qml` is deleted. One injected typed backend supplies the same page and dialog, with loading/read failures visible in native too. Page/dialog invalidation avoids duplicate native inventory reads.

Host `659f457a-503b-45b6-b342-2471385f4728` served the direct packaged browser. Four live packages and the full editor appeared in both targets. Browser rename to `Varia VS6 page check` reached native inventory; restoring `Varia VS6` in the native editor reached the browser without reload. Native Back returned to Idle; browser Equipment is still the root and correctly offers no Back. Selecting Last Shot (Advanced) now produces the same translucent cards through the shared background binding, without browser warnings/errors. The original plain background was restored. Screenshot: `/tmp/decenza-wasm-shared-equipment-page-glass.png`. Full home/status-bar/route parity remains pending.

### Native layout dependency — accepted on Mac

- Wasm build 1792509381669 and native build 1792509381670 passed. Paired package: `f53762f78e2f0655f4aca08e691613d4d1b99862f6b8014566d807439e302788`.
- Qt Creator run 1792509383830 passed six suites; the session fixture omitted the newly required layout capability/revision. After fixing that fixture, run 1792509383831 passed `tst_remotesessionbackend` without warnings. Together the seven focused suites pass.
- Launched native through Qt Creator; host `aa833571-4ec9-4ce5-a431-ee92a696892d`. Live `/api/session/layout` matches `/api/layout` after removing the HTML-only configured flags, with all nine zones. POST rejects with 405, Allow: GET, no-store.
- Packaged `/wasm/` loads the actual shared EquipmentPage and all four inventory cards with no browser console warnings/errors. Layout revision updates, rollback rejection, package mismatch, size limits, explicit retry and disconnect clearing are covered by focused tests. Full native status/home widgets remain pending dependency wiring; this is not full home-page acceptance.

### Typed machine state dependency — accepted on Mac

- Wasm build 1792509381675 passed without warnings; native 1792509381676 passed with the known private-Qt-header/OpenSSL/unwind warnings. Exact matching package: `f9f7823ada0f49cac85a7e76f72daacc9c464a121f74f8e9d3d448f19c1be6ce`.
- Qt Creator run 1792509383832 passed all eight focused suites without warnings: machine snapshot, remote session, remote settings, translations, QML registration, assets, package and warning lint. Tests cover schema/type/enum validation, unchanged output on failed parsing, matching native/remote QML enum metadata, fresh-stream gating and clearing live facade state on invalidation/destruction.
- Restarted through Qt Creator with the old process stopped first. Host `420783bd-af8b-4491-969b-44d1d38a6618` reports simulator Ready, native values (93 C group, 160 C steam, refill kit unknown), and a Sleep transition after clicking native Sleep. Dismissing the screensaver returned native UI to Ready.
- Packaged `/wasm/` loads all four shared equipment cards with no browser warnings/errors; `/equipment` retains its original HTML title. No status-bar UI or machine commands are claimed complete by this read-only service slice.

### Native readout preferences and targets — accepted on Mac

- Paired builds: Wasm 1792509381679, native 1792509381680; package `ae922f0463c24ad646a8f8024f09efca91897add6858dacf4c5e1ed9916f1cd9`. Wasm succeeded without compiler warnings. Qt Creator also reported an unrelated attempt to configure the deleted feasibility project; closed only that obsolete project via File > Close Project and verified the original workspace remains active.
- Run 1792509383833 passed seven of eight suites. The new settings test incorrectly expected live values after the transport handshake but before a fresh snapshot. Corrected it to assert stale defaults, send sequence one, and then assert live values. Run 1792509383834 passed the settings suite without warnings.
- Native restarted via Qt Creator with no prior process running. Live host `a4e34fc5-5701-46a7-9f88-50b4141719e9` reports 84 C profile target, 36 g active target, 18.1 g ratio dose, 160 C steam target, percent water units and auto refill override, agreeing with the native UI. Browser loads the shared equipment page without console warnings/errors. Update/disconnect behavior is covered by the focused suites.
- Snapshot helper and native-named readout facades expose authoritative values, preserving display calculations in shared QML. Status-bar mounting and scale/tare/navigation actions remain pending.

### Scale readouts and action transport — accepted on Mac

- Paired Qt Creator builds: Wasm 1792509381684 (zero warnings), native 1792509381685 (five existing private-Qt/OpenSSL/unwind warnings). Matching package `dd8fdf307da518a995af71a75653374e2d76fe314393e4cb031a0cc5d0ac35a8`.
- Qt Creator run 1792509383835 passed ten focused suites without warnings: machine snapshot, action validator, remote session/settings/translations, QML registration, web request guard, assets, packaging and warning lint. Action tests cover fresh-session gating, correlated acceptance/rejection, malformed/oversized results, concurrent-action rejection, disconnect cancellation, no reconnect replay and pending teardown.
- Stopped the previous process and launched native through Qt Creator. Simulator host `e9ac9f17-d6ea-473f-aa9b-3638b6e5a890` reports simulated scale connected. Live tare dispatch returned 200/accepted; duplicate and wrong-host requests returned 409, GET 405, query-bearing target 400 and foreign Origin 403. Acceptance means native dispatch, not a hardware completion acknowledgement.
- Packaged browser still renders four actual shared EquipmentPage cards without console warnings/errors; proof `/tmp/decenza-scale-services-equipment.png`. Scale widget mounting and full status-bar/navigation parity remain pending. Scan dispatch is covered by the transport fixture; no live discovery scan was performed.

### Steam pitcher projection and shared selection — accepted on Mac

- Paired builds 1792509381688 (Wasm, zero warnings) and 1792509381689 (native, six known Qt-private/OpenSSL/unwind warnings) produced package `48a558a4803a9caab27c743418a89075eda65a61f2e8c196adbb95d51a0100a0`.
- Qt Creator run 1792509383836 passed ten suites and found one invalid test fixture: the wrong-type test gave the name field a valid string. Corrected the fixture to supply a boolean; run 1792509383837 passed the pitcher suite. All eleven focused suites are now green, including native heater policy, shared selection/parser, remote facades and accessibility registration. No warning suppression was added.
- Native restarted through Qt Creator; host `1937a7ee-e4a4-4c92-92d6-ae86f801077d` exposes Small, Large, Clean, Off and the synthetic Heater off entry, with stored selection 3. Opening native Steam presets shows the matching Off selection and separate Heater off entry; restored the prior home view without changing selection or starting steam. Browser loads the four shared equipment cards without console warnings/errors (`/tmp/decenza-pitcher-services-equipment.png`).
- Native SettingsBrew and remote snapshot lookup now share the sentinel/display-index helper. Projection refreshes only on settings changes, preserves absent fields and rejects malformed known values rather than silently substituting defaults. Freshness/disconnect tests clear the browser projection. The browser accessibility facade now exposes the native `announceLabel` call shape through its local polite announcement path; no real-screen-reader acceptance is claimed.

### Shared preparation and capture ownership — implemented on Mac

The accepted model uses one host-owned preparation mode (espresso, steam, hot water,
flush) with independent navigation, editors, history, dialogs and drafts. Selection
never starts a machine operation. `PreparationController` now owns both deterministic
weight detectors, dose/milk updates, manual steam-time protection and the measured
milk/time pair. Shared pages observe `PreparationState`; the former QML detector was
removed and `MilkCapture.qml` only presents the host state. HTML remains intact.

- Paired builds 1792509381697 (Wasm, zero warnings) and 1792509381698 (native, three
  existing OpenSSL/unwind warnings) produced package
  `7f7c8fd81496822998a9eeb008184f13ee99bdad2ca88f00ec548314e06ee645`.
  The native QML gate passed all 335 files.
- Run 1792509383838 passed nine of eleven suites. Corrected two fixture expectations:
  the rejection sample had accidentally entered the valid dose range, and phase
  assertions needed to wait for MachineState's queued notification. Run
  1792509383839 passed the two affected suites. All eleven focused suites pass.
- Restarted native through Qt Creator after verifying the old process exited. Simulator
  host `fad46547-8bc4-48a4-b919-96e4462a3e2c` reports the required preparation snapshot.
  Native Steam selection arms the one host milk detector. A same-package remote Flush
  request leaves native Settings open; Back shows Flush selected. The machine remains
  Ready. Restored espresso mode without starting a shot.
- Packaged Wasm still renders the real shared EquipmentPage with no console warnings
  or errors (`/tmp/decenza-preparation-equipment.png`). Browser preparation adapters
  are covered by transport tests, including rejection, disconnect, and no event replay.
  Task 2.34 is complete. Task 2.35 remains open: full native/Wasm preparation-page
  interaction and live simulated load/disconnect verification need the shared home
  and steam routes mounted in Wasm. This does not claim full home/status-bar parity.

### Browser Quit policy and preparation rejection cleanup

Shared Quit widgets and layout commands now emit an AppShell request. Native handles
it with its existing Quit behavior; the browser intentionally leaves both immediate
and confirmation-based requests unhandled. The Wasm entry point disconnects Qt's
QML quit/exit signals after engine initialization and disables last-window auto-quit.
Qt 6.12 source confirms those default signal connections are installed during load.
There is no host shutdown request on this path.

Builds 1792509381704 (Wasm, no warnings) and 1792509381705 (native, three existing
OpenSSL/unwind warnings) passed. Qt Creator run 1792509383840 passed all five
focused suites: customwidgethtml, remotesessionbackend, preparationstate,
preparationcontroller, and qmlregistration. The native process was stopped before
Run, then the packaged browser reloaded the shared EquipmentPage without warnings
(`/tmp/decenza-wasm-quit-package.png`). The production browser does not mount the
Quit widget yet; its interactive smoke check remains part of shared-home acceptance.

Rejected asynchronous preparation requests now clear the matching pending home-row
selection. Tests cover forwarding preparation failures, excluding scale failures,
and detaching the old session's failure relay when the facade is rebound.


### Portal service and shared brew persistence — verified on Mac

Native and remote Portal status use one state-name table. The guarded reconnect
request uses the existing native device guard and the common session action
contract. BrewDialog now saves dose, grind, RPM, yield and temperature through one
native operation; ratio memory is updated there. Raw profile yield and the effective
session target are distinct fields, and explicit-step temperature formatting is
shared C++ code.

- Paired Qt Creator builds 1792509381709 (Wasm, zero warnings) and 1792509381710
  (native, four existing OpenSSL/unwind warnings) succeeded, producing package
  `4140b302659b84a5e12c848913409dc8a791e295a4f4d8c022d2256c33a6e8b1`.
- Run 1792509383841 passed seven of nine suites. Updated the snapshot field-count
  fixture and fake HTTP server's Portal route/action fixture; 1792509383842 passed
  both affected suites. All nine focused suites pass: belkaportal,
  machineuisnapshot, remotesessionbackend, remotesettings,
  remotetranslationmanager, sessionactionrequest, profilemanager,
  temperaturedisplay and qmlregistration.
- Stopped the old native process before starting its replacement through Qt Creator.
  Simulator host `54a83fa6-5c33-4994-a966-e2e7f50bbdcf` reports Portal disconnected
  and unowned. A matching-package reconnect request returned the expected correlated
  HTTP 409 rejection; no device scan was started.
- Native Brew Settings retained the selected recipe, 18.1 g dose, 36 g derived
  yield, grind 12 and displayed temperatures after confirming unchanged values and
  reopening. Closed with Cancel (`/tmp/decenza-native-brew-consolidated.png`).
- The packaged browser renders the four shared equipment cards without console
  warnings/errors (`/tmp/decenza-wasm-portal-package.png`). Existing HTML root still
  returns HTTP 200 and its Shot History title. The full shared BrewDialog is not yet
  mounted in the browser; this evidence covers its consolidated native save and the
  typed services needed for subsequent shared-page integration.


### Host steam countdown and exact device-state projection

`SteamAutoFlushController` replaces the QML timer. It owns elapsed-time measurement,
cancels on leaving connected Steam/Puffing, and signals expiry once. MainController
uses the same predicate before and after releasing steam permission before requesting
Idle. Shared SteamPage reads the projected countdown. Native state/substate byte codes
and headless status are now exposed under the same DE1Device QML property names.

Qt Creator builds 1792509381714 (Wasm, zero warnings) and 1792509381715 (native,
five known Qt-private/OpenSSL/unwind warnings) succeeded; the QML gate passed all
335 files. Run 1792509383843 passed all five suites: steamautoflushcontroller,
preparationcontroller, machineuisnapshot, remotesessionbackend and qmlregistration.
Controller tests cover expiry once, manual state exit, disconnect, and fresh re-arm;
remote tests cover stale-state clearing, and parser tests preserve the protocol byte
range including firmware error codes.

After stopping the old process and restarting through Qt Creator, simulator host
`2e2a8f33-05f0-4814-ad14-8530f3f7bdcf` serves matching package
`cace1727b3cfd3e4a9ed29b9bf0d007222e4ab94017c590322824484377bd971`.
Ready reports state 2/substate 0, headless true and countdown zero. The native Steam
page renders its unchanged controls. The browser renders shared EquipmentPage without
console warnings/errors (`/tmp/decenza-wasm-autoflush-package.png`). The simulator
currently starts steam directly in Pouring and does not model Puffing; actual countdown
expiry is therefore verified with deterministic device-state tests, not claimed as a
live simulator observation. Shared browser Steam-page acceptance remains pending.


### Host-owned Steam lifecycle and shared calculations

PreparationController/MainController now own start-time scaling, end-time preset
reset, permission release, and the live coach's milk-derived-duration flag.
SteamPage observes the results. The snapshot carries last-on-scale milk and the
scaled-time flag; late join/reconnect does not replay capture events.
Native and remote SettingsBrew use one SteamCalculations helper for names, milk
bounds, scaling and effective duration. The native ProfileManager also has a
validated revision-guarded Brew Settings commit; the complete browser dialog
transport still needs MainController's recipe baseline guard.

Qt Creator builds 1792509381718 (Wasm) and 1792509381720 (native) passed. Run
1792509383844 passed ten of twelve focused suites. The two failures were new test
fixtures: queued signal observations, a missing applied pitcher baseline, and
recipe ID zero. After correcting those fixtures, run 1792509383845 passed
preparationcontroller, profilemanager and remotesessionbackend without warnings.
The twelve suites now pass across these runs. Tests cover start-time milk priority,
manual timeout preservation, once-only end reset/release, parser validation,
remote freshness, shared calculations, and invalid/stale Brew Settings commits.

A live log caught IdlePage's old AppShell milk-load notification handler. It now
listens to PreparationState. Paired builds 1792509381723/1724 passed after that fix;
the native QML gate covers 335 files. The replacement was launched through Qt
Creator only after the old process exited. Simulator host
`1d520c84-21c6-4645-af50-d442877eae9e` serves package
`e2a951a7b15c8029b1a5296bb5675cd43e292d10226c47b6b91df032e48e924c`.
A native-started simulator steam cycle projected Steam/Pouring, state/substate 5/5.
Increasing time from 30 to 35 seconds unlocked manual time; stopping restored
30 seconds, flow 150, the time lock, and cleared session milk/scaled flags.
No milk load was simulated in this live check; priority and once-only capture are
covered by deterministic tests. Shared EquipmentPage remains rendered in Wasm
without console warnings/errors (`/tmp/decenza-wasm-steam-lifecycle.png`). Full
browser SteamPage controls and visual acceptance remain pending under task 2.35.


### Guarded pitcher selection and scale lifecycle review

Selected-pitcher temperature now synchronizes in native SettingsBrew without a
page. Shared SteamPage no longer writes temperature in snapshot handlers. The
paired action `/api/session/steam/select-pitcher` validates an ordered-preset SHA-256
revision and integer index, uses captured milk from the host, and dispatches the
existing native selection operation. Browser state changes only on a host snapshot.

Review also found and fixed two preparation edge cases: losing scale availability
could re-lock manual time, and a replacement scale could inherit the old live-milk
fallback. Relocking now requires a genuine finite load-removal sample. A target
change waits for a new sample; target loss and selected empty-pitcher-weight changes
clear the live fallback. Closing an open attempt by changing pitcher restores the
prior captured milk, just as Cancel does. Regressions cover these transitions.

Qt Creator builds 1792509381726 (Wasm, zero warnings) and 1792509381727 (native,
four existing OpenSSL/unwind warnings) passed; native QML gate 335/335. Run
1792509383846 passed eleven of twelve suites. Four valid remote-settings fixtures
still omitted the new revision; their fixture setter now uses the shared projector.
Run 1792509383847 passed the affected suite without warnings. All twelve focused
suites pass across these runs, including preparation, settings, heater policy,
auto-flush, coach, action validation, preset snapshots, remote session/settings/
translation and QML registration.

After a single Qt Creator restart, simulator host
`422a67d7-5c05-4a4f-8285-3f58a045a031` serves matching package
`a4dae40ef61b2c3a72bf37b567abc5185d218d0b34af6b7fd4162d95865fce18`.
Reselecting its existing pitcher returned correlated HTTP 200. Reusing that request
ID returned 409; a different preset revision returned 409; index 99999 returned 400.
The host stayed Ready and preserved preset selection, 30-second timeout, flow 150,
temperature 160, and the active recipe. Live checks deliberately did not swap to a
different pitcher because that normally deactivates the selected recipe; native
selection and Heater-off behavior are covered in focused tests. Existing HTML root
still serves Shot History. Wasm shared EquipmentPage renders with no console
warnings/errors (`/tmp/decenza-wasm-pitcher-actions.png`). Full shared SteamPage
mounting and user-facing async action handling remain pending.


### Atomic Steam page preparation and time controls

Shared SteamPage now sends one host operation for activation, pitcher selection,
and each five-second adjustment. A synchronous machine-state UUID changes on raw
state, substate and connection transitions; delayed requests from another state
are rejected. Selected-preset notifications now fire only when the authoritative
selection/presets change, preserving the page’s slider bindings between telemetry updates.

Qt Creator builds 1792509381734 (Wasm, no warnings) and 1792509381735 (native,
four existing OpenSSL/unwind warnings) passed; QML gate 335/335. Eleven focused
suites pass across runs 1792509383848/3849. The initial remote-session fixture
did not route the new Steam endpoints; its corrected route/name mapping passes.

Simulator host `0a6852a4-54a7-4990-8bcf-726f55b0403f` ran matching package
`d80881dbe8477fc7d2a853266440f419c51af485dde5fa50c6bb762fefa22af8`.
Preparation and current-pitcher selection returned 200. Two successive +5 requests
changed timeout 30→35→40; two -5 requests restored 30. A different context UUID
returned 409 without mutation. Presets remained unchanged. Native Steam settings
opened with expected duration/flow/temperature controls; Wasm reconnected without
console warnings/errors (`/tmp/decenza-wasm-steam-actions.png`). Stop/back and full
shared Steam/Home mounting remain incomplete; no claim of page parity is made.


### Steam Stop and Back confirmation

Qt Creator builds 1792509381740 (Wasm, no warnings) and 1792509381741
(native, QML gate 335/335) passed. Run 1792509383850 passed eight focused
suites covering stop state, action schema, machine snapshot, remote session/settings,
QML registration, preparation and heater policy. The QML-registration suite retains
its previously documented skipped scenarios.

Simulator host `1ae72583-17bb-4aba-be90-f99583f87dd6` ran package
`097a30869c495ba8f6602e31eaae48f6b583d59565ad293de220065665f0c0ea`.
Remote Back returned confirmed dismissal while the native Steam page stayed open;
native Back then dismissed normally. Stop at Idle returned 409. During a simulator
steam operation, Stop returned 200 with `dismissPage:true`, and the next snapshot
was Idle/Ready with the original 30-second timeout. The host's two-tap preference
was false, so two-tap transitions are covered by deterministic tests, not this live
check. Shared full-page mounting remains pending.


### Shared Steam editor and graph transport

Qt Creator Wasm build 1792509381755 passed without warnings; native build
1792509381757 passed the 335/335 QML gate with the existing OpenSSL/unwind warnings.
An explicit Qt Creator CMake reconfigure refreshed a stale native package identity;
both headers and the installed bundle then matched
`cf779a70bbf7091cf2dd71f740c7bb52cb143ad415ea4f20abc56532b7efe94b`.
All twelve focused suites pass across runs 1792509383851/3852 (editor/schema,
graph model/transport/schema, session, settings, steam health and QML registration).
The first run exposed fixture sequence ordering, an intentional duplicate-name
warning, an early asynchronous assertion, and a disconnect expectation that ignored
retained-but-stale session state. Their corrected tests passed on rerun.

Host `f6e8e7a8-868a-47e5-9bdd-5af0540b1f83` accepted a selected-preset duration edit
30→35 and restored it to 30. A reused editor revision returned 409. Pressure graph
visibility toggled and restored; a stale expected value returned 409. The original
preset snapshot and graph preference were confirmed restored. Native plus/minus
controls also changed 30→31→30 through the same editor facade.

The new `/api/session/steam-graph` served 75 aligned pressure/flow/temperature
samples at 14.772 seconds of a native simulator cycle, revision 78; that single
local HTTP request took 13.11 ms. This is a functional observation, not a performance
budget measurement. The browser's same shared model transport loaded without new
console warnings/errors. Screenshots: `/tmp/decenza-native-steam-editor.png`,
`/tmp/decenza-native-steam-live-graph.png`, `/tmp/decenza-wasm-steam-model.png`.
The browser still opens shared EquipmentPage. SteamPage mounting awaits the native
warning/coach/preference services and shared shell behavior.

### Steam shared service with Qt Remote Objects — 2026-10-21

Supersedes the preceding Steam browser-adapter approach. Both builds now instantiate
the same `SteamService` facade and generated `SteamStateReplica`, and compile the
same `SteamPage.qml`, `SteamGraph.qml` and `SteamLabels.qml`. The native source
delegates to existing controller/editor operations. The binary WebSocket uses the
existing server's origin/cookie checks, package and host identity, bounded buffers
and revocation. Browser reconnection constructs a fresh node and never replays a
command whose result was lost.

Qt Creator Wasm build 1792509381791 succeeded without warnings; native build
1792509381792 succeeded with the existing OpenSSL deployment-target/unwind warnings.
The native gate passed 338/338 QML files in build 1792509381780; subsequent native
builds reused that gate stamp. Both final package headers, installed bundle and
running API match `aaa2b184d9cd2a99ff50d56727647a906a102ceef0710e55a1a14667666d69b1`.

Thirteen focused suites passed across Qt Creator runs 1792509383853–3857:
Steam service, session server, Steam edit request, Steam page editor, stop state,
Steam health, Steam graph model, remote session backend, remote settings,
preparation controller, completion overlay, translation reactivity and QML
registration. Failures were fixed and rerun, not counted as passes: incorrect
upgrade-path matching, missing QtRO host initialization, local replica readiness,
duplicate first-session warnings, stale accessibility fixtures, and transport
tests that confused an already-sent operation with a replay. The graph and warning
tests cover full snapshots, same-host catch-up, new-host baselines and lost history.

Live checks used only the native Mac simulator, started/stopped through Qt Creator:

- Native duration edited 30→31→30 through the shared facade.
- Browser flow edited 1.50→1.55→1.50; the native control followed both changes.
- Browser pressure visibility changed native visibility and was restored. Both
  interfaces followed chart/timer changes; the final preference is timer.
- Browser Stop stopped the simulator and returned to the previous browser page.
- A fresh browser joined an already-running session, opened the shared Steam page
  and displayed samples beginning at time zero. This caught and fixed a shell
  initialization bug: Steam navigation now waits for the shared service's state,
  rather than treating the first live phase as the previous phase.
- Stopping the native host showed the browser's disconnected state. The old
  browser package was rejected after updating the host; reloading loaded the
  matching bundle. No browser warning/error messages were recorded in the final run.
- HTML `/`, `/equipment` and `/recipes` still returned 200 with HTML content.

`evidence/mac-qtro-steam-restored.json` records Idle/Ready, the original
Small/Large/Clean/Off presets, selected Off, duration 30, flow 150, temperature 160,
and timer mode. Simulator sessions naturally updated session history/telemetry.

Code accounting: removed four dedicated browser adapters totaling 1,276 physical
lines, plus Steam-specific controller/action plumbing. The new service, generated
contract declarations and connection types total about 1,500 physical lines
including comments/blanks, before existing-server integration and build glue.
About 337 of those lines are reusable transport/connection code. This first slice
is approximately flat in overall handwritten code; its benefit is one behavior
path and reusable infrastructure, not a claimed whole-app percentage reduction.

Qt source checks: the in-process replica is initialized while retaining Default
state (`qremoteobjectreplica_p.h:83`, `qremoteobjectnode.cpp:1527–1536` in Qt 6.12).
Only the published local node uses initialized readiness; remote replicas require
Valid. A separate test exposed same-node in-process reacquisition losing property
notifications while the first replica awaits deferred deletion: Qt reuses its
implementation (`qremoteobjectnode.cpp:626–641`) without repeating the direct
source-to-replica wiring (`:1287–1317`, `:1527–1538`). Production native creates one
replica; production reconnect creates a new remote node. No Qt patch is shipped.
No matching public bug was found in the initial tracker search; an isolated
upstream reproduction and duplicate search remain a follow-up, not a filed report.

This completes the scoped Mac shared-service integration (section 10), not full
Steam/Home parity. Browser startup is still Equipment; shared Home navigation and
opening Steam preparation while idle remain under the existing unfinished tasks.
Mobile, remote computers, release packaging automation and full-suite acceptance
remain outside this local slice. HTML removal is still a separate future change.


## Equipment shared service — Mac validation, 2026-10-21

The scoped Equipment integration is accepted on this Mac. Native and Wasm compile
the same Equipment page, card and editor and use one facade/backend backed by the
native Qt Remote Objects source. The source delegates to existing operations and
storage, retaining compare-and-set edits and removal/deletion rules. The browser
HTTP adapter and second inventory cache are removed.

Qt Creator evidence:

- Wasm build 1792509381796 succeeded without warnings (47.568 s).
- Native build 1792509381798 succeeded (23.949 s), including the 338/338 QML gate.
  Existing OpenSSL deployment-target and large eh_frame linker warnings remain.
- Focused run 1792509383858: 8/8 suites passed in 17.080 s: Equipment service,
  Equipment storage, shared form controls, remote settings, remote session backend,
  remote session server, Steam service and QML registration. These cover two QtRO
  clients, scoped completions, stale edits, storage lifecycle rules, refresh without
  notification loops, disconnect failure and reconnect without replay.
- Native and Wasm package identity matches:
  `69a271ed26ff2b68eac845c4fce62618a22ae9f771feee1b710e9309510a7338`.

Live checks used the existing native Mac simulator, started/stopped only through
Qt Creator, and the packaged browser at `http://127.0.0.1:8888/wasm/`:

- Browser renamed the unused Varia package to `Varia VS6 QtRO check`; the native
  inventory followed. Saving an older native draft was rejected with the shared
  conflict message and preserved that draft. Canceling it and opening a fresh
  native editor allowed restoring `Varia VS6`; the browser followed the restore.
- The browser's shared grinder-brand picker displayed registry suggestions and
  canceled without changing the form.
- With `Unsaved QtRO draft` in the browser editor, stopping the host disabled Save
  and showed disconnection. Restarting the same build and choosing Reconnect
  preserved the draft and re-enabled Save. The test draft was then canceled.
- Final `/api/equipment` and `/api/equipment/options` responses exactly matched
  the pre-test baseline, including active selection and creation defaults. No
  equipment was activated, removed or deleted during live checks.
- `/`, `/equipment` and `/recipes` returned HTTP 200 with HTML content. Browser
  warning/error logs were empty; current Qt Creator output contained no matching
  QML reference/type/binding or sanitizer error.

Restored equipment snapshot: `evidence/mac-qtro-equipment-restored.json`.

Code accounting: the six superseded Equipment HTTP/storage/QML adapters totaled
1,799 physical lines. The new Equipment contract, facade, source, shared snapshot
assembler and QML backend total 828 lines, a reduction of 971 lines in that
comparison. These numbers include blanks/comments and exclude integration edits,
build glue and tests; they are not a whole-branch net reduction. Equipment reuses
Steam's connection, authentication and transport rather than implementing another
one. Recipe migration should extract reusable lifecycle and conflict machinery
from these working services while retaining domain-specific policies.

This completes section 11 only. Remote computers, mobile devices, release
packaging automation, full-suite acceptance and full native UI parity remain
pending. HTML is retained during the transition, with retirement planned once
Wasm is proven; maintaining two web interfaces indefinitely is not the goal.

## Recipe and dependent pages — integration progress, 2026-10-21

The shared Recipe services and approved History/review/comparison/profile-editor
dependencies are wired, but section 12 is not yet accepted. Native and Wasm use
the same page files. Profile generation and frame editing use a portable local
draft model; explicit commits retain host validation and source/destination
revision checks. Conversation tests use fake providers only.

Qt Creator run `1792509383866` passed 20 of 21 relevant service, profile and
rendering suites. The remaining AI ownership fixture lacked a storage key;
after correcting that fixture, `1792509383867` passed `tst_aimanager` without
warnings. The transport-disconnect test also exposed a real readiness bug:
Qt's `isInitialized()` includes `Suspect`. All twelve service facades now use
one readiness helper that rejects that state while preserving in-process use.

Live checks against the Mac simulator reached the shared Recipe bean/profile
pickers, Profile Info and editor. They found image cache-version query rejection,
an invalid keyboard-container anchor, string-valued QML profile numerics and a
missing title/format-derived editor choice in the transient catalog response
(not a missing field in saved profiles). These have source fixes and focused regressions;
the corrected builds and live paths still need verification. No persisted user
recipe or profile was changed in these checks. This is progress evidence, not
full Recipe acceptance or a claim of measured development-speed improvement.

Further Mac evidence: Qt Creator run `1792509383868` executed all 164 discovered
suites despite the named request; 162 passed. The remaining failures were the
Steam QML source assertion still naming the old access path and missing
`failOnWarning` guards in ten new suites. After correcting those checks, run
`1792509383869` passed all twelve affected suites without warnings.

The browser loaded History (1,304 stored shots), filtered by Sublunar, opened
review, and compared two shots using the shared graph. Promoting the Oct 20
10:13 shot opened the shared wizard with shot data; an existing recipe name
correctly disabled Save. Equipment selection updated that local draft, which
was discarded. A separate browser edit renamed Sublunar Espresso Latte to
`Sublunar Espresso Latte QtRO check`, visible in the native recipe list. After
the browser restored the original name, an older native draft was rejected
with the draft preserved. That draft was discarded. All twenty recipe maps
from `/api/recipes` exactly match the pre-service baseline; `/recipes`,
`/equipment` and `/beans` still returned HTTP 200 HTML.

The icon catalogue is now shared from the canonical resource directory
(75 SVGs, approximately 304 KiB) rather than a per-page Wasm whitelist. Wasm
build `1792509381857` passed without warnings; native `1792509381858` passed
with the existing OpenSSL deployment-target/unwind warnings. Both embed package
`59392377c8b7a0a493124a246d4effaca45ee3b009142a8da84297f2a182b9b8`.
The local wiki manual has a short preview entry, not published.

Remaining runtime investigation: enabling Qt's Wasm screen-reader support after
load has repeatedly resulted in a stale-session screen, with immediate
Reconnect succeeding. Qt builds the accessibility tree synchronously; that
is a plausible cause of an event-loop stall exceeding the five-second liveness
deadline, but it has not been measured and is not established as the cause.
Do not treat reconnect success as clean initial-load accessibility acceptance.

Follow-up: Wasm build `1792509381861` passed without warnings; native build
`1792509381862` passed with the same three known link warnings. Both embed
package `aba700ed4d6c240f048fafb94d48e4dea88d5554bc4b7626c09879cf85456f5e`.
The normal client now enables accessibility before QGuiApplication using the
opt-in used by Qt's own Wasm test harness; the standalone Qt reproduction is
unchanged. The first live load exposed the complete Recipe accessibility tree
without the previous late-activation stale-session failure. This is one clean
load, not a cross-browser accessibility acceptance result.

Post-fix D-Flow checks opened the correct shared editor with numeric values,
edited a local temperature draft from 84 to 84.1 while the native target stayed
84, opened and canceled Save As, and discarded the draft. Recipe thumbnails
rendered without the earlier cache-query errors. A native restart through Qt
Creator disabled browser actions and showed the disconnected state; Reconnect
restored the unsaved Recipe name draft, which was discarded. The shared page
stack is hidden until its services are ready, avoiding transient missing-profile
messages during catalogue refresh.

The browser archived and restored the used Hometown Blend Latte recipe; the
archived count changed from five to six and back to five. After restoration the
complete `/api/recipes` response exactly matched the saved baseline. All three
existing inventory HTML routes still returned HTTP 200 HTML. Qt Creator run
`1792509383870` passed `tst_equipmentservice` without warnings, including the new
real-source Recipe test: an accepted mutation completes once on the host after
client disconnect, the client receives one terminal disconnect failure, and
reconnecting does not replay the mutation. Existing two-client Recipe tests
cover successful update and stale revision refusal.


### Recipe acceptance follow-up — Mac, 2026-10-21

The paired Wasm/native builds `1792509381871` / `1792509381872` passed
(Wasm: no warnings; native: the same three known link warnings), with package
`1cc7732c5210119aa1a6de83007afb6982a5ecc0822f9cf55aa52ac456c321e6`.
The shared text-field workaround now exposes the correct browser accessibility
value when ratio selection changes yield from 36.0 to 54.3 and a dose change to
20 changes it to 60.0. These checks are for the browser tree, not screen-reader
acceptance. Some custom numeric controls still expose a stale numeric AX value
while their visible value and accessible description are correct; a minimal
reproduction is still needed before classifying/reporting that separate issue.

All five existing editor destinations opened from the shared picker: D-Flow,
A-Flow, Pressure, Flow and Advanced. The Advanced editor rendered its two frames,
allowed frame selection and displayed the selected frame's parameters. The
profile knowledge dialog displayed bundled D-Flow / Q content without calling
an AI provider. These draft/navigation checks did not save profile files.

Qt Creator run `1792509383875` passed the expanded Equipment/Recipe service suite,
including real-source clone provenance, activation completion/refusal and bag
relink rules. Run `1792509383876` passed the shared editor-model suite, including
name/content-derived editor selection and refusal to persist transient
`editorType` / `editor_type` fields, with and without the source JSON document.

Live shared-wizard checks created Recipe 88 (`Wasm Recipe validation 2026-10-21`),
cloned it to 89, and saved a replacement bag on the clone (129 to 149). Host reads
confirmed clone provenance and preserved dose/yield/profile/grind. Activating 88
updated the native shot plan and a fresh second Wasm client; the simulator
remained Idle/Ready and never started a shot. After maintainer approval, both
zero-shot test Recipes were deleted using the shared list controls. The complete
20-record `/api/recipes` response then exactly matched the saved pre-test baseline,
including no active Recipe. The native shot plan was restored to 18.1g in,
36.2g out at 1:2, 84/94 degrees and grind 12. This confirms those observed values;
it is not an assertion that every host preference was snapshotted.

History sort changed from Date/descending to Roaster/ascending, survived a fresh
browser reload, then was restored to Date/descending. The second client opened
the shared shot-review graph and outcome data. Both browser console warning/error
logs were empty. Existing `/recipes`, `/equipment` and `/beans` routes still
returned HTTP 200 with `text/html` after test cleanup.

The new native-source Profile and History integration tests are still being
validated. Runs `1792509383877` and `1792509383878` exposed test build wiring
issues, not passing tests; the latter showed History's unnecessary direct
MainController link dependency. Their final results will be recorded below.


### Recipe slice accepted on this Mac — final validation, 2026-10-21

Section 12 is complete for the agreed Mac scope. Both targets compile the same
Recipe, wizard and dependent page QML; HTML remains available. This accepts the
Recipe slice, not the entire migration or cross-device deployment.

Qt Creator run `1792509383883` passed the real-source Profile and History suites
without warnings. Full run `1792509383884` passed **164/164 suites**, zero failed,
zero skipped, no test warnings, in 47.33 seconds. Earlier integration-test failures
were corrected fixture resources, sample data and link dependencies; they are not
counted as passes. `git diff --check` also passed.

Wasm build `1792509381882` passed without warnings and native build
`1792509381883` passed with the three previously recorded link warnings. Matching
package: `fff2818c1ae39235d6feebb37d8b49c62ca02d32155956183043d5cf5f272f6b`.
The old native process was stopped through Qt Creator, absence verified, and the
new app started once through Qt Creator (incremental build `1792509381887`).
The packaged browser reconnected, opened the shared Recipe wizard and profile
information graph; a second client loaded History and shot review with its graph,
outcome and metadata. Both browser warning/error logs were empty. The unchanged
Recipe draft was canceled and the extra test client closed.

| Acceptance area | Evidence |
| --- | --- |
| Recipe create, edit, clone, activate, archive, restore and bag relink | Live shared browser controls, native activation observation and host reads; temporary data removed with permission |
| Conflicts, disconnect and no replay | Live stale native draft refusal and reconnect; real-source two-client service regressions |
| History navigation, search, review, comparison and promotion | Live shared pages; promotion enters the same wizard |
| History preferences and metadata | Live sort persistence/restoration; real-source saved-search validation/persistence and canonical metadata read-back tests |
| Profile editor destinations and independent drafts | All five editors opened live; local draft left native target unchanged |
| Profile commit, stale source/destination refusal and try-draft | Real ProfileManager/source integration tests against isolated files; no transient editor-type fields persisted |
| History load action correlation and storage errors | Real-source callback matching and database-open failure regressions |
| Existing HTML and data restoration | Final `/recipes`, `/equipment`, `/beans` return 200 HTML; complete Recipe response exactly matches the original 20-record baseline after restart |

Testing found and fixed a shared storage error: opening the filtered-history
worker database could fail and still return an empty successful page. The query
now reports the failure to its caller. History's source also uses an injected
load callback, matching Recipe's pattern while retaining MainController as the
canonical action owner; this makes real storage/service tests independent of the
full application controller.

Final visual evidence: `/tmp/decenza-recipe-final-mac.png`. The delivered local
client is `http://127.0.0.1:8888/wasm/`. Profile saves and History metadata writes
were tested against isolated test files/databases, not the user's originals.
Paid AI calls and external uploads were not exercised; their service boundaries
remain covered by fixtures. Phone/tablet, remote-machine HTTPS and full screen
reader acceptance remain outside this Mac slice. The custom numeric-control AX
value issue noted above remains open and is not claimed fixed by these tests.


### Shared Beans inventory — Mac validation, 2026-10-21

`/wasm/#beans` now mounts the existing `BeanInfoPage`, `BagCard` and
`ChangeBeansDialog`; there are no copied page/card/editor files. Inventory,
activation and lifecycle calls use the Bag service introduced for Recipe. Bag
sorting uses the existing shared preferences service. Finished-count failures
are explicit, and reconnect refreshes both the count and an open finished shelf.

Automatic link inspection now has one native owner, `BagLinkController`. It
uses bounded, deduplicated waiters and producer terminal signals, including
misses and transient failures. A result writes through the serialized storage
path only if both the original blob and canonical ID still match in the same
transaction. Stale reconciliation checks that the same link question still
applies. This also fixes the former card-instance race in native Decenza.

Paired Qt Creator builds: Wasm `1792509381892` passed without warnings; native
`1792509381893` passed with the existing OpenSSL minimum-OS and large unwind-table
linker warnings. Both generated package IDs were
`011276a803f735e3741834b6a32b4dba4983dfc0d0c736c75a04247c05c5c3a2`.
Focused run `1792509383885` passed all six suites without warnings: Bag service,
bag-link controller, bag storage, Bean Base client, Bean UI service and shared
preferences. Coverage includes finish/restore/freeze/thaw, active selection,
count-read failure/disconnect, duplicate inspection, stale URL/identity results,
and terminal network misses/failures. The first full run found only a missing
`failOnWarning` hook in the new test; the hook was added and affected run
`1792509383887` passed both checks without warnings.

Live native/browser checks showed matching inventory cards and editor fields.
A browser sort-direction change appeared in native and was restored to Last
used descending. `Gran Bar` search found two finished bags; their controls
remained present after stopping/restarting the host through Qt Creator and
reconnecting without reloading the browser. The browser preserved its search
and open shelf and refreshed the count. Native and Wasm editors opened and
cancelled; the Wasm freeze-date picker opened and cancelled. Lifecycle writes
were exercised in isolated service tests, not on the owner’s bags. All 14 open
and six finished bag records matched the before-check API snapshots exactly.
HTML `/beans`, `/recipes` and `/equipment` returned 200 text/html. No browser
warnings/errors were captured during these checks.

The browser accessibility text-input mirror could diverge from rendered QML
after popup focus changes when targeted through AX; clicking the rendered input
restored normal typing. This is recorded as an unresolved accessibility/input
validation issue alongside the earlier Qt reports, not a screen-reader pass.
Remote/mobile, trusted HTTPS and real-machine gates remain pending. Screenshots:
`/tmp/decenza-shared-beans-mac.png`, `/tmp/decenza-beans-editor-mac.png`.

Final full Qt Creator run `1792509383888` passed **165/165 suites**, zero failures, skips or warning-emitting suites, in 42.6 seconds.


### AppShell and saved-layout source audit — 2026-10-22

The current source declares 41 `AppShell` signals. Native `qml/main.qml` handles all
41; `qml/webclient/Main.qml` handles 20, intentionally ignores the two Quit signals,
and has no handler for the other 19 destinations. The browser entry currently starts
on Recipes rather than `IdlePage`, so its saved-layout actions are not reachable as a
complete Home workflow. The existing `LayoutActions` catalog/dispatcher has 41 action
IDs; its source coverage and task owners, plus the route-by-route missing-handler list,
are recorded in `design.md` under “AppShell and saved-layout closure ledger.”

This is source inspection only. It did not validate every actual page caller, action
argument, operation refusal/error, popup/Loader dependency, close policy, or return
path. Task 15.3 remains open, as do the dependent route/Home tasks; source presence is
not runtime acceptance. No builds, app interaction or new behavior evidence is
claimed by this audit.


### Home operation prerequisites — Mac, 2026-10-22

The shared HotWaterPage and FlushPage use one SharedPresetDraft for revision-
checked edits, selection, failure retention and explicit reload. A live check
found imperative ValueInput assignments severing their QML value bindings after
an edit; removing those assignments fixes the same defect in native and Wasm.
Browser Cup weight 65 → 75 g appeared in native; native restoration to 65 g then
appeared in the browser without reloading. Flush duration 3 → 3.5 s and native
restoration to 3 s likewise propagated both ways. Original presets were restored
(Cup 65 g / 85 °C / 8 mL/s; Quick flush 3 s / 7 mL/s). Earlier stale preset Save
was rejected; Cancel retained the editor and Discard reloaded the host values.

GrindPickerDialog emits one combined grind/RPM edit; RecipeContext validates
both before changing either and publishes one revision. The temperature quick
picker uses the existing shared machine-control service and ProfileManager's
canonical apply method. Its identity guard compares the live profile filename
and title, including unsaved and recipe-generated profiles; it does not confuse
Settings.app's last saved filename with the active profile. Machine-state and
current override checks remain host-owned. Transport tests cover correlation;
no full browser Home or temperature-picker acceptance is claimed here.

Paired Qt Creator builds 1792509381919 (Wasm, zero warnings) and 1792509381920
(native, three existing linker warnings) passed. Focused Qt Creator run
1792509383896 passed all 11 suites without warnings: machine controls/presets,
recipe context, custom widgets, recipe selection, machine snapshot, History,
shared forms, QML registration, inventory and inventory self-test. The inventory
check reconciles every application QML/JS/module source and both generated
native/Wasm manifests; deliberately unassigned and unknown-manifest fixtures
fail as intended. Source inclusion remains separate from behavior acceptance.

Native was stopped and relaunched through Qt Creator, with one original-workspace
instance and Simulation Mode active. A Quick simulator flush opened the native
Flush operation page; the browser returned to its current Recipes base after
completion. HTML `/`, `/beans`, `/recipes` and `/equipment` returned 200 text/html;
no browser warning/error logs appeared. Complete operation-stop/overlay and Home
navigation acceptance remain open. Screenshot: `/tmp/decenza-hot-water-sync-fixed.png`.


### Shared Brew Settings — Mac, 2026-10-22

The native and Wasm builds now use the same BrewDialog, backed by existing
MachineControl, Recipe, Bag, Profile and RecipeContext services. Recipe and bag
writes publish the committed row before correlated completion; native consumers
reuse their canonical reconciliation and avoid the redundant post-write read.

Paired Qt Creator builds 1792509381931 (Wasm, zero warnings) and 1792509381932
(native, three existing linker warnings) passed with matching package identity
`29cefb907e0e4a62fedfcbd2d4c85e8a965a531a015c2aa1685268ec4c394800`.
Eighteen focused suites passed across runs 1792509383897–1792509383899 after
correcting two tests that expected the removed redundant read and one setup spy
count. Final QML-focused run 1792509383900 passed all four suites.

Live checks verified draft Cancel, native changes rejecting stale browser writes,
explicit Reload, and recipe temperature/yield saves retaining unrelated local
dose drafts without false conflicts. Cup tare and capture sound synchronized in
both directions. Disconnect disabled writes; the old client rejected the new
package, and reloading the matching client did not replay its unsaved dose.
All original values were restored: dose 18 g, ratio 3 / yield 54 g, recipe
temperature offset 0 (84/94 °C), grind 10, cup tare 0 g, capture sound off.
No records were created or deleted. Fresh matching-client warning/error logs were
empty. HTML `/`, `/beans`, `/recipes`, `/equipment` and packaged `/wasm/`
returned 200 text/html on the final paired build. Screenshot: `/tmp/decenza-shared-brew-mac.png`.

Task 17.2 remains open for profile-backed/no-recipe controls, capture hardware,
and complete readiness/GHC behavior; this evidence covers the recipe-backed Mac
workflow only. Full Espresso, Home and global navigation acceptance remain open.

### 2026-10-22 — Auto-Favorites, completion correlation, and settings integration (in progress)

The same AutoFavoritesPage/AutoFavoriteInfoPage and ProfileSelectorPage are now
registered in the browser target. Auto-Favorites reads reuse the canonical history
queries with per-request correlation; preference writes reuse HistoryService's
revision checks. These pages are not yet accepted by live Mac checks.

A live-graph test exposed an application-side QList overload mistake: appending a
QVariantList to another QVariantList appended its elements, rather than one nested
value. In particular, appending an empty segment did not grow the list and left a
loop running indefinitely. Explicit QVariant wrapping preserves segment nesting;
goal gap decoding now supplies QPointF values to renderers on both snapshots and
incremental updates. This was not established as a Qt Remote Objects defect.

The common EspressoCompletionCoordinator replaces shell-specific completion UI.
Host operation IDs correlate late history-save results with their originating
shot. Review found that the flow-stop token must be captured synchronously before
the deferred shot-ended event; focused regressions cover a fast next cycle and
mixed steam/espresso flow transitions. Test execution and live acceptance remain
pending for this final implementation.

Qt Multimedia's stock 6.12.0 WebAssembly backend built through Qt Creator using
the workspace wrapper in tools/qtmodules-wasm (Release build 1792509381943).
This proves module compilation only, not Screensaver/media behavior. The native
SDK was not modified; browser media still needs host URL delivery and runtime
checks.

Wasm builds 1792509381945 and 1792509381946 passed without warnings. They predate
the final correlation/settings changes. Native build 1792509381947 caught a
missing test-fixture setter, which was fixed. Build 1792509381948 and test run
1792509383902 refused the changed source/native pair because its bundled Wasm
package identity was stale; no tests ran in 3902. A fresh source-frozen Wasm/native
pair and focused tests are required before restarting the app. HTML is unchanged.

The final paired builds for that slice passed: Wasm 1792509381956 (zero warnings)
and native 1792509381957 (343/343 QML gate, three existing linker warnings), with
package identity `0823d85923378b6bef53069bb74353ba43800ca057eef778ba174d4bf2f1b7c7`.
All twelve focused suites passed across runs 1792509383903/1792509383904. The only
3903 failure was a source-graph fixture that never started the model flush timer;
fixing the fixture to initialize a shot made the focused rerun pass. The actual
nested-list hang and segmented-goal regressions passed.

Native was restarted through Qt Creator with one listener on port 8888 and its
simulator confirmed active. Auto-Favorites loaded real history and its shared
details graph rendered. Its right-axis button was inert: it still called a
read-only browser adapter. HistoryShotGraph now uses the existing shared
RecipeContext preference operation; that fix awaits the next paired build.
Native Settings/Machine water units saved through SettingsPreferencesService and
were restored from milliliters to the original percent setting.

Two simulator espresso checks completed on the matched pair. A reload at about
19 seconds recovered the graph from the beginning and continued to the host's
stop-at-weight result (53.6 g, 37.4 s), then both interfaces opened the new review.
The next shot reset readings/graph to zero; Stop in Wasm showed “Stopped manually”
and opened the new saved result (17.8 g, 13.8 s), rather than the previous shot.
No browser warning/error logs appeared. Screenshots:
`/tmp/decenza-espresso-midshot-rejoin.png`,
`/tmp/decenza-espresso-manual-stop.png`, and
`/tmp/decenza-espresso-current-review.png`. The two simulator history rows remain
for inspection; no original records were deleted. These checks do not close all
operation views, all global overlays, or the remaining Auto-Favorites actions.


### Shared layout/profile follow-up — 2026-10-22

After Qt Creator restarted, native build 1792683623240 completed successfully.
All twelve focused suites passed across runs 1792683626047/1792683626049; the
first run exposed fixture-only assumptions in CustomWidgetHtml and LibrarySharing.
The library delete fixture now releases replies explicitly in reverse request
order, rather than relying on socket scheduling. Native launch through Qt Creator
kept one port-8888 listener and confirmed the simulator.

Live browser checks verified Auto-Favorites graph axis cycling through Weight,
Temperature, Flow and back to Weight, with no browser warning/error logs. The
new shared Pressure draft displayed the canonical defaults (20 s preinfusion,
8 ml/s, 4 bar exit, 10 s hold at 8.4 bar, decline 30 s to 6 bar, 90 C). The draft
was discarded and the Profile list remained unchanged. Native Layout opened and
its status-bar style changed to Surface and back to Standard. Evidence images:
`/tmp/decenza-shared-history-axis.png` and
`/tmp/decenza-shared-pressure-defaults.png`.

Home/library/screensaver integration is now in progress; the foregoing runtime
checks predate those edits and do not establish acceptance for them.


### Shared Home, Library and Screensaver runtime pass — 2026-10-22

Wasm build 1792683623252 and native build 1792683623254 passed through Qt Creator
with package identity `6f779f738bdc321ca0e9cb5c2126022d9faba360191057d628e0c5d26de1452d`.
The native QML gate passed 344/344 files; focused run 1792683626050 passed all
14 selected suites without warnings. An explicit native CMake reconfigure was
needed after kit switching to refresh the old identity. Native was stopped and
started only through Qt Creator; port 8888 had no listener before launch and one
Decenza listener (PID 29803) afterwards. Native Connections confirmed simulation.

The browser renders the actual IdlePage, configured layout and StatusBar, with
weather and host preparation updates. Runtime found a missing page-title binding
and reentrant startup readiness notification; fixes are pending the next build.
Flip Clock, Strange Attractors, flat Shot Map and the 3D globe rendered from the
same QML and followed native preference changes. Browser touch dismissed its
screensaver without changing native navigation. The original Videos & Images
type and flat map projection were restored; cached videos were retained.
Screenshots: `/tmp/decenza-shared-home-wasm.png`,
`/tmp/decenza-shared-flipclock-wasm.png`,
`/tmp/decenza-shared-attractor-wasm.png`, `/tmp/decenza-shared-globe-wasm.png`.
The map initially showed zero shots; this is renderer evidence, not validation
of the external shot-feed result.

3D Pipes exposed a missing shared PipeCylinderGeometry registration. Videos
exposed browser autoplay rejection; the shared page now explicitly mutes an
AudioOutput after attaching the video surface, based on the installed Qt 6.12
backend source. The failure limit also failed to clear the pending source before
destroying the decoder, allowing the Loader to recreate it. These fixes await
the next build/runtime pass. Community catalog rows loaded through LibraryService,
but direct external thumbnail URLs failed in the browser; bounded same-origin
host delivery is being added. None of these unfinished paths is marked accepted.

Stock Qt Multimedia/Quick3D plus shared screensaver code increased the linked
Wasm from 66,607,011 to 75,021,178 bytes (gzip 22,414,104 to 25,381,945 bytes).
The packer and native asset verifier now both enforce a 96 MiB Wasm maximum
instead of 64 MiB. This is a measured packaging adjustment, not startup/performance
acceptance on remote or mobile devices.

### Shared settings integration and media follow-up — 2026-10-22

The next matched pair (Wasm 1792683623268, native 1792683623269) passed. Focused
Qt Creator test run 1792683626052 passed all 12 selected suites without warnings:
Theme service, Library sharing, screensaver tickets/ranges, settings preferences,
layout service, QML registration/inventory/self-test, shared controls, package and
asset checks. Native was launched through Qt Creator, with only PID 62735 listening
on port 8888. These results predate the Connections/Machine/clipboard batch below.

The browser rendered shared 3D Pipes after the geometry registration fix, with
no Pipes error in captured console output. Evidence:
`/tmp/decenza-shared-pipes-wasm.png`. Native theme color editing accepted a
reversible primary-color change; the original #4e85f4 and Default Dark selection
were restored. Videos & Images was restored after the Pipes check, retaining
cached media. The browser title/readiness fixes also ran in this pair.

Valid community thumbnails now load through the host. Some upstream thumbnail
URLs return HTTP 200 with HTML instead of PNG; the host correctly rejects these.
The shared Library card needs to show its existing live preview when data exists,
or an explicit unavailable-preview label for an absent community image. That fix
is in the next build batch, not yet runtime accepted.

Videos remain unresolved. The browser repeatedly failed media and reset the
failure counter on PlayingState, then accumulated more than 1,400 DOM video
nodes. Playback was stopped by leaving the page. Source review found Qt 6.12
QWasmVideoOutput appends each video element but its destructor does not remove it
(qwasmvideooutput.cpp:107–116,566–599). The app-level changes under validation reset
failure counts only after playback position advances and reuse the browser player
across media transitions. Page-destruction cleanup remains an upstream concern.
A standalone bounded reproduction in tools/webclient/media-repro passed its Qt
Creator Wasm Release build (1792683623282, zero warnings). Starting with one DOM
video element, 50 replacements produced 51; another 50 produced 101. No source
or network request was required. Filed QTBUG-151767 with a source-only ZIP:
https://qt-project.atlassian.net/browse/QTBUG-151767. The issue is distinct from
QTBUG-151746's no-exec deferred-deletion case. This measures DOM retention, not
decoder or heap allocation. A cached H.264 MP4 played successfully from the
standalone server in the same browser, so missing H.264 decoding does not explain
the app failure. Host media delivery and app playback lifecycle remain unverified.

Connections and Machine settings, host Shot Map preferences, and local async
clipboard handling are being integrated with shared app QML. These are not marked
complete: paired builds, browser reachability, host log-view integration, and
workflow tests remain outstanding. Existing HTML routes have not been removed.


### 2026-10-22 — shared settings/calibration/log validation

- Qt Creator Wasm build `1792683623291` passed with zero warnings; native build `1792683623292` passed the QML gate (344/344). Both carried package ID `dab1b55ca7b87b3c2235d68f5cb73827781a0a0b14de6c10bc0e1ced478a1b48`. Native still reports the existing OpenSSL deployment-version and large unwind-info linker warnings.
- Twelve selected suites passed after correcting the inventory fixtures and two test-harness issues (`1792683626054`, corrective run `1792683626055`): calibration policy/math/service, connections, WebDebugLogger, SystemLog, SubsystemLogView, preferences, QML inventory/selftest, package/assets.
- Native simulator: shared Connections log rendered three canonical simulator entries and local Clear emptied the view. Flow Calibration rendered 20 shot curves and advanced to the next wizard step; cancelled without saving. Auto Calibration was restored to its original enabled state; multiplier remained 1.0. Machine settings rendered.
- Browser boot and matched-host connection passed. Video media remained failing, despite limiting the reused player to one DOM video element. Media playback acceptance remains open. Source investigation found Qt 6.12 maps HTML `emptied` to `EndOfMedia` (`qwasmvideooutput.cpp:825–834`), so the page could consume a fresh host ticket during load. A guarded-completion fix is pending runtime verification; this is distinct from filed lifecycle issue QTBUG-151767.

### 2026-10-22 — media playback and expanded settings integration

- Qt Creator Wasm build `1792683623310` passed with no warnings; native `1792683623311` passed with QML 345/345 and the existing three linker warnings. Packaged identity: `49d06640f65a4ccee7b409b9954486faec2caa714c91fb03945cd86cb9c0d3f1`. Native was stopped and relaunched exclusively through Qt Creator; PID 98312 was the sole port-8888 listener.
- Browser screensaver rendered actual video, then advanced naturally from a 34.951583-second clip to a 45.5455-second clip while retaining one DOM video element. Dismissal paused playback and returned the browser to Home; native remained on Shot Upload settings, simulator Ready. Screenshots: `/tmp/decenza-wasm-screensaver-playback.png` and `/tmp/decenza-wasm-screensaver-dismissed.png`. This closes the initial playback failure, not reconnect, full playlist/cache, or mobile acceptance.
- Language-tab opening coincided with a live-session timeout; source review identified repeated full-registry count work on progress events. Coalesced notifications/count invalidation and screensaver reconnect recovery are pending a new paired build and runtime check. Runtime undefined Home fields on disconnect and the media position-handler warning are also corrected in source, pending validation.
- Run `1792683626056`: 13/15 focused suites passed. Language paging fixtures accidentally constructed two scalar rows instead of one map; ShotUpload's browser-chooser test used the native host node. Both fixture fixes await rerun. The new two-replica private-account-link test passed.
- Source review found deferred Descale cancellation and disconnect/reconnect completion bugs. Canonical deferred cancellation and host workflow tracking fixes plus regressions are pending paired validation. Back remains navigation-only, as accepted. Explicit pitcher/steam overrides retain their existing semantics.
- AI settings, Migration, Backup, local import/download and Debug integration is in progress. Existing HTML remains intact. No provider calls, credential changes, bulk uploads, imports, restores, or firmware updates were executed in these checks.

### 2026-10-22 — startup repair, shared dialogs and simulator window

- The expanded Settings shell initially prevented Wasm startup: `SettingsSearchMatcher.mjs` imported `third_party/fuse/fuse.mjs`, which was absent from its resource manifest. The browser console identified this before the generic `RuntimeError: Aborted()` banner. Added the canonical Fuse file to the Wasm module, the shared About QR image, and the remaining bundled font weights. Build-time resource-closure checking is being added to prevent missing relative module imports and literal qrc assets from reaching deployment.
- Qt Creator Wasm `1792683623333` passed without warnings; native `1792683623335` passed QML 349/349 with the existing three linker warnings. Both carried identity `a2ce48447075630230cea58a21a0b82eb1222b026fd87535bb8d9c5083f48c86`. The new GlobalSupport confirmation/private-reply, GHC simulator guard and Visualizer import suites all passed (`1792683626059`, four suites). Earlier corrective runs `1792683626057`/`1792683626058` completed all 19 focused settings/language/media/import/maintenance suites.
- Shared global support dialogs now use correlated host event IDs, one host-owned MCP deadline, and requester-only crash-log replies. These tests do not substitute for live crash/MCP UI acceptance or authorize sending reports.
- Live startup exposed two GHC window issues: its separate native QML engine could not access the TranslationManager singleton, and Qt Wasm's default visible-window mode was fullscreen. Explicit translator injection and `Window.Windowed` fixed those. Paired builds `1792683623338`/`1792683623339` passed; the browser rendered Home and a normal 400x470 GHC window. Closing the browser window was local. Native PID 50089 was the sole port-8888 listener after a Qt Creator restart. Screenshot `/tmp/decenza-wasm-home-restored.png` shows Home after closing the overlay. The earlier `/tmp/decenza-wasm-startup-fixed.png` shows the fullscreen overlay, not Home.
- The maintainer then identified that the browser ignored the existing Hide GHC Simulator preference. Corrected source now observes the shared preference and waits for preference readiness; Debug tab visibility also uses the native host's published debug-build flag. These latest fixes await paired build and runtime verification.
- Browser Settings search filtered results with real keyboard input and opened Machine → Background. The picker exposed patterns, presets and 139 image IDs, but many thumbnails returned HTTP 503 because the eager image list exceeded the host's two-job preview limit. Virtualized/bounded thumbnail loading is pending paired verification; catalog visibility alone is not image-rendering acceptance. Cancel left the preference unchanged. Screenshot `/tmp/decenza-wasm-shared-background.png` records this pre-fix picker.
- Language settings opened without the earlier long blocking delay (96 ms for the CUA click call, not an end-to-end performance benchmark). A manager-only download signal absent from Wasm was found and moved to correlated LanguageService completions; that fix awaits the next build. Async clipboard and disconnected-import cleanup fixes are also pending validation.
- Simulator Descaling: entering preparation turned the steam heater off; Back left preparation active; reopening and explicit Cancel restored the 160°C steam target and returned to settings. Transport entry rendered its existing introduction/start control, without starting a drain. Full maintenance sequences and multi-client acceptance remain open.
- HTML routes remain intact. No credentials changed, paid provider requests, reports, publishes, restores or real-device maintenance operations were performed.

### 2026-10-22 — hide-setting regression fixed and full Mac suite green

- Latest paired Qt Creator builds: Wasm `1792683623348` (zero warnings), native `1792683623349` (existing three linker warnings), matched package identity `486229bfe407d58312cccd4e5dd6d785d35a7e4498d4af0626f5be56fd65198e`. Native was restarted through Qt Creator only; PID 95899 is the sole port-8888 listener.
- Full Qt Creator run `1792683626063`: **202/202 suites passed**, no failures or skipped suites, 33.930 seconds. Prior full runs exposed stale PORTAL/refusal-message assertions, an unacknowledged mock upload at teardown, a missing non-default-constructibility assertion, missing fail-on-warning setup, and a Steam timer-boundary fixture. Corrections preserve production validation; the Steam test now waits for the actual elapsed-time notification instead of sleeping 1100 ms. A deliberate missing-row warning is explicitly expected only in its negative Flow Calibration test. Two focused corrective suites also passed in `1792683626062`.
- The packaging target now checks generated QML/qrc manifests before staging deployment. It rejects missing relative QML/ES-module dependencies and complete literal qrc asset references; three fixture cases run as `tst_webclientresourceclosure`. Runtime-selected host media is outside this static check.
- Fresh Wasm startup honors the existing checked Hide GHC Simulator preference, without flashing a window before preferences arrive. Debug tab visibility follows the native host's debug-build flag. Live round trip: uncheck in Wasm showed both browser and native GHC windows; recheck hid both. Original value **checked** restored. Screenshots: `/tmp/decenza-wasm-ghc-hidden.png`, `/tmp/decenza-wasm-shared-hide-setting.png`.
- The shared background picker now creates a bounded GridView and loads one thumbnail at a time per client with bounded retry and close cancellation. In the browser, visible thumbnails loaded, scrolling instantiated further images, and selecting image 19 updated the preview; cancelled without saving. No new browser warning/error appeared during that check. Screenshot `/tmp/decenza-wasm-background-images-loaded.png`. Two-client contention and final native grid comparison remain open; this is not a blanket image/media acceptance.
- Missing Language download signal warnings are replaced by local correlated service completions. Support-email/TOTP clipboard buttons await async completion, and disconnected local import selection now fails and closes its resources instead of remaining busy. Full tests/builds cover these source changes; live clipboard permission-denial and file picker checks remain open.

## Shared screensaver mode and Settings runtime pass — 2026-10-22

On the Mac host PID 95899 / package `486229bfe407d58312cccd4e5dd6d785d35a7e4498d4af0626f5be56fd65198e`, one Wasm tab kept Settings open while a second loaded `#screensaver`. Changing the host preference through the first tab rendered the existing Flip Clock, growing Quick3D Pipes, Strange Attractor, flat Shot Map with terminator, and rotating Quick3D globe in the second. No warning/error console entries accompanied these mode renders. This verifies the shipped graphics/module/resource path on this Mac, not frame-rate, remote-device or full accessibility acceptance. Screenshots: clock, pipes, attractor, flat map, globe. The shot-map response was empty; nonempty marker/profile rendering still needs fixture evidence.

Preferences were restored to Videos & Images / Espresso, with Flat map shape and the existing video cache preserved. Clicking the second tab’s screensaver returned only that browser to Home; the native app remained on Home with simulator Ready, and the Settings browser retained its own page. Fresh media-page entry rendered a cached video. A retained Map-to-Videos transition instead stayed on the gradient with a paused ready DOM video: this is a confirmed defect, not a passing media transition. The shared page now requests media on mode entry, stops it on exit, guards requests outside Videos, and releases a correlated ticket if its response arrives after leaving the mode. Paired rebuild and live retest are pending below.

The Settings reachability pass opened Connections, Machine, Calibration, Themes, Layout, Screensaver, Shot Upload, AI, MQTT, Language & Access, About and Debug. This establishes entry/rendering, not every control’s behavior. Firmware Check now displayed bundled stable v1352 and its notes; Update now remained disabled with the explicit simulator guard. No firmware update, paid AI request, account change, external upload or destructive data operation was performed.

The pass found History & Data could not load (`Text.selectByMouse` does not exist), String Browser called three LanguageService properties as functions, startup connection booleans received undefined values, and four layout-editor toasts were anchored inside managed layouts. These are corrected in shared QML; the next build/retest must verify them. The String Browser loaded 4,822 translation entries and opened inline customization. Tool-driven key presses did not change its text, so this does not establish text-save/IME acceptance. No custom string was saved.

### Repair verification

Qt Creator Wasm build `1792683623357` succeeded with zero warnings; native build `1792683623358` passed the 349/349 QML gate and had only the three previously recorded linker warnings. Their package ID is `c8a9826bf37d02a290fa25cae7a22abde54677c39ea15b55b018cc42e098008b`. Focused run `1792683626064` passed all five suites (`tst_screensavermediatickets`, `tst_settingsconnectionsservice`, `tst_languageservicesource`, `tst_layoutservice`, `tst_webclientresourceclosure`) without test warnings. An intermediate native build was correctly rejected by the stale-source gate because a fix landed after its QML copy step; the final build above includes the complete frozen batch.

Native PID 10339 was started through Qt Creator after stopping PID 95899 and verifying TCP 8888 was free. Its executable is the original checkout’s macOS Debug app, and it is the sole TCP 8888 listener. The fresh browser loaded without GHC while the shared hide preference remained checked. History & Data now loads in Wasm and native. Connections, Layout and String Browser produce none of the earlier QML warnings after the restart. The native Water readout editor also opens and closes without changing preferences.

On the retained second browser tab, Videos→Shot Map paused the video; Shot Map→Videos resumed a new cached clip without reload. DOM inspection found one video element, `paused:false`, `readyState:4`, and increasing playback time. Qt still emits its known `QWasmVideoOutput::stop()` and frame-loop-exit diagnostic warnings during stop; this is not described as a warning-free multimedia run.

Earlier behavior, superseded by the 2026-10-25 shared-sleep revision below: the native Sleep button put the simulator to sleep and opened the native screensaver. Dismissing only the browser screensaver returned that client to Home with machine status Sleep, while native kept showing its screensaver. Tapping the native screensaver restored simulator Ready. This proves local browser dismissal does not wake the host. It also exposed a missing explicit browser Wake affordance after dismissal; the shared Sleep widget is being made state-aware, with validation still pending.

Firmware check showed installed simulator v1, bundled stable v1352, release notes, and disabled flashing. ZIP chooser invocation was attempted without selecting a file, but the automation surface did not expose a chooser or completion; chooser cancellation/import remains unverified. No import or data change was performed.


### Explicit Wake and final paired repair verification

Qt Creator Wasm `1792683623361` passed without warnings; native `1792683623362` passed QML 349/349 with the existing three linker warnings. Both carry package identity `f3f9018ff79753f8bc5589c2a26b78b453996801f0e5646426698a0cc6676b72`. Native PID 44083 is the original checkout’s macOS Debug executable, started through Qt Creator after stopping the preceding process and checking port ownership. Focused run `1792683626065` passed `tst_customwidgethtml`, `tst_machinecontrolservice` and `tst_machinecontrolpresets`, without warnings (2.260 seconds).

The shared compact Sleep widget and compiled Sleep tile now choose Sleep/Wake from host machine phase through one `LayoutActions` definition. Wake uses the existing correlated host machine-control operation. Live verification: native Sleep left the browser on Home with Wake machine; clicking that browser control exited the native screensaver, woke the simulator and returned both clients to Ready. Browser Ready after Wake. The original Hide GHC Simulator preference remains checked and the browser has no simulator window on startup.

The String Browser footer now describes translation entries rather than unique strings: multiple keys can have the same fallback text, so its row count legitimately differs from the language summary’s unique-string count. No user translation was changed. HTML routes and all existing user data remain intact; pending file chooser, text-save, multi-client and device acceptance are not implied by these repairs.


### Translation keyboard and maintenance navigation verification

Paired Qt Creator builds `1792683623365` (Wasm, no warnings) and `1792683623366` (native, QML 349/349, three existing linker warnings) share identity `13a09fad6d65d9c8bc4205684e49f050a2ed4877d2fb1db786366a234737f6ad`. Native PID 76532 was launched through Qt Creator after stopping the prior instance and verifying port 8888 was free; it is the sole listener. Focused run `1792683626066` passed five maintenance/language/backup/server-policy suites without warnings (1.470 seconds).

Changing the browser language to German updated native Home immediately; the optional paid-translation prompt was declined and English restored. A translation editor defect was confirmed: the inline TextEdit accepted paste but not keyboard input, its child was absent from Wasm accessibility DOM, and Back left it focused. The shared row now uses Grouping rather than ListItem, preserving its editable child, and Back finishes that actual editor rather than focusing its enclosing ListView focus scope. A 150 ms centering timer was replaced by focus/keyboard geometry events. Normal keyboard input and Enter saved a temporary “Manual draft check” label; browser and native About rendered it. Clearing the field and pressing Back restored zero customizations and the native Manual label. This covers keyboard save/Back on this Mac, not mobile IME or a screen-reader session.

Maintenance preflight read the actual host session `machineUi.simulationMode:true`, `de1IsHeadless:true`, `de1Connected:true`; the native connection log independently reports the simulator attached and no real machine connection for this process. Browser Start Descaling entered the active phase. Its visible Back control returned to Settings while Descaling continued; reopening rejoined it. Native auto-navigation showed the active procedure, and native Back likewise left it running. Explicit browser Stop returned the host to Ready and restored its 160°C steam target. It incorrectly showed “Descale Complete” after an abort; that discovered shared completion-state bug is being corrected, so this is not full maintenance acceptance.

Transport likewise started in the simulator, retained its phase after browser Back, and could be rejoined and explicitly stopped. Browser returned to preparation/Ready; the native observer showed “Draining finished” with conditional early-stop guidance because its local `userStopped` flag did not see the other client’s Stop. Shared stop-outcome handling is pending. No maintenance action touched physical hardware.

ZIP picker attempts through both accessibility and pointer activation did not expose a chooser to the automation surface. Qt 6.12 may use File System Access `showOpenFilePicker`, which is not observed by Playwright’s input-file chooser event, so the timeout is not proof of a product failure. No file was selected/imported. File chooser cancellation remains open.


### GHC simulator host-command verification

On the same matched `13a09...f6ad` pair, temporarily unchecking Hide GHC Simulator exposed the shared 400×470 window. GHC Flush and Steam entered the native simulator operations; native Qt Creator output recorded `Starting steam`, `State -> Steam`, `Stop requested`, `State -> Idle` and `SubState -> Ready` for browser GHC start/stop. Browser phase/navigation followed these host transitions. The service controller has no physical `DE1Device` fallback and refuses commands without live simulation mode plus an attached simulator; these guards are covered by the existing `tst_ghcsimulatoroperationpolicy` suite in the full green run. The original hide=true preference was restored, the simulator scale was tared back to zero and host phase was Ready. No espresso shot or real-device operation was created. This completes the Mac simulator integration scope of 27.1, not physical GHC acceptance.

Read-only local HTTP smoke checks on this pair returned HTTP 200 for the existing HTML `/` (text/html), Wasm `/wasm/` (text/html), and `/api/session` (application/json).


### Shared maintenance outcomes, diagnostic routing and upstream reproduction

Qt Creator paired builds 1792683623375 (Wasm, zero warnings) and 1792683623376 (native, three existing linker warnings) share package 139bfb069000dc3fc29ce3a8d9e9127969e228342a6380f53696f54051a6bc96. Focused run 1792683626068 passed all four maintenance/assets/library suites. Full run 1792683626069 passed **202/202**, with no failures, skips or test warnings (35.250 seconds). The original checkout's native PID 95253 was started through Qt Creator after stopping the prior process and checking port 8888 was free.

Explicit Stop now publishes a shared stopped outcome, so both clients distinguish an aborted maintenance run from successful completion. Transport stopped and Descale stopped were verified against the simulator. Back/reopen preserves that outcome; Done clears it across clients. A final Descale Start/Stop/Done check returned the native observer to Home/Ready and the browser to Settings, without retaining a false Complete view. Natural completion, reconnect during operation and physical hardware remain separate acceptance checks.

Diagnostic query routes now retain query parameters on the /wasm redirect and resolve allowlisted assets by path. Live checks returned 200 for HTML /, /wasm/, /wasm?accessibility-repro=list-item, /wasm/?accessibility-repro=list-item, and /wasm/qtloader.js?build=smoke; an unlisted script remained 404. Existing HTML routing stays intact. The Library double-click action now invokes the existing shared apply operation, but actual application of a library fixture remains unverified.

The isolated ListItem/TextEdit fixture reproduced a Qt 6.12 accessibility defect: changing the parent accessible name removes the editable child from the browser DOM while QML still renders it. This was filed as [QTBUG-151770](https://qt-project.atlassian.net/browse/QTBUG-151770) with standalone Qt-only QML, exact source references and no Decenza-specific dependency. Actual screen-reader behavior is not claimed. Diagnostic fixtures are explicitly excluded from shipped application presentation coverage; FormProbe and RenderingProbe use production shared controls, and the other fixtures isolate Qt/media behavior rather than replacing app UI.


### Trackpad scroll-distance adjustment

The user's Mac trackpad report was too little movement per gesture, not frame stutter. Exact Qt 6.12 source inspection showed Wasm puts browser pixel deltas in both pixelDelta and angleDelta (qwasmwindow.cpp:949–970), and Flickable's default wheel path multiplies angleDelta by wheelScrollLines * 24 / 120 (qquickflickable.cpp:1776–1785). The default three lines therefore yields 60% distance. Wasm bootstrap now sets the public QStyleHints wheelScrollLines property to five before creating QML, making that scale 100%. No page-specific handlers, timers, copied QML or patched Qt were introduced. Native and HTML scrolling are unchanged.

Qt Creator Wasm build 1792683623380 passed with zero warnings (49.754 s); native 1792683623381 passed with the three existing linker warnings (12.314 s). Package identity is 1a22234aaf5af471123be11bd3dd3e196b91b1bc01adfe9ee87880931dd2b763. PID 1215 is the original checkout's sole native listener on 8888, launched through Qt Creator after stopping PID 95253 and verifying the port was free.

On the same 1280×720 Machine settings viewport, a quarter-page automated scroll moved the Steam Heater / Keep warm row from y≈480 to y≈372 before the change (108 pixels), and to y≈300 after it (180 pixels): a 67% increase, matching the source calculation. Before, after. This verifies the distance scale, not real trackpad gesture timing or the user's comfort; user feedback remains pending. The local wiki manual now briefly describes browser pixel-distance scrolling. The related animation-timer report QTBUG-133695 was checked and is already fixed for 6.11 Beta3; it is a different issue, not evidence of a regression in this build.


The user then confirmed the distance change is better but continuous trackpad scrolling remains substantially slower than other Mac windows. The exact existing upstream issue is [QTBUG-141540](https://qt-project.atlassian.net/browse/QTBUG-141540), In Review with no fix version, with a proposed noScrollPhaseMode/direct scrolling patch (Gerrit 686407). We added a Qt 6.12 confirmation and the partial-mitigation measurements to that report rather than filing a duplicate. The user explicitly chose to report/track Qt and continue parity, keeping the improvement; no private-Qt animation workaround is included. Scroll comfort is therefore a known upstream hold, not an accepted native-equivalence result.


### Settings search and shared unit preference

On the 1a2223 pair, browser Settings search found Temperature unit / Show temperatures in Fahrenheit, closed when selected and scrolled to that actual shared control. Toggling it updated native Home to 199.4°F / 320°F and the native shot-plan temperatures, while native navigation stayed on Home. Native refresh. Toggling it back restored Celsius on both clients. This covers this search result and app preference propagation; it is not acceptance of every settings action.

On live pair `af2ede12`, browser Settings search selected Fahrenheit while the native client remained on Calibration. Both clients displayed 93°C as 199.4°F and 160°C as 320°F; switching back to Celsius restored both displays. Native and browser navigation remained independent (native stayed on Calibration, browser on Machine). This verifies the shared temperature-unit control and its cross-client snapshot update, not all Settings Machine writes.


### Browser-specific scroll feedback and folder cancellation

The user reports Mac trackpad scrolling in Edge is effectively unusable on Favorites and requires several gestures for each movement in Settings / Machine; Safari remains usable but slow. Isolated CUA wheel events move both shared views in Edge. This establishes that the views accept discrete wheel input, not usable continuous-trackpad scrolling. The browser comparison was added to the existing [QTBUG-141540 comment](https://qt-project.atlassian.net/browse/QTBUG-141540), explicitly distinguishing user-observed behavior from automated evidence. No further scrolling workaround or private Qt change was introduced.

The shared folder transfer now exposes Cancel while waiting for directory selection; cancellation closes the requester-local selection and ignores a late result. Profile Import and History & Data use the same component and treat cancellation without a warning. Qt Creator Wasm 1792683623386 passed with zero warnings (54.399 s); native 1792683623387 passed QML 349/349 with the existing three linker warnings (45.984 s). Matching package: 0cd90635f07409c4e07223bf27d5a1d7c84cc8711ac066d2e830c4b90bb8aaed. Focused run 1792683626070 passed all four suites: tst_localfiletransfer, tst_localfilesave, tst_profileimportservicesource and tst_settingspreferencesservice (7.020 s, no warnings).

The original-checkout native PID 11849 was started through Qt Creator after stopping the prior instance and checking port ownership. In Microsoft Edge, the shared Profile Import Browse button opened the native browser folder chooser; pressing its Cancel returned to the page with Browse available and no pending Cancel button or error. No files were selected or transferred. This proves actual browser-picker cancellation in Edge; silent-picker recovery in the embedded browser and successful fixture import remain separate checks.


The embedded-browser silent-picker path also passed live recovery: Browse displayed Cancel folder selection; Cancel cleared it without an error; a second Browse/Cancel cycle worked. Neither attempt selected/transferred files.

### Favorites navigation and filter scope

On the same 0cd906 pair, Auto-Favorites Info loaded the existing shared graph and statistics. Show for the default Bean + Profile + Equipment grouping opened history with 52 matching shots, matching the favorite count (50 loaded initially of 52). Recipe from the Blooming Espresso favorite opened an unsaved shared wizard draft with the expected bean, profile, 18g dose, 30g target and grind 9. Cancel returned without saving a Recipe.

Temporarily changing grouping to Bean + Profile + Grind setting + Weight split the existing data correctly. Show for the 26.6g/42g, grind 12 Sublunar group returned exactly two shots; the filter banner exposes the canonical 26.5g dose bucket and 42g target. The original Bean + Profile + Equipment grouping was restored. Load success/refusal, concurrency and remaining native regression checks are still open, so task 18.2 is not marked complete.


### Profile Import runtime warning cleanup

The live browser console exposed a stale empty onBatchImportComplete handler targeting ProfileImportService, which does not expose that signal. The no-op was removed from shared QML. Qt Creator Wasm 1792683623390 passed with zero warnings (87.743 s); native 1792683623391 passed QML 349/349 with the same three linker warnings (118.690 s). Both packaged manifests carry 91d08dbb4d3ff5b2cb9a19545a89b88d08e53e55c97860397e212f299e6d7f56. Native PID 23289 was launched through Qt Creator after stopping PID 11849 and verifying port 8888 was free.


### Shared-page disconnect presentation

The first reconnect pass used package 9d795772f83198ead6d3398fe71540930be1f18216d479944efffb9253157c59 (Wasm build 1792683623393, zero warnings; native 1792683623394, QML 349/349 and the three existing linker warnings). Focused Qt Creator run 1792683626071 passed tst_historyservice, tst_recipecontextservice and tst_machinecontrolservice in 3.000 s with no warnings. Favorites settings retained typed display values while disconnected, disabled their writes, and restored enabled controls after explicit Reconnect. The original grouping and preferences remained unchanged.

That pass exposed additional empty-snapshot presentation warnings in Hot Water and Profile Import. The shared pages now provide typed disconnected display values; service-ready guards still control writes, and the Hot Water flow draft only resynchronizes from a ready service when the user is not editing. These are shared QML changes, not browser-only control copies. Final paired-build and live reconnect validation are pending below.


Final validation: Wasm 1792683623398 passed in 43.292 s with zero warnings; native 1792683623399 passed in 51.081 s with QML 349/349 and only the same three linker warnings. Package cd38be058302eeda553824d581a18085f6e3a5af45202b212c28b245d91f78cd matches in both manifests. Qt Creator stopped the old instance before launching PID 36315, then stopped/restarted that matched build as PID 36451 for the disconnect test; port ownership was checked between each launch. Favorites settings, Hot Water and Profile Import all recovered through explicit Reconnect without reload and emitted no new warning/error logs during this pass. Favorites controls disabled while offline and restored the same values; Hot Water restored Cup 65g, 85°C and flow 8mL/s. No dispense or import operation was started. Hot Water after reconnect.


### Screensaver layout preview closure (Mac)

On package cd38be058302eeda553824d581a18085f6e3a5af45202b212c28b245d91f78cd, all four non-media screensavers rendered using shared ScreensaverItem in full and compact layout modes: Flip Clock, Pipes, Attractor, and Shot Map. The four compact previews also appeared on native Home through the same host layout. Browser logs contained no new warnings/errors. Pipes visibly generated 3D geometry; Attractor rendered its evolving curves. Shot Map rendered its map texture with no simulator markers, so this does not establish populated-map data correctness or globe performance.

The test used the empty centerStatus and topRight zones. The temporary full-size preview was repositioned through the editor to avoid the existing action row; overlap is possible in this manually positioned layout. No preview was activated, so no sleep or screensaver-type change was requested. All temporary widgets were removed and centerStatus offset restored from -320 to its original -120. The final parsed GET /api/layout response exactly equals the saved baseline, including IDs, options, scales, offsets and ordering. Together with the earlier fullscreen Flip Clock/Attractor/Pipes/flat-map/globe checks, this closes task 25.2's Mac implementation/module/preview scope. Broader resize/input/performance and real-device graphics remain under 25.4 and 28.3.

A separate source review found that IdlePage used zero for an absent centerStatus offset while the preview/service default is -65. IdlePage now asks the existing LayoutService helpers for offsets/scales rather than keeping a second default definition. A proposed helper-only test was removed because it would pass with the old UI bug; paired build and existing layout tests are the relevant immediate checks, with runtime coverage recorded separately.


The shared positioning cleanup passed Qt Creator Wasm 1792683623402 (45.457 s, no warnings) and native 1792683623403 (43.924 s, QML 349/349, three existing linker warnings). Matching package: dbc82a2ca300b744bd3ff6dcd39c008749e0fe77219e2d66c95abfe860c64e5f. Focused run 1792683626072 passed tst_layoutservice and tst_qmlregistration; tst_customwidgethtml caught its outdated source-level expectation of the old inline temperature binding. That assertion now names the typed vesselTemperature binding; rerun 1792683626073 passed (0.530 s, no warnings). This test checks preservation of a binding, not the entire rendered Hot Water behavior; the preceding live reconnect check supplies that evidence. Native PID 40653 was launched through Qt Creator after stopping 36451 and verifying port 8888 was free.


### Natural maintenance completion and browser rejoin

On package dbc82a2ca300b744bd3ff6dcd39c008749e0fe77219e2d66c95abfe860c64e5f, the sole native host PID 40653 reported simulationMode=true, de1Connected=true and de1IsHeadless=true before maintenance. Transport ran to its natural simulator finish (the canonical simulator cycle is 12 s), returned Ready and showed Draining finished. Done cleared the outcome. Together with the earlier explicit-stop/Back/reopen checks and maintenance service/policy connection-loss tests, this completes task 19.2's Mac/fixture scope; no physical draining or BLE loss was exercised.

Descaling's safe preparation showed heater Off and 25°C, without a hot-steam override. The five simulator steps advanced at their existing 15 s cadence: Init, FillGroup, Return, Group and Steam. Reloading the requesting browser during FillGroup rejoined the current step and elapsed time (28 s rather than restarting); the independent Home observer remained on Home. The rejoined run completed naturally and showed rinse instructions. Heater hold persisted until explicit Done, which restored the original 160°C steam target and Ready. No real device or maintenance consumables were involved. However, native and Wasm both displayed 0% throughout: setSimulatedState bypasses DE1Device's descale-progress initialization. Fixing the canonical simulator/device path remains required before closing 19.1.

One observer load briefly showed `installQtDialogAccessibility is not defined` above an otherwise running Home UI; ordinary reload cleared it. Source review found synchronous helper-before-bootstrap ordering and the helper included/allowlisted in the package. There is not enough evidence to attribute the intermittent load failure to ordering, caching or transport. No speculative retry/suppression was added; retain it as an unresolved startup observation for repeat-load testing.


Three subsequent ordinary observer reloads showed Home without the startup banner, and the served accessibility-helper SHA-256 matched the current package manifest. This narrows the observation but does not prove a fix or explain the original failure.

### New-profile draft cancellation (Mac)

On the dbc82a2 package, Profile Selector → Add a profile → Create new profile opened the existing shared editors for Pressure, Flow, D-Flow, A-Flow, and Advanced. Each Back → Discard returned to the selector. Advanced also accepted a new local frame before discard. No Save/Save As/Try operation was invoked. The selector retained D-Flow / Q as currently selected and the same counts (12 favorites, 11 built-in, 0 downloaded, 1 user-created); native stayed on Home with Gran Bar · D-Flow / Q, 18g dose, 54g yield and grind 10. No new browser warnings/errors occurred. Source review confirms creation uses ProfileEditorDraft locally and discard only navigates back; explicit host operations own Save/Try. This supplies cancellation-route evidence for 18.1, not full auto-load/favorite-control acceptance.


### Canonical simulator descale progress fix

DE1Device now routes both BLE state packets and simulated state updates through one descale timing lifecycle. The simulator and progress estimate share its 15-second step constant (75 seconds total); actual firmware retains the measured 30/30/120/120/420-second schedule (720 total). Regressions exercise nonzero progress, step weighting, remaining time and reset on completion; firmware bounds explicitly reject the simulator schedule. No browser-specific progress timer was added.

Final Wasm build 1792683623408 passed in 40.506 s with no warnings; native 1792683623409 passed in 60.052 s with four existing linker warnings (OpenSSL deployment target, repeated for a test binary, and compact unwind size). The unchanged QML lint gate was stamped, not rerun by this C++-only change. Both package headers and the served host report a834d5787e01fc35c312749c0e3ade1f8fb7a8b331d6d8fa8b7743079903eb48. Qt Creator stopped PID 40653, the port was verified free, and started the sole original-checkout listener PID 47655. Session preflight confirmed simulationMode=true, de1Connected=true, de1IsHeadless=true and Ready. Full run 1792683626074 passed 201/203 suites, including both new descale regressions, but exposed a SubsystemLogView test-module resource failure and three unclassified file-picker QML fixtures. These failures remain open until repaired and rerun.


Live simulator verification on a834d578 passed: the browser showed 28%, step 2/5, 54 seconds remaining at 21 seconds, then reloaded into the existing cycle without restarting it. Native showed 41%, step 3/5, 44 seconds remaining at 32 seconds. Browser later showed 72% at step 4 and 89% at step 5 before natural completion. Browser progress, after reload, native progress, completed rinse screen. Done restored the original 160°C steam target and Ready; no new browser warnings/errors occurred. A related source review caught percentage text rounding 0.999 to 100%; shared DescalingPage now floors the percentage so it cannot announce completion early. That final display change and the test-fixture repairs are being built/verified separately.


### Favorites preferences and remaining facade gaps

On the a834d578 pair, Favorites Order showed its existing Custom list; selecting A–Z changed only the local preview, and Cancel/reopen restored Custom and the original order. The visually named rows had blank accessibility names, with a confirmed TypeError because RemoteAccessibilityManager lacked cleanForSpeech. The fix shares the existing native cleanup implementation; its paired build/runtime check is pending.

Auto-Favorites Hide unrated removed unrated groups and restored them when disabled. Open brew settings after load was temporarily enabled, observed in a second browser, and disabled there; the original browser visibly returned to Off. Max items changed from 10 to 15 and propagated to the second client, then returned to 10. Second client, restored preferences. Original grouping Bean + Profile + Equipment and both false switches were retained. No Load action was invoked, so shot/recipe/profile state was not changed by these checks. Browser AX numeric values lagged the visibly updated switches/stepper, while the stepper description changed correctly; actual screen-reader behavior remains unaccepted.

Changing only the URL hash from profiles to favorites did not navigate the already running QML stack; reload opened the requested route. This is direct evidence for the still-open full browser history/navigation task 16.4, not a passing deep-link/Back/Forward result.


### Additional shared facade and calibration findings

The pre-fix a834d578 browser reproduced the graph-chip defect on the October 22, 9:04 AM shot review: tapping Flow emitted `GraphChipRow.qml:52: Cannot assign to read-only property "showFlow"`. GraphChipRow is being routed through the same RecipeContext/PreferenceWrite path used by GraphLegend. The browser ProfileManager facade also lacked the canonical dialInLabels table used by shared profile difference blocks; it now exposes the existing ProfileDialInText map rather than duplicating labels.

Review of calibration drafts found that Flow Save used the live revision instead of the draft baseline, and Sensor confirmation could adopt an external calibration update. Shared QML now captures the Flow draft revision; Sensor calibration changes disarm the reading and close confirmation until the user reviews/re-enters it. Write uses the validated context and calibration revisions, with readiness/validity/busy guards. Host CAS preservation regressions and focused QML checks passed; final paired/live results are recorded in the calibration entry/cancel section below.


### Final shared controls, calibration regressions and maintenance closure

Wasm 1792683623413 passed (83.214 s, no warnings); native 1792683623416 passed (42.614 s, QML 349/349, the three existing linker warnings). Both package IDs equal `84b0a0222fda461b386e3df7fdfbe11b38820acd7e8dfdf75893fd454d63beb5`. Qt Creator stopped PID 47655, port 8888 was verified free, and launched original-checkout PID 59269. The sole listener reports the matching package, simulation mode, headless simulator and Ready.

Full run 1792683626075 passed 202/203, including all new calibration CAS and QML policy regressions. SubsystemLogView still failed because Qt's URI-keyed `qmake_Decenza.qrc` was shared between test modules: moving only OUTPUT_DIRECTORY did not isolate generated resources. Moving each module into a separate CMake subdirectory and adding the parent fixture-header include path fixed it. Build 1792683623420 passed with no warnings; focused 1792683626076 passed both affected suites. Full run 1792683626077 then passed **203/203** in 33.270 s with no test warnings.

On the matched running package, Favorites Order labels now contain each cleaned profile name and removal action; Cancel preserved the existing order. On the October 22, 9:04 AM shot, Flow could be hidden in review, remained hidden in comparison with the 9:03 AM shot, and was restored in comparison. No new browser warning/error appeared; the original Flow preference was restored. The shared graph chips now use the canonical preference operation rather than writing a read-only replica.

A further simulator descale completed naturally on the final percentage implementation. The last-stage capture shows 94% at 72 seconds, step 5/5, before completion; explicit Done restored the 160°C steam target and Ready. Combined with the prior native/browser progress, mid-cycle reload, explicit stop/cancel/Back checks and service error/connection-loss fixtures, task 19.1's Mac/fixture scope is complete. This does not claim physical descaling acceptance.


### Calibration entry, refusal and Back checks

On package `84b0a022`, Settings → Calibration → Pressure showed the shared 9.00 bar hold and stored +0.00 bar correction. Editing the local reading to 9.10 bar enabled Apply and opened the Write confirmation; Cancel dismissed it without invoking Write. Temperature entry showed 90.00°C and disabled Apply until input changed. Back restored the active Gran Bar / D-Flow / Q recipe and 84°C target on both paths. No shot or calibration write was started. Fixture tests cover units, bounds, invalid input, stale concurrent revisions, refusal, and save/cancel policy.

The final package `6ddbf6f` resolved the missing/clipped Flow Calibration navigation: browser and native both show Reset and Save above the fixed Back control. In the browser, a local multiplier draft changed from 1.00 to 1.01; Back discarded it, and reopening showed 1.00. Native Back worked with its multiplier already at 1.00; no edited native draft was tested in that run. Auto-calibration was restored to enabled (`true`) on both clients, and the multiplier remained 1.00. This confirms the Mac simulator/UI path and fixture-covered writes/refusals; no external sensor or physical-machine calibration was performed.

Follow-up package `af2ede12` passed Wasm build 3439 (41.786 s, zero warnings) and native build 3440 (19.709 s, five known warnings: two Core/GuiPrivate reconfigure warnings and three usual linker warnings). Native Flow Calibration was explicitly changed from 1.00 to 1.01, then Back was pressed; reopening showed 1.00 without saving. Browser Back and auto-calibration enabled (`true`) were rechecked. The browser and native sliders rendered Material pink (browser, native). The shared font-role/bootstrap and navigation initialization changes were included in the paired build; focused run 6082 passed all four navigation, settings, shared-form and theme suites in 8.920 s without warnings. These checks extend the prior calibration evidence rather than replacing its package/build/test record.

Qt Creator paired builds: Wasm 3430 passed in 45.120 s with zero warnings; native 3431 passed in 50.559 s with QML 349/349 and the usual three linker warnings. Focused run 6080 passed operation-completion overlay, machine-control service and calibration-operation policy (3/3, 2.250 s); run 6079 passed sensor refusal tests (3/3, 2.040 s). Full run 6077 passed 203/203 suites in 33.270 s.


Evidence reconciliation: task 25.1 is closed by the existing Mac shared-mode, media transition, local-dismissal and explicit host Sleep/Wake checks above; no repeated screensaver run was needed. Tasks 25.3–25.4 retain transfer/cache, resize/performance and broader graphics gates. Task 15.3 remains open because its transitive popup/action inventory is broader than the direct root-route ledger.

### Screensaver playback and media-route follow-up — package `af2ede12`

The Wasm client loaded and reloaded `/wasm/#screensaver`; its DOM video reached `readyState=4`, remained unpaused, and rendered the actual MP4 at 1280×800. Reusing the active media ticket, HEAD returned 200 with `video/mp4`, length 4,533,027 and `Accept-Ranges: bytes`. A range GET for bytes 0–15 returned 206 with `Content-Range: bytes 0-15/4533027` and a 16-byte body ([headers](evidence/decenza-screensaver-range-206.headers.txt)). A missing canonical UUID returned 404 ([headers](evidence/decenza-screensaver-missing-ticket-404.headers.txt)). A later request using a previously consumed ticket also returned 404; no 416 response was observed.

Dismissal returned only the browser to Home; native remained on Calibration. The DOM video then reported `paused=true` with `readyState=4` and retained its source, matching the known Qt issue QTBUG-151767. No settings or media uploads were changed. These are live endpoint/playback checks, not an automated ShotServer HTTP regression: the current tests cover ticket-manager expiry/consumption but there is no HTTP integration harness. Task 25.3 remains open for upload/download/selection, bounds and cache-revision coverage.


### Hot Water Home activation and automatic stop

On bc84643f, the same simulator-only host PID 64895 handled browser Home → Hot Water → selected Cup. Long press opened the existing Hot Water settings (65g, 85°C, 8mL/s); Back retained the selected Home row. Tapping the selected Cup started host Hot Water and auto-opened the browser operation view and native operation view. No preset values were edited. Browser reload was initiated while active, but startup finished after the short run; therefore this is not evidence of mid-operation rejoin. The host stopped automatically at 64.1g for the 65g target and native returned Home/Ready; the refreshed browser opened its requested Settings route and showed the same final weight. Reload did not replay the start. Explicit stop, mid-run join and the full four-operation matrix remain under 17.3.

### Final calibration reconnect and Hot Water lifecycle observations — 2026-10-22

On final package `6ddbf6f`, the browser was left on Calibration while the user-stopped native host (Qt Creator process 72203) was confirmed stopped and port 8888 free. Restarting process 72828 served the same package in Ready/headless-simulator state. Browser Reconnect returned to Settings; no new warning/error was observed after 02:09 UTC. This verifies recovery from a host restart for the visible calibration page, not calibration against physical instruments.

The same package also supplied partial Hot Water lifecycle evidence. After browser startup during an active run, accessibility inspection showed 224.1g and an available Hot Water Stop action. The run completed at 352.3g and displayed the completion overlay, then returned to the initial Settings route; Back returned Home. A separate explicit Stop at 4.1g returned the machine to Ready, with the original Cup preset restored and no preset edits. Completion overlay is the screenshot from `/tmp/decenza-hotwater-active-rejoin.png`; despite that temporary filename, it captures completion, not the active-rejoin state. The active value and Stop availability were verified separately through accessibility inspection. These checks add Hot Water evidence only; the four-operation lifecycle matrix and task 17.3 remain open.

The paired builds for this final package passed: Wasm Qt Creator build 3430 in 45.120 s with zero warnings; native build 3431 in 50.559 s with QML 349/349 and the usual three linker warnings. Focused run 6080 passed operation-completion overlay, machine-control service and calibration-operation policy (3/3, 2.250 s); run 6079 passed sensor refusal tests (3/3, 2.040 s). Full run 6077 passed 203/203 suites in 33.270 s.

For 19.4, the task-19 dependency/action inventory is recorded in [the route ledger](appshell-route-ledger.md). The wiki manual now says: “In manual Flow Calibration, **Save** keeps the multiplier change; **Back** discards it. If another client changes calibration first, reopen the page before trying again.” This does not claim physical-device acceptance.

### Shared layout editor round trip — 2026-10-22

On the final `6ddbf6f` package served by host PID 72828, the browser changed the `lowerMidBar` alignment from right to left through the existing Zone Options popup. `/api/layout` showed `left`, and the native Home Grind/Weather row rendered left-aligned. The browser then dragged Grind after Weather; the reordered preview and native Home both showed Weather before Grind (browser, native). Dragging Grind back restored the original order. The final parsed response and raw JSON are byte-identical to the saved baseline: SHA-256 `43f241f1535b2e724356a48df6b8c177414812c0f2fbf6bd23508592d87d04ea` for both. The captured baseline, left-alignment response and restored response are retained as JSON evidence. Existing palette/library and shared screensaver preview checks cover the picker/catalog presentation; the present run supplies the missing reorder/zone-option cross-client round trip. This closes 22.2 only; widget action/editor closure remains under 22.3–22.4.

### Partial Espresso and Settings search checks — 2026-10-22

On package `6ddbf6f` at host PID 72828 in headless simulator mode, the browser started the current D-Flow / Q espresso selection with the existing 18g recipe and 54g target. The machine advanced from preheat to Pouring. Changing extraction view from Chart to CupFill was reflected in native; the paired captures are browser and native. Reloading the browser at 13.5 seconds / 17g rejoined the active shot at 20.5 seconds / 27.7g, still Pouring with the same 54g target. Returning to Chart at 36.3 seconds / 51.8g showed the graph from its beginning after rejoin (browser graph). The shot completed at 37.4 seconds / 53.6g and was saved to review on both clients at October 22, 8:18:42 PM (browser review); the recorded stop reason was `reviewAtTargetWeight`. Browser Back returned to its initial Settings route while native Back returned Home independently. No new error was observed after 02:09 UTC.

These checks cover current-value hydration, an active browser rejoin, view selection and independent Back navigation. They do not close task 17.1: full graph/coach/portal/gauge dependency coverage, all phase transitions and the broader task-17 controls remain open. The native screenshot embeds a DELL U4021QW ICC profile while the browser screenshot has no embedded profile, so raw screenshot RGB values are not a valid theme-parity comparison; the saved session reports matching resolved `errorColor` (`#ff4444`) and theme inputs.

Dependency clarification (2026-10-25): `CircularGauge.qml` has no production QML
callers, and `LiveCoachingBanner` is instantiated by SteamPage, not EspressoPage.
Do not add new Espresso UI to make those inventory labels reachable or count the
unused gauge as behavior-validated. Espresso's actual conditional dependency is
`PortalReadout`, backed by the existing BelkaPortal facade and machine snapshot
fixtures; real sensor acceptance remains under 28.3. Reuse the paired live
Chart/Cup Fill and operation-transition evidence above when finishing 17.1.

Browser Settings search was also checked partially. Searching for water refill opened the Machine tab with its 5mm row highlighted. Searching for MQTT opened the lazy MQTT tab with the Enabled control highlighted; no toggle or connection was initiated. Native search parity is partial; task 20.1 remains open until every tab/result and its focus/return path are covered.

### Settings search, DE1 log view and local diagnostics download — 2026-10-22

Native search for water refill opened Machine and highlighted the 5mm Water Refill Threshold row. Keyboard focus after selecting a result was not established. Together with the browser search checks above, this is partial parity evidence; task 20.1 remains open.

For task 24.2, the browser's existing local Share Log File action downloaded `~/Downloads/debug.zip` (320,385 bytes, modified 20:25); the ZIP contained one entry and passed CRC validation. No external share was made. Clear View is local to each `SubsystemLogView`: the browser view showed no rows while native retained its separate view; the host log was not cleared. This is expected client-local behavior, not a task 20.2 discrepancy. No settings values were changed during these checks.

### Settings search external route and keyboard diagnosis — 2026-10-22

With package `6afaaa81` and sole simulator host PID 88050, native and browser Settings search for “profile” both opened the shared Profiles page through the Auto-Load Profile result. Native Escape closed the search dialog and left Settings visible after settling. Space then returned native Home because the existing window-wide Space-to-stop shortcut intercepted button activation; this was not an Escape/pop-up navigation failure. The search button now accepts Space's shortcut-override event before the native global shortcut, following Qt 6.12 `qquickitem.cpp:939–971`. The zero-inset custom background correction and keyboard fix await the next paired live check; this entry does not mark task 20.1 complete.

### Root routing and search keyboard follow-up — 2026-10-22

Package `1d7ccddc` passed paired Qt Creator builds 3453/3454 (Wasm zero warnings; native QML gate 349/349, three known linker warnings). Sole native simulator host PID 9819 served the matching package. Opening `/` rendered the shared Home and Settings UI. Native and browser Settings search both reopened with Space after Escape returned focus to the search button; the browser button background also rendered at its full intended height.

The retained HTML entry `/legacy/` loaded its shot list. Following the October 22, 8:18:42 PM shot opened `/legacy/shot/1343`; its Back, comparison, adjacent-shot and menu links retained the legacy prefix. Menu navigation loaded Recipes and the screensaver-media upload form under `/legacy/`. No records or media were changed.

Full Qt Creator run 6085 passed 200/203 suites in 35.920 seconds. Its failures exposed an incorrectly typed browser-history serialization result, missing inventory classification for the history helper, and a query-bearing recipe bookmark returning 404. The live HTTP probe ran (not skipped) and passed root, Wasm alias and legacy landing checks before that 404. Corrections are in the next integration batch; browser history acceptance and task 29 runtime closure remain open until retested. Wasm build 3457 subsequently passed with zero warnings; its native counterpart is being batched with the shared modal-navigation corrections to avoid an extra build/restart cycle.

In a read-only pass on the same running `1d7ccddc` pair, Connections, Machine, Calibration, History & Data, Themes, Layout, Screensaver, Shot Upload, AI, MQTT, Language & Access, About and Debug all opened in both clients. The browser console reported no new warning/error during that pass. Paired end-state captures show browser Debug and native Debug, including the enabled Hide GHC Simulator preference. This establishes tab reachability and search Escape/Space focus recovery, not full control/action or every search-result acceptance. No preference, credential, upload, provider test or device connection was changed.

### Root Wasm and legacy route acceptance — 2026-10-22

The matched package `19d0ceea920253b8785bb180b5662741e1fd2f4b466f6b3d7b785fefbea4b11b` passed Qt Creator Wasm build 3459 (45.325 s, no warnings) and native build 3460 (58.120 s, QML gate 349/349, three known linker warnings). Focused run 6086 passed all five selected suites in 8.710 s with no warnings or skips; it included the opt-in localhost ShotServer route probe.

That probe checked the packaged root entry and `/wasm/` alias, `/index.html` redirect with query context, legacy list/settings/upload/media-upload routes, old recipe and bean bookmarks with query parameters, `/api/shots` without redirect, a real shot detail selected from that API response, the small root-level `/shot/<id>/shot.json` attachment route, and the legacy missing-shot page. It checked that the JSON download remains an attachment and that HTML/API/file responses do not redirect to another app route. The asset fixtures also test the Wasm MIME/allowlist, matching and mismatching package identities, malformed/missing manifest failures (503), integrity failures and traversal rejection.

During an install transition, the old host returned 409 for both `/` and `/wasm/` because the installed bundle identity did not match its running application; `/legacy/` still returned 200 with the HTML shot list. This is the expected package-mismatch refusal and confirms the retained HTML entry was reachable. It is not evidence of a live 503 response for an absent bundle. Raw responses were captured at `/tmp/decenza-root-transition.{headers,body}`, `/tmp/decenza-wasm-transition.{headers,body}`, and `/tmp/decenza-legacy-transition.{headers,body}`; they are local temporary files, not committed evidence. The latest root Wasm reload had started on host PID 27447 when this entry was written; its final browser-load result is not included.

Task 29.2 is complete for the Mac route scope: old query-bearing recipe/bean paths and a real legacy shot detail resolve through the compatibility aliases, with legacy list/detail/recipe/upload pages also checked live. Task 29.5 is complete for the focused test scope: the route probe and the asset failure/security fixtures passed. Tasks 29.1 and 29.3 remain open: no live HTTP request was made with the installed bundle absent, and form/XHR uploads were not submitted. The final browser loader/resource startup at both root and `/wasm/` is recorded in the following section and closes task 29.4. Earlier menu/detail link checks and source inspection support the legacy URL rewrite, but do not replace those remaining route and form checks.

### Browser navigation checks on package 19d0ceea — 2026-10-22

The matching host at PID 27447 rendered the shared client at both `/` and `/wasm/`. Root Settings navigation passed browser Back to Home, Forward to Settings and reload at `#settings`. With Settings search open, the first browser Back closed search and retained Settings; the second returned Home. A cold `#equipment` entry loaded the shared inventory. Browser Back cancelled an unsaved equipment name edit without saving or leaving Equipment, then a second Back returned Home. A reload attempt while that draft was open did not unload it, but the automation did not expose an actual confirmation dialog, so the visible beforeunload prompt is not claimed verified.

In the Recipe wizard, browser Back opened the existing unsaved-change dialog. Escape retained the exact draft and route. A subsequent Back followed by Discard returned to Recipes; Forward reopened the original saved record, with its bean, profile and shot plan intact. Native Decenza remained on Home throughout these browser routes. No recipe or equipment changes were saved, and the separate navigation tab reported no warnings or errors during these checks.

Changing the hash to `#recipes` in an already running Home document changed the URL without navigating. Reload then opened Recipes. This is a confirmed central history issue, not accepted deep-link parity; its correction is batched with missing unload-draft coverage in shared import, conversation and shot-review components. Task 16.4 remains open.

## 2026-10-22 — Canonical QtRO services replace browser HTTP adapters

The independent-review cleanup is implemented and validated on matching package `23b935d7`: new shared Preparation and Host Session Remote Objects, bounded LanguageService catalogs, existing History/Layout/preference/control services, removed obsolete HTTP/diagnostic adapters and retired development proxy. Both Qt Creator kits pass; native QML gate is 349/349. Full run 6089 passed 203/203 with no failures, skips or warnings. Live root/alias bootstrap, independent preparation/navigation, graph preference write/restore, native/browser last-shot wallpaper, Danish/English catalog round trip, legacy HTML search and host-epoch rejection/reload passed. The running-document `#recipes` failure above is corrected; full browser-history acceptance remains open. Detailed scope, corrections, captures, restored preferences and measured reductions are in [qtro-adapter-cleanup.md](qtro-adapter-cleanup.md).

The retained diagnostics root also rendered the shared local form controls and the recorded `blooming_aborted.json` graph (253 samples), after removal of its HTTP backends. Remote/mobile/HTTPS/screen-reader/real-machine and complete workflow acceptance are not implied by this Mac result.

## 2026-10-23 — Whole-PR review corrections

All 28 confirmed independent-review findings have corrections in the shared implementation. The final combined Qt Creator build passed QML 358/358 and full run `1792763853129` passed 206/206 suites. Paired Mac checks covered draft guards, bidirectional color dragging with original theme restored, stored-shot advisor context, native backup completion, independent browser reviews, root/alias startup and retained legacy/API/MCP routes. The exact dispositions, package/build IDs, test scope and unperformed hardware/platform checks are in [review-corrections.md](review-corrections.md). This closes the review-correction batch only; the existing broader parity acceptance and later simplification work remain open.

## 2026-10-24 — Auto-Favorites acceptance (18.2)

On `4dbb686c`, Qt Creator build `1792869330214` passed in 9.736 s with the three existing linker warnings. The original checkout's sole native host (PID 24697) served matching package `4345f5b0` with simulation enabled. Browser Load for Sublunar restored 18.1 g dose, 36 g yield and grind 12 in both interfaces without starting a shot; the enabled post-load preference opened the shared brew dialog. Native navigation remained independent. Reapplying Gran Bar restored its original 18 g, 54 g and grind 10 plan.

Profile-only grouping refreshed the ranked list (D-Flow / Q: 607 shots, 63%); restoring Bean + Profile + Equipment returned the original groups. Gran Bar Info loaded its graph/statistics, and Show matched the card's 54 shots (50 initially loaded). Max items 10 and both switches Off were restored. The browser reported no warnings/errors. Root, alias, legacy HTML and MCP remained available; MCP initialization, ping, 67-tool listing, state read and session close passed on the shared listener.

This closes 18.2 together with the earlier Recipe-draft/cancel, cross-client preference and disconnect checks recorded above. Existing `tst_historyservice` cases cover correlated list/detail requests, invalid preference values and stale-revision refusal; the unchanged full Qt Creator run `1792869330539` passed 207/207 suites. No new test harness or screenshots were retained. Remote/mobile/accessibility acceptance remains separate and open.

### 2026-10-24 Safari production startup diagnosis (2.4/2.5; acceptance remains open)

Safari 27.2 (22625.2.8.11.9) on the Mac, same Qt 6.12 Release bundle with `DECENZA_WASM_FAST_LINK=OFF` and temporary DOM timestamps, no timeline recording:

| Origin | Navigation to C++ main | QML load | Navigation to QML loaded |
| --- | ---: | ---: | ---: |
| HTTP loopback (`127.0.0.1`) | 0.994 s | 1.181 s | 2.193 s |
| HTTP Mac LAN address | 0.811 s | 15.066 s | 15.916 s |

The tablet's compressed production module downloaded in 1.108 s (29,459,010 bytes); transfer alone does not explain the reported ~25 s startup. Private browsing did not help. These measurements locate the large origin-dependent difference in QML initialization, rather than download or font registration. [WebKit 324968](https://bugs.webkit.org/show_bug.cgi?id=324968#c3) confirms that plaintext HTTP can trigger intentional Enhanced Security restrictions; the report is resolved INVALID, not an open regression. The HTTPS comparison below confirms the practical remedy on this Mac. Release configuration already uses `-O3`, without SAFE_HEAP or assertion instrumentation; CI now explicitly disables the development fast-link override. No exception ABI, worker model, or memory-size change is justified by this evidence.

HTTPS follow-up: the rebuilt Mac bundle reached C++ main at 0.694 s, completed QML at 1.934 s (1.219 s QML initialization), and rendered its first frame at 1.959 s. The maintainer independently confirmed it was much faster. Native and browser Settings showed HTTPS enabled and authenticator login disabled, and the live simulator UI was connected. The final hosting decision makes TLS unconditional with no HTTPS setting; TOTP remains optional and existing authenticated configurations retain it. HTTP redirects use 307 to preserve methods and bodies. TLS certificate validation against the app's exact public certificate passed for `/`, `/wasm/`, `/legacy/`, `/api/session`, MCP initialize/initialized/ping/list (67 tools)/read-only machine state/session close. Safari's self-signed-certificate warning was handled by the maintainer; this does not establish public-CA provisioning or broader device acceptance. Combined Qt Creator build `1792869330227` passed the 359/359 QML gate; focused run `1792869330544` passed 4/4 suites, without test warnings. Temporary startup probes and symbol-map flags were removed after measurement; local fast-link defaults were restored. Trusted hostname/automatic issuance and the other original acceptance gates remain open.

The optional HTTPS preference used during the diagnostic test was removed at the maintainer's request. The final implementation deletes the plaintext serving branch rather than adding a new settings/property/migration path.

Final always-HTTPS implementation was rebased onto `87cc142b6` and passed Qt Creator build `1792869330232`; full run `1792869330545` passed 207/207 in 40.360 s with no test warnings. The requested tab favicon reuses the existing iOS app-icon PNG through a resource alias, without copying an image; it was added afterward and passed combined build `1792869330234`.

Post-favicon focused run `1792869330546` passed 2/2 without warnings. After the final Qt Creator restart, native and Safari both showed the HTTPS URL and the optional authenticator control, with no HTTPS toggle. Safari visibly rendered the app logo in its tab. The favicon endpoint returned the exact 10,738-byte existing app icon; `/`, `/wasm/`, `/legacy/`, `/api/session` and the read-only MCP probe passed again over certificate-validated HTTPS. A plaintext POST to `/mcp?redirect-check=1` returned 307 with the same path/query on HTTPS. No additional CI batch was started.

### 2026-10-24 HTTPS review corrections

Addressed all three review findings: the TLS listener checks certificate coverage before each new handshake and regenerates for changed local IPv4 addresses; redirect parsing waits for bounded complete headers and drains late body data; the MCP guide uses browser downloads and its installers embed the device certificate in a client-specific `NODE_EXTRA_CA_CERTS` configuration. The debug CLI example uses the same stdio bridge with an explicit downloaded certificate path. IPv4 SAN generation is shared by iOS and OpenSSL.

Qt Creator build `1792869330247` passed; final unchanged build `1792869330251` passed with no warnings. Focused run `1792869330552` passed the transport suite including four new regression cases (changed addresses, pre-handshake refresh, fragmented headers/late body, oversized headers). Full run `1792869330553` passed 207/207 in 33.570 s without test warnings. The native host was restarted through Qt Creator. Live fragmented POST acceptance preserved the remote hostname, path and query; the generated Bash installer's syntax and actual Node configuration payload were exercised in a temporary directory, and Node initialized MCP with certificate validation enabled using that generated trust file. The generated PowerShell certificate/config fields were inspected; Windows execution and a physical DHCP/network transition were not performed. No full CI build was started.

### 2026-10-24 Profile import acceptance (18.3 accepted)

Continued from merged main `63026699b` in the original checkout. Combined Qt Creator build `1792869330253` passed and the Mac simulator was restarted through Qt Creator. The native selector/Add flow opens the shared single-code and multi-import pages; short codes disable Import, lowercase is normalized, and returning preserves that client's code draft. The browser multi-import code form also clears on Cancel. Invalid non-alphanumeric input releases the operation; no profile is created.

The native host initially fetched an empty Visualizer list successfully, then discovered the user's newly shared **TurboTurbo S**. The browser rendered its profile preview and imported it using the supplied code. The resulting `downloaded/turboturbo_s.json` has the expected title. Reusing the code through the single-import page opened the duplicate resolver; Save As New opened its naming step, Back returned to the choices, and Cancel preserved the imported profile and released the operation. No existing profile was overwritten.

Local-folder checks used a temporary de1plus fixture: one valid Tcl profile was discovered and the invalid file ignored. The native picker and browser folder adapter both work. A changed version offers Update; duplicate/name cancellation preserves the source, and a valid Save As New created **OpenSpec Profile Import 2026-10-24** without changing the active recipe/shot plan. A browser duplicate prompt did not open on the native screen. These two imported profiles are temporary acceptance data; cleanup follows the remaining profile-action checks. No screenshots are retained. Focused Qt Creator run `1792869330557` passed ProfileManager, ProfileService, both import sources and ShotReviewActions (5/5).

### 2026-10-24 Profile action acceptance and R2 correction (18.5 accepted)

Renaming a downloaded profile wrote a shadow file through ProfileStorage instead of updating its downloaded file. The canonical rename now retains the local path it read. The existing rename regression is table-driven for user and downloaded files: the downloaded case failed on old code (`1792869330554`) and passes after the fix (`1792869330555`). The existing pure catalog helpers now receive their notifying data as arguments: compiled QML removes unused reads that previously attempted to establish those dependencies. With one verified host, the control build reproduced stale cards in both clients; final build `1792869330267` passed and native→browser and browser→native renames immediately refreshed both cards without reloading or changing filters. This changes no remote contract.

Native Copy refused a whitespace-only name and created **OpenSpec Profile Copy 2026-10-24**. Its content selected the shared Flow editor; a local 93.0→93.1°C draft was discarded through the unsaved-changes dialog, leaving the saved file at 93.00°C. The copy contains no persisted editor-type metadata. The earlier all-five-editor routing/independent-draft checks remain applicable. A built-in Flow profile offered Edit/Copy while omitting Rename/Delete. Browser Delete opened its confirmation and Cancel preserved the copy; the browser prompt stayed local while native navigation continued. With explicit action-time authorization, browser deletion of the copy refreshed native, native deletion of the imported Tcl copy refreshed the browser, and browser deletion of the Visualizer profile completed. No copies of their three filenames remain in the profile directories. The active shot plan remains Gran Bar · D-Flow / Q, 18 g in, 54 g out, grind 10 and 84/94°C.

The user reported that the Shot Review R2 button hid live values during capture. Its shipping label returned progress or `...` before considering incoming TDS. Shared QML now shows live TDS, with the count alongside it during an averaged run. The existing reading-handler test also executes the shipping label against successive readings and checks that saving still waits for completion: old code failed (`1792869330556`); the correction passed in focused run `1792869330557` (5/5). Full run `1792869330558` passed **207/207** before the version-only rebase; final focused run `1792869330560` passed ProfileService and ShotReviewActions (2/2, no test warnings). No new physical R2 capture has been performed on this Mac. The short R2 manual correction was subsequently published with task 20.4; unrelated wiki drafts remain untouched.

Rebased onto `f4fc4b646`; the only upstream delta was `versioncode.txt`. The extra native copy was closed with explicit user authorization. Final UI control was bound only after Qt Creator's process and listener matched (PID 8810); start no second copy while Qt Creator is still launching. Certificate-validated HTTPS smoke checks returned 200 for `/`, `/wasm/`, `/legacy/` and `/api/session`; MCP initialize/ping/tools-list/machine-state/session-close passed with 67 tools and simulator mode confirmed. No CI build was dispatched. The user accepted the existing native full ZIP backup, verified under 23.1–23.3, as the profile-export path; no new individual-profile Export action is required. The three approved cleanup items (**OpenSpec Profile Copy 2026-10-24**, **OpenSpec Profile Import 2026-10-24**, and newly imported **TurboTurbo S**) were deleted through the shared UI. This closes 18.5.

### 2026-10-24 Community/library acceptance (18.4 accepted)

Native Settings→Layout→Library→Community loaded the public catalog; scrolling the visible right-hand pane reached Browse All and opened the shared Community page. The Wasm `#community` route also loaded the catalog and thumbnails. Native filtering to Items did not change the browser's all-types filter or selected Calm Green card. Older entries without thumbnails display “Preview unavailable.”

Browser Add to Library displayed Downloading and imported **Calm Green** into the host library. A second download completed as “already in library” (confirmed in the Mac MCP log) and left exactly one local entry, `cb7181e7-83fd-49bc-a35d-15baa7da7db0`. The entry appears in native and Wasm My Library. Browser compact/list view and the native grid choice remained independent. The downloaded theme is retained for the subsequent theme checks; it was not applied, and nothing was published externally.

The existing `tst_librarysharing` integration fixture now exports a stored layout through the canonical service, imports that JSON again, and verifies data preservation without applying it to the active layout. A temporary empty-export mutation failed on missing data (`1792869330561`); production code was restored immediately, and focused Qt Creator run `1792869330562` passed with no warnings. The same suite covers malformed catalog responses, request-specific failures, stale layout refusal and thumbnail failures/bounds. No new test file, runtime hook or HTTP adapter was added. Public publishing and real account writes were not performed. No screenshots are retained.

The subsequent Safari console check exposed a saved-library preview defect: relative thumbnail URLs in the local-entry metadata overrode the card's local thumbnail method and resolved against QML's `qrc:` base. The facade now projects those fields through its existing thumbnail method, producing native file URLs and Wasm URLs resolved against `resourceOrigin()`. The host/API payload is unchanged. The existing fixture's new assertion failed with the old projection (`1792869330565`, relative API route instead of native file URL), then passed with the fix restored (`1792869330566`, no warnings). Combined build `1792869330278` passed in 43 seconds; final full Qt Creator run `1792869330567` passed 207/207 suites without warnings. After the original host was restarted through Qt Creator (PID 31504, package `14e1db27`), full and compact Calm Green previews rendered in both clients; native grid and browser list selection remained independent. HTTPS root/alias/legacy/session reads and MCP initialize/ping/tools-list/machine-state/session-close passed (67 tools, simulator confirmed). This closes the reopened preview check without another test target or an HTTP domain adapter.

### 2026-10-24 Settings search acceptance (20.1 accepted)

Qt Creator combined build `1792869330272` passed in 72 seconds with only the three existing linker warnings; `tst_recipesearch` run `1792869330563` passed without warnings. The original checkout's single host was restarted through Qt Creator (PID 21696), and Safari reloaded its paired entry document. Each of the 272 Mac-available generated results was selected in both clients. The two visited-name sets match exactly, all 13 tab destinations work, and the external Profiles shortcut returns to Settings without changing the selected profile. Hidden controls return to their containing card.

An inactive PORTAL panel previously had no live search target because its marker belonged to unloaded content. The marker now belongs to the Loader; the index generator treats an explicitly labelled Loader as one result. Native logs now recognize this hidden target and contain no SettingsPage warnings; Safari's warnings view also had no SettingsPage match. The existing production index gate checks that metadata, without another test file. Escape closes search and Space reopens it in both clients; browser Back closes search while retaining Settings. Automation used fresh accessibility snapshots and visible controls when the tool truncated Layout's large tree; no screenshots or automation code were retained in the repository. Reuse this result for unchanged search entries: subsequent fixes need the affected target and existing index/matcher checks, not another 544-click pass.

### 2026-10-24 Connections acceptance (20.2 accepted)

On Mac package `14e1db27`, browser Scan for Devices put both clients into Scanning, and the native log recorded host BLE/Wi-Fi/USB discovery. A Wi-Fi scale appeared in the shared discovered list; it was not selected or connected. The DE1 and primary scale stayed simulated. A manual hostname containing spaces was rejected with the visible `invalidAddress` toast; the browser retained its draft, Cancel closed it, and native opened with its own empty field and disabled Connect button. Browser Auto Test off updated native, and native restored the original on value in both clients. No saved device was forgotten and no physical machine or peripheral connection was made.

The unchanged canonical connection/protocol and service fixtures are reused from full Qt Creator run `1792869330567` (207/207, no warnings): `tst_machinecontrolservice`, `tst_settingsconnectionsservice`, `tst_simulatedscale`, `tst_difluidr1`, `tst_difluidr2`, `tst_belkaportal` and the existing Wi-Fi/USB scale suites. They cover dispatch/refusal, request correlation, disconnect/reconnect, transport/protocol failures, manual-request ownership and cancellation. Hardware pairing and instrument-specific behavior beyond those fixtures remain unverified. The Wasm control dispatches through `MachineControlService` to the host BLEManager; no browser radio-scanning implementation was added.


### 2026-10-24 Machine preferences acceptance (20.3 accepted on Mac/fixtures)

Paired Mac controls now pass browser-write/native-restore checks for USB charging; all three charging modes; steam keep-warm, recipe ownership, auto-flush and two-tap stop; map enable; temperature/water units; refill threshold and all kit overrides; extraction view and review timeout; Edit After Shot, Clear Notes, per-screen zoom and unpaired Pocket integration. Manual-city typing stayed local until commit, updated the host, and was cleared from native with browser refresh. Both map previews rendered the host location and the same populated 249-shot dataset; closing each returned independently. Original preferences, preset selections, empty manual city, and background None were restored. Android launcher and simulator-availability refusal use the existing canonical fixtures; physical battery/peripheral effects and developer-simulator workflow acceptance are not claimed.

MCP readback exposed a real theme refresh defect: writes succeeded while the controls displayed the previous value. The source published its revision before the later theme fields. Publishing revision after the completed snapshot fixes mode, glass and preset refresh through the existing facade. The existing `facadeEditsThroughRemoteObjects` test now reads mode/glass inside the notification: old code failed in `1792869330570`; corrected code passed in `1792869330571` (2.050 s, no warnings). Its isolated settings restore also now uses the actual `theme/mode` key. Combined build `1792869330286` passed; full Qt Creator run `1792869330572` passed **207/207** in 34.700 s without warnings. Both directions of mode, glass and dark/light preset selection then passed live.

The background dialog previously submitted the current revision at Apply, allowing an older dialog to overwrite another client's change. It now captures the opening revision, retains its selection on conflict, and accepts an explicit retry using the returned revision. The shared Qt Creator build `1792869330290` passed (69.490 s, existing linker warnings). With native PID 50316 and package `3907988d`, native Dot Grid and browser Grain drafts stayed independent; native Apply succeeded, browser Apply was refused with the translated conflict message and its Grain draft intact, and explicit retry succeeded. Both clients then returned to background None. This is a shared-dialog correction with unchanged remote contracts; the existing source CAS fixtures are reused. The final dialog-only change was verified by this paired live check, not another full-suite rerun.

A separate stock Qt checkbox-state defect was isolated and filed as [QTBUG-151799](https://qt-project.atlassian.net/browse/QTBUG-151799). `SharedStyledSwitch` now uses its matching Switch accessibility role in both builds, with correct cross-client accessible state; the existing shared-control test passes (`1792869330569`, no warnings). The Qt-only CheckBox reset probe remains available for upstream verification. Actual screen-reader acceptance and the older custom-numeric observation remain open; see `tools/webclient/upstream-accessibility.md`.

Certificate-validated root, Wasm alias, legacy and session reads returned 200. MCP initialize, ping, tools-list, settings/machine reads and session close passed (67 tools, simulator confirmed); settings readback confirmed the original values. No CI build or screenshot archive was created. Safari displayed a significant-memory reload notice after extended use with several Wasm tabs open; this is an observation for the existing startup/soak gate, not a passed memory test or a proven single-client leak. When presentation and write results disagree, check canonical MCP readback before treating a stale control as a failed RPC.

### 2026-10-24 Settings inventory/manual closure (20.4 accepted)

The task-20 ownership/action map is complete in `appshell-route-ledger.md`, using the paired checks and existing regression evidence above. [Wiki commit `ea33782`](https://github.com/Kulitorum/Decenza/wiki/Manual/ea33782898b3fb862cec9f99b718a99db7216b07) publishes only the short shared-browser/settings, HTTPS/MCP, retained-background-draft and live-TDS corrections (10 added/7 removed lines); the previous HTML screenshots are labelled Legacy. Unrelated wiki drafts were excluded with an isolated index. This documentation-only closure requires no new build or screenshot collection. **118/197** items are complete; integration/account/AI settings (task 21) are next.

### 2026-10-24 AI settings acceptance (21.1)

`AISettingsPage` hosts the same `SettingsAITab` as Settings. Paired native/Safari checks passed provider selection and restoration (Gemini → OpenAI/Anthropic → Gemini), browser GPT-6.1 Sol selection observed by native, native GPT-6 Luna restoration observed by the browser, model-menu Cancel, and bidirectional taste-intake preference writes/restoration. Both clients opened the existing conversation; an unsent browser draft stayed out of native, survived close/reopen, and was cleared without sending a message or clearing conversation history. Stored keys were neither read nor changed.

An invalid custom endpoint previously closed its dialog before the host refused it. The dialog now uses the existing correlated `PreferenceWrite` completion, retaining its draft on refusal and closing after confirmed success. The shared helper accepts an optional opening revision; immediate controls retain their existing current-revision behavior. The endpoint draft also freezes its provider. Paired invalid-entry/Cancel checks pass. Browser submission of the ordinary OpenAI endpoint was reflected in native and restored there. Changing the provider to Anthropic while an OpenAI draft was open produced a refusal without either endpoint changing; explicit retry changed only OpenAI. Certificate-validated MCP readback confirmed the final Gemini selection and both empty endpoint overrides. No provider request was made.

Combined Qt Creator build `1792869330292` passed (71.292 s); full run `1792869330573` passed **207/207** in 34.640 s without warnings. Existing AI provider/manager, conversation-session, preference, private-URL and request-lifecycle fixtures supply pending/error/cancellation and credential-isolation coverage; no paid live calls or token/key rotation is claimed. The shared unreferenced `AIConversationPanel` was then removed (188 lines) after the static/dynamic caller audit in `qtro-adapter-cleanup.md`. Build `1792869330295` passed (97.791 s), and registration/assets/package/inventory/resource-closure run `1792869330574` passed **6/6** in 4.190 s without warnings. The final manifests contain 360 app sources, 358 native, 357 Wasm and 355 shared; package `b87498c8`. This retirement changes no reachable UI. Global shortcut/route closure remains task 15.3. The final group restart and integration manual note remain under 21.4.

### 2026-10-24 Upload-account and network integration acceptance (21.2–21.3)

Both shared Shot Upload screens show the same linked destination states and missing-shot counts (741 Visualizer, 1253 Decent). Both recovery date pickers open and Escape preserves the original local range. Account credentials, destination/automatic-upload preferences and shot records were preserved; no bulk live upload or recovery import was started. Existing passing `tst_decentshotupload` fixtures exercise both destination adapters, missing-shot selection/batches/progress, readiness refusal, 429 retry and idle-machine scheduling. `tst_visualizershotlist` covers the recovery list envelope, date filtering, paging and terminal/error decisions; `tst_shotuploadcommandpolicy` checks recovery bounds and completion identity. The service fixtures cover pending replies, disconnect/no-replay, machine-choice ownership and requester-private account URLs. This is paired UI plus isolated provider/transport acceptance, not a claim of fresh live account linking, publishing or recovery import.

The shared MQTT screen shows the host’s disabled/disconnected state and existing TLS configuration in both clients. Browser Retain Messages off updated native, native restored on in the browser, and an out-of-range port edit left native at 1883; the browser field was restored. MQTT remained disabled. Existing passing `tst_mqttclient`/`tst_mqttservicepolicy` fixtures cover strict values, stored-password exposure/re-entry policy, broker connect/disconnect/refusal, protocol fallback, network-loss retry and discovery identity. No real broker connection, certificate/password change or Home Assistant publication was made. MCP access/confirmation and remote-mode controls render from the common catalog; access/session/private-reply coverage uses the existing MCP protocol/session/remote-access and preference fixtures. Shared MCP help opens in both clients (native Escape, browser Close); token-rotation and Tailscale-signout confirmations were opened and cancelled in the browser without applying them.

These unchanged canonical suites are reused from full run `1792869330573` (207/207); no extra test target, speculative hardening, CI run or screenshot file was added. The integration ownership map follows the same QML → facade → QtRO → canonical host path: `SettingsVisualizerTab` → `ShotUploadService`/upload preferences; `SettingsAITab` → `AISettingsService`/`ConversationSession`/AI-MCP-network preferences; `SettingsHomeAutomationTab` → `MqttService`. Final matching-package restart/read checks and the short manual correction remain 21.4. **121/197** items are complete.

### 2026-10-24 Integration batch closure (21.4 accepted)

The final native process (PID 67404) was stopped/started only through Qt Creator, with the original checkout and port owner verified. It serves package `b87498c8`; native AI settings and the browser’s dedicated `#ai` page both load the shared controls with Gemini selected. The earlier client was refused after the package/host epoch changed; its built-in Reload interface action loaded the matching package and connected. There was no pending mutation during this restart; no-replay behavior uses the existing disconnect fixtures. Previous paired provider/model, local conversation and endpoint-dialog conflict/refusal/retry results remain applicable after retiring the unreachable component.

Certificate-validated root, Wasm alias, retained legacy HTML and session routes returned 200. MCP initialize/ping/tools-list/machine-read/session-close passed (67 tools, surface 1.20.0, simulator confirmed). Final canonical readback confirmed Gemini and empty OpenAI/Anthropic endpoint overrides. The short manual update was published as [wiki commit `c584498`](https://github.com/Kulitorum/Decenza/wiki/Manual/c58449889c0df654f35821dc003ce745f0b0dc13): endpoint-save draft retention and the accepted full-ZIP profile backup path (four added lines). Unrelated wiki drafts remain untouched.

Validation used one 207-suite run for the shared preference change and the six focused resource/package checks after removal; it did not repeat the full suite for documentation or unchanged UI. Sensitive credential/token mutation, live broker/public bulk-upload effects, paid-provider responses and broader device/screen-reader checks are not claimed; their isolated fixture evidence and remaining platform gates are explicit above. **122/197** items are complete. Continue with the existing theme/widget group; batch changes and defer optional cleanup.

## Task 22.1 — shared theme controls (2026-10-25)

Package `14dd78fd5b08512a84c7572d764c6881793bf45de455ce2f1fca5b99d9504f76`, native PID 32465, built and launched through Qt Creator in the original checkout. Source comparison confirms every HTML colour role is in Qt (43/43); Resistance remains additional. The 49-control gap in `web-parity.md` is implemented with the existing shared editor plus Advanced and the existing Library/Community components. No browser-only form or HTTP adapter was added.

- Native and Safari render the same Advanced dialog, with eight font roles and 17 CRT parameters from canonical bounds/defaults. Safari Body 18→19 immediately appeared in native; fonts-only reset returned both to the original eight defaults. Native CRT enable and Scanline intensity 0.36→0.35 immediately appeared in Safari; the parameter and disabled state were restored. Original light/dark palette maps and fonts compare equal after the checks.
- Native Home's colour list updates while only the browser theme editor is open. The native-page scan remains demand-driven; the existing real-QtRO test verifies multiple views and disconnect release. Existing Qt dynamic accessible-name/ComboBox limitations remain in the upstream accessibility ledger; actual VoiceOver is not claimed.
- Theme-only Library shows existing Calm Green and only the relevant Save current theme menu action. Community/Browse All opens the same page with Themes selected. Native Dark selection remained independent of the browser Light selection. Browser Back/Forward restores Light in the final bundle. The test exposed and fixed a missing page-property mapping. It also exposed overlapping preview/Browse All requests; Browse All is now disabled during its preview and enabled after completion, verified live.
- Save-current reachability was checked without adding another persistent test theme. Existing Library service/provider fixtures cover unchanged save/apply/share/delete behavior; no new live public share or deletion is claimed. Earlier task-20.3 palette/background/image/pattern/shot-wallpaper and conflict evidence is reused for those unchanged paths.
- Qt Creator build 1792869330300 passed; full run 1792869330575 passed 207/207 suites (40.570 s, no warnings/skips). After dialog presentation, browse gating and history fixes, build 1792869330307 passed with only the three existing linker warnings. Final focused run 1792869330577 passed 8/8 suites (4.500 s, no warnings/skips): theme service, operation/navigation overlay, registration, assets/package, inventory/self-test and resource closure. New assertions extend existing suites only.
- Final HTTPS probe: `/`, `/wasm/`, `/legacy/` and `/api/session` return 200; MCP initialize, ping, 67-tool discovery, machine read and session close pass on the shared port. Simulator remains active. No CI run or source-tree screenshots were added.

Manual updated in wiki commit `5383cdcd931805af0e56891e08cfc3cdbe659a7c`: shared Advanced/font/reset paths and Library/community entry.

## Task 22.4 — shared widget editors (2026-10-25)

The shared Custom editor passed native Cancel without a write, browser direct text entry/Save, native concurrent save with the browser draft retained on conflict, and explicit Reload/retry. Full/compact previews showed the draft independently of the accepted host value. SVG Espresso and Unicode coffee icons plus a colour preset saved through the browser and round-tripped through the same native editor. Readout options changed scale value-only/icon presentation and were restored; native/browser Sleep editors showed identical existing quit/confirmation/icon preferences without changing them. The shared gesture picker saved Settings as the temporary widget's double-click destination and that gesture opened browser Settings independently of native navigation. Reassigning only that disposable widget to Quit left both browser and host usable.

Reusing the disposable widget as Flip Clock verified native size 1→0.5, browser stale-save refusal/explicit Reload, browser pointer save 0.25, and pointer plus Right-arrow save 0.55. Escape/Cancel closed the editor. Existing task-25.2 screensaver/map/shot-plan preview evidence and the unchanged catalog, dispatcher, gesture-reservation, layout CAS and real-QtRO fixtures are reused; this is not a new exhaustive traversal of every widget/action. Task 22.3 remains open.

The run exposed app-side accessibility gaps: widget selection previously enclosed Edit/Remove buttons inside a Button role, and emoji cells had no accessible actions. Shared rows now use named groups with a separate keyboard-capable selection control; icon/emoji cells have named press/keyboard actions and preserve the owner's selection binding. Both native and Safari expose the icon cells; Safari exposes grouped Edit/Remove siblings. Three screensaver sliders now have shared translated names (native Size observed live). Safari's stock Qt number-input bridge still omitted that name in the inspected tree, despite working pointer/arrow adjustment and save. Comprehensive Tab traversal and actual VoiceOver are not claimed; Qt source inspection and the existing upstream ledger inform the separate 2.9/28.3 assessment. No browser DOM workaround or new test target was added.

Qt Creator build `1792869330313` passed (68.980 s); focused run `1792869330579` passed **8/8** suites in 4.700 s without warnings/skips: custom-widget/action catalog, layout service, registration, assets/package, inventory/self-test and resource closure. Final label-only build `1792869330316` passed (69.228 s, three existing linker warnings). Final package `f0370ba1264e5e3aae4799f169b68d3d69022280a4ccb6f7be53964b358fe9b1` runs as PID 62445, stopped/launched solely through Qt Creator. The HTTPS probe passed root, Wasm alias, retained legacy HTML, session and MCP initialize/ping/67-tool discovery/machine read/session close. The temporary widget was removed; the complete parsed layout exactly equals its pre-test snapshot. No CI run or source-tree screenshot was added.

The short shared-editor save/conflict/Quit manual clarification was published in wiki commit `fd1f7044d6a875d5e7f9e28bf18eef3de54990f0`. **124/197** tasks are complete.

## Landing boundary — 2026-10-25

Freeze this batch at **124/197**; task 22.3 and the remaining OpenSpec gates stay open for follow-up after landing. Final source review found that Community's new theme mode hid variable/action filters while still submitting their previous values. Theme requests now omit those inapplicable filters, preserving their selections when returning to other types.

Final combined Qt Creator build `1792869330322` passed (129.149 s, zero errors; three existing Qt-private configuration warnings and three existing linker warnings). Full run `1792869330583` passed **207/207** in 44.440 s with no warnings/skips. Qt Creator Run performed an unchanged successful build and launched only the original checkout; PID 5910 owns port 8888 and serves package `b22d57ccb245a10e1cd187f2dd858505d2cadf9c9e23d8a2797ff6fbeb5095d4`. Certificate-validated HTTPS root, alias, legacy and session reads pass, together with MCP initialize/ping/67-tool discovery/machine read/session close. No CI build was dispatched.

The maintainer then selected a single Library entry under Settings → Layout. Removed the Themes entry/dialog, theme-only panel mode and its extra AppShell/navigation arguments; Advanced remains on Themes. This supersedes the theme-only entry-point check above. Wiki commit `3be45c10594d501a0676fb23a7b5c55630559a44` corrects the manual. Combined build `1792869330325` passed (143.547 s, three existing linker warnings); final focused run `1792869330584` passed **9/9** without warnings/skips (theme, library, navigation, registration and package/resource checks). The restarted original app is PID 10256, package `19e35c37e32f3d28e96c2a486009ac55b276e0f4d0c367c006a1356b1bb4b81e`; all four HTTPS routes and MCP checks above pass again. Reuse the 207-suite run for unchanged code; no additional full run or UI traversal was added for removing the extra entry point.

## Shared sleep/wake revision — 2026-10-25

The maintainer replaced independent browser screensaver dismissal with host-owned sleep state and reconfirmed synchronized machine-driven operation pages. The existing ScreensaverService now publishes the native manager’s active state; both QML clients wake through MachineControlService. Removed the separate native wake callback and obsolete screensaverRequested dispatch. One OperationNavigationPolicy mapping now supplies native and browser espresso, steam, hot water, flush, descaling and transport navigation, including the two maintenance phases previously absent from Wasm’s automatic routing. Ordinary browsing stays local.

Combined Qt Creator build `1792869330333` passed (252.694 s, existing Qt-private/linker warnings only). Full run `1792869330589` passed **207/207** in 46.330 s without warnings/skips. The existing media-ticket suite’s new real-QtRO case verifies joining an already-sleeping source, both state updates and detach/rejoin after a host change; its JUnit report confirms execution. The existing navigation suite checks every mapped phase. No test target was added.

The original checkout was stopped/launched only through Qt Creator; PID 27385 owns port 8888, package `70cfb212a0a6c5ca3b1a55c3f1cd8519ff038909f1038d1cedb8e0ce2b8ae7a3`, simulator confirmed. Native Sleep opened both screensavers; MCP confirmed phase Sleep. Edge’s existing localhost client was refreshed while asleep and started directly in the shared screensaver. Clicking its video surface woke native and the machine to Ready. Edge Sleep slept both; native touch woke both. Native keyboard Sleep and another native touch also round-tripped while Edge retained its own Settings route. No screensaver preference was changed.

Edge Settings left native Home unchanged. Starting simulator Steam automatically opened the same Steam page in both; reloading Edge while Steam was running rejoined it. Completion returned native to Home and Edge to Settings. Hot Water also opened both shared pages and completed. The Quick Flush was too short to capture paired active-page evidence; espresso/descaling/transport routing uses the shared mapping tests and previous workflow evidence, not a new live-operation claim. The broader operation-lifecycle matrix stays open. Original Espresso preparation selection was restored, with the same profile and both interfaces awake.

Qt 6.12 `qtmultimedia/src/plugins/multimedia/wasm/common/qwasmvideooutput.cpp:566–617` creates a hidden body-level video without playsinline. WebKit documents fullscreen playback on iPhone without that attribute (https://webkit.org/blog/6784/new-video-policies-for-ios/). A small browser-bootstrap observer requests inline playback for Qt video elements. Edge’s Elements panel confirmed **playsinline on the actual playing Qt video**, and its QML surface woke both interfaces; DevTools was then closed. Actual iOS system-player behavior still requires the existing real-device gate (25.4).

Certificate-validated HTTPS root, Wasm alias, legacy and session routes and MCP initialize/ping/67-tool discovery/machine read/session close passed again. No CI build or screenshot file was created. Task 25.1 is accepted for this revised Mac slice; **124/197** tasks are complete.

Manual update published as wiki commit `2393298f7af887535bea18114b1f4fb01973d43a` (shared sleep/wake and operation-screen behavior, with one canonical explanation in Screensavers). Unrelated wiki drafts were preserved.

## PR #2088 review follow-up — 2026-10-25

Combined GitHub Codex and independent review identified two defects. Shared advanced theme controls now send on the existing `valueCommitted` signal, keeping remote-operation busy state out of an active adjustment. Transient sleep replaces the current browser history entry instead of adding a second Back step.

Qt Creator build `1792869330341` passed; focused run `1792869330596` passed **9/9** in 6.400 s without warnings/skips (theme service, shared form controls, navigation overlay, registration and bundle/resource checks). The preceding full **207/207** run covers unchanged code. Live native and Edge numeric edits saved and fonts-only reset restored the original empty override map. Edge Home → Settings → native Sleep → browser Wake → one browser toolbar Back returned directly to Home. Both interfaces finish awake. Certificate-validated HTTPS root/alias/legacy/session and all shared-port MCP probes pass for package `61e93f3baaa90a21ea3cc93388e5c51ce30eed2cbb119a288625190a39562e0e`. No additional CI build, test target or screenshot file was added; **124/197** tasks remain complete.

## Home and Steam continuation — 2026-10-25

The original checkout's existing host (PID 45104, package `61e93f3b`) remains the sole port-8888 listener; `/api/session` confirms simulation. Native and Edge use the same IdlePage, SteamPage, HomePresetActions and inventory/service facades. Browser bag HTTP adapters are absent. The existing Qt Creator build `1792869330346` finished successfully; focused run `1792869330597` passed **7/7** in 2.660 s without warnings/skips: preparation controller/service, machine-control presets/service, Steam service/stop state/UI state. No production changes or new tests were needed for these checks.

- **14.1:** browser Home selected Gran Bar · Slayer /w Fill; native's newly opened Recipe row showed that selection first, while the already-open browser row retained its order. Native reselected Gran Bar · D-Flow / Q and the browser selection followed without moving its pills. Browser bean selection Gran Bar → Sublunar Espresso and native restoration updated shared bag/plan/grind state without starting a shot. Browser profile popup selected Blooming Espresso and then D-Flow / Q, closing on selection and updating native's target/plan. Finally reapplying Gran Bar · D-Flow / Q restored 18 g in, 54 g out, grind 10 and 84/94°C. Source inspection confirms the canonical inventory services and shared `PillFit.keepOrder` freeze/reopen policy, with host-owned recipe activation.
- **2.45:** browser Home's selected Small pitcher started the host simulator and both shared Steam pages appeared. Browser +5 changed native's timer target 65→70 s; native −5 returned the browser target to 65 s. Browser selected Large (flow 1.00); native selected Small (flow 0.80), updating both. With two-tap disabled, browser Stop ended Steam and dismissed only its own page. Temporarily enabling two-tap through the shared Machine settings let browser Stop publish Purge on both displays; native Purge returned the machine to Ready. Two-tap was restored to disabled. Home double-click Steam opened its idle preparation controls (65 s/0.80/160°C) without starting a run. Production `MainController` owns prepare/select/adjust/stop and the stop latch; shared QML tracks correlated results and displays failures. Existing service tests cover queued completion, detach/refusal and no replay; stop-state tests cover stale first-tap rejection. The page's debug-only stop preview is not host machine state.

Certificate-validated HTTPS root, alias, legacy and session returned 200; MCP initialize/ping/67-tool listing/machine read/session close passed on the same listener. These close **14.1 and 2.45: 126/197 complete**. Captured/live milk lifecycle acceptance (2.41), the remaining Home/widget actions and real-device gates stay open. No CI run, test target or screenshot file was added.

The same host then completed Home's Mug Hot Water run and returned both displays to their own Home state. Browser selected Thorough Flush, started it with a second tap and stopped it while both displayed the shared Flush page. Quick was restored and browser Tare returned the host scale to zero. Home's selected Gran Bar Recipe started Espresso on both; native target adjustments 54→59→54 g applied and the simulator completed at 37.5 s/53.6 g. Native automatically opened Shot Review for **2026-10-25 10:03:23 AM**. Cup Fill rendered the completed result; Shot Chart was restored afterward. The simulated shot remains in history.

Edge reload during that shot encountered a renewed certificate warning; the maintainer handled it. Consequently that attempt does not establish mid-shot rejoin or browser automatic review. Opening the completed `#espresso` route exposed an actual Back defect: the shared page requested a stop even when idle, which the host correctly refused. The page now dismisses locally when espresso is no longer active, retaining its correlated stop path during espresso. Build/live verification follows; task 17.3 stays open.

The correction uses the existing `OperationNavigationPolicy.machineOperationPage` rather than another phase list or remote property. Paired build `1792869330349` passed in 75.550 s, with QML 359/359, warning-free Wasm and the three existing native linker warnings. After restoring the original Qt Creator project, focused run `1792869330600` passed **7/7** in 4.330 s without warnings/skips (machine control, completion navigation, registration, inventory and bundle/resources). The original app was restarted through Qt Creator. A fresh browser `#espresso` route at Ready now returns Home through Back without a machine mutation.

The second simulator shot, **2026-10-25 10:16:49 AM**, completed at 37.4 s/53.6 g and remains in history. Browser reload during preheat rejoined the operation, and later both graphs showed Pouring at approximately 27 s. A further reload coincided with completion: its graph retained samples from time zero and the end marker, and native and browser automatically opened the same Shot Review. This establishes preheat join, graph continuity and automatic review; it does not establish reconnect during mature live pouring or complete the broader 17.3 lifecycle matrix. Original settings remain restored.

## Browser fullscreen and Home Screen — 2026-10-25

The requested shared-header control uses a small local `BrowserDisplay` boundary, not a saved host preference or remote command. It requests root-element fullscreen, then landscape through Screen Orientation where available; browser exit updates the icon and releases orientation. Unsupported browsers receive Home Screen/rotation guidance. Home Screen metadata references the existing 180×180 iOS icon, served like the favicon, and the browser container respects all four safe-area insets. No QML page, icon bitmap, test target or legacy interface was duplicated.

Final paired Qt Creator build `1792869330357` passed in 102.754 s, including QML **359/359** and warning-free Wasm; only the three existing native linker warnings remain. Focused run `1792869330601` passed **7/7** in 4.620 s without warnings/skips. Restarted original host PID 90977 serves package `ef8c592b6906593675f6a67c0781bcea6f51a851c79300126ad155e755fe869c`, with simulation confirmed. Edge's header entered real fullscreen, rendered correctly after resize, exited by its icon, and updated after browser Escape. Native has no fullscreen control and retained its own screen.

Certificate-validated HTTPS root/alias/legacy/session routes returned 200. `/apple-touch-icon.png` returned image/png, 180×180, byte-identical to the canonical iOS asset. The served entry includes Home Screen and safe-area metadata. Shared-port MCP initialize, ping, 67-tool discovery, machine read and session close pass. The short wiki entry is published as signed commit `ee7d395` (Fullscreen and Home Screen, Web Server section). No CI run or screenshot file was added. Task 16.5's implementation and Mac checks are complete: **127/198**. Actual iPhone/Android fullscreen, orientation, Home Screen installation and safe-area acceptance remain under the open real-device gates; the desktop result does not close them.

## Edge scrolling follow-up — 2026-10-25

The maintainer reported Safari scrolling normally while Edge moved approximately one pixel per gesture in Settings. Qt 6.12 delivers Wasm wheel input with `NoScrollPhase` and a nonsynthesized source (`qwasmwindow.cpp:949–970`), selecting Flickable's angle-delta/easing path (`qquickflickable.cpp:1772–1815`). Upstream proposal 686407 remains NEW (last updated 2026-06-02). An initial direct-position workaround incorrectly treated every parent Control's `wheelEnabled=false` as disabling its children; the real Page default exposed that mistake. It was replaced, not retained alongside the correction.

`BrowserDisplay` now adapts those events to Qt's existing pixel-gesture path after child controls have handled input. Qt's nonblocking `WheelHandler` supplies gesture completion; there is no custom timeout or browser-specific multiplier. The adapter respects the enclosing ScrollView's wheel setting and reuses Qt movement/snapping behavior. One regression in the existing Quick test suite uses an actual Page, checks 100 one-pixel updates, nested boundary propagation, reversal, disabled interaction, movement completion and a large first event. The temporary browser/Qt event recorders were removed.

Paired Qt Creator build `1792869330380` passed in 110.260 s (three existing native linker warnings). Run `1792869330611` passed the regression's suite in 1.950 s; run `1792869330612` passed the other seven focused suites in 4.340 s without warnings/skips. The fullscreen help was also made lazy to remove its startup implicit-height binding loop. The original host was stopped and started through Qt Creator; PID 57735 serves package `c32225b6002bbb4d3a977f4803877a5ffb46193d7c7404760f5ba8644525431e`. HTTPS root/alias/legacy/session and shared-port MCP initialize/ping/67-tool discovery/machine read/session close pass.

With DevTools closed, automated Edge input moved the AI settings content by the supplied distance. The maintainer subsequently confirmed real trackpad scrolling working in Edge and then Safari on this build, and confirmed that it still works after quitting and restarting Edge. This closes the reported Mac trackpad regression. The delayed diagnostic-prompt replies do not establish a captured physical-event trace: that page had already been replaced. Safari acceptance is the maintainer's live check; computer control was blocked on its then-current browser URL. Full local run `1792869330613` then passed **207/207** in 34.640 s without warnings/skips. The broader browser/device matrix remains open. **127/198 complete.** No CI run or screenshot file was added.

## Home service closure — 2026-10-25

Task **14.2** is complete without further implementation or tests. `HomePresetActions` submits typed requests; `NativeMachineControlServiceSource::execute` applies the canonical controller/preset operations and validates the observed selection and machine context. `validateStart` enforces Ready/GHC/simulation rules, including deferred recipe activation. Home does not sequence a remote write followed by a dependent local read. `MachineControlService` correlates queued completion and terminates pending requests without replay on disconnect. Existing `tst_machinecontrolservice` policy, correlation, typed-payload and no-replay cases and the shipping Home-action tests passed in full run `1792869330613`. Reuse the paired recipe/profile/bean selection and Steam/Hot Water/Flush/Espresso starts recorded above; broader capture/operation and widget checks remain in their existing tasks. **128/198 complete.**

The remaining Home configuration checks complete **2.28, 14.3 and 14.4**. Both targets register the same `IdlePage`, `StatusBar`, layout zones/widgets and `SharedAppNavigation` sources. The paired Home renders the saved layout and shared host selections/telemetry; existing route guards and operation navigation are retained. Native Home's double-click opens the shared Flush and Hot Water configuration pages. Browser configuration/deep-link navigation left native on its own route. Flush's editor and Add/Cancel worked in both; a temporary browser rename appeared natively and native Save restored **Quick** in the browser. Native duration **3→3.5 s** refreshed the browser, which restored **3 s** and saved on Back. Original flow remains **7 mL/s**. Rapid automated text entry had inconsistent final characters; this is not accepted as keyboard/IME or screen-reader coverage, whose existing gates remain open.

Hot Water exposed an actual legacy-data failure: the saved Mug had an empty mode and zero flow, so strict preset-edit validation refused an unrelated temperature change and Back could not save. `SettingsBrew` now supplies legacy defaults and the hardware's existing effective flow bounds from one canonical reader; single-row reads reuse that projection. Reading does not rewrite stored rows. The existing native RecipeContext integration test now covers seed rows, the reproduced empty-mode/zero-flow row, unchanged storage on read and a successful temperature-only commit. No new target or browser-side normalizer was added.

Qt Creator build **1792869330384** passed in **49.402 s** with only the existing OpenSSL/unwind linker warnings. Focused run **1792869330614** passed **3/3** in **3.060 s**, and final full run **1792869330615** passed **207/207** in **39.830 s**, without warnings/skips. Native was restarted through Qt Creator (PID 67398 at verification), serving package `8d8e67bf8fd3b6544a695b27daf2a4cd1c0ba2c836fc5e95606577a1f22bba06`. Native **85→86°C** now saved and returned Home; after pairing the refreshed browser with this host, it showed **86°C/0.5 mL/s**, restored **85°C** and returned Home successfully. Mug remains **350 g**, with the same effective **0.5 mL/s** flow and weight mode. The normal save persisted the corrected legacy fields.

The final certificate-validated probe passed root, Wasm alias, retained `/legacy/` and session reads, plus shared-port MCP initialize/ping/67-tool discovery/machine read/session close. Signed wiki commit **f35ed26** publishes the short shared-preset and first-tap/second-tap/Back instructions. Reuse the prior paired Steam/start/stop/picker and Home operation evidence above; no additional CI, screenshots or broad acceptance rerun was added. **131/198 complete.** Tasks 2.41, 17, 22.3 and the remaining browser/device/accessibility gates remain open; this accepts the shared Home workflow on Mac, not the complete operation or optional-widget matrix.

## PR #2092 fullscreen review correction — 2026-10-25

Independent review found that the entry document's `const screen` shadows the browser Screen object, silently skipping landscape lock/unlock. Both paths now use `window.screen`. The existing `tst_customwidgethtml` suite executes the shipping fullscreen JavaScript with that actual entry declaration and verifies landscape lock, exit unlock and state notifications. Qt Creator run **1792869330621** built as needed and passed **1/1** focused suite in **1.860 s**, without warnings/skips. Qt Creator was restored to Decenza-Desktop afterward. Reuse the preceding full **207/207** result for unchanged code; no full CI or broad rerun was added. Actual mobile orientation remains unverified under the existing device gates; **131/198 complete**.

## Post-merge continuation — 2026-10-25

PR #2092 landed as `55a65dfc`. Task **4.1** is satisfied by the existing 20.3/22.1/22.2 evidence: host-persisted font/CRT controls previously available only in HTML, paired native/Safari updates and restored originals, shared background previews with conflict/retry, and saved layout alignment/order round trips. The adaptation added one shared Advanced dialog and extended the existing ThemeManagementService/catalog; it required no browser-only editor or HTTP domain adapter. Developer hours were not tracked and are not estimated retrospectively. Reuse builds/runs **1792869330300 / 1792869330575 / 1792869330577** and the later entry-point-removal checks recorded above. No test or build was repeated for this reconciliation. **132/198 complete**; mobile/theme acceptance and the remaining Steam capture checks are still open.

### Paired operation lifecycle closure (17.3)

The maintainer started original-checkout **PID 13553**, package **6ab59c91**, epoch `33612dd9-527d-4ea1-8fd0-3b317f5a892d`; HTTPS session confirms simulation. Qt Creator was not used. Native and Edge Steam displayed the same 65 s/0.80 settings. Native +5 produced 70 s on both; browser reload during Steam retained that target and the running elapsed time. Host readback confirmed `milkTimeLocked=false`. Native Stop returned Ready, restored 65 s/0.80 and `milkTimeLocked=true`, and cleared transient milk/scaling state; the browser retained its independent idle Steam page. Preset duration was briefly changed 65→66→65 before the run and is restored. No nonzero milk was injected: captured/live fallback and single-apply/end-release tests in `tst_preparationcontroller`, plus real-QtRO snapshot/event tests in `tst_preparationservice`, retain their existing passing evidence. Live nonzero capture in 2.35/2.41 is still unverified.

Native then started the existing **Thorough Flush (30 s)** while Edge was on Settings. Both showed Flushing; browser reload rejoined the existing run with its accumulated scale reading, rather than restarting it. Natural completion returned native Home and Edge to Settings. Quick (3 s/7 mL/s) and scale zero were restored. Reuse the earlier explicit Flush Stop check; no preset definition was changed for this run.

This completes the four-operation simulator lifecycle matrix for **17.3**: Espresso's mature-pour reload, earlier graph samples and saved October 22, 8:18:42 PM outcome are recorded above, supplemented by both October 25 simulator completions and paired automatic Shot Review; Hot Water's active join, explicit Stop and natural completion are recorded above; Steam's Stop/Purge and the current reload/reset check cover its host-owned lifecycle; Flush now has active reload and natural completion as well as explicit Stop. Existing machine-control correlation/disconnect/no-replay, live-graph epoch/reset and completion-navigation fixtures passed the recorded full **1792869330615 (207/207)**; no production change calls for repeating them. Stop-at-weight remains host-owned, with the recorded Espresso saved result `reviewAtTargetWeight`. This accepts Mac simulator lifecycle only, not physical stop accuracy or remote/mobile resilience. **133/198 complete.**

### Brew Settings profile-only follow-up (17.2 remains open)

Paired recipe-backed drafts stayed independent: native 0→+1°C saved, stale browser confirmation was refused with explicit Reload, and the browser reloaded and restored 0°C. Loading Blooming Espresso exposed the same profile-only dialog in both clients. Native committed **18.1 g / +1°C / absolute 55 g**, and browser readback rendered all three. Browser selected 1:2 (36.2 g derived), then Cancel preserved the saved 55 g. Reapplying the original **Gran Bar · D-Flow / Q** restored **18 g / ratio 3 / 54 g / 84·94°C / grind 10** in both clients. No profile, Recipe or bean baseline was saved through its separate Update action. Existing Brew Settings CAS/invalid-value/readiness/GHC and capture-ownership fixtures are reused; live nonzero scale capture remains open. Certificate-validated root, alias, `/legacy/`, session and shared-port MCP initialize/ping/67-tool discovery/machine read/session close passed on this host before the checks. No build, CI run, new test or screenshot file was added.

### Reconcile earlier feature obligations with accepted slices

Seven earlier feature tasks are already satisfied by their later accepted implementation/action groups. This reconciliation adds no code, test, build or repeated UI traversal:

| Earlier tasks | Accepted evidence reused |
|---|---|
| 5.1–5.2 Recipe list/actions | Task 12's paired CRUD, activation, promotion, reassignment and conflict checks; task 26.2's child-workflow/shipping-handler and canonical storage/QtRO coverage in `shared-component-resource-audit.md`. Conditional public recommendations use isolated fixtures where the observed catalog supplied no action. |
| 5.3 Bean inventory | Tasks 13/26.2 inventory, shared sorting/search, open/finished/Tea and active-state checks. Task 5.4 stays open for contextual deep-link closure. |
| 5.5 Equipment | Tasks 11/26.2 shared fields, activation, archive/restore, deletion/refusal rules and cross-client updates. Canonical isolated records supply destructive persistence coverage; original records were preserved. |
| 7.1–7.2 Settings/integrations | Tasks 20/21's paired control/status/draft checks and existing provider/account/MQTT/session fixtures. These tasks permit fixture evidence; no live paid request, secret rotation or fresh account link is implied. |
| 8.1 Files/media/restore | Tasks 23/25.3, `data-update-validation.md` and `media-validation.md`: actual pickers and test ZIP restores, large-download content/hash, media selection/cancellation and canonical streaming/error fixtures. |

The relevant recorded full-suite and focused runs remain valid for unchanged production code. Device checks (5.6/7.3/28.3), exhaustive widget/action closure, History metadata/external actions, deep-link context, library timeout acceptance and release-device acceptance are not closed by this reconciliation. **140/198 complete.** Final MCP readback confirms original dose **18**, target **54**, grind **10**, Quick Flush **3 s/7 mL/s**, and Steam **65 s/0.8 mL/s**.

Task **25.4** is now complete for its stated Mac scope: the remaining CRT/photo/wake check and restored preferences are recorded in `media-validation.md`, reusing the earlier all-mode resize, video, input and Debug resource observations and published manual guidance. No new production code, test, build, CI run or screenshot file was added. Real-device graphics/media and release-performance gates remain open. **141/198 complete.**

### Nonzero preparation capture — 2026-10-25

Original-checkout build **1792869330423** passed in 138.3 s, serving package
**901e3684** from PID82388. Qt Creator attached to that simulator process;
its existing `SimulatedScale::setSimulatedWeight` supplied the loads. No simulator
feature or test-only endpoint was added.

With the 25 g Small pitcher and 0.27 s/g calibration, 125 g raw captured 100 g
of milk and set 27 s in native and Edge. A 225 g raw load captured 200 g and set
54 s. Capture sequence advanced exactly once per load (0 → 1 → 2). Idle Duration
edits change the preset, so the Small preset was restored to 65 s before starting.
Removing the pitcher and starting through the existing GHC simulator used the
captured 200 g fallback: 54 s. Native's in-run +5 changed the target to 59 s and
unlocked manual timing. Edge reconnected after debugger pausing expired its
session; the target remained 59 s and capture sequence stayed 2.

Natural completion reset session milk and last-live milk to zero, restored
65 s/0.8 mL/s, relocked timing and cleared the scaled flag. Last-capture outcome
fields correctly retain their previous result. The original learned milk/time
pair was restored through the existing settings setters; dose 18 g, yield 54 g,
pitcher tare 25 g, temperature 160°C and calibration were preserved. Prior
explicit-stop observations and the existing captured/live fallback, manual lock,
single-start and phase-exit tests supply the other lifecycle cases. Full run
**1792869330631** passed 207/207 without warnings/skips. Task 2.41 is closed;
with task 26.3 this makes **143/198 complete**.

The next check reproduced a separate Home issue: selecting Recipes after Steam
left preparation in steam mode, so an 18.5 g load did not arm bean capture.
The shared standard and compact Recipes pickers now select espresso through the
existing preparation service; the existing compact-picker regression gains one Recipes row. Paired
live acceptance remains pending before closing 2.35/17.2.

### Maintainer iPhone fullscreen check — 2026-10-25

On iPhone Safari over a Tailscale URL, the maintainer saw the expected unsupported
fullscreen guidance and confirmed that **Open as Web App works**. This verifies
the Home Screen launch route on an actual iPhone. Exact iOS/Safari versions,
orientation-lock behavior, Android fullscreen and the remaining mobile workflow
checks are not established by this report and remain under 28.3. No screenshot
was retained in the repository.

### iPhone rotation correction and landing slice — 2026-10-25

Safari remote inspection identifies the device as iPhone 17 / iOS 27.2. Its
Home Screen web app, served by the Android host (package `f608beb2`), reproduced
landscape clipping at browser zoom **1.0**: the safe-area container was
**832 × 420 CSS pixels**, but Qt's normal window/canvas remained **832 × 874**.
Invoking Qt's existing maximize action resized the client to fit; the maintainer
then reported correct landscape after repeated rotations. The temporary framed
window measured **824 × 390** inside that container. The browser shell now starts
maximized and frameless, without minimum dimensions that exceed a phone viewport.
The shared pages and legacy HTML are unchanged by this window correction.

The maintainer also reported an intermittent startup `TypeError` in Emscripten's
method bridge, and visible accessibility elements after background/resume. These
are unresolved device observations, not claimed fixed by maximizing the window.
A temporary all-exceptions breakpoint caught normal `EmscriptenSjLj` control flow
during resize; it was disabled and execution resumed. Do not treat its interrupted
half-painted frame as another confirmed production defect. Remaining real-device
rotation/resume acceptance stays under 3.6/28.3.

The existing Upload & Install APK flow moved from About to Debug, including its
picker, confirmation, transfer, correlation and disconnect handling. Android-host
capability gating remains unchanged; Mac testing does not establish an actual
Android installation. Settings search now points to Debug. No installer protocol
or additional browser adapter was introduced.

Qt Creator paired build **1792869330432** passed in **100.7 s**, and full run
**1792869330635** passed **207/207** in **38.75 s**, without test warnings/skips.
The original-checkout simulator was restarted through Qt Creator (PID19483,
package `ac4271c7`). Its standard Home tile changed Steam back to espresso and
restored the espresso plan when Recipes was opened. `/`, `/wasm/`, `/legacy/` and
the session endpoint returned 200; MCP initialize, ping, tool listing (67 tools),
machine read and session close passed. Tasks 2.35/17.2 remain open for their full
paired nonzero-bean/draft scope. No additional CI or screenshot archive was made.

The matching client also loaded through the Mac's trusted Tailscale hostname.
Its DOM reported `frameless maximized`, with equal container/window dimensions at
**985 × 781** and **1540 × 481** after changing the viewport. Standard Recipes
selection restored espresso in both native and browser. Both shared Debug tabs
loaded, with the Android-only installer hidden on Mac; both clients returned Home.
The existing shared-switch implicit-height warning was observed in both the
released iPhone bundle and this Mac client and remains a follow-up observation.

Static Wasm/JavaScript responses still use `Cache-Control: no-store`. Task 3.6
now explicitly tracks package-versioned HTTP caching, warm-download reuse,
upgrade invalidation and fresh entry/session checks. A terminated Qt runtime
still initializes on cold launch. Caching is not implemented by this slice.

### Package-versioned caching — 2026-10-25

The fresh bootstrap now stamps `/wasm/<package-id>/` as its asset base. Only
allowlisted static code at that versioned path uses private, one-year immutable
caching; entry documents, unversioned URLs and errors remain `no-store`.
Existing integrity and session compatibility checks remain in place. The asset
fixture verifies a package change rejects the old URL and selects the new base,
including the gzip path. No service worker or runtime persistence was added.

Qt Creator paired build **1792869330435** passed in **44.9 s**. The three focused
asset/package/session suites passed, then full run **1792869330637** passed
**207/207** in **36.55 s**, with no warnings or skips. On the original-checkout
host, package `ee7b11f3`, Edge 156's normal profile over trusted Tailscale HTTPS
returned all four code assets from cache with **zero transferred bytes**, while
the fresh entry transferred **4,110 bytes**. The Wasm resource's cache read took
**176 ms**; this is resource delivery, not complete Qt startup time. A cold
trusted-origin sample transferred **29,710,047 bytes** across those four assets.
The self-signed LAN origin did not cache in Edge, consistent with
[Chromium's certificate-error policy](https://github.com/MicrosoftEdge/WebView2Feedback/issues/2634).
Root/alias/legacy/session returned 200, and shared-port MCP initialization,
67-tool discovery, machine read and session close passed. Task 3.6 remains open
for its phone/session acceptance; **143/198 tasks remain complete**.

### Unresolved iPhone landscape follow-up — 2026-10-25

The maintainer reports the new maximized-window build **does not fix** landscape
scaling: repeated physical portrait/landscape rotations produce the correct
layout only about **10%** of the time. Keep this under existing tasks **3.6/28.3**;
phone inspection is held at the maintainer's request. No device pass is claimed.

Relevant upstream reports describe
[incorrect resize-event dimensions](https://bugs.webkit.org/show_bug.cgi?id=267545),
[standalone viewport corruption on resume](https://bugs.webkit.org/show_bug.cgi?id=262207)
and [safe-area height errors](https://bugs.webkit.org/show_bug.cgi?id=254868).
These are leads, not a confirmed diagnosis. Qt 6.12 already listens to window,
visual-viewport and element resize events (`qwasmintegration.cpp:137–154`,
`qwasmscreen.cpp:282–320`). The next phone check must compare browser viewport,
container and Qt window/canvas dimensions in the failing state. If Qt alone is
stale, try its documented `qtResizeContainerElement()` after layout/resume;
if browser dimensions are wrong, investigate the viewport separately. Do not
add timer-based retries or disable user zoom to mask the failure.

### Brew Settings nonzero capture acceptance — 2026-10-25

Qt Creator attached to the original simulator PID75508, package `ee7b11f3`, using
Application Output's PID-specific Attach control. Its existing simulated-scale
slot supplied the loads; no test endpoint or production code was added. Native
Weigh saved a temporary **10 g** cup. Both open shared Brew Settings dialogs then
updated from 18 g to **18.5 g** for a **28.5 g** load, including the same **55.5 g**
derived yield. Repeating the load left the capture sequence at **1**.

After cancelling the browser dialog and navigating to a blank page, the next
**30 g** load captured **20 g** in the still-open native dialog, advancing the
sequence to **2**. Rejoining History left that native dialog open; a subsequent
fresh Home entry displayed **20 g / 60 g at 1:3**, without replaying a capture.
The independent bidirectional workflow-selection/draft matrix remains 2.35;
these observations do not close it.

Cup tare **0 g**, dose **18 g**, target **54 g**, ratio **3** and scale **0 g** were
restored and confirmed through native UI/MCP before ending the debugger session.
No Recipe/profile was saved or test shot created. Reuse the earlier paired
recipe/profile commits, conflict/Cancel checks, canonical profile-manager,
machine-control and preparation fixtures, and full **207/207** run recorded above.
This closes the missing live part of **17.2**: **144/198 complete**. No new test,
screenshot archive or CI build was added for this acceptance check.

Qt Creator then stopped the attached process and restarted the original macOS
run configuration as the sole listener, **PID11487**. Its required incremental
build **1792869330439** completed in **475 ms**. The new host has zero capture
sequences/outcomes, the restored preferences above, and the same matching
package. Root/alias/legacy/session and shared-port MCP initialization, discovery,
machine read and session close passed after restart. The old browser session
disabled its controls with the stale-session notice.
