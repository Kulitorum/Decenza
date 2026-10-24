# Diagnostics and support — Mac validation

## Scope

Tasks 24.2–24.4 use the existing application QML and host services. This batch
adds no replacement browser page, HTTP domain adapter, test target, or external
provider call. Task 23's completed file acceptance is recorded separately in
[data-update-validation.md](data-update-validation.md).

## Confirmed defects and corrections

- Live multiline diagnostics shared one sequence ID, so `SubsystemLogView`
  discarded every physical line after the first. The logger now reserves one ID
  per line while retaining one batched file append; concurrent writers enqueue
  deliveries in persisted order. Source and shipping-view fixtures cover snapshot
  fencing, every detail line, marker/level filters, display/pending bounds and
  disconnect/refill.
- A lazy native log replica could be reacquired before its predecessor's deferred
  destruction. Stock Qt 6.12 reuses the in-process implementation without wiring
  the new replica's source signals (`qremoteobjectnode.cpp:631–636,1528–1536`).
  The facade now waits for retirement before acquiring from the current node.
  A real in-process fixture covers snapshot/live delivery, rapid visibility churn,
  replacement nodes and destruction. Native's empty view was observed on package
  `7f92b4fd` while its browser showed the same host's startup records; the live
  correction was confirmed after restart in both clients below.
- Overlong crash-report notes could enter an error state whose Retry reused the
  same hidden, invalid draft. Shared validation now keeps notes editable, displays
  and announces the host's 4,000-character/control-character restrictions, and
  preserves text. The shipping dialog fixture covers correction, exact-boundary
  admission, unrelated/stale replies, failure, retry and success without publishing.
- Settings Continue Chat discarded text before the host accepted its request.
  It now correlates replies, preserves a refused draft unless replaced by newer
  text/IME composition, rejects duplicate pending sends and exposes unsaved state
  through the retained Settings tab. Existing shipping-handler tests exercise it.
- Loading the latest conversation retained the prior shot's summary and per-turn
  ID. The host now refuses an overlapping context load, then clears the old shot
  context before switching threads. Actual-source fixtures use two stored threads
  and a held shot read; no AI provider is called.
- Closing shared crash/profile/communication dialogs did not resume native's
  advisory queue. A shared closure signal now schedules reconciliation, then
  resumes only after the last blocking modal closes. A shipping queue-function
  regression verifies that queued prompts remain held and resume in order.
- Live keyboard checks exposed two related gaps: Continue Chat's Tab traversal
  reached the underlying native API-key field/browser Back control, and the shot
  conversation reached Older Shot after Send. Shot Summary also left keyboard
  focus underneath its modal. Qt 6.12's popup defaults `focus` to false
  (`qquickpopup.cpp:2448–2456`); the common dialog now requests focus when modal,
  and both conversation panels use that existing dialog boundary.
- Settings' unsaved-message projection could evaluate before its Repeater
  created a Loader; leaving Settings then discarded the draft. The page now
  follows the actual typed Loader item and uses the shared Cancel/Discard dialog,
  with Discard disabled during an admitted send. Lifecycle, navigation and actual
  shared-dialog keyboard regressions pass in the final focused/full runs below.

## Build and test status

Combined original-workspace build **1792763852549** passed in 146.096 seconds,
including the **359/359** QML gate. Configure emitted the expected pinned-Qt
private-module notices; linking retained the two local OpenSSL deployment-target
warnings and large unwind-table warning.

Qt Creator's active project changed to `Decenza-Desktop` during that build.
Focused run **1792763853159** used the other checkout and is excluded. The user
then explicitly reserved Qt Creator for these checks; the original checkout was
selected and confirmed before focused run **1792763853160**. Eight suites passed;
two new fixtures failed because their setup was incomplete: in-process QtRO hosts
needed listeners, and shot projections needed nonzero fixture IDs. Those setup
corrections preserve the original behavioral assertions and warning failures.

The same run measured the synchronous filtered snapshot on a near-2 MiB log
with 50 concurrent persisted messages (9 samples each): 400-line median/worst
**19.679/66.922 ms**, 1,000-line **37.467/128.072 ms**. The latter exposes a real
host-thread hitch. The canonical snapshot path was moved off-thread, with
only raw-byte/sequence capture under the retained file lock and one shared parser.
The completed correction and rerun are recorded below.

Combined build **1792763852552** passed after the asynchronous snapshot
correction. Focused run **1792763853161** passed all ten affected suites. Full run
**1792763853162** passed 205 suites, with only the inventory ledger's missing two
new test-only mocks failing; after classifying those outside the app denominator,
full run **1792763853163** passed **206/206** in 34.470 seconds, with no skips or
test warnings.

Near-limit asynchronous timing (9 samples each; 50 concurrent persisted messages):

| Limit | Main admission median/worst | Completion median/worst | Concurrent append median/worst |
|---|---|---|---|
| 400 lines | 0.034 / 0.056 ms | 12.714 / 14.229 ms | 0.048 / 0.504 ms |
| 1,000 lines | 0.029 / 0.034 ms | 14.388 / 18.378 ms | 0.044 / 0.385 ms |

These are isolated Mac Debug measurements, not real-machine/remote-device budgets.
Eight physical snapshot jobs remain admitted until their callbacks drain. The
shared parser, byte/fence capture, live merge, rejection/retry and destruction
fixtures all pass.

## Live observations

Package **d78b5c629d1797bacc8f316641e05a64da2b381bff9d23fe37deed0b80921b72**,
PID 32210, epoch `517949de-8ccc-4d7c-9de1-e73764f4c7f3`, restored native's previously
empty connection log. Native and browser showed the same simulator startup
records. Native Clear emptied only its own view; the browser stayed populated.
Debug→Connections refilled the native view successfully.
Root, Wasm alias, legacy HTML, session and a fresh read-only MCP session passed.

A browser-only unsent draft survived closing and reopening Continue Chat. The
acceptance text was then removed without sending, clearing history or calling a
provider. A live invalid-action MCP request showed the correlated browser prompt;
the maintainer explicitly clicked Allow and the registry refused the unknown
action, so nothing ran. This is approval/refusal evidence, not Deny/timeout evidence.
The live Deny/timeout probe later stopped before dispatch because confirmation
was Off; temporary enable/restore permission has been requested.

The maintainer then reported Debug no longer scrolled. Adding the diagnostic card
exposed the fixed RowLayout's minimum-height overflow. The shared page now has a
bounded vertical Flickable, scrollbar and existing scroll-down indicator; the
long converter hint wraps inside its column. Build 1792763852557 and focused run
1792763853164 passed before the final hint-wrap adjustment. Native scrolling moved
the scrollbar to its end and revealed the previously clipped converter controls.
Final combined build **1792763852560** passed in 121.713 seconds with no errors
and the same three local linker warnings. Full run **1792763853165** passed
**206/206** in 36.250 seconds, with no skips or test warnings.

Current package **719974554ebcc4386b01b08fd05c9aebc425eb99d49ec2c058e789d1b2398a10**,
native PID 92394, epoch `6c42da20-820f-4c7c-885f-17e112f79208`, passed the final
paired layout check. Actual native wheel scrolling reached 100%; Edge reached
96.97% and exposed the lower Profile Converter controls. Both show the wrapped
hint and controls within their card. Root, Wasm alias, legacy,
session and a fresh MCP initialize/ping/tools-list/read-only machine-state/close
passed on this package (67 tools; simulator Ready and no operation).

No paid AI call, public crash report or real machine operation was performed.
Device/screen-reader and supervised hardware acceptance remain under their
existing separate gates.

## Final support acceptance — package ca809b5f

Combined build **1792763852563** passed in 137.917 seconds with no errors and
the same three local linker warnings. Focused run **1792763853166** passed the
three changed suites; full run **1792763853167** passed **206/206** in 35.330
seconds, with no skips or test warnings. The lifecycle regression starts before
Repeater creation, creates/removes the real Loader item and checks dirty-state
propagation and guarded Back. The actual shared-dialog fixture verifies loss of
underlying focus, Tab/Shift-Tab containment and restoration after close.

Live paired checks used package
**ca809b5f17acf64fbad1fce22d7a0ff6d74aeb7034fe4efc6607c61102aae2a5**:

- Browser Shot Summary's Tab wrapped on Dismiss instead of reaching the page
  behind it. The shot conversation's next Tab after Send reached its own
  transcript, replacing the previous Older Shot escape. Native's converted shot
  conversation rendered its existing header, context, attachment, input, Send
  and Back; Tab stayed inside it. No message or taste rating was submitted.
- Continue Chat in native and browser retained separate local drafts after
  closing/reopening. Settings Back showed Cancel/Discard; Cancel preserved the
  message, and Discard removed only the disposable acceptance text and returned
  Home. Native close restored focus to Continue Chat. No provider was called,
  conversation history cleared or credential changed.
- The host was stopped/restarted only through Qt Creator, checking process and
  port ownership first. The retained browser refused the changed host epoch and
  required reload, as designed; no mutation replayed. This is stale-host refusal,
  not a claim that a draft survives a full host-repair reload. Same-host replica
  loss, retired completion rejection and explicit new-request recovery pass the
  two-node QtRO fixtures. The extra live MCP Deny/timeout probe remains unrun
  because confirmation is Off and permission to change it is pending; distinct
  dialog IDs, one host deadline and stale/duplicate refusal pass fixtures.
- Final native PID **58632**, epoch `0eac73dd-de3b-4746-ab75-00958ed7a180`, was
  verified as the original-checkout executable. Root, Wasm alias, legacy HTML,
  session and a fresh read-only MCP session passed; MCP surface 1.20.0 listed
  67 tools. The simulator remained connected, Ready and idle. No QML binding,
  type or assignment error appeared in the inspected current-session log.

Tasks 24.2–24.4 are complete for their stated Mac/shared-service checks. Real
screen-reader, remote/mobile/HTTPS and supervised hardware acceptance remain
explicitly unverified in the existing final gates. IAB did not expose native
browser unload-warning wording, so this does not close that separate route gate.

## Support action inventory

| Shared presentation/action | Canonical owner | Accepted evidence / remaining check |
|---|---|---|
| Language selection, catalog search/edit, add, import/export and translation failures | LanguageService → TranslationManager | Task 24.1 and `navigation-language-validation.md`; no paid translation/public upload. |
| Live subsystem snapshots, level/marker filters, local Clear, refill | SystemLogService → WebDebugLogger | Shipping-view/source tests, bounded asynchronous timing and paired live log evidence above. |
| Diagnostic `.log`/ZIP download, cancel and retry | SystemLogDownload → SystemLogService | Task 23 actual native/browser downloads and shared transfer/failure fixtures. |
| Crash notes, submit/retry result, close | GlobalSupportService → CrashReporter | Shipping dialog limits/draft/correlation/failure fixtures; no public report submitted. |
| MCP Allow/Deny/deadline and stale reply rejection | GlobalSupportService → McpServer | Distinct-ID shipping dialog, host deadline and replica-loss/no-replay fixtures; live invalid-action Allow/refusal passed; live Deny/timeout awaits confirmation-setting permission. |
| Communication/profile-refusal acknowledgement and modal queue resume | GlobalSupportService → ProfileManager; native advisory queue | Exact event/request ownership and shipping queue-handler fixtures; public/hardware failure not induced. |
| Continue Chat open/send/failure/close, shot conversation intake/retry | ConversationSession → AIManager/ShotHistoryStorage | Two-thread source, correlation/reentrant disconnect and shipping-handler fixtures; paired live draft retention, Cancel/Discard and modal focus now pass. |
| Shot Summary and MCP setup help | Canonical stored-shot analysis; shared local help | Browser Summary displayed the latest Oct 23, 8:46 AM shot's stable-puck/Clean verdict without an AI call and now retains keyboard focus; native help opened and Escape dismissed it. Help wording distinguishes local desktop setup from compatible remote clients. |

## Manual

The local wiki checkout `/tmp/decenza-wasm-followup-wiki` contains short entries
for diagnostic downloads, crash-note validation and retained chat drafts. The
single `wiki-manual-task23.patch` also records those follow-up edits; publication
is pending with the feature. It corrects legacy-HTML wording and documents
D-Flow/A-Flow name routing versus other editors' content routing.

### Final integrated package recheck — 2026-10-24

The added diagnostics made the former fixed Debug layout overflow. Its shared
Flickable now uses the available viewport height, with a scrollbar and wrapped
converter hint; it does not raise the minimum window size. Actual scroll gestures
on package **2403b27d** moved Edge's scrollbar from 0 to 95.46694 and native's
from 0 to 100. The lower converter and translation controls are visible.

Combined build **1792763852587** passed (three existing local linker warnings),
then focused shared-control/handler run **1792763853177** passed 2/2. The last
full run before the Tea accessibility-label-only edit was **1792763853176**,
206/206 with no skips/test warnings. The authorized Qt Creator restart is
original-checkout PID89737, epoch 07a00740-5886-4af8-85d1-d4000c108f1c. Root,
Wasm alias, legacy HTML and session returned 200; read-only MCP initialization,
ping, 67-tool listing, machine read and session close passed again.
