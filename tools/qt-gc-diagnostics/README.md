# Qt QML garbage-collection diagnostics

`qt-6.12.0-gc-diagnostics.patch` applies to Qt 6.12.0's `qtdeclarative` source.
It instruments QtQml only. In an incremental collection, Qt rescans the JS stack
before cross-validation. When the full mark still finds objects the incremental
mark missed, the patch records object details and an observed retaining path, then
aborts so Decenza's existing crash handler can report it. The fatal message carries
a compact path even when the separate debug log is not included in a report.

The retaining path is bounded and can be incomplete, particularly if the mark
stack drains recursively. It identifies one observed path, not necessarily the
mutation that caused the discrepancy. An abort during a machine operation stops
Decenza's software stop-at-weight control.

The test project uses a separate macOS QtQml build compiled with
`DECENZA_QML_GC_TEST_HOOKS`; that define must never be used in the Android library.
Its controlled tests check an ordinary collection, a mark repaired by the rescan,
and an unrepaired mark that reports a named root and aborts. The Android arm64
library has been built with the test hooks disabled and checked against the Qt
6.12.0 SDK's exported symbols, feature headers, SONAME and dependencies. It
still needs Android runtime and crash-report verification before release.
