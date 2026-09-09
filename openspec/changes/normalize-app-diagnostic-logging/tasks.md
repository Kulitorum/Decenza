## 1. Establish coverage

- [x] 1.1 Inventory first-party runtime log calls in C++, headers, QML and platform bridges, map each owner using the design, and document concrete exceptions; verify the inventory accounts for all capture families and dormant source call sites.
- [x] 1.2 Record the DE1 formatting baseline and representative sanitized examples in change evidence, distinguishing the historical census from current-source coverage; verify counts against the captured 5,240-line window.

## 2. Shared formatting and runtime context

- [x] 2.1 Register the missing subsystem owners and add aliased C++ and compile-time QML logging helpers sharing one registry/formatter; verify catalog generation and representative C++/QML calls.
- [x] 2.2 Preserve supplied warning context and format unattributed messages and multiline continuations in WebDebugLogger; verify source retention, missing-context behavior, severity filtering, session-like message content, reentrancy and shutdown using existing logger tests.
- [x] 2.3 Give memory/FD diagnostics a registered owner and per-dump identity on every row; verify a page without its dump header remains attributable.

## 3. Migrate first-party emitters

- [x] 3.1 Convert battery, app startup/settings/location, memory and platform emitters; verify source inventory coverage, unchanged charging decisions and truthful command-versus-observation messages.
- [x] 3.2 Convert history, backup/restore, profile and recipe emitters; verify owner filters, missing-row versus SQL-error context, and known recipe refusal reasons.
- [x] 3.3 Convert shot/frame/timer, steam and remaining machine/UI emitters, reusing existing SAW/Calibration/device markers; verify no duplicate signal-forwarded events and no new per-sample INFO traffic.
- [x] 3.4 Convert Visualizer, ShotServer/MQTT and other remaining network emitters; verify subsystem ownership, unchanged transport behavior and preserved repeat collapsing.
- [x] 3.5 Convert AI/conversation and bag/UI emitters, plus all remaining inventory entries; verify the source inventory has no unexplained first-party bypasses.
- [ ] 3.6 Correct the evidenced severity/wording gaps: update failures/recovery, benign R2 status, unavailable calibration diagnostic ranges and misleading status statements; verify each with a focused existing test or captured log and preserve numerical/control behavior.

## 4. AI and bag operation outcomes

- [x] 4.1 Carry one diagnostic operation identity through bag local fetch/archive/provider stages and general AI requests, capturing bag/shot and provider/model at dispatch; verify two consecutive or overlapping fetches and a settings change remain distinguishable.
- [x] 4.2 Log exactly one truthful terminal result for local/provider failure, valid empty result, successful interpretation, invalid output, rejection and cancellation/supersession; verify existing test-provider callbacks exercise each outcome without paid calls or duplicate UI/MCP terminal logs.
- [x] 4.3 Bound and sanitize new outcome fields and URL identity, keeping prompts/page bodies/responses/secrets out of the main log; verify errors that echo sensitive content do not leak it and logging adds no requests.

## 5. Enforce and validate

- [x] 5.1 Extend the existing build-free marker gate and workflow path triggers across the inventoried first-party sources; verify negative fixtures fail for mixed C++/headers, QML, dynamic/lowercase prefixes and native bridges while shared helpers and justified crash-safe exceptions pass.
- [ ] 5.2 Build and run the relevant tests through Qt Creator MCP, then run the required full suite and affected text/QML gates; record exact Mac results. Android, iOS and other platform compilation is verified through beta builds, per the user's workflow clarification; record those results when available.
- [x] 5.3 Exercise representative workflows on a current build, inspect the complete unfiltered current-session log through MCP, and compare subsystem/WARN queries, fallback census and log volume; verify all remaining gaps are explained and historical entries still paginate/filter correctly.

## 6. Documentation and final review

- [x] 6.1 Update LOGGING.md, affected catalog descriptions and the wiki's short diagnostic-log guidance; verify docs describe actual coverage, AI outcome semantics, historical-prefix compatibility and framework-context limitations.
- [x] 6.2 Reconcile implementation evidence with every acceptance scenario, revalidate this OpenSpec change, and review the diff for behavior changes outside logging; verify all completed tasks have evidence and any unresolved task remains unchecked.


## Validation status

Mac build and all 117 test suites pass. Current-build MCP retrieval and source/text
checks are recorded in evidence.md. Tasks 3.6 and 5.2 remain unchecked for the mobile
charge/mismatch observation and normal beta platform builds; the update, R2 and
calibration diagnostic checks within 3.6 already pass on Mac. No separate platform
test workflows will be dispatched, per the user's clarification.

On 2026-09-09 the user authorized merging PR #1930, explicitly held beta builds,
and requested CLI archival as the PR's final commit. Archive closure therefore
does not claim the two outstanding validation portions passed. They remain
recorded here and will be carried into the follow-up logging change after merge;
the updated mobile capture depends on an eventual beta build. No beta workflow
or deployment is authorized by this archive.
