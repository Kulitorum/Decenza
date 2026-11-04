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
original library has been built with the test hooks disabled and checked against the Qt
6.12.0 SDK's exported symbols, feature headers, SONAME and dependencies. It
still needs Android runtime and crash-report verification before release.
The allocator fix and weak-history additions below are source-only until the
replacement documented in `android/qt-overrides/README.md` is complete.

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
incremental weak scan visits them, compared with the later full mark. Those ownership probes did not change Qt runtime or deferred-abort behavior.
The later diagnostic and allocator changes are documented below.

### Compiled review replay and allocation stress

The follow-up fixture compiles the actual shared button, focus indicator and tap
area with qmlcachegen. `ReplayPage.qml` reproduces their results-card nesting and
comparison data changes; `ReplayStack.qml` uses real StackView replacements.
`ProbeInteractionHost` reproduces the native accessibility adapter's C++
`QPointer<QObject>` storage and change notification, without audio or settings.
It is a reduced fixture, **not the complete PostShotReviewPage**.

Qt Creator build `1793559335269` and run `1793559335419` (2026-11-04,
macOS arm64, stock Qt 6.12.0):

| Experiment | Result |
|---|---|
| Compiled page creation, comparison loading, and clear/reload at five marking stages | 450 cycles pass; all 2,250 inspected animation/timer wrappers marked |
| Automatic GC, compiled QML, direct pages and StackView, 1 µs and 1 ms budgets | 8,000 updates / 1,000 pages; 154 validations and sweeps; no mismatch |
| Same stress with `QV4_FORCE_INTERPRETER=1` | 8,000 updates / 1,000 pages; 152 validations and sweeps; no mismatch |
| Original after-weak-scan ownership controls | Both still reproduce missed marking |

Every stress update asserts active button focus, a running sequential focus
animation, and the host's retained-item pointer. Across both execution modes,
3,432 comparison updates began after weak marking while marking was still
active. GC proceeds through allocations and queued Qt event-loop work; only
cleanup forces any final pending collection. Observer callbacks count the real
validator and sweep without changing their algorithms. The compiled run uses
`QV4_FAIL_ON_INVALID_AOT=1` so incompatible generated code cannot silently fall
back to interpreted execution. Both runs emit the known offscreen font warning.
At that revision, the standalone suite was intentionally red solely for its
ownership controls.

These runs do not reproduce #2179 or rule out Android-specific behavior, the
full page/service graph, or longer-lived heap corruption. A separate upstream
[QTBUG-149512 fix](https://github.com/qt/qtdeclarative/commit/059ee9db91507b8b86cdf4bf033985c9ea41e0d4)
addresses allocations during sweep losing allocator bitmap entries. The installed
6.12.0 `qv4mm.cpp:265–308` has the old implementation; no experiment here establishes
that defect as the cause of #2179. The useful next targets are an Android/full-page
replay and weak-scan ownership history for the five affected wrappers.

### Earlier reports and the sweep-allocation lead

The historical reports cover several contexts and heap types. Grouping them by
collector involvement is useful; it does not establish one shared root cause.

| Reports | Context | Heap object being destroyed / diagnostic |
|---|---|---|
| [#1927](https://github.com/Kulitorum/Decenza/issues/1927) | Settings, Shot Map, shortly after suspend/resume | Earlier analysis identifies a context-wrapper destructor |
| [#1937](https://github.com/Kulitorum/Decenza/issues/1937), [#2030](https://github.com/Kulitorum/Decenza/issues/2030) | Scale settings; post-shot settling | ErrorObject stack-trace destruction |
| [#2054](https://github.com/Kulitorum/Decenza/issues/2054) | Post-shot settling, still on espressoPage | JS function destruction / compilation-unit release |
| [#2076](https://github.com/Kulitorum/Decenza/issues/2076), [#2168](https://github.com/Kulitorum/Decenza/issues/2168) | Cold start, flat/dark Shot Map, machine asleep; crashes at 284 / 269 seconds | Same JS-function sweep frames; #2168's bad pointer contains text bytes |
| [#2179](https://github.com/Kulitorum/Decenza/issues/2179) | Review page opening | Five missed QObject wrappers; intentional abort after validation |

The first reports predate ShotResultsCard and the shared-button extraction.
Those components cannot explain the entire historical cluster by themselves.
The synchronous log-handler lead was also insufficient: #2054 and #2076 already
contained the queued-delivery fix (#2045). Suspend/resume, physical scale teardown,
and a shot are each absent from some reports, so none is a necessary common trigger.

The upstream `v6.11.2` source was checked on 2026-11-04: its
[`Chunk::sweep()`](https://github.com/qt/qtdeclarative/blob/v6.11.2/src/qml/memory/qv4mm.cpp#L270)
also snapshots `extendsBitmap` before destruction and overwrites the bitmap
afterward. Thus the earliest reports' Qt version contains the same source-level
defect; this is not limited to 6.12.0. Different corrupted destructor victims are
compatible with live-object memory reuse, but do not prove a shared cause. #2179's
marking discrepancy remains a less direct match than the older corrupted-pointer
sweep crashes.

**New positive probe:** `tst_qmlgc_sweep.cpp` adapts the regression scenario from
the QTBUG-149512 fix linked above. It fragments the heap, allocates ordinary JS
objects from a managed object's destroy callback, roots them with PersistentValue,
then inspects the allocator's actual allocation/extent bits and free lists. It
does not alter those bits or patch Qt. A before-sweep allocation row is the control.

Qt Creator build `1793559335275`, run `1793559335422` (2026-11-04, stock
Qt 6.12.0, macOS arm64):

| Case | Lost live extent slots | Free runs beginning inside live objects |
|---|---:|---:|
| Allocate before sweep: 256 rooted objects | 0 | 0 |
| Allocate during sweep: 8 rooted objects in the word being swept | 8 | 8 |

The during-sweep row correctly fails. `timeLimit=0` disables incremental marking,
so this is a distinct allocator defect that does not require a missing incremental
write barrier. The test stops before intentionally allocating over the corrupt
free entries. It establishes a mechanism for subsequent live-memory reuse on this
SDK, **not an application trigger or a reproduction of #2179**. The Android library
has not been runtime-tested with this scenario. The standalone suite now has two
deliberately failing CTest entries: ownership and sweep allocation.

The follow-up experiments targeted:

- Context and callback destruction during GC, including the historical Shot Map's
  XMLHttpRequest callback and Canvas repaint path. At tag `v2.0.8`,
  `ShotMapScreensaver.qml:59–80` owns an XHR closure; current main instead receives
  host service data, so replaying current code alone misses that historical path.
- Errors and their stack traces during component creation/destruction. #2179's
  ReferenceErrors and the older ErrorObject victims make this worth a controlled
  comparison; they do not prove that the `pa` error caused native corruption.
- Large QObject-wrapper populations and partial weak scans. Qt 6.12.0's
  `qv4mm.cpp:671,705–736` scans weak handles in batches of 1,024. Ordinary JS
  allocation pressure does not guarantee this many weak QObject wrappers. Create
  and retire subtrees between batches, recording ownership/top-parent state and
  wrapper marks, rather than only pausing before the first batch.

### Destruction, partial scans, incubation, and old XHR callbacks

Further Qt Creator results on the same stock macOS Qt 6.12.0 SDK:

| Probe | Evidence | Result |
|---|---|---|
| Ordinary `Component.onDestruction` allocations | Build `1793559335286`; 100 rounds, 16 objects each | All 1,600 handlers ran in `CallDestroyObjects` (stage 11), before `DoSweep` (15); no mismatch |
| Partial weak scan with 8,192 QObject wrappers | Same build; 12 cases, 1/3/5 batches, optional retirement of 4,096 objects, with/without three `pa` errors per page | 1,200 pages / 6,000 animation and timer wrappers marked correctly; 1,800 expected ReferenceErrors |
| Asynchronous QML incubation | Build `1793559335284`, run `1793559335426` | 1,000 completed review subtrees and 10 cancellations; 93,060 collections while loading; no mismatch |
| Historical XHR closure lifetime | Build `1793559335293`, run `1793559335429` | 400 requests, 1,200 validations/sweeps; valid JSON, invalid JSON, and page deletion before completion all finish without a GC mismatch |

The partial-scan test confirms that only 29, 1,060, or 3,108 of the population's
wrappers have been marked when construction starts. This actually exercises a
partially traversed weak list, rather than only pausing at its first instruction.

`ProbeNetwork.h` supplies in-memory QNetworkReply objects: the XHR test makes no
network requests. It preserves the self-capturing closure from v2.0.8's
`ShotMapScreensaver.qml:59–80`, with response delivery at five GC marking stages.
Deleting the page does **not** suppress its callback in this fixture. Valid JSON
then produces a caught `Error: Invalid write to global property "shots"`;
invalid JSON produces a caught SyntaxError. Qt's
`qml/qqmlxmlhttprequest.cpp:1048–1049,1612–1622` holds a persistent JS wrapper and
a ref-counted context and checks whether that context pointer is null. That is
not a test that the original page QObject still exists. This is another source
of error objects, not evidence of native corruption. The fixture omits the map's
Canvas and services.

The diagnostic-patch audit found that the first bitmap comparison precedes the
extra tracing pass. The extra trace cannot itself generate that first mismatch.
Its root path records the **later** stop-the-world mark, not the original weak
scan's ownership history. Therefore `rootStage=MarkWeakValues` does not establish
whether a target was skipped, became C++-retained after its original visit, or
lost an already-set mark bit. A useful next field diagnostic must distinguish
those cases by retaining the original visit's wrapper, top-parent, ownership,
and mark-bit state. None of these experiments establishes #2179's cause.
The later patched-library validation is documented below.

### Weak-scan history and allocator fix validation

`qt-6.12.0-gc-weak-history.patch` applies **after** the existing diagnostics patch.
It keeps bounded, per-engine records of original incremental weak visits, suppresses
updates during both later full marks, and adds `WEAK_HISTORY` lines only when a
QObject-wrapper mismatch is reported. Each line includes the last original visit's
QObject/top-parent addresses, ownership flags, keep-alive decision, and before/after
mark bits, alongside the current addresses and flags. Flags are bit 0 = QQmlData
exists, bit 1 = C++ ownership, bit 2 = root under construction. `seen=0` is not
proof of a skipped visit if `incomplete=1`; address reuse also limits inference.

The new controls now run against a complete patched macOS QtQml build with test
hooks. Qt Creator build `1793559335304` and run `1793559335431` pass the diagnostic
suite, including ownership promotion after scanning, deliberately erased marks,
rescan repair, preservation of validated marks, and deferred abort. The fault
injection remains confined to test children and must not be compiled into Android.
A compact weak-history summary precedes the retaining path in `DEFERRED_ABORT`,
so the crash reporter's 512-byte message limit preserves the key distinction.

`qt-6.12.0-sweep-allocation-fix.patch` applies third and backports QTBUG-149512.
It preserves allocations made during destruction instead of overwriting their
bitmap entries. Qt Creator run `1793559335430` passes `tst_qtgc_fixed_sweep`:
the same during-sweep scenario that lost eight extent slots and created eight
free runs inside live objects on stock Qt now reports **zero of each**. The
256-object before-sweep control also passes. All three patches apply in order to
fresh SDK source and reproduce the source used for these builds byte-for-byte.
These results validate the allocator fix and diagnostics on macOS; they do not
establish the cause of #2179 or substitute for Android runtime validation.

For the separate allocator candidate, the upstream `6.12` branch contains the
QTBUG-149512 fix as of 2026-11-04 and its `.cmake.conf` identifies version
`6.12.1-alpha1`. The latest published tag checked was `v6.12.0`. Thus **6.12.1 is
the current target, not a released or proven #2179 fix**. An Android comparison
with and without that fix is still needed.
