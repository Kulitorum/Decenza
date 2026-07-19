## Why

PR #1553 took **53 commits** to land, and six rounds of agent review plus roughly an hour of live clicking to stabilise. Most of those commits were not new work — they were repairs to the previous commit, of a small number of mechanically detectable defect classes. The follow-up PR #1558 then repeated the pattern in miniature: a build-breaking defect reached a release tag, and the review of the fix found the same bug class four more times in the same file.

The reason these reach review (or a release) rather than a developer's terminal is that **almost nothing runs before merge**. Measured on the current tree:

- **No pull request is ever compiled by CI.** All six platform workflows trigger only on `workflow_dispatch` and `push: tags: v*`. The single `pull_request`-triggered workflow, `text-invariants.yml`, runs three Python text-linters and never invokes a compiler.
- **The 83-test suite runs once per release.** `ctest` appears in exactly one workflow — `linux-release.yml` — behind the same tag-push trigger. A pull request can therefore merge to `main` with a failing test suite, and nobody learns until a release build.
- **There is no warning policy.** `-Werror=unused-result` (added by #1553) is the *only* compile option in `CMakeLists.txt`. Otherwise the build inherits generator defaults, and on iOS the Xcode defaults actively suppress about fifteen diagnostic classes (`-Wno-shadow`, `-Wno-conversion`, `-Wno-non-virtual-dtor`, `-Wno-unused-parameter`, …).
- **ASan is configured but never runs in CI**, and **UBSan does not exist** anywhere in the tree.

The compiler is not doing a worse job than before; it is doing the job on the developer's machine only, on one platform, after the code is already written. Cheap automated checks exist and are simply not wired to the moment that matters.

## What Changes

- Add a `pull_request`-triggered CI job that **builds the project and runs the full test suite under UndefinedBehaviorSanitizer**. This is the top priority: it introduces pre-merge verification and UBSan in one step, and it makes the 83 existing tests into a gate rather than a release-time formality.
- Add an **`ENABLE_UBSAN` CMake option**, alongside the existing `ENABLE_ASAN` block, so the same instrumented run is reproducible locally.
- Run the existing **ASan** configuration in the same pre-merge job (or a scheduled companion), so the already-written instrumentation stops being dead configuration.
- Turn on **`-Wall -Wextra -Werror` by default immediately**, carrying the diagnostic classes that currently have occurrences as an explicit, labelled `-Wno-<name>` block in `CMakeLists.txt`. Everything else is a hard error from the first commit. That block is the backlog: entries are only ever removed, one class per pull request, and the change is complete when it is empty and deleted. There is deliberately **no advisory warnings job** — a count in a CI log has no forcing function, and the exemption list does the same job where people can see it.
- Keep builds fast. The diagnostics themselves cost nothing; sanitizer instrumentation stays **off by default** so routine local builds are unchanged, and the pre-merge job is one cached platform with a measured time budget.
- Verify every flag change on **all six platforms** before it lands. #1553's flag was verified on macOS and Android only, and broke the iOS release build on code behind `#ifdef Q_OS_IOS`.
- Document the **limits of compiler enforcement**, so it is not oversold. `-Werror=unused-result` caught `SecRandomCopyBytes` (Apple annotates it `warn_unused_result`) and was silent on the identical `RAND_bytes` bug (OpenSSL does not annotate it) — on the branch five of six platforms build. A diagnostic's coverage is bounded by third-party annotations, so a clean build is not evidence of a clean codebase.

Explicitly **not** in scope: adding clang-tidy or static analysis, fixing the runtime defects the sanitizers surface (separate work), and declaring the change finished while any exemption remains.

## Capabilities

### New Capabilities
- `pre-merge-verification`: pull requests are compiled and their tests executed by CI before merge, with the sanitizer configuration and failure semantics that gate applies.
- `sanitizer-coverage`: which sanitizers are available as CMake options, which run in CI and when, and how a sanitizer finding is triaged versus a test failure.
- `compiler-diagnostics`: the project's warning policy — what is advisory, what is an error, and the cross-platform evidence required before a diagnostic is promoted to `-Werror`.

### Modified Capabilities
- `build-config`: gains the requirement that CI verification is not release-gated — the existing spec describes the six platform workflows as tag-triggered, which this change makes no longer the whole story.

## Impact

- **`CMakeLists.txt`** — new `ENABLE_UBSAN` option beside the existing ASan block (around lines 200-217); the block currently auto-enables ASan for non-Apple Debug builds and must keep that behaviour.
- **`.github/workflows/`** — one new pre-merge workflow; `text-invariants.yml` is the existing model for a fast `pull_request` job. Note the ccache/sccache interaction: `prune-caches.yml` fires on build-workflow completion, and a new frequently-running job will add cache churn against the 10 GB repo cap.
- **`tests/CMakeLists.txt`** — 83 tests currently register via `add_decenza_test`; two (`tst_settling`, `tst_decentscalewifi`) are documented as timing-sensitive under CPU contention and already use `--repeat until-pass:3` in `linux-release.yml`. Sanitizer slowdown (UBSan ~1.2-2x, ASan ~2x) will make that worse and the runtime budget needs checking against the current ~6 minute suite (two tests dominate at ~350 s each).
- **CI cost and latency** — this adds a build to every PR where there was none. The job must be fast enough to not become the thing people wait on, which is the main design constraint.
- **No application code changes.** Any defect these tools surface is fixed as separate work, not as part of wiring them up.
