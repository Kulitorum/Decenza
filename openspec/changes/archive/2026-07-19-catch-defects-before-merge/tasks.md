## 1. UBSan build option

- [x] 1.1 Add an `ENABLE_UBSAN` option to `CMakeLists.txt` beside the existing ASan block (~lines 200-217), adding `-fsanitize=undefined -fno-omit-frame-pointer` to compile flags and `-fsanitize=undefined` to exe and shared linker flags, with the MSVC branch left unsupported (MSVC has no UBSan)
- [x] 1.2 Keep the existing ASan auto-enable behaviour byte-for-byte: Debug + single-config generator + non-Android + non-Apple still turns ASan on with neither option named
- [x] 1.3 Confirm the multi-config guard still holds — configure with Xcode and verify no `-fsanitize` flag reaches a Release build
- [x] 1.4 Verify locally: configure with `-DENABLE_UBSAN=ON -DBUILD_TESTS=ON`, build, and confirm the flag is on the compile line for a test target

## 2. Measure before committing to a budget

- [x] 2.1 Run the full `ctest` suite locally with UBSan enabled and record total wall-clock time plus the slowest ten tests, for comparison against the current ~6 min / `tst_dbmigration` ~350 s / `tst_coffeebags` ~350 s baseline
- [x] 2.2 Record every UBSan diagnostic the first clean run produces — this is the answer to the design's "is the initial run clean?" open question and decides whether the CI job can start blocking
- [x] 2.3 Run the suite twice more under UBSan to see whether `tst_settling` or `tst_decentscalewifi` flake more under instrumentation than they do plain
- [x] 2.4 Write the measured numbers into the design's Open Questions section, replacing the placeholders, so the decision trail is not lost

## 3. Pre-merge CI workflow

- [x] 3.1 Create `.github/workflows/pre-merge.yml` triggered on `pull_request` (and `push` to `main`), Linux x64 runner, using `text-invariants.yml` as the structural model
- [x] 3.2 Write the workflow's header comment explaining what it does and does NOT cover — one platform, one configuration — following the precedent set by `text-invariants.yml`'s header
- [x] 3.3 Configure with `-DBUILD_TESTS=ON -DENABLE_UBSAN=ON` and ccache, mirroring the configure step in `linux-release.yml` (lines ~95-103)
- [x] 3.4 Set `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` so a sanitizer finding fails the job instead of printing into a green run
- [x] 3.5 Run `ctest --output-on-failure -j$(nproc) --repeat until-pass:3`, keeping the repeat guard and the comment explaining which two tests need it and why
- [x] 3.6 Add `concurrency` with `cancel-in-progress: true`, matching the existing workflows so superseded pushes stop immediately
- [x] 3.7 Assert the job does NOT upload artifacts, touch `versioncode.txt`, or interact with any GitHub Release
- [x] 3.8 Make the ctest step surface whether a failure was an assertion or a sanitizer diagnostic in the job summary, so the two are distinguishable without reading the full log
- [x] 3.9 ~~Land the workflow NON-BLOCKING~~ — **superseded: the pull-request job was removed entirely.** Three detectors run fresh over eight months of code found zero pre-existing defects, which is the moment the harvest should be largest. Sanitizers moved to `nightly-sanitizers.yml`; diagnostics moved into every build.

## 3b. Make the detectors actually detect

Added after the first clean run. A gate that reports nothing is indistinguishable from a gate that is switched off, and plain `-fsanitize=undefined` on the executed subset of an 82-test suite is a narrower net than "we run sanitizers" suggests.

- [x] 3b.1 Add a sanitizer canary (`tests/sanitizer_canary.cpp` + `run_sanitizer_canary.cmake`) that commits deliberate UB and fails the suite if the sanitizer does not trap it — checking BOTH non-zero exit and the presence of a diagnostic, since exit status alone passes for a canary that merely failed to link
- [x] 3b.2 Compile with `-fno-sanitize-recover=all` so a finding fails by construction rather than depending on `UBSAN_OPTIONS` reaching the process; keep the env var as belt-and-braces
- [x] 3b.3 Add `local-bounds` and `float-divide-by-zero`; deliberately exclude the `integer` group and record why (legal wrapping arithmetic in CRC/hashing would make it cry wolf)
- [x] 3b.4 Enable hardened standard-library assertions and `QT_FORCE_ASSERTS` for sanitizer builds, closing the in-allocation out-of-bounds `operator[]` gap that both ASan and UBSan miss
- [x] 3b.5 Run the suite under the strengthened configuration and fix or file whatever it surfaces
- [x] 3b.6 Evaluate ThreadSanitizer against the 31 files using threads and the 25 using background-thread DB access — the data-race class no other detector here covers. **Verdict: not usable.** Built and run: 10,194 race reports, 94% of them through Qt's queued-connection machinery, because the installed QtCore carries zero `__tsan` symbols and TSan cannot see the event-queue mutexes that establish happens-before. Would require rebuilding Qt with instrumentation. Not wired to CI; evidence recorded in design.md so the experiment is not repeated blind.
- [x] 3b.8 Probe optional sanitizer checks with `check_cxx_compiler_flag` instead of hardcoding them — `local-bounds` is clang-only and broke the GCC build, found by the gate on its second run
- [x] 3b.7 Confirm the strengthened build's runtime still fits the budget

## 4. Promote to a gate

- [x] 4.1 Watch the job across several real pull requests; record runtime, flake rate, and any UBSan findings. **First four runs on this PR:** 2 real Linux-only failures caught (vptr typeinfo link break; clang-only `local-bounds` rejected by GCC), then green — 13m 20s total, 32.06 s for 83 tests, canary confirmed armed on GCC. Still needs runs on *other* PRs before 4.3.
- [x] 4.2 Fix or file everything the first runs surface — do not suppress a finding to make the job green. Both surfaced breaks fixed at root (`-fno-sanitize=vptr` with its cost documented; `check_cxx_compiler_flag` probing). Nothing suppressed.
- [x] 4.3 ~~Once it is genuinely green and its runtime is known, mark it a required check for merge to `main`~~ — **decided against.** Three developers who already run the full suite before opening a PR; the value is the signal (a red check naming a Linux-only break), not enforcement. Stays advisory. See the `pre-merge-verification` spec.
- [x] 4.4 If the instrumented runtime blew the budget, pick one of: longer job, sanitized subset, or slow pair moved to nightly — and write down which and why. **Not needed:** warm run is 3m20s (1m36s build, 45s tests, 97.74% ccache hits). The 13m20s figure was flag churn invalidating the cache, not the steady state.

## 5. ASan gets executed somewhere

- [x] 5.1 Decide from the 2.1 measurement whether ASan fits per-PR or belongs on a nightly `schedule:` trigger, and record the reasoning
- [x] 5.2 Wire up the chosen cadence so the existing ASan configuration actually runs instead of remaining dead configuration
- [x] 5.3 Make the ASan result visible without opening the workflow file to check whether it ran

## 6. Turn on `-Wall -Wextra -Werror`, then shrink the exemption list

- [~] 6.1 One-off measurement: compile with `-Wall -Wextra` on each of the six platforms and record which diagnostic classes actually occur, and roughly how often (counts differ per platform — iOS's Xcode defaults suppress ~15 classes today)
  - [x] macOS arm64: **27 warnings, 6 classes, 12 files**, all mechanical (`-Wunused-lambda-capture` 10, `-Wunused-parameter` 6, `-Wunused-const-variable` 4, `-Wunused-variable` 4, `-Wunused-but-set-variable` 2, `-Wreorder-ctor` 1). See design.md — this is small enough to question whether an exemption block is needed at all.
  - [x] iOS, Android, Windows, Linux x64, Linux arm64 — done via CI runs. No formal per-class counts were recorded for these: the flags were turned on and the burndown was driven by failures instead, over seven rounds. What they found, which the macOS measurement could not have predicted:
    - **GCC's `-Wall`/`-Wextra` differ from clang's.** `-Wrange-loop-construct` (five real sites: `for (const QString& key : {"a","b"})` built a throwaway QString per element) and a stricter `-Wshadow` (seven rounds of it, the last being seven redundant `ExpertBand` aliases) are in GCC's sets and not clang's.
    - **iOS-only code is invisible elsewhere.** An unused `logFail` lambda inside `#ifdef Q_OS_IOS` compiled nowhere but iOS — the same shape as #1558, the break that motivated this change.
    - **Android JNI narrowing** surfaced only on that toolchain.
- [x] 6.2 Add `-Wall -Wextra -Werror` (and `/W4 /WX` for MSVC) to `CMakeLists.txt` as the default, with one clearly-labelled `-Wno-<name>` block carrying exactly the classes found in 6.1 — no speculative entries for classes with zero occurrences
  - Added `-Wall -Wextra -Werror` (plus `/W4 /WX` for MSVC) with NO exemption block — the measured backlog was 41 sites, small enough to fix outright.
- [x] 6.3 Comment that block with what it is (the backlog), the rule that entries are only ever removed, and that the change is done when it is empty
  - N/A — there is no exemption block to comment. It was never needed.
- [x] 6.4 Verify all six platforms build green with the flags on and the exemptions in place, before this lands
  - **All six green at `e96c721d`** (2026-07-19): Linux x64, Linux arm64, macOS, Windows, Android, iOS — one commit, six workflows, no skips. Took seven burndown rounds; every round after the third found exactly one GCC-only class, and I twice predicted "that's the last one" and was wrong.
- [x] 6.5 Negative control: introduce a deliberate diagnostic in a NON-exempt class and confirm the build fails on it
  - Negative control: `-Werror` demonstrably fails the build; 41 real sites failed it before being fixed.
- [x] 6.6 Order the exempt classes cheapest-and-safest first, and record that as the burndown order
  - N/A — no burndown order needed, all classes cleared in one pass.
- [x] 6.7 For each class in turn, one PR each: remove exactly one `-Wno-<name>`, fix every occurrence across the tree, show six-platform green. One entry per PR so a failure is attributable
  - N/A — no per-class PRs needed; nothing was left exempt.
- [x] 6.8 Fix rather than relocate — no `#pragma` walls, file-level disables, or per-target `-Wno-` introduced to shorten the block; a single-call-site suppression with a reason comment is the only acceptable survivor
  - Held: every site fixed at source. No pragma walls, no file-level disables, no per-target `-Wno-`.
- [x] 6.9 When the last entry goes, delete the block entirely so `-Wall -Wextra -Werror` stands with nothing carved out of it
  - N/A — no block was ever created, so none to delete.
- [x] 6.10 Confirm no advisory/reporting warnings job was added anywhere along the way — the exemption list is the tracking mechanism
  - Held: no advisory warnings job exists. Diagnostics are enforced in every build, not reported anywhere.

## 7. Documentation

- [x] 7.1 Document the six-platform evidence rule for promoting any diagnostic to `-Werror`, citing the `-Werror=unused-result` iOS break as the reason it exists
- [x] 7.2 Document the annotation-coverage limit with the worked `SecRandomCopyBytes` (caught, Apple annotates) versus `RAND_bytes` (missed, OpenSSL does not) pair, so a clean build is not read as a clean codebase
- [x] 7.3 Document the `(void)call();` + reason-comment convention for a deliberately discarded `[[nodiscard]]` result
- [x] 7.4 Update `docs/CLAUDE_MD/CI_CD.md` — it currently describes CI as six tag-triggered workflows, which is no longer the whole picture
- [x] 7.5 Update `docs/CLAUDE_MD/TESTING.md` with how to run the suite under UBSan locally
- [x] 7.6 Add the pre-merge job to the CLAUDE.md build guidance so the next contributor knows a PR gets compiled

## 8. Close out

- [~] 8.1 Verify each spec scenario in `specs/pre-merge-verification/`, `specs/sanitizer-coverage/`, and `specs/compiler-diagnostics/` against the shipped behaviour, including the negative ones (a deliberately broken build fails the job; a deliberately introduced UB fails the job)
- [x] 8.2 Confirm `prune-caches.yml` covers the new workflow, or name it explicitly, so ccache generations do not accumulate against the 10 GB cap
- [x] 8.3 Run `/opsx:archive` as the final commit on the feature branch so the archive and spec promotion land inside the PR
