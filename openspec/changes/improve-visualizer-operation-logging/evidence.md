# Validation evidence

## Scope correction

The earlier implementation and its 117-suite Mac run / 21-line live Visualizer exercise validated the now-removed lifecycle logging. They do not certify this revised implementation. See `source-audit.md` for the full-week evidence that drives the quieter change. The final regression result and live observations are recorded below.

## Historical volume

9,342 physical lines reviewed from the rolling DE1 week. Removing automatic FD inventories eliminates the 2,052 observed inventory/header records without replacing them. A replay of the 1,267 battery snapshots using state changes plus five-point progress retains 364 and suppresses 903. These are source/replay estimates against historical messages, not counts from an updated mobile build. The historical memory log is sparsely emitted; it cannot reproduce the complete minute-by-minute input to the new trend gate.

## On-demand FD access

Verified `src/mcp/mcpresources.cpp` registers `debug_get_fds` and directly returns `FdDiagnostics::snapshot()`. Neither that registration nor `src/core/fddiagnostics.*` is changed. Only the obsolete automatic CrashHandler inventory and its APK-install call sites are removed.

## Final Mac regression and source checks

Qt Creator MCP full suite, run **1788809091565**: **117 passed, 0 failed, 0 skipped**, 43,980 ms. Its preceding app build succeeded in 64,873 ms. The sole build warning is the existing debug linker `__eh_frame` compact-unwind size warning; no compile errors or test warnings were reported.

The new memory regression initially rejected the implementation: an interpolated middle median turned one allocation step into a false trend. The fix uses an observed median. The passing regression now checks every step location in a 360-sample history, plus jitter, isolated spikes, decline, fast/slow growth, zero samples and an interrupted clock sequence. Existing AI production tests verify silence before completion and one interpreted terminal outcome; logger tests retain function-only context and omit redundant function context alongside a complete source location.

Passed source checks: registered log markers and all 18 gate fixtures; translation-key conflicts; translated rich text; font-family literals; MCP tool budget; production test-source duplication; `git diff --check`; strict OpenSpec validation.

## Live Mac inspection before the final discovery trim

One process from this checkout (PID 58735), started through Qt Creator at 13:25:57. The old process had already exited and port 8888 was free. Read the complete session through the local MCP endpoint: **203 physical lines**, no pagination remainder. The app used its existing simulator configuration. The observed simulator steam start/stop retained scaling decisions, timer commands and real disconnected-scale warnings, with no duplicate SteamPage phase receipts. No tool sent machine commands or a paid AI request. A keyboard shortcut reached the app just after its unexpected launch; future live launches/restarts are explicitly left to the user, as now recorded in CLAUDE.md.

Three hostname-resolution attempts produced one failure warning; repeated connection-timeout/fallback failures were also suppressed. The two on-demand memory samples (startup and the first full QML-tree sample) remained available, with no routine `[Memory]` snapshot or QObject-delta record. One battery poll and one forecast-availability record remained. MQTT is excluded as requested.

That capture exposed repetitive discovery start/end receipts, so the final source removes duplicate manager/worker starts and ends and collapses unchanged discovery outcomes. The full suite above includes this final trim. Verification in the user-restarted build is pending.

## On-demand FD verification

Called DE1 MCP `debug_get_fds` successfully during the review: `supported=true`, with descriptor and socket details returned. This verifies availability, not a leak verdict; one census cannot establish sustained growth. The MCP implementation remains byte-for-byte unchanged in this PR.
