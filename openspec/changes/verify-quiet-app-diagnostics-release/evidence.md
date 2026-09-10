# v2.0.5 release verification

The user requested an updated **2.0.5** prerelease on September 9, 2026. This
authorizes tag-driven builds and publication; it does not establish mobile
runtime validation.

**Published: [v2.0.5 build 3589](https://github.com/Kulitorum/Decenza/releases/tag/v2.0.5).**
All six platform workflows passed; five GitHub assets were replaced and iOS was
uploaded to App Store Connect. Tasks 1 and 3 are complete. Task 2 remains open.

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
not weaken compiler enforcement. Subsequent validation is recorded below.

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
Linux build and test run without uploads then verified this fix before another
tag push. Since Android published 3588, the next release attempt used 3589.

## Final release validation

Source: `151708d85c03f56191f677d59355ad481b09555f`.
Qt Creator MCP full Mac run **1788809091567** passed **117 suites, 0 failures,
0 skipped**, in 50,310 ms after rebuilding the test-helper fix, with no test
warnings. Linux verification run
[34418030595](https://github.com/Kulitorum/Decenza/actions/runs/34418030595)
was dispatched on this source with uploads disabled. Compilation passed; **114
of 115 tests passed**, while `tst_aiproviders` crashed in
`retryKeepsOperationIdentityAndOneSuccess` on all three attempts, about one second
into each run (the first retry). No artifacts were published by this check.

The failing regression exercises an older production defect from PR #1114:
`tryScheduleRetry` calls `m_retryFn` directly, but every provider's `sendRequest`
replaces that same callable before serializing its captured request by reference.
The active capture can therefore be destroyed while still in use. The fix calls
a local copy, keeping the callable and request alive through the resend. Retry
delays, request generation checks, selected providers and log output are unchanged.
The existing test reproduced the failure on Linux; successful verification is
recorded below.

Retry-fix source: `22aaffda4d08b546349d2730534f4ae484e65771`.
Qt Creator MCP full Mac run **1788809091568** passed **117 suites, 0 failures,
0 skipped**, in 45,410 ms, with no test warnings. Text invariants run
[34419863360](https://github.com/Kulitorum/Decenza/actions/runs/34419863360)
passed on that source. Linux verification run
[34419869784](https://github.com/Kulitorum/Decenza/actions/runs/34419869784)
passed with uploads disabled: **115 of 115 tests passed**, with no retry needed,
in 48.14 seconds. `tst_aiproviders` passed on its first attempt in 1.03 seconds,
confirming the regression is fixed. The job completed successfully, including
AppImage packaging.

## Build 3589 publication

The existing `v2.0.5` tag was moved to the verified retry-fix source
`22aaffda4d08b546349d2730534f4ae484e65771` using a lease against its prior value.
The release stayed visible as a prerelease. Its existing notes were preserved
with one user-facing bug fix added: a crash while retrying an AI request after
a temporary provider error. Android CI injected build 3589 and committed it to
main as `0ab4f68ec14e5cc5bffd89e933727a291d3e4a31`.

Every run below used the tagged source above and logged the same version-code
bump, 3588 to 3589. All completed successfully:

| Platform | Run | Publication verified |
| --- | --- | --- |
| Android | [34421737141](https://github.com/Kulitorum/Decenza/actions/runs/34421737141) | APK asset `553894745` |
| Linux ARM64 | [34421737137](https://github.com/Kulitorum/Decenza/actions/runs/34421737137) | AppImage asset `553896579` |
| Linux x64 | [34421737188](https://github.com/Kulitorum/Decenza/actions/runs/34421737188) | AppImage asset `553901393`; all 115 tests passed without retries in 48.31 s |
| Windows | [34421737135](https://github.com/Kulitorum/Decenza/actions/runs/34421737135) | Installer asset `553910061` |
| macOS | [34421737140](https://github.com/Kulitorum/Decenza/actions/runs/34421737140) | DMG asset `553915091`; Apple notarization accepted |
| iOS | [34421737138](https://github.com/Kulitorum/Decenza/actions/runs/34421737138) | App Store Connect reported `UPLOAD SUCCEEDED with no errors` |

The release API confirmed exactly five uploaded, nonempty assets, each with a
new asset ID and a creation time after this tag push. The release remained
visible with `isPrerelease=true`, `isDraft=false`, and `Build: 3589`. Its body
matched the prior notes plus only the retry-crash fix and CI's build-number
change. App Store Connect upload success does not establish TestFlight
processing completion. Task 1 is complete.

DE1 MCP `app_get_info` read during publication reported the Samsung SM-X210
still running **2.0.5 build 3587** on Android 16. The updated mobile charging-log
observation remains pending; build success does not satisfy task 2.

## Manual publication

Wiki commit `e567a3ff0d4ebb89476bb072e5ac7d34528555a6` reconciles both archived
manual patches into three sentences under
[Getting Debug Logs](https://github.com/Kulitorum/Decenza/wiki/Manual#getting-debug-logs).
It explains subsystem labels and suppression, BeanBase lookup outcomes, and why
a report needs the complete time window. The current wiki was cloned before the
edit; the published HTML was retrieved and checked for the exact paragraph in
the intended section. Task 3 is complete.
