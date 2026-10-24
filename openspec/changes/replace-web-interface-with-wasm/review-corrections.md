# Whole-PR review corrections — 2026-10-23

The independent whole-PR review of #2075 at `3dc8ffcad5baa6482d34abf23503821908f41161` accounted for 1,159 changed files: 1,006 text diffs and 153 binary/asset inspections. Unmodified vendored Qt is excluded from changes. Twenty fresh findings plus eight earlier findings were rechecked; all 28 distinct findings were confirmed. The fixes preserve the shared production QML, existing Qt Remote Objects architecture, retained HTML and MCP routes.

This ledger tracks defect corrections, not completion of the remaining parity or platform acceptance. It does not close task 32's later simplification work.

## Corrected findings and regression scope

| # | Priority | Confirmed defect | Evidence / verification scope |
| --- | --- | --- | --- |
| 1 | P1 | Persist the source filename when trying another profile | `tst_profilemanager`: Try another profile, reconstruct startup, save without overwriting the former profile. |
| 2 | P1 | Copy backup request identity before clearing active state | `tst_backupoperationpolicy`: success/failure completion identity for backup and restore. |
| 3 | P1 | Record requested shot ID before validating stored-shot reply | `tst_aimanager`, `tst_conversationsession`: real stored-shot completion, idle/busy disconnect cleanup and stale-client refusal. |
| 4 | P2 | Route browser Escape through the page leave guard | `tst_customwidgethtml`: shipping navigation handlers honor release-time page guards. |
| 5 | P2 | Retain local profile drafts across stack reactivation | `tst_customwidgethtml`: shipping editor handlers preserve drafts and accessible overwrite labels. |
| 6 | P2 | Activate shared buttons only after a completed click | `tst_sharedformcontrols`: real pointer release outside the control must not activate. |
| 7 | P2 | Emit shot-deleted domain event for request-scoped deletes | `tst_historyservice`: correlated deletion emits the domain event; long Unicode notes round-trip and aggregate oversize is refused. |
| 8 | P2 | Use Qt internal separators for canonical-path containment | `tst_screensavermediatickets`: portable containment expectations; Windows runtime remains unverified. |
| 9 | P2 | Release browser-owned upload holds on connection loss | `tst_shotreviewactions`, `tst_remotesessionserver`: independent review leases, final-owner release and exactly-once connection close. |
| 10 | P2 | Align transport size with accepted RPC payloads | `tst_remotesessionserver`: bidirectional 2 MiB logical writes, chunk streaming and explicit oversize closure. |
| 11 | P2 | Retire idle remote conversation sessions | `tst_aimanager`, `tst_conversationsession`: real stored-shot completion, idle/busy disconnect cleanup and stale-client refusal. |
| 12 | P2 | Open Bluetooth settings while adapter is disabled | Removed the adapter-availability precondition; canonical OS settings action remains unchanged. No physical Bluetooth-off OS check. |
| 13 | P2 | Keep color picker enabled throughout its drag | `tst_sharedformcontrols`, `tst_customwidgethtml`: real color drag commits once after preview; accessibility steps commit. |
| 14 | P2 | Replace previous review after browser shot completion | `tst_customwidgethtml`: shipping operation-completion stack handlers replace prior review and preserve chronological browser history. |
| 15 | P2 | Restore accessible names on overwrite buttons | `tst_customwidgethtml`: shipping editor handlers preserve drafts and accessible overwrite labels. |
| 16 | P2 | Preserve shot mode when embedded profile JSON is empty | `tst_customwidgethtml`: empty embedded profile JSON retains explicit historical-shot mode. |
| 17 | P2 | Accept Qt sequences in shared list bindings | `tst_customwidgethtml`: real QVariantList sequences work in import, screensaver days and shot-map bindings. |
| 18 | P2 | Reset duplicate resolution after terminal import failure | `tst_customwidgethtml`: failed terminal import resets the actual duplicate resolver. |
| 19 | P2 | Allow existing long shot notes through shared validation | `tst_historyservice`: correlated deletion emits the domain event; long Unicode notes round-trip and aggregate oversize is refused. |
| 20 | P2 | Preserve newer active equipment when a fork completes | Rechecks current active equipment in the asynchronous completion. No direct HTTP race regression harness exists. |
| 21 | P2 | Allow reset and shrinking edits on near-limit layouts | `tst_layoutservice`: prospective canonical configuration, near-limit shrink/reset and atomic oversize refusal. |
| 22 | P2 | Restore the profile active before sensor calibration | `tst_customwidgethtml`: shipping calibration lifecycle captures/restores the current profile. |
| 23 | P3 | Report the requested translation-upload toggle state | `tst_customwidgethtml`: requested toggle state is reported on correlated acknowledgement, including refusal. |
| 24 | P2 | Keep unrelated navigation from cancelling shared milk capture | `tst_shotreviewactions`: shipping review handlers and per-view ownership; `tst_difluidr2`: real device/proxy event distinctions; `tst_shotrecord_cache`: historical RPM capability. |
| 25 | P1 | Scope refractometer readings to their owning shot session | `tst_shotreviewactions`: shipping review handlers and per-view ownership; `tst_difluidr2`: real device/proxy event distinctions; `tst_shotrecord_cache`: historical RPM capability. |
| 26 | P2 | Derive historical grinder capability from the shot equipment | `tst_shotreviewactions`: shipping review handlers and per-view ownership; `tst_difluidr2`: real device/proxy event distinctions; `tst_shotrecord_cache`: historical RPM capability. |
| 27 | P3 | Use the lexical error-toast ID in history-data handlers | `tst_customwidgethtml`: failure handlers use the lexical toast ID. |
| 28 | P2 | Preserve Steam editor commits while another save is pending | `tst_customwidgethtml`: pending editors preserve/refuse later commits explicitly. |

## Integration details

Review leases belong to an admitted transport client and a view UUID. Closing a connection releases its view leases; upload holds end only after the final view of that shot releases. Refractometer events identify their owning review, and the owner cannot change during a run. Navigation does not cancel another observer's host-owned milk capture. Readings are distinct from cached-value notifications when the device target changes.

Transport accepts bounded logical QtRO writes and chunks them into 128 KiB WebSocket messages. Native output queues, logical writes and input buffering are bounded. Qt's Wasm `QWebSocket::bytesToWrite()` cannot report the browser JavaScript socket's queued byte count; a measured browser-network queue bound is not claimed.

## Actual integration results

The final combined Qt Creator build `1792763852478` passed in 107.026 s, including the matching Wasm bundle. Native QML diagnostics passed **358/358**; Wasm lint emitted no warnings. Native linking retained the three known local OpenSSL deployment-target/unwind warnings. Full Qt Creator run `1792763853129` passed **206/206 suites in 35.650 s**, with no failed or skipped suites reported and no warning-bearing suites. The earlier focused run passed 12/13 suites; its new shared-control regressions exposed a live QColor-reference comparison mistake and a clipped button fixture. Both were corrected before the full pass. Color tests now cover inside/outside release, cancellation, and RGB/HSL accessibility commits.

Independent cross-review of the fix delta covered the storage/transport, backend and QML/lifecycle changes. It caught an incomplete physical R2 start path: actual start packets now freeze the destination before the first reading, reject concurrent requests and arm the existing silence watchdog/run ceiling. Packet→proxy→ownership regressions cover successful completion, abort, transport loss and lost-terminal timeout. Cached target announcements cannot produce measurement readings or run boundaries. No further confirmed blocker remained in that review.

Final matching package: `471789f5d440f6c533059e051ab6eeb117faec83fe6dac39a263c78e4be70b83`. Qt Creator stopped the earlier process, port/process checks confirmed it was gone, and Qt Creator launched the final native simulator host as the sole port-8888 listener (PID 34649).

Mac runtime checks:

- Native and root Wasm loaded the shared UI. A second client also rendered from `/wasm/`. Both clients' unsaved D-Flow dose drafts remained at 18.1 g after dismissing the Back/Escape prompt; both test drafts were then discarded. The original host recipe remained 18.0 g in / 54.0 g out.
- A browser color drag changed the hue from 220 to 261 and updated native; a native drag changed it to 248 and updated the browser. The original `#4e85f4` color and **Default Dark** selection were restored. The resolved palette matched all canonical dark defaults and the original background source remained `none`.
- Advisor context for the **October 23, 8:46 AM** shot loaded through the real stored-shot source in both native and Wasm, reaching the taste-intake UI. No message was submitted to an AI provider.
- Native **Backup Now** completed and its button became available again. The refreshed local `Documents/Decenza Backups/shots_backup_20261023.zip` was 13,133,863 bytes, with six entries and no CRC error. Restore/failure paths are regression-tested; no live restore was performed.
- Two browser review views showed the October 23, 8:46 AM and October 22, 8:18 PM shots independently. Stepping updated the review route/data; closing one browser left the other usable. Both remaining review pages were then left normally. This checks navigation and lease admission/cleanup without claiming a physical refractometer reading.
- Read-only HTTP checks returned 200 for `/`, `/wasm/`, `/legacy/`, `/legacy/recipes` and `/api/session`, whose package identity matched both builds. The retained legacy shot list rendered in a browser. MCP initialization, its 67-tool catalog, `machine_get_state` and `app_get_info` succeeded on the same listener; the temporary MCP session was closed. The final browser session reported no warnings/errors during these checks.

Local captures: `/tmp/decenza-review-profile-draft.jpg`, `/tmp/decenza-review-color-drag.jpg`, `/tmp/decenza-review-theme-restored.jpg`, `/tmp/decenza-review-advisor-context.jpg` and `/tmp/decenza-review-legacy.jpg`. They are temporary local evidence, not committed artifacts.

Packet/helper regressions do not substitute for physical refractometer acceptance. Windows runtime, physical Bluetooth-settings launch, the REST equipment-fork race through a live HTTP harness, real-device, remote-host, mobile, trusted HTTPS and screen-reader gates remain unperformed here. The full-suite summary reports CTest suite outcomes; the opt-in live route test was not enabled in that run, so the separate HTTP/MCP checks above provide the live listener evidence. Existing broader acceptance tasks remain open. Build-only CI for this correction commit is tracked separately from the local results.

## Version 2.0.9 prerelease target

The maintainer requested this work for the first **2.0.9 prerelease**. The canonical CMake project version is now 2.0.9; `versioncode.txt` remains 3631 for local/build-only checks and is still managed by CI on a release tag.

Combined Qt Creator build `1792763852480` passed in 74.104 s with the three known local linker warnings. Full run `1792763853130` passed **206/206 suites in 34.840 s**, with no failures, skipped suites or warning-bearing suites. After stopping the old process through Qt Creator and confirming port 8888 was free, Qt Creator launched the sole 2.0.9 simulator host (PID 48473). Both native and Wasm About show **Version 2.0.9, Build 3631**. Both generated package headers and the served session agree on `ba17aef1ea57009f236a44182853a73f2b4e364b5af7809028078fa30c027b77`.

The root/alias, retained legacy routes, session API and read-only MCP checks were repeated successfully against 2.0.9; MCP initialization reports `appVersion: 2.0.9`, and its temporary session closed. Local capture: `/tmp/decenza-review-version-2.0.9.jpg`. No prerelease tag, release publication or App Store upload was performed.

### macOS bundle metadata correction

The package inspection found that the existing macOS plist still hard-coded version 1.0.0/build 1. It now uses the canonical CMake project version and CI-managed build code. Qt Creator build `1792763852483` passed in 72.448 s with the same three local linker warnings; the generated bundle plist reports **2.0.9 / 3631**. This metadata-only correction follows the full 206-suite pass above.

After a Qt Creator stop and an empty port/process check, Qt Creator launched the corrected host as the sole port-8888 listener (PID 51161). Both generated headers and the served session match `4a46923c9e932da89347987f1c77d58c7afe3f0fba2cc82ea908a927df9cb0ae`. Wasm About reports 2.0.9/build 3631 with no browser warnings/errors. Root, alias, retained legacy routes, session API and the 67-tool MCP initialization/read-only calls/session close all passed again.

## Additional Codex PR review comments before merge

The merge preflight read all three GitHub Codex review submissions and found four unresolved comments outside the 28-finding correction list above, including the older release-upload comment. All four were verified against production callers; this brings the confirmed corrections to 32.

| # | Priority | Confirmed defect | Correction / verification scope |
| --- | --- | --- | --- |
| 29 | P1 | Parallel macOS/Windows release uploads can silently succeed without an uploaded asset | Publishing runs now require the maintainer-created release in `plan`, before parallel builds. Upload failures propagate. This preserves the documented release-notes/prerelease setup; the workflow does not invent or publish a release. Actionlint and a mocked execution of the actual script cover tag/manual success, missing release, API failure and malformed version. No real publication was attempted. |
| 30 | P1 | Bean-history worker reads a query QPointer while cancellation can delete it | Dispatch through the captured application context and inspect query lifetime only on the main thread. The global pool joins during application shutdown; canceled workers still release their bounded capacity. Existing bean-search regressions now force canceled-job deletion, verify capacity recovery and destroy the real source before queued completion. |
| 31 | P2 | Equipment Save is disabled before the first composing IME word can commit | Shared Save permits a commit attempt during composition, then validates committed fields and duplicates before writing. Seven real input-method-event regression rows cover four first gear values and duplicate creation/resolution. This is not physical mobile-IME acceptance. |
| 32 | P2 | ZIP worker reads the service QPointer during service destruction | Dispatch through the captured application context; inspect the service only in its queued callback. Retain the archive directory through delivery/abandoned cleanup. Independently checked against Qt lifetime/dispatch implementation; no new ZIP fixture or TSan coverage is claimed. |

Qt 6.12 evidence: `qtbase/src/corelib/kernel/qmetaobject.cpp:1731-1740` reads the receiver thread during invocation; `qtbase/src/corelib/kernel/qcoreapplication.cpp:957-968` joins the global thread pool before QObject teardown. Capturing the application pointer before launching the worker keeps dispatch independent of the job/service lifetime.

The maintainer explicitly declined another CI dispatch: finish the existing all-platform run, then merge. That run tests production commit `3acf25996`; the subsequent 2.0.9 version, macOS metadata and these four corrections require separate local evidence and are not represented as CI-tested by that older run.

Local acceptance for these additional corrections: combined Qt Creator build **1792763852485** passed in **110.210 s**, including the matching Wasm client and **358/358** QML gate, with the same three local linker warnings. Focused run **1792763853131** passed both changed suites in 5.220 s; full run **1792763853132 passed 206/206 in 33.120 s**, without failures, skips or warning-bearing suites. Actionlint and the five publishing-preflight dry-run scenarios passed through Qt Creator process tools.

Qt Creator stopped PID 51161, process/port checks confirmed it was gone, and Qt Creator launched PID 57364 as the sole port-8888 listener. Generated native/Wasm headers and served session match package `2ca49e82e844bb8519acc03a4ca3b7399a74f44a53da918b21fc814e2748e28c`. Root, Wasm alias, legacy/legacy Recipes, session and read-only MCP checks passed again; MCP reports 2.0.9/build 3631 and its temporary session closed normally.

