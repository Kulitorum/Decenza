# Qt QML garbage-collection diagnostics

`qt-6.12.0-gc-diagnostics.patch` applies to Qt 6.12.0's `qtdeclarative` source.
It instruments QtQml only. In an incremental collection, Qt rescans the JS stack
before cross-validation. When the full mark still finds objects the incremental
mark missed, the patch records object details and an observed retaining path and
unions the validated full mark with the diagnostic trace's mark before sweeping.
Decenza records the original native stack and waits
for a new connected DE1 state report of idle or sleep before aborting for a report.
It requests a state reading after the mismatch and when the connection changes;
it never aborts based only on an older cached idle state.
Disconnected or not-yet-reported state does not count as idle. If the machine never
reports a safe state, the abort remains pending. New operation requests from the
app are blocked while an abort is pending; stop and skip requests remain available.

The retaining path is bounded and can be incomplete, particularly if the mark
stack drains recursively. It identifies one observed path, not necessarily the
mutation that caused the discrepancy. The idle check is based on received DE1
state, which can lag behind a physical button press or a connection failure. The
app cannot block starts from the machine's physical controls.

The test project uses a separate macOS QtQml build compiled with
`DECENZA_QML_GC_TEST_HOOKS`; that define must never be used in the Android library.
Its controlled tests check an ordinary collection, a mark repaired by the rescan,
an unrepaired mark that reports a named root and continues with the full mark,
and a tracing pass that misses a previously validated mark.
The app's deferred abort policy is tested separately. The Android arm64
library has been built with the test hooks disabled and checked against the Qt
6.12.0 SDK's exported symbols, feature headers, SONAME and dependencies. It
still needs Android runtime and crash-report verification before release.
