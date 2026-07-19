## ADDED Requirements

### Requirement: Pull Requests Are Compiled by CI
Every pull request targeting `main` SHALL be compiled by a GitHub Actions workflow, and a pull request that does not compile SHALL show a failing check.

Prior to this change no `pull_request`-triggered workflow invoked a compiler: all six platform workflows triggered only on `workflow_dispatch` and `push: tags: v*`, and the sole `pull_request` workflow (`text-invariants.yml`) ran Python text-linters. A compile error could therefore reach `main`, and did reach a release tag.

The purpose is a **second compiler**, not a second test run. The full suite is already run locally before every pull request, so tests are gated by process; what is not covered is that those local runs happen on macOS/clang, while Linux/GCC, Windows/MSVC, iOS and Android are first compiled at release-tag time, after merge.

#### Scenario: Pull request that does not compile
- **WHEN** a pull request is opened or updated with code that fails to compile
- **THEN** the pre-merge workflow runs on the `pull_request` event, the build step fails, and the pull request shows a failing check

#### Scenario: Pull request that compiles
- **WHEN** a pull request is opened or updated with code that compiles cleanly
- **THEN** the build step succeeds and the workflow proceeds to run the test suite

### Requirement: Verification Is Advisory, Not A Required Check
The pre-merge workflow SHALL report its result as a visible check and SHALL NOT be configured as a required status check for merging.

This is a deliberate decision, not an unfinished migration. The project has three developers who already run the full suite before opening a pull request; a merge-blocking gate would add enforcement machinery where the behaviour it enforces is already the norm. The signal — a red check naming a Linux-only compile failure — is the whole value, and a human deciding what to do about it is sufficient at this size.

This does not contradict the `compiler-diagnostics` rejection of advisory reporting. That rejection targets a job emitting a *number nobody acts on*; a failing check on a specific pull request names a specific break in that pull request's own code, which is actionable on its face.

#### Scenario: Compile failure on a pull request
- **WHEN** the workflow fails on a pull request
- **THEN** the pull request shows a failing check a reviewer can see and act on, and merging is not mechanically blocked

#### Scenario: Proposal to make it required
- **WHEN** someone proposes marking the workflow a required status check
- **THEN** it is weighed against team size and existing practice rather than adopted by default, since the value here is the signal and not the enforcement

### Requirement: Full Test Suite Runs Before Merge
The complete `ctest` suite SHALL run on every pull request, not only on a release tag push. A failing test SHALL fail the pull request's checks.

The suite already exists — 83 targets registered through `add_decenza_test` — but `ctest` is invoked in exactly one workflow (`linux-release.yml`) behind a tag-push trigger, so in CI it runs after merge, once per release.

This is kept because it costs about 45 seconds on top of a build that is happening anyway, not because local testing is doubted: the suite is already run locally before every pull request. What CI adds is running it against a *different toolchain* (Linux/GCC rather than macOS/clang), plus cover for the occasional exception to that norm.

#### Scenario: Pull request breaks an existing test
- **WHEN** a pull request is opened whose changes make any registered test fail
- **THEN** the pre-merge workflow reports a failing check naming the failed test, before the pull request is merged

#### Scenario: Timing-sensitive tests under load
- **WHEN** the suite runs on a loaded CI runner and a clock-cadence test (`tst_settling`, `tst_decentscalewifi`) flakes
- **THEN** the run uses `--repeat until-pass:3` so a transient timing flake does not fail the pull request, while a genuine failure still fails all three attempts

### Requirement: Pre-Merge Verification Is Not Release-Gated
The pre-merge workflow SHALL be independent of the six platform release workflows, and SHALL NOT upload artifacts, bump the version code, or publish to any release.

#### Scenario: Pre-merge run completes
- **WHEN** the pre-merge workflow finishes for a pull request
- **THEN** no GitHub Release is created or modified, `versioncode.txt` is unchanged, and no artifact is uploaded to a release

### Requirement: Pre-Merge Verification Completes Quickly Enough To Be Used
The pre-merge workflow SHALL complete within a time budget that keeps it a gate rather than a bottleneck, and SHALL use compiler caching consistent with the existing workflows.

A job slow enough that contributors merge without waiting for it provides no gate. The instrumented suite is slower than the plain one (UndefinedBehaviorSanitizer roughly 1.2-2x), and two tests already dominate the ~6 minute runtime at roughly 350 seconds each.

#### Scenario: Cached incremental run
- **WHEN** the pre-merge workflow runs for a pull request with a warm ccache
- **THEN** it completes within the documented budget and reports build plus test status

#### Scenario: Suite runtime regression
- **WHEN** a change makes the instrumented suite exceed the documented budget
- **THEN** the budget overrun is visible in the workflow timing and treated as a defect in the change, not absorbed silently
