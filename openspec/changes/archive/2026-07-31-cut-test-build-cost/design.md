## Context

The proposal establishes the measurements: `tests/` is 40% of a clean build (1,956 s of 4,846 s
cpu), the suite doubled in July 2026, 215 s of production-source compilation is pure duplicate work,
and the documented cost model points at link cost, which is 3%.

Three properties of the current shape constrain any fix:

1. **Each test is its own executable.** 106 targets, each linking `decenza_testlib` and Qt. Link is
   cheap — 58 s total, 3.0% — but the compile fan-out is not: 17 production sources are compiled
   into more than one target.
2. **The suite is built and run through Qt Creator, not a shell.** `CLAUDE.md` forbids an assistant
   from invoking `cmake`/`ctest` directly. Anything that depends on a runner flag depends on
   per-developer Qt Creator configuration and is not durable.
3. **There is exactly one PR-time CI slot**, `text-invariants.yml`: build-free by design, pure
   Python over the source, ~1 minute. CLAUDE.md names it as the shape a new PR-time check has to
   fit, and states plainly that a check needing the app built does not belong there.

## Goals / Non-Goals

**Goals:**

- Remove duplicate compilation of production sources and Qt resources across test targets, without
  making incremental builds worse.
- Keep the duplication from returning, by a check rather than by guidance.
- Correct the documented cost model, and give the write-time decision ("should this test exist?") a
  stated bar.

**Non-Goals:**

- Deleting any test, or defining when a test may be deleted.
- Collecting per-test outcome history. See Decision 4 — this was specified, then cut.
- Coverage instrumentation or mutation testing.
- Reducing test *run* time. The suite is ~31 s; that is not the problem.
- Consolidating test targets to reduce their count. Target count drives 3.0% of the cost, and
  consolidation does not touch the 625 s of test-body compile.

## Decisions

### 1. Deduplicate into narrow intermediate libraries, not into `decenza_testlib`

The obvious fix — move the nine-times-duplicated sources into the shared `decenza_testlib` — is **a
wash on incremental builds**, and incremental builds are where a developer waits.

Measured, not predicted — `touch src/controllers/profilemanager.cpp` then rebuild, cpu from the
`.ninja_log` diff, `tests/`-attributable only. Compile 6.79 s; relink 1.42 s for these nine targets;
0.53 s mean across all 107; `libdecenza_testlib.a` archive 0.32 s:

| Scenario | Today (9 duplicate compiles) | Into `decenza_testlib` | Into a narrow library |
|---|---|---|---|
| Clean build | 72.6 s wasted | **0 s** | **0 s** |
| Touch `profilemanager.cpp` | **77.0 s** (measured) | ~67.0 s | **~23.0 s** |

Widening the shared library trades a bounded compile fan-out for a link fan-out across **every** test
target: 9 relinks become 107. It still comes out ahead of today — link is cheap enough that 107 of
them cost less than 8 redundant compiles — but it recovers only a seventh of what a narrow library
does, and its relink fan-out grows with every test target added while the narrow one's does not.

Recorded because the first draft of this table was wrong in a way worth not repeating: it predicted
13 s for the narrow library by using the 0.53 s all-target link mean, when these nine are among the
heaviest binaries in the suite and relink at 1.42 s each. The prediction for the other two columns
(78 s and 67 s) held. The conclusion did not change, but the margin it rests on is 54 s, not 65 s.

Rule: **an intermediate library's consumer set is never wider than the set of targets that need
it.**

The measured consumer sets collapse further than expected. The three largest sources share one
identical nine-target set, and the two three-target sets are subsets of it, so three libraries cover
175 s of the 215 s.

Alternatives considered:

- **One fat `decenza_testlib`.** Simplest; loses on incremental builds and degrades with growth.
- **Unity/jumbo builds for test TUs.** Attacks the 625 s directly, but breaks per-test isolation of
  static state and makes a single test edit recompile a batch — the same incremental regression in
  another shape.
- **Consolidating test targets.** Saves 3% and no test-body compile.

### 2. Qt resources are compiled once

`qrc_resources.cpp` is compiled in three test targets, 22.4 s of it duplicate. Resource blobs are
static and change rarely, so a shared compilation has no incremental downside. `qrc_emoji.cpp` at
17.7 s in `tst_emojiassets` is not duplicated — it is simply the largest single edge, and whether
that target needs the compiled resource at all is worth one look, not a redesign.

### 3. The duplication rule is enforced by a check, not by guidance

The rule in Decision 1 is exactly the kind that decays: it is invisible in review, violated by a
one-line addition to a source list, and its violation costs nothing today. The tree already contains
a case nobody noticed — `tst_visualizershotparse` compiling `shotanalysis.cpp` which
`decenza_testlib` already carries.

So it goes in `text-invariants.yml`, which is the repo's one PR-time slot and whose constraints this
check satisfies exactly: it reads `tests/CMakeLists.txt` as text, needs no Qt and no compile, and
runs in seconds. This is the same argument that workflow's own header makes about the invariants
already there — "they are checked here because guidance alone did not hold."

**Two objective invariants, both with a correct answer, both currently violated:**

- a production source appearing in two or more test targets' source lists
- a source appearing in a test target's list that `decenza_testlib` already compiles

Deliberately **not** checked: anything requiring judgement. An advisory "this branch adds N seconds
of test build cost" number was considered and cut — it changes no decision on any particular PR, it
would print on every PR that touches tests, and a report nobody must act on is noise by the same
standard applied to Decision 4.

This also replaces what an earlier draft aimed at: putting the cost term into `pr-test-analyzer` and
the OpenSpec task templates. Those live outside the repo — a global plugin cache overwritten on
update, and the openspec CLI's own templates. Editing them would put a Decenza-specific number into
tools shared across every project. A check in the repo is agent-agnostic: it holds whether the test
was added by an assistant, by a maintainer, or by a contributor using no agent at all.

### 4. No outcome history is collected

An earlier draft of this change specified a durable per-test-function record of which tests have
ever failed, to make "has this test ever discriminated?" answerable later. It is cut.

It failed its own test. Checked against the problem this change exists to fix:

| Trait of the problem | Present in the ledger? |
|---|---|
| Grows monotonically | Yes — rows only ever added |
| Never retired | Yes — a row for a deleted test would persist |
| Costs on every run, forever | Yes — a hook in all 2,761 test functions |
| Value never re-measured | Yes — nothing would ever ask if it earned its keep |

The delivery mechanism made it worse: a macro in all 107 test classes plus a meta-test enforcing the
macro, which is new test infrastructure added to fix a problem caused by too much test
infrastructure.

And it influenced no decision *in this change*. This change ships no retirement policy by design, so
the ledger's entire value was deferred to a conversation that may not happen — speculative
machinery.

If that conversation becomes live, the data needs no new code: ctest writes
`Testing/Temporary/LastTestsFailed.log` and `LastTest.log` on every run today. Build the ledger then,
from what exists, to answer a question someone is actually asking.

Recorded here rather than deleted silently, so the same design is not re-proposed from scratch.

### 5. Deduplication is verified by measurement, and reverted if it does not measure

The clean-build saving is a number, not an argument: 215 s of duplicate compiles plus 22 s of
duplicate qrc, against a 4,846 s baseline. If a before/after clean rebuild does not show it, the
restructuring is wrong and is reverted independently — nothing else in this change depends on it.

The incremental case in Decision 1 is a *prediction* and must be measured separately, because it is
the only thing distinguishing a narrow library from the shared one.

## Risks / Trade-offs

- **The value bar is prose, and prose already failed here.** `TESTING.md` carried a cost warning
  through the month the suite doubled. → Acknowledged rather than mitigated: the prose half is a bet,
  stated as one in the proposal, with the September ratio as its check. The structural half does not
  depend on it.

- **Narrow libraries add CMake targets.** More build graph to understand. → Bounded by the measured
  consumer sets: three libraries cover 175 s of the 215 s, and the rule ("no wider than needed")
  prevents proliferation by construction.

- **A check that only fails on duplication permits the suite to keep growing.** It stops
  regression on one axis, not growth on the axis that dominates. → True and deliberate. Growth is
  the prose half's problem; encoding a growth budget would require a threshold nobody can defend.

- **Two of the sources are two-consumer cases worth ~2–3 s each.** Building a library for those
  costs more understanding than it saves. → The check reports them; whether each earns a library is
  a judgement made per case, and leaving a 2 s duplicate in place with the check failing is not an
  option — so either it moves, or the target's list drops it, or the check needs a documented
  exception with the reason.

## Migration Plan

No runtime behaviour and no user-visible surface changes, so nothing rolls out or back in the
shipped app. Verification is a clean-rebuild diff against `baseline.md`. If the numbers do not move
as predicted, the CMake work is reverted on its own.

## Open Questions

- Whether `tst_emojiassets` needs the compiled emoji resource at all, or can assert against the
  source tree via `DECENZA_SOURCE_DIR` as other tests do. Worth one look; not worth a redesign.
- Whether the two-consumer duplicates earn libraries or an exception list, per the risk above.
