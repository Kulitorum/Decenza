## Context

The measured starting state (verified against the tree at `23042b54`):

| Signal | Where it lives today | When it runs |
|---|---|---|
| Compile | 6 platform workflows | `workflow_dispatch`, `push: tags: v*` |
| 83-test `ctest` suite | `linux-release.yml` only | same tag-push trigger |
| Text linters | `text-invariants.yml` | `pull_request` — the only such workflow |
| ASan | `CMakeLists.txt` ~200-217 | local non-Apple Debug builds; never in CI |
| UBSan | nowhere | never |
| Warning policy | `-Werror=unused-result` only | every build, since #1553 |

So the only automated check a pull request receives is three Python scripts that read QML as text.

Two incidents motivate the shape of this design rather than just its existence:

1. **#1553 took 53 commits.** The dominant defect class was a writer that could not report failure paired with a caller that announced success. Six review rounds each found a new instance in the previous round's fix. It was eventually addressed structurally with `[[nodiscard]]` plus a flag — which is the point: it became mechanically detectable only once the codebase annotated its own invariant.

2. **The flag from #1553 broke the iOS release build.** It was verified on macOS and Android; the offending call sat behind `#ifdef Q_OS_IOS`. Then the review of *that* fix found the same defect on the OpenSSL branch, where `-Werror=unused-result` had said nothing, because OpenSSL does not annotate `RAND_bytes`.

Read together: compiler enforcement is real but narrow and platform-shaped, and the project's actual gap is that nothing at all runs before merge.

## Goals / Non-Goals

**Goals:**
- **`-Wall -Wextra -Werror` enabled by default on all six platforms.** This is the end state this change commits to, not an aspiration filed elsewhere. The staged path below is how it is reached without a six-platform outage; it is not a substitute for reaching it.
- Compile every pull request and run the existing 83 tests against it, before merge.
- Run those tests under UndefinedBehaviorSanitizer, as the highest-yield detector whose reach does not depend on third-party annotations.
- Make the already-written ASan configuration actually execute somewhere.
- Drive the warning backlog to zero, tracked by an explicit exemption list that only ever shrinks.
- **Add no significant time to builds.** A developer's incremental build must not get measurably slower, and the pre-merge CI job must stay fast enough that nobody is tempted to merge without waiting for it. A gate people route around is not a gate.
- Record why compiler enforcement is not a completeness guarantee, so a green build is not misread.

**Non-Goals:**
- Flipping `-Wall -Wextra -Werror` on in a single commit ahead of the burndown. The end state is a goal; a big-bang switch is the failure mode, because it converts an unmeasured backlog into a simultaneous outage on six platforms with no way to tell the diagnostic that mattered from the noise.
- clang-tidy or static analysis. Higher setup and noise cost; revisit once the warning picture is known.
- Fixing what the *sanitizers* find as part of this change. UBSan findings are runtime defects in application code and are separate work; the warning backlog, by contrast, is explicitly in scope because `-Werror` cannot be reached without clearing it.
- Building all six platforms per pull request. Cost and latency are prohibitive; see the platform-coverage decision below.

## Decisions

### One new workflow, doing build + test + UBSan together

Rather than a bare "run tests on PR" job now and sanitizers later, the first job is instrumented from the start. The user's stated priority is UBSan on the existing suite, and there is no reason to pay for two separate CI wire-ups. `text-invariants.yml` is the working model for a `pull_request`-triggered job in this repo.

*Alternative considered:* add UBSan to the existing `linux-release.yml` test step. Rejected — it would run at tag time, which is after merge, and so would not prevent anything. It also couples sanitizer runtime to release latency.

### Linux as the pre-merge platform

The pre-merge job builds Linux x64 only. It is the cheapest runner, it is the platform where `ctest` already runs, and sanitizer support is best there.

The honest cost: this is exactly the blind spot that broke iOS. A Linux-only gate cannot see `#ifdef Q_OS_IOS` or `Q_OS_WIN` code. The mitigation is not to build six platforms per PR — it is the six-platform evidence requirement in the `compiler-diagnostics` spec, which applies specifically to the change class that has burned us (promoting a diagnostic), plus keeping the tag-push builds as the full-matrix check.

*Alternative considered:* matrix over all six. Rejected on cost and latency; a gate people route around is worse than a narrower gate they trust.

### UBSan first, ASan second

UBSan is cheaper (roughly 1.2-2x versus ASan's ~2x plus a large memory multiplier), and the defect classes it finds — signed overflow, bad shifts, misaligned loads, invalid enum and bool values — are plausible in a codebase doing DER encoding, BLE byte-wrangling, and fixed-point scale arithmetic. ASan's findings are more severe when they occur but its runtime cost is harder to fit in a per-PR budget.

So: UBSan in the pre-merge job; ASan on a nightly schedule, or per-PR only if the measured runtime allows. The decision is deferred to the implementation task that measures it, rather than guessed here.

### Sanitizer must halt, not warn

By default UBSan prints a diagnostic and continues, so an instrumented run can report undefined behaviour and still exit zero. Set `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` so a finding actually fails the job. A detector that reports into a passing build is the same silent-failure shape this whole effort exists to remove.

### `-Wall -Wextra -Werror` goes on immediately, with a shrinking exemption list

Enable the full flag set on day one. For each diagnostic class that currently has occurrences, add a matching `-Wno-<name>` to a single, clearly-labelled block in `CMakeLists.txt`. Everything not on that list is a hard error from the first commit.

The list is the backlog. Its length is the remaining work, it is visible in the build file rather than a CI artifact, and each stage of the cleanup is one commit that deletes one entry and fixes that class across the tree. The change completes when the block is empty and can be deleted — there is no separate switch-flipping event at the end, because the flags have been on the whole time.

This is why there is **no advisory warnings job**. An earlier draft had one, plus a count baseline and a ratchet that failed CI if the total rose. All three are unnecessary:

- *Measuring the backlog* is a one-off. Run `-Wall -Wextra` once per platform, read the diagnostic names, write the `-Wno-` list. That is a task, not a standing job.
- *The ratchet* is subsumed. Any class not on the exemption list is already `-Werror`, so new warnings in cleared classes fail the build immediately — strictly stronger than failing when a count rises, and with no baseline file to maintain or race.
- *A dashboard nobody acts on* was the real risk. A number in a CI log has no forcing function; a `-Wno-` line in the build file is visible to everyone who opens it and is obviously debt.

The one thing the exemption list does not cover is a *new* warning in a class that is still exempt. That is an accepted gap: it is bounded by how long the list survives, and closing it would need the count-baseline machinery this decision exists to avoid.

*Alternative considered:* advisory job, baseline count, ratchet, then a blanket flip at zero. Rejected as above — more moving parts, weaker enforcement during the burndown, and it ends with the single highest-risk commit of the whole effort.

*Alternative considered:* fix everything first, then enable. Rejected — leaves the tree unprotected throughout, so cleared classes silently refill.

### Build time is a constraint, not an afterthought

`-Wall -Wextra` costs effectively nothing: the analyses run during normal semantic analysis whether or not the diagnostics are printed. The real build-time risks in this change are the sanitizer and the new CI job, and both are scoped accordingly:

- **UBSan is opt-in and off by default.** `ENABLE_UBSAN=ON` is set by the CI job and by developers who want it; a normal local build is byte-for-byte as fast as today.
- **The pre-merge job is one platform with ccache**, and `cancel-in-progress` kills superseded runs. Task 2.1 measures the instrumented suite before the job is allowed to become a required check, and 4.4 forces an explicit decision if it blows the budget rather than letting it drift into a long wait.

### The two slow tests set the budget

`tst_dbmigration` and `tst_coffeebags` run ~350 s each and dominate the ~6 minute suite; `tst_settling` and `tst_decentscalewifi` are documented as timing-sensitive and already need `--repeat until-pass:3`. Under UBSan all of this gets worse. The implementation must measure the instrumented runtime first and, if it blows the budget, choose deliberately between a longer job, a subset for the sanitized run, or moving the slow pair to nightly — recording which, rather than letting the job quietly become a ten-minute wait.

## Risks / Trade-offs

- **Sanitizer slowdown makes the flaky tests flakier** → The two clock-cadence tests already flake under CPU contention. Measure instrumented runtime before enabling; keep `--repeat until-pass:3`; if a test becomes unreliable under instrumentation, exclude that specific test from the sanitized run with a comment naming it, rather than dropping the repeat guard or the sanitizer.

- **Linux-only gate misses platform-guarded code** → Accepted and stated openly; this is the known hole. Tag-push builds remain the full-matrix check, and the six-platform rule covers diagnostic promotion, which is the case that actually caused a release break.

- **UBSan finds a pile of pre-existing undefined behaviour on day one** → Likely, given no sanitizer has ever run here. If the initial run is not clean, the job lands non-blocking with its findings filed, and flips to blocking in a follow-up once they are fixed. Do not merge a blocking job that is already red, and do not suppress findings to make it green.

- **Cache churn against the 10 GB repo cap** → A per-PR job adds ccache generations. `prune-caches.yml` fires on build-workflow completion and skips while builds are running; confirm the new workflow is covered by that logic, or the cap gets hit and every build goes cold.

- **CI cost rises** → A build per PR where there was none. Mitigated by one platform, ccache, and `concurrency` with `cancel-in-progress` so superseded pushes stop immediately, matching the existing workflows.

- **False confidence** → The largest non-technical risk. A green pre-merge badge invites the belief that the code is verified, when the gate is one platform, one configuration, and a test suite of unknown coverage. The `compiler-diagnostics` spec requires this limitation to be written down; it should also be said plainly in the workflow file's own header comment, the way `text-invariants.yml` explains what its checks do and do not protect.

## Migration Plan

1. Land `ENABLE_UBSAN` in `CMakeLists.txt` — inert until requested, no behaviour change for existing builds.
2. Add the pre-merge workflow **non-blocking** (not a required check) and observe it over real pull requests: read the runtime, the flake rate, and the initial UBSan findings.
3. Fix or file whatever the first runs surface.
4. Make it a required check once it is green and its runtime is known.
5. Advisory warnings job, then read its output before proposing any `-Werror` promotion.

Rollback at any step is deleting a workflow file or setting the option default back to `OFF`; nothing here changes application code or release behaviour.

## Open Questions

- Instrumented suite runtime — unknown until measured. Determines whether ASan can be per-PR or must be nightly, and whether the full suite fits the sanitized run.
- Is the initial UBSan run clean? Determines whether step 4 is days or weeks away.
- Does `prune-caches.yml`'s `workflow_run` trigger already cover a new workflow, or does it need naming explicitly?
- Should the pre-merge job also run the three text-invariant scripts, folding `text-invariants.yml` into it, or stay separate? Separate keeps the fast linters fast; folding reduces workflow count.
