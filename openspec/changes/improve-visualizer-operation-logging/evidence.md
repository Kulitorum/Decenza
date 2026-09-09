# Validation evidence

## Scope correction

The earlier implementation and its 117-suite Mac run / 21-line live Visualizer exercise validated the now-removed lifecycle logging. They do not certify this revised implementation. See `source-audit.md` for the full-week evidence that drives the quieter change. The final regression result and live observations are recorded below.

## Historical volume

9,342 physical lines reviewed from the rolling DE1 week. Matching deleted emitters and replaying the battery gate accounts for **3,641 fewer historical rows (38.97%)**:

| Source | Historical rows removed/suppressed | Basis |
| --- | ---: | --- |
| Automatic FD inventories and headers | 2,052 | 2,044 descriptor rows plus eight headers; emitters deleted |
| Battery snapshots | 903 | Replay retains 364 of 1,267; state change or five points from last emitted percentage, reset per session |
| Extraction color / raw scale weight traces | 248 | 127 + 121; emitters deleted |
| Routine settling progress | 137 | Four interrupted-stability records and final outcomes retained |
| QObject class-delta reports | 124 | Routine delta emitter deleted; on-demand data retained |
| Auto-load invocation receipts | 76 | 60 idle-countdown + 16 Sleep-to-Idle invocations; actual work logs retained |
| Duplicate SteamPage state receipts | 77 | Phase/state/substate/isSteaming/settings-visible emitters deleted; scaling decisions retained |
| AI diagnostic-file receipts | 24 | Eight each for prompt, response and Q&A; files still written |
| **Total** | **3,641** | Disjoint source families |

Subtracting only these rows leaves 5,701. This is a historical source/replay estimate, **not a measured total from the updated mobile build or an exact PR-to-PR log comparison**: the captured binaries predate PR #1930. It excludes additional reductions from healthy constant-weight messages, discovery/reconnect/forecast repeats, successful Visualizer file receipts and AI stage chatter added in #1930. It also excludes shorter payload/source context, which saves bytes rather than necessarily saving lines. The historical memory log is sparsely emitted; it cannot reproduce the complete minute-by-minute input to the new trend gate, so no reduction is claimed for its 590 RSS records.

## On-demand FD access

Verified `src/mcp/mcpresources.cpp` registers `debug_get_fds` and directly returns `FdDiagnostics::snapshot()`. Neither that registration nor `src/core/fddiagnostics.*` is changed. Only the obsolete automatic CrashHandler inventory and its APK-install call sites are removed.

## Final Mac regression and source checks

Qt Creator MCP full suite, run **1788809091565**: **117 passed, 0 failed, 0 skipped**, 43,980 ms. Its preceding app build succeeded in 64,873 ms. The sole build warning is the existing debug linker `__eh_frame` compact-unwind size warning; no compile errors or test warnings were reported.

The new memory regression initially rejected the implementation: an interpolated middle median turned one allocation step into a false trend. The fix uses an observed median. The passing regression now checks every step location in a 360-sample history, plus jitter, isolated spikes, decline, fast/slow growth, zero samples and an interrupted clock sequence. Existing AI production tests verify silence before completion and one interpreted terminal outcome; logger tests retain function-only context and omit redundant function context alongside a complete source location.

Passed source checks: registered log markers and all 18 gate fixtures; translation-key conflicts; translated rich text; font-family literals; MCP tool budget; production test-source duplication; `git diff --check`; strict OpenSpec validation.

## Live Mac inspection before the final discovery trim

One process from this checkout (PID 58735), started through Qt Creator at 13:25:57. The old process had already exited and port 8888 was free. Read the complete session through the local MCP endpoint: **203 physical lines**, no pagination remainder. The app used its existing simulator configuration. The observed simulator steam start/stop retained scaling decisions, timer commands and real disconnected-scale warnings, with no duplicate SteamPage phase receipts. No tool sent machine commands or a paid AI request. A keyboard shortcut reached the app just after its unexpected launch; future live launches/restarts are explicitly left to the user, as now recorded in CLAUDE.md.

Three hostname-resolution attempts produced one failure warning; repeated connection-timeout/fallback failures were also suppressed. The two on-demand memory samples (startup and the first full QML-tree sample) remained available, with no routine `[Memory]` snapshot or QObject-delta record. One battery poll and one forecast-availability record remained. MQTT is excluded as requested.

That capture exposed repetitive discovery start/end receipts, so the final source removes duplicate manager/worker starts and ends and collapses unchanged discovery outcomes. The full suite above includes this final trim.

## Final user-started Mac verification

The user started the final build on September 9 at 16:43:36 (session 76). Background process inspection found exactly one Decenza process, PID 94580, running this checkout's `build/Qt_6_11_2_for_macOS_Debug/Decenza.app/Contents/MacOS/Decenza`; its startup record reports the final build time 13:31:14. No tool launched, restarted, foregrounded or sent input to this instance.

Read all **157 physical rows through 245.948 seconds**, with `hasMore=false`, through the local Mac MCP endpoint. The capture includes one DNS-SD result at 29.720 seconds, one hostname-resolution warning, and one each of the distinct connection-timeout, WiFi-to-Bluetooth fallback and FlowScale-fallback warnings. Reconnect attempts continued at 49.669, 79.669 and 139.768 seconds without repeating those warnings or the unchanged DNS-SD result. No resolver browse start/end receipts remain. One battery snapshot and one forecast-availability record remain.

The appended on-demand memory summary contains five samples, including four minute-spaced full-QML samples, with no automatic RSS or QObject-delta log lines. This short run verifies sampling and startup silence; the deterministic regressions above validate sustained-growth detection. On-demand memory text is returned at download time, not appended to the persisted log. MQTT remains excluded as requested.

The prior 84-second Mac capture had four completed DNS-SD cycles and 16 discovery/resolver receipt lines; the final 246-second capture has one discovery result. These runs have different activity and observation windows, so their overall totals (203 versus 157) are not a whole-app percentage comparison. Neither Mac capture establishes mobile charging behavior or a week-long post-change volume.

## On-demand FD verification

Called DE1 MCP `debug_get_fds` successfully during the review: `supported=true`, with descriptor and socket details returned. This verifies availability, not a leak verdict; one census cannot establish sustained growth. The MCP implementation remains byte-for-byte unchanged in this PR.
