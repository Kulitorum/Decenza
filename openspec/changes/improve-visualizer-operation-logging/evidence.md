# Mac validation and review evidence

Validated on 2026-09-09 in `/Users/jeffreyh/Development/GitHub/Decenza`, branch
`codex/improve-visualizer-operation-logging`, based on merged PR #1930
(`5c2f1d585bde0fd5675eae91988fa89de7208b29`). All builds and tests used Qt Creator
MCP with the Mac Qt 6.11.2 Debug configuration. No standalone Decenza app was
launched, and no live Visualizer requests, paid AI calls, beta workflows or machine
commands were used.

## Build and tests

| Check | Final evidence |
| --- | --- |
| Focused production callback regressions | Qt Creator test run `1788809091558`: `tst_visualizershotparse` passed, 5,640 ms runner duration. |
| Full Mac suite | Qt Creator test run `1788809091560`: **117 passed, 0 failed, 0 skipped**, 52,910 ms; no tests reported warnings. Includes AI/provider, BeanBase, logger/MCP, profile/payload and coffee-bag regressions. |
| Visualizer assertions in final full run | **39 Qt Test cases passed**, 0 failed/skipped/blacklisted, 4,213 ms inside `tst_visualizershotparse`. Includes the original recovery-parser tests. |
| App build after final tests | Qt Creator build `1788809091560`: succeeded, 0 errors, 0 warnings, 1,061 ms. |
| Registered logging source gate | Passed: 458 covered runtime files, 20 helper headers, 241 QML files; all 18 gate self-test fixtures passed. |
| Translation keys, rich text, font-family QML gates | Passed; 3,505 translation keys checked. No QML behavior or UI code changed. |
| MCP tool budget | Passed. No new tool or log-reader surface. |
| Test source duplication | Passed: no production source compiled into more than one of 113 CMake test targets. Existing shared libraries are reused. |
| OpenSpec | `openspec validate improve-visualizer-operation-logging --strict --json`: valid, no issues. |
| Whitespace | `git diff --check`: passed. |

The focused target compiles the actual importer and links the existing uploader,
profile, history and logger libraries. `ControlledNetwork` never delegates to a
real network backend: tests deliver HTTP responses and transport failures directly
to production callbacks, including reversed completion order. Recovery inserts and
deduplicates real records in an isolated SQLite database. Profile saves use a
PID-specific test application directory; upload artifact writers use a test-only
PID path instead of overwriting the user's Documents files.

The final regression set verifies retry budgets and request counts, result signals,
record/parent IDs, one terminal per operation, cancellation, duplicate decisions,
partial batch counts, and main-log privacy while 422 UI errors and dedicated upload
files keep their original content. Final review also added assertions that list
responses identify the current page and recovery clears the previous item's local
shot ID/retry count. The full suite was rerun after those corrections.

See [source-audit.md](source-audit.md) for every entry/exit and forwarding consumer,
with **runtime-tested versus source-only** branches stated individually. Tests
cover representative production paths rather than claiming every dormant branch
was triggered. Existing permissive parsing and shared application-state patterns
are documented for later behavioral review; this change is diagnostic only.

## Complete persisted sample

[validation-log.txt](validation-log.txt) is the complete controlled sample written
through `WebDebugLogger::handleMessage` from actual Visualizer callback emissions.
It contains a failed connection test, a later successful connection test and an
empty shot list, plus two deliberately seeded historical entries. It is a
controlled validation log, not a user's device session or a capture of the whole
117-suite process. Assertions compare the whole file with the production
`sessionLinesMatching` INFO/WARN filters.

| Census | Result |
| --- | ---: |
| Complete persisted file | 11 lines / 2,136 bytes |
| Current Visualizer events | 9: 3 DEBUG, 5 INFO, 1 WARN |
| Current terminal outcomes / distinct terminal IDs | 3 / 3 |
| Duplicate terminal IDs | 0 |
| Current unformatted events / Runtime fallback events | 0 / 0 |
| Maximum current line | 281 characters |
| Visualizer selection, DEBUG minimum | 10 lines (9 current + 1 tagged historical) |
| Visualizer selection, INFO minimum | 7 lines (6 current + 1 tagged historical) |
| Visualizer selection, WARN minimum | 2 lines (1 current + 1 tagged historical) |
| Untagged historical fixture | 1 line, preserved in full file and absent from subsystem selections |
| Echoed private response content | 0 occurrences |

The untagged historical line demonstrates why a full-window read remains necessary
for older reports. No reader behavior was changed to hide or rewrite it. Separate
production callback tests verify stripping credentials, share-code queries and
fragments, bounding oversized IDs, and excluding echoed server content/notes.

## Review and outstanding work

Implementation review covered the full changed source and all consumers listed in
the audit. No remaining introduced correctness issue was identified. Public
signals, network payloads, request routing, application retry limits and pacing,
UI counters, persistence calls and device control retain their existing behavior.
Private callback signatures now carry only diagnostic ownership; neutral field/URL
formatting is shared with AI rather than copied.

Local tasks 1–5 are complete. Tasks **6.1–6.3 remain unchecked**: beta builds for
Android/iOS/other platforms, updated-device charging observations inherited from
#1930, and wiki publication. See [validation-holds.md](validation-holds.md); the
short [wiki patch](wiki-manual.patch) is prepared but unpublished. MQTT remains
intentionally excluded. A green Mac result does not certify the held platforms.

The OpenSpec change remains active for PR review. Before a requested merge, check
readiness and retained holds, use the **OpenSpec CLI** to archive, commit that
archive as the final PR commit when possible, and read checks on that exact head.
