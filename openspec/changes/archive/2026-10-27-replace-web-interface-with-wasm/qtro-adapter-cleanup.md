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
- Applied Last Shot (Advanced) temporarily. The same latest-shot curves rendered as the native and browser wallpaper through HistoryService. Restored background **none**, retaining the original advanced flag.
- Browser language selection changed native and browser text to Danish after complete catalog delivery. Restored **English**.
- `/legacy/` still served the 1,307-shot HTML history; typing Sublunar returned **72 shots**. The full-suite route probe also exercised API/file and bookmark/legacy route contracts.
- Stopping host PID 79747 through Qt Creator immediately removed usable browser state. After port 8888 was free, Qt Creator launched one new process, PID 81175, with a fresh epoch. The old tab's explicit Reconnect was refused and offered **Reload interface**; using it restored Settings against the new epoch. Original background/graph preferences remained restored. The short wiki manual now instructs reloading after native restart.

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


## Deeper simplification inventory — 2026-10-23

Task 32.1 was independently reviewed against merged main `732e8ec6` by separate
C++/service and QML reviewers, then cross-reviewed during the follow-up. The
baseline has 285 files / 36,143 physical lines under `src/remote` (284 code files /
36,134 lines after its LICENSE): 42 facade pairs / 12,452 lines, 42 native source
pairs / 18,991 lines, 42 `.rep` contracts / 824 lines, 41 registration headers /
977 lines, and 33 other code files / 2,890 lines. `src/webclient` has 34 files /
3,583 lines including CMake. The app QML baseline is 342 files / 112,006 physical
lines. These are lexical inventories, not an assertion that every line is an
avoidable adapter or every behavior is proven.

Ranked concrete service opportunities:

1. Seven ordinary facade replacement prefixes repeated the existing
   `RemoteServiceReplica::replace` lifecycle. Additional mutation facades have
   the same retirement rule while retaining domain-specific progress/graph data.
2. Eight native sources repeated one exact-key validator across 70 call sites.
   Their closed key sets and field validation agree; `ServiceValue::hasExactKeys`
   can own that predicate without merging dissimilar string/value policies.
3. Four production Steam consumers still read a browser-only mirror in
   `RemoteSettingsBrew`, produced via HostSession. The canonical `SteamService`
   already supplies those facts; shared `SteamCalculations` handles captured/last
   milk calculations. Preserve the separate MachineUiSnapshot/SteamEditRequest
   contract and the existing `/api/session` projection.
4. Other manual pending trackers require more judgment: Calibration revision and
   busy state, Preferences domain/name metadata, Connections' native callbacks,
   and DebugConversion's correlated operation map are not identical policies.

Necessary boundaries include SystemLog's lazy acquisition, BeanSearch's explicit
cancellation/read-only resume, Conversation's client-bound sessions, ShotReview's
view leases, ShotUpload's private receipts, and Steam's graph generation/health.
They do not excuse dispatching through a retired replica. Wire declarations and
compile-time registration remain necessary, as do browser lifecycle, OS/file,
accessibility, and hardware boundaries.

The QML review ranked seven Home pill paging blocks (214 affected lines),
advanced-editor frame mutations (74), three editor load/save lifecycles (301),
Flush/HotWater continuation logic (138), and equipment display fallbacks (seven
sites / 28 lines) for later consolidation. These are affected lines, not promised
savings. Profile defaults, independent drafts/type conversion, overwrite and Back
rules, and HotWater's select-on-add behavior differ and must be preserved.
`AIConversationPanel` was retired during task 21 acceptance. A whole-repository
reference check found no production caller. The persisted-widget path was also
checked: `LayoutItemDelegate.qml` maps its closed type catalog to fixed resource
URLs, and `CustomItem.qml` renders configured text/actions rather than loading
QML types. No QML uses `Qt.createComponent` or `Qt.createQmlObject`. Thus saved
custom widgets cannot instantiate this panel. The live conversation overlay in
`SettingsAITab.qml` remains shared and was checked in both clients.

### Evidence-backed faults found while applying the inventory

A live paired-client check found the browser accessibility tree retaining an
espresso button after Home visually changed to a Steam summary. Operation mode
now derives from shared preparation state rather than an imperative publisher.
The shared ShotPlanItem recreates its semantic child when its role changes,
working around Qt's stale Wasm DOM role/label. Both transition directions and
activation of the restored brew button passed live on package `dfa1debe`.
The generic reproduction and Qt source analysis were posted on
[QTBUG-133597](https://qt-project.atlassian.net/browse/QTBUG-133597?focusedCommentId=2663271).
This is accessibility-tree evidence, not a VoiceOver/TalkBack acceptance result.
Native retained an unsaved D-Flow dose edit while browser workflow selection
changed, and cancelling the native draft preserved its original dose.

The subsequent simulator run exposed a separate duration defect: Home advertised
60 seconds from historical milk while the host correctly ran its current
65-second timeout. PreparationController now projects the duration using the
same calculation as its actual start: fresh live milk, captured milk, then cached
live milk, with manual/unscaled time preserved. Historical milk can describe a
plan but cannot substitute for a current load when calculating its duration.

The service review found a shared connection-retirement bug: pending-failure
notifications ran while the old replica still appeared ready. A notification
handler could admit another write against it. Replacement now withdraws public
readiness before those callbacks and preserves a nested replacement. The follow-up
also guards admissions against connection replacement and owner deletion during
synchronous notifications; queued completions must retain their originating
replica when callers may reuse IDs. Existing domain suites exercise these cases
with real QtRO fixtures, including nested replacement, owner deletion and queued
old replies. Three manual attachment boundaries remain: Conversation's
client-owned sessions, ShotReview's view/measurement leases, and SystemLog's lazy
subscription. Ordinary attachment now uses the existing helper in 39/42 facades.

### Follow-up validation — package 47aab851

Combined Qt Creator build **1792763852501** passed native Debug and embedded Wasm
Release in **325.826 s**, with QML **358/358** and the three previously observed
local linker warnings (OpenSSL deployment target and debug unwind size). Adding
the projected Steam property regenerated the QtRO/QML metadata and both targets'
QML caches; this timing is not an unchanged incremental-build measurement.

The first full run **1792763853137** passed 205/206. Three new controller rows
checked the phase getter before its queued start handler ran. The test now waits
for `shotStarted`, which follows phase notification, and retains the exact timeout
and commit-count assertions. Focused run **1792763853138** passed all three suites;
full run **1792763853139** passed **206/206** in **32.340 s**, with no failures,
skips or test warnings. Earlier full runs 3135/3136 also passed before the duration
projection was added. No production start semantics changed to satisfy the test.

Qt Creator stopped the previous process; its PID and the port-8888 listener were
absent before Run started the original checkout's rebuilt app. The live session
confirmed Simulation Mode and package
**47aab85166ac0dff0f5269c75a38489e315c1a49b26000ad116e0f6af73f48c8**.
Native and browser Home showed **65 s**, matching the host. A browser timing edit
propagated **66 s** to native Home, then was restored to **65 s**.

A simulator Steam run survived browser reload. The rejoined browser showed the
earlier pressure/flow/temperature samples and an active 65-second run; the native
client remained Steaming. Stopping natively updated the browser while leaving it
on its separate Steam preparation route. A brief second run verified the shared
Chart-to-Timer change and browser stop. The original Timer preference, Small
pitcher, 65 s, flow 80, temperature 160 and rate 0.27 were restored.

Root `/`, `/wasm/`, `/legacy/` and `/api/session` returned 200. An independent MCP
session on the same listener passed initialize, initialized, ping, tools/list
(67 tools, surface 1.20.0), read-only machine state and session close. These Mac
observations do not establish physical-scale capture, screen-reader, remote,
mobile, HTTPS or real-machine acceptance; their existing tasks remain open.

### Final shared-control checks — package 0ca07210

The compact Espresso/Steam/Hot Water/Flush pickers previously opened locally
without selecting host preparation mode. All four now call the existing
HomePresetActions helper before opening. Closing a picker stays local; same-mode
Steam selection still reaches the canonical capture-reset behavior. Admission
does not start an operation, and client-correlated refusals use existing shared
failure feedback. The shipping-function harness exercises all four modes,
unavailable/refused admission, close and same-mode selection.

Live validation also found stale numeric accessibility values: after restoring
Hot Water to 65 g, its browser name/paint showed 65 but the number input still
exposed 500. Custom Quick Item roles are assigned after Qt's constructor-time
value-signal hookup. The existing AccessibleValueChange helper now emits numeric
notifications for ValueInput and its popup; both nodes route accessibility steps
through the existing request/commit handlers instead of breaking the consumer's
value binding. The popup exposes the same authoritative value/range and its gear
step. Source citations are beside the workaround, and the generic finding is
published on [QTBUG-133597](https://qt-project.atlassian.net/browse/QTBUG-133597?focusedCommentId=2663272).
Stock Qt remains unchanged.

The real-helper regression lives in the existing shared-form-control executable,
with its C++ result combined with the existing QuickTest result. It covers initial
and host-echo events, both slider actions, popup range/gear, bounds, binding
preservation, inactive accessibility and reactivation. No extra test executable,
duplicated helper compilation or new production file was needed.

- Compact-control build **1792763852506** passed in **103.521 s**; full run
  **1792763853140** passed **206/206** in **33.840 s**.
- Final combined build **1792763852508** passed in **313.432 s**, QML **358/358**,
  with the same three local linker warnings. The helper's new QML method
  regenerated both targets' metadata/QML caches. Full run **1792763853141** passed
  **206/206** in **32.990 s**, with no failures, skips or test warnings.
- Qt Creator stopped PID 98110; process and listener checks confirmed it was gone
  before starting the final package. Its unchanged Run build **1792763852510**
  took **0.333 s**. The session reported package
  **0ca072108fec713c47fed9615cf24508b964108ed90e8a6e5c55c9911fcc1156**.
- The final browser initially exposed the actual **65 g / 85 C / 80** numeric
  values. Browser Up/Down committed **65→75→65 g** to native controls; native
  edits also updated the browser's inline and open-popup numeric values. Escape
  dismissed the popup. The 65 g target was restored.
- Native Steam selection and compact Profiles selection preserved the browser's
  open Hot Water value dialog. Profiles restored host **espresso** mode. Browser
  compact Flush and Profiles selection preserved native Connections Settings;
  the host remained **Ready**, with **isFlowing=false**. This validates the
  compact selection correction and independent local navigation/dialogs, not
  the still-unperformed physical-scale capture part of task 2.35.
- Repeated root, `/wasm/`, `/legacy/`, API and MCP probes passed on the final
  package. MCP still advertised 67 tools at surface 1.20.0 and returned machine
  state successfully. No new CI run or platform claim is attached to this batch.

The remaining operation checks were performed on package `47aab851` before the
numeric/compact-control corrections. A temporarily enlarged Hot Water target
proved active browser rejoin and subsequent automatic target stop; its original
**65 g, 85 C, 8 mL/s** preset was restored. A temporary **45 s** Quick Flush
proved active rejoin. A second run's browser Stop returned the host to Ready
well before that timer expired, with native logs confirming user exit and no
completion overlay. Its original **3 s / 7 mL/s** preset was restored. These fill specific
task-17.3 gaps; they do not close its complete espresso/stop-at-weight/saved-outcome
and independent-route acceptance.

Independent source reviews found no remaining actionable defect in the saved
Steam-duration and compact/numeric-control changes. Earlier service cross-reviews
were followed by the full suite. An extra in-process warmup replica in new test
fixtures was removed after reading Qt's multiple-acquire signal hookup; no
shipping native rebind caller was found, so this is not recorded as a confirmed
application failure or a shipped Qt modification.

Against baseline `732e8ec6f6038cf96634cf747fcece6547b3fb2c`, this batch changes
**75 production files: +1,117 / -828 lines (net +289)** and **32 test/build files:
+2,968 / -251 (net +2,717)**. These are `git diff --numstat` physical-line counts,
excluding documentation and captures. The cleanup removed duplicate browser
Steam projections, eight validator copies and matching attachment paths; the
confirmed lifetime/notification/action fixes account for additional production
code. This is not claimed as a net line reduction. The larger presentation
opportunities above remain recorded for later work, rather than being counted as
completed or used to justify another architecture change.
