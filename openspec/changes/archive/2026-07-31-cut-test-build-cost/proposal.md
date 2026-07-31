## Why

The test suite is growing faster than the code it covers, and it is now 40% of every build.

Measured from a clean rebuild (`.ninja_log`, macOS Debug, 4,846 s cpu total): `tests/` accounts for
**1,956 s, 40%**. Measured from git: `tst_*.cpp` went from **20 files / 357 KB** in April 2026 to
**107 files / 3,080 KB** on 31 July — and 47 of those files, half the suite, landed in July alone
(+33,927 lines added, 1,543 removed, against 53,243 lines added to `src/`). Two test files have ever
been deleted in the project's history, one of them only because its feature was removed.

The existing guidance is real but points at the wrong variable. `TESTING.md` says a test costs "a
compile *and a link*", which reads as though target count were the driver. It is not:

| Slice of the 1,956 s | Cost | Share | Scales with |
|---|---|---|---|
| Production sources recompiled inside test targets | 688 s | 35.2% | the source list passed to `add_decenza_test` |
| Test source TUs | 625 s | 31.9% | **how much test code exists** |
| moc / autogen | 519 s | 26.5% | both |
| qrc | 66 s | 3.4% | resource-linking targets |
| Link + timestamps | 58 s | **3.0%** | target count |

Link is 3%. And relocating test code between targets does not reduce the cost of compiling that
code — moving 208 test functions into `tst_profilemanager.cpp` pays the same compile under a
different name. So the advice most likely to be followed is aimed at the smallest slice, while the
two largest go unmentioned.

Underneath that, 46 object files are pure duplicate work: 17 production sources compiled into more
than one test target, **215 s**, plus **22 s** of `qrc_resources.cpp` compiled three times. Two of
the five slowest edges in the entire build are the same file compiled into different targets. One
source, `src/ai/shotanalysis.cpp`, is already in `decenza_testlib` and compiled a second time by a
target that already links it — nothing detects that today.

## What Changes

- **No production source is compiled into more than one test target.** Shared sources move to
  narrow intermediate libraries linked by exactly the targets that need them — not to the
  universally-linked `decenza_testlib`, which would trade a compile fan-out of nine for a link
  fan-out of 106 and is measurably worse on incremental builds. `qrc_resources.cpp` stops being
  compiled three times.

- **A build-free PR check enforces that**, in `text-invariants.yml`, the repo's one PR-time slot
  and the shape CLAUDE.md says a new PR-time check has to fit. It fails on two objective
  invariants with correct answers: a production source compiled into two or more test targets, and
  a source recompiled by a target that already links it via `decenza_testlib`. Both are violated in
  the tree today.

- **A write-time value bar.** A new test states what defect shape it catches that the existing suite
  does not. "More cases of the same shape" is insurance, not detection — legitimate, but argued on
  risk and named as such. This complements the existing falsifiability rule, it does not replace it.

- **One invariant, one test.** The first explicit anti-overlap rule this repo has had.

- **The cost model in `TESTING.md` and `CLAUDE.md` is corrected** to the measurements above.

Deliberately **out of scope**: any retirement policy, deletion quota, coverage tooling, or
collection of per-test outcome history. An earlier draft of this change specified a durable ledger
of which tests have ever failed. It was cut, because it had the same shape as the problem — it grew
monotonically, was never retired, cost something on every run forever, and had no mechanism to
re-measure its own value. It would also have added a hook to all 2,761 test functions and a
meta-test to enforce the hook: new test infrastructure to fix a problem caused by too much test
infrastructure. Its value was entirely deferred to a retirement decision this change does not make.
When that decision is live, ctest already writes `Testing/Temporary/LastTestsFailed.log` on every
run and the data can be read from what exists.

## Capabilities

### New Capabilities

- `test-suite-cost`: where test build cost actually sits, what a new test must justify before it is
  kept, the one-invariant-one-test rule, and the enforced structural rule that no production source
  is compiled into more than one test target.

### Modified Capabilities

None. `build-config` and `change-verification` cover build instrumentation and the deliberate
absence of a pre-merge CI gate; neither carries requirements about what the suite costs or how it
grows. The new PR check is build-free and so does not disturb `change-verification`'s reasoning
about why build-dependent gates stay off the critical path.

## Impact

- `tests/CMakeLists.txt` — narrow intermediate libraries absorb the duplicated production sources;
  `qrc_resources.cpp` is consolidated; `tst_visualizershotparse` drops its redundant
  `shotanalysis.cpp` entry. No test source changes, no test deleted, no coverage lost.
- `scripts/check_test_source_duplication.py` (new) and `.github/workflows/text-invariants.yml` — the
  PR check. Pure Python over `tests/CMakeLists.txt`, no Qt, no compile, consistent with the five
  checks already there.
- `docs/CLAUDE_MD/TESTING.md` — corrected cost model; value bar; one-invariant-one-test; the
  narrow-library rule.
- `CLAUDE.md` — the same corrections, since it repeats the "compile *and* a link" framing.

Expected build effect, verifiable by diffing `.ninja_log` across clean rebuilds before and after:
**−237 s cpu**, 4,846 s → ~4,609 s, **−4.9%**. The incremental case is measured separately and is
what justifies narrow libraries over widening `decenza_testlib`.

The growth-rate effect of the value bar is a bet and **cannot be verified at merge**. `TESTING.md`
already carried a cost warning and July doubled the suite anyway, so more prose is not expected to
behave differently on its own; what is different is that the duplication half is now enforced by a
check rather than by whether someone remembered. The first signal on the prose half is the September
ratio of test lines to source lines, recorded in `baseline.md` with the exact command.
