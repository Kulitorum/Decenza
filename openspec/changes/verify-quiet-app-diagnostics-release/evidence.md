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
