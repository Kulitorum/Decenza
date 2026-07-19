## 1. UBSan build option

- [x] 1.1 Add an `ENABLE_UBSAN` option to `CMakeLists.txt` beside the existing ASan block (~lines 200-217), adding `-fsanitize=undefined -fno-omit-frame-pointer` to compile flags and `-fsanitize=undefined` to exe and shared linker flags, with the MSVC branch left unsupported (MSVC has no UBSan)
- [x] 1.2 Keep the existing ASan auto-enable behaviour byte-for-byte: Debug + single-config generator + non-Android + non-Apple still turns ASan on with neither option named
- [x] 1.3 Confirm the multi-config guard still holds — configure with Xcode and verify no `-fsanitize` flag reaches a Release build
- [ ] 1.4 Verify locally: configure with `-DENABLE_UBSAN=ON -DBUILD_TESTS=ON`, build, and confirm the flag is on the compile line for a test target

## 2. Measure before committing to a budget

- [ ] 2.1 Run the full `ctest` suite locally with UBSan enabled and record total wall-clock time plus the slowest ten tests, for comparison against the current ~6 min / `tst_dbmigration` ~350 s / `tst_coffeebags` ~350 s baseline
- [ ] 2.2 Record every UBSan diagnostic the first clean run produces — this is the answer to the design's "is the initial run clean?" open question and decides whether the CI job can start blocking
- [ ] 2.3 Run the suite twice more under UBSan to see whether `tst_settling` or `tst_decentscalewifi` flake more under instrumentation than they do plain
- [ ] 2.4 Write the measured numbers into the design's Open Questions section, replacing the placeholders, so the decision trail is not lost

## 3. Pre-merge CI workflow

- [x] 3.1 Create `.github/workflows/pre-merge.yml` triggered on `pull_request` (and `push` to `main`), Linux x64 runner, using `text-invariants.yml` as the structural model
- [x] 3.2 Write the workflow's header comment explaining what it does and does NOT cover — one platform, one configuration — following the precedent set by `text-invariants.yml`'s header
- [x] 3.3 Configure with `-DBUILD_TESTS=ON -DENABLE_UBSAN=ON` and ccache, mirroring the configure step in `linux-release.yml` (lines ~95-103)
- [x] 3.4 Set `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` so a sanitizer finding fails the job instead of printing into a green run
- [x] 3.5 Run `ctest --output-on-failure -j$(nproc) --repeat until-pass:3`, keeping the repeat guard and the comment explaining which two tests need it and why
- [x] 3.6 Add `concurrency` with `cancel-in-progress: true`, matching the existing workflows so superseded pushes stop immediately
- [x] 3.7 Assert the job does NOT upload artifacts, touch `versioncode.txt`, or interact with any GitHub Release
- [x] 3.8 Make the ctest step surface whether a failure was an assertion or a sanitizer diagnostic in the job summary, so the two are distinguishable without reading the full log
- [ ] 3.9 Land the workflow NON-BLOCKING (not a required check) and let it run against real pull requests first

## 4. Promote to a gate

- [ ] 4.1 Watch the job across several real pull requests; record runtime, flake rate, and any UBSan findings
- [ ] 4.2 Fix or file everything the first runs surface — do not suppress a finding to make the job green
- [ ] 4.3 Once it is genuinely green and its runtime is known, mark it a required check for merge to `main`
- [ ] 4.4 If the instrumented runtime blew the budget, pick one of: longer job, sanitized subset, or slow pair moved to nightly — and write down which and why, rather than letting the job silently become a long wait

## 5. ASan gets executed somewhere

- [ ] 5.1 Decide from the 2.1 measurement whether ASan fits per-PR or belongs on a nightly `schedule:` trigger, and record the reasoning
- [ ] 5.2 Wire up the chosen cadence so the existing ASan configuration actually runs instead of remaining dead configuration
- [ ] 5.3 Make the ASan result visible without opening the workflow file to check whether it ran

## 6. Turn on `-Wall -Wextra -Werror`, then shrink the exemption list

- [ ] 6.1 One-off measurement: compile with `-Wall -Wextra` on each of the six platforms and record which diagnostic classes actually occur, and roughly how often (counts differ per platform — iOS's Xcode defaults suppress ~15 classes today)
- [ ] 6.2 Add `-Wall -Wextra -Werror` (and `/W4 /WX` for MSVC) to `CMakeLists.txt` as the default, with one clearly-labelled `-Wno-<name>` block carrying exactly the classes found in 6.1 — no speculative entries for classes with zero occurrences
- [ ] 6.3 Comment that block with what it is (the backlog), the rule that entries are only ever removed, and that the change is done when it is empty
- [ ] 6.4 Verify all six platforms build green with the flags on and the exemptions in place, before this lands
- [ ] 6.5 Negative control: introduce a deliberate diagnostic in a NON-exempt class and confirm the build fails on it
- [ ] 6.6 Order the exempt classes cheapest-and-safest first, and record that as the burndown order
- [ ] 6.7 For each class in turn, one PR each: remove exactly one `-Wno-<name>`, fix every occurrence across the tree, show six-platform green. One entry per PR so a failure is attributable
- [ ] 6.8 Fix rather than relocate — no `#pragma` walls, file-level disables, or per-target `-Wno-` introduced to shorten the block; a single-call-site suppression with a reason comment is the only acceptable survivor
- [ ] 6.9 When the last entry goes, delete the block entirely so `-Wall -Wextra -Werror` stands with nothing carved out of it
- [ ] 6.10 Confirm no advisory/reporting warnings job was added anywhere along the way — the exemption list is the tracking mechanism

## 7. Documentation

- [x] 7.1 Document the six-platform evidence rule for promoting any diagnostic to `-Werror`, citing the `-Werror=unused-result` iOS break as the reason it exists
- [x] 7.2 Document the annotation-coverage limit with the worked `SecRandomCopyBytes` (caught, Apple annotates) versus `RAND_bytes` (missed, OpenSSL does not) pair, so a clean build is not read as a clean codebase
- [x] 7.3 Document the `(void)call();` + reason-comment convention for a deliberately discarded `[[nodiscard]]` result
- [x] 7.4 Update `docs/CLAUDE_MD/CI_CD.md` — it currently describes CI as six tag-triggered workflows, which is no longer the whole picture
- [x] 7.5 Update `docs/CLAUDE_MD/TESTING.md` with how to run the suite under UBSan locally
- [x] 7.6 Add the pre-merge job to the CLAUDE.md build guidance so the next contributor knows a PR gets compiled

## 8. Close out

- [ ] 8.1 Verify each spec scenario in `specs/pre-merge-verification/`, `specs/sanitizer-coverage/`, and `specs/compiler-diagnostics/` against the shipped behaviour, including the negative ones (a deliberately broken build fails the job; a deliberately introduced UB fails the job)
- [x] 8.2 Confirm `prune-caches.yml` covers the new workflow, or name it explicitly, so ccache generations do not accumulate against the 10 GB cap
- [ ] 8.3 Run `/opsx:archive` as the final commit on the feature branch so the archive and spec promotion land inside the PR
