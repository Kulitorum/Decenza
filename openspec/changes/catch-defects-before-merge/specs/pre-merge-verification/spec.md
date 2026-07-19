## ADDED Requirements

### Requirement: Pull Requests Are Compiled by CI
Every pull request targeting `main` SHALL be compiled by a GitHub Actions workflow before it can be merged. A pull request that does not compile SHALL fail its checks.

Prior to this change no `pull_request`-triggered workflow invoked a compiler: all six platform workflows triggered only on `workflow_dispatch` and `push: tags: v*`, and the sole `pull_request` workflow (`text-invariants.yml`) ran Python text-linters. A compile error could therefore reach `main`, and did reach a release tag.

#### Scenario: Pull request that does not compile
- **WHEN** a pull request is opened or updated with code that fails to compile
- **THEN** the pre-merge workflow runs on the `pull_request` event, the build step fails, and the pull request shows a failing check

#### Scenario: Pull request that compiles
- **WHEN** a pull request is opened or updated with code that compiles cleanly
- **THEN** the build step succeeds and the workflow proceeds to run the test suite

### Requirement: Full Test Suite Runs Before Merge
The complete `ctest` suite SHALL run on every pull request, not only on a release tag push. A failing test SHALL fail the pull request's checks.

The suite already exists — 83 targets registered through `add_decenza_test` — but `ctest` is invoked in exactly one workflow (`linux-release.yml`) behind a tag-push trigger, so it runs after merge, once per release.

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
