# v2.0.5 release verification

The user requested an updated **2.0.5** prerelease on September 9, 2026. This
authorizes tag-driven builds and publication; it does not establish mobile
runtime validation.

## First build attempt

Tag source: `acee1e476e249aab0b131b5e262b47943b9a9ffb`, intended build 3588.
Linux x64 run [34416434620](https://github.com/Kulitorum/Decenza/actions/runs/34416434620)
and ARM64 run [34416434682](https://github.com/Kulitorum/Decenza/actions/runs/34416434682)
failed because GCC treats the four constructor parameters shadowing
`AIOperationLog` members as errors. The Mac compiler had accepted them. The
remaining four workflows were cancelled before publication; the prior build
3587 assets and release notes remained intact.

The release fix renames only those constructor parameters and their initializer
references. It changes no logging, request or machine-control behavior and does
not weaken compiler enforcement. A fresh full Mac run and all six tag-triggered
builds must validate the fixed revision before task 1 is complete.

Qt Creator MCP full Mac run **1788809091566** passed **117 suites, 0 failures,
0 skipped**, in 44,510 ms after rebuilding the fix. No test warnings. Registered
logging markers and `git diff --check` also passed.

## Build 3588

Tag source: `612788008ff603c8615cebe252e1c6660da2611f`.
Android run [34417143830](https://github.com/Kulitorum/Decenza/actions/runs/34417143830)
and Linux ARM64 run [34417143829](https://github.com/Kulitorum/Decenza/actions/runs/34417143829)
passed and published their assets. Android injected build 3588 into the release
notes and committed the version code to main.

Linux x64 run [34417143817](https://github.com/Kulitorum/Decenza/actions/runs/34417143817)
then reached test compilation and failed on the same GCC shadow rule in the
shared `DiagnosticCapture` helper (`prefixes`). Its constructor parameter is
renamed to `capturedPrefixes`; app code and test behavior are unchanged. The
remaining macOS, Windows and iOS runs were cancelled before publication. A full
Linux build and test run without uploads will verify this fix before another
tag push. Since Android published 3588, the next release attempt must use 3589.

## Manual publication

Wiki commit `e567a3ff0d4ebb89476bb072e5ac7d34528555a6` reconciles both archived
manual patches into three sentences under
[Getting Debug Logs](https://github.com/Kulitorum/Decenza/wiki/Manual#getting-debug-logs).
It explains subsystem labels and suppression, BeanBase lookup outcomes, and why
a report needs the complete time window. The current wiki was cloned before the
edit; the published HTML was retrieved and checked for the exact paragraph in
the intended section. Task 3 is complete.
