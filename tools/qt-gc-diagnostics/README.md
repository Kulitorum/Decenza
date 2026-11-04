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

## Ownership-change investigation (#2179)

`ownership-repro/` is a standalone stock-Qt 6.12 test project. It compares
adopting a QObject subtree under a C++ root, or changing its root to C++ ownership,
before and after the incremental weak-wrapper scan. It uses the collector's
existing state boundaries; it does not inject incorrect marks. It is deliberately
outside the app's default suite: the two after-scan cases assert correct marking
and currently fail on stock Qt 6.12.0.

Qt Creator run `1793559335413` (2026-11-04, macOS arm64, stock Qt 6.12.0):

| Probe | Result |
|---|---|
| Adopt a subtree before the weak scan | Pass |
| Adopt it after the weak scan | Missed wrapper; full-mark validation reports a discrepancy |
| Change root to C++ ownership before the weak scan | Pass |
| Change it after the weak scan | Missed wrapper; full-mark validation reports a discrepancy |
| Create the actual shared button at four marking boundaries | All four pass |
| Push a page containing that button through StackView at those boundaries | All four pass |

Each button case inspects the five animation/timer wrappers matching the types
in #2179, with the collection still active: all five were marked. The probe uses
the shared implementation with local style/interaction adapters, not the full
shot-review page or its native accessibility adapter. The offscreen platform
also emits a font-alias warning; it does not affect the marking assertions.

The source explains the ownership cases: `markWeakValues()` evaluates the current
QObject/top-parent keep-alive state (`qv4mm.cpp:705–736`), while
`QJSEngine::setObjectOwnership()` changes flags without marking an existing
wrapper (`qjsengine.cpp:1384–1395`). QML creation has its own write barrier
(`qqmlobjectcreator.cpp:2195–2203`), which is why creating a button during a
collection is not equivalent to adopting an already-existing unmarked subtree.
These references are to the installed Qt 6.12.0 sources.

The related [QTBUG-130767 fix](https://github.com/qt/qtdeclarative/commit/d148d8d78419effdc28b6bf74704379aa9a5950a)
marks newly wrapped objects after weak marking; it is already present in 6.12.0
(`qv4qobjectwrapper_p.h:231–255`). Upstream `dev`'s setter and weak-mark loop were
also inspected on 2026-11-04 and still use the same logic; that is source evidence,
not a runtime test of a newer Qt release.

This establishes an ownership-change marking gap, **not #2179's cause**. Ordinary
button creation and StackView insertion did not reproduce the report. The missing
field evidence is the affected wrappers' ownership/top-parent state when the
incremental weak scan visits them, compared with the later full mark. No Qt
runtime or deferred-abort behavior was changed by this investigation.
