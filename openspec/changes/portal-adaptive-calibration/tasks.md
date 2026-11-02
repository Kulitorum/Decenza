# Tasks

## 1. Measurement and storage

- [x] 1.1 Implement mass integration, gap and scale safeguards; retain existing analyzer tests.
- [x] 1.2 Record serving-source and mounting provenance; remove log inference, slow-onset detectors and note injection; extend storage/capture regressions.
- [x] 1.3 Separate the schema migration from the deferred background presence scan, scope read-only calibration and refresh eligibility on dose edits; extend migration/storage coverage.
- [x] 1.4 Measure realistic and multiplied database costs and record them at the query call site.

## 2. Shared presentation

- [x] 2.1 Gate history labels on PORTAL data, translate all new text through TranslationManager and remove generic settings keywords; verify text invariants and QML compilation.
- [x] 2.2 Keep only the shared structured AI block and remove the optional assertion-free real-shot diagnostic branch; retain executable payload assertions.
- [ ] 2.3 Publish the prepared calibration entry in the separate wiki repository. The patch is removed here; wiki commit 89bc054 is prepared locally, but GitHub denied janhcla write access (403). Maintainer publication is required.

## 3. Integration validation

- [x] 3.1 Functional source 7fe70f0a, including main 621853e6, passed [CI 41417048370](https://github.com/janhcla/Decenza/actions/runs/41417048370): Linux 205/205, 205 unique starts with no retries or test failures, QML gate 357/357, Android and Wasm builds passed. APK v3 signature and QtQml build ID were verified. The following documentation-only commit uses these implementation results.
- [x] 3.2 [Regression controls 41345407581](https://github.com/janhcla/Decenza/actions/runs/41345407581) passed against source 032a1ae2: six deliberate regressions failed at the intended assertions, with all four owner suites green before and after restoration.
- [ ] 3.3 Native/browser interaction and physical-sensor acceptance remain unverified. No current build was started on a paired host or installed on the live tablet; perform this manual acceptance before release.

## 4. Review follow-up

- [x] 4.1 Reuse the mounting-mode decoder in both database readers; create the optional index after the deferred presence pass (section 8).
- [x] 4.2 Reject simulated serving scales, leave initial cup mass uncovered, retain actually measured pre-drip baselines and derive the ratio envelope only from finite positive reference ratios. [Four regression controls](https://github.com/janhcla/Decenza/actions/runs/41348913871) against source 949fd637 failed at the intended assertions; both owner suites passed before and after restoration.
- [x] 4.3 Render both review-card warning icons through the shared emoji image helper.
- [x] 4.4 Full Linux suite, Android/Wasm builds and QML passed in CI 41348885569; [text invariants](https://github.com/janhcla/Decenza/actions/runs/41348888596) passed on source 949fd637.

## Workflow follow-up

- Archive this change as the final PR commit before an explicitly requested merge.

## 5. Complete 20-point review audit

- [x] 5.1 Consolidate unmerged PORTAL schema in migration 51; deferred scan behavior supersedes the earlier migration-50 backfill design. Test upgrades from main 47 and previews 48/49/50. Main's added-water migration 47 remains independent.
- [x] 5.2 Reuse one fit per coffee/mounting in each history request for unmeasured shots, display measured EY on paired rows, share prediction safeguards and skip reference reads for bulk export/flow calibration and invalid provenance. Scoped History assertions pass. In the eight-row fixture, source 4bc1afcf reduced fits/pool loads from 7/7 to 3/3; Unicode resolution now uses two small SQL reads per pool load, measured at the query call site.
- [x] 5.3 Recompute yield-dependent thermal eligibility using corrected metadata; prove the cache and review agree after a yield edit.
- [x] 5.4 Expose mounting selection to accessibility, use Theme fonts and reconcile stale comments/unused advice parameters and the tail coverage constant.
- [x] 5.5 Recheck all 20 findings and record the full suite/platform/QML evidence above. The existing PR description contains the finding-by-finding audit; the final documentation commit gets a fresh text-invariants check. Wiki publication remains blocked under task 2.3 until a maintainer grants access.
- [x] 5.6 Address the additional GitHub findings: use the decoder in Settings, exclude cup/curve mass mismatches beyond the 0.1 g editing step and transact metadata plus eligibility together, including the MCP-owned transaction path. [Six controls on 4bc1afcf](https://github.com/janhcla/Decenza/actions/runs/41411332247) failed at their intended assertions, with all three owner suites green before/after restoration.

## 6. GitHub review at 4bc1afcf

- [x] 6.1 The user accepted the later GitHub recommendation: schema-only migration with presence backfill once in the background after PORTAL ownership/attachment. The earlier startup placement is superseded by section 8.
- [x] 6.2 Accept baseline noise within 0.1 g while retaining observed mass, preserve small negative baselines, and drop equal-time EC duplicates without inventing a gap.
- [x] 6.3 Gate ratio advice on measurement quality rather than empirical prediction uncertainty; verify advice remains consistent with 0/1/3/5 references. [Six controls on 4dd43776](https://github.com/janhcla/Decenza/actions/runs/41414838994) failed at their intended assertions; all four owner suites passed before/after restoration.
- [x] 6.4 Share Options assembly across all analysis entry points, including the advisor fallback, and describe PORTAL units/ranges in the shared system prompt.
- [x] 6.5 Reuse mounting decoding in normalization/validation, finish the tail-window constant cleanup, stamp mounting only during active PORTAL capture, and distinguish missing provenance from virtual-scale data.
- [x] 6.6 Current platform suite and regression controls passed as linked above. GitHub comments through review 5734030687 were assessed and addressed; the separate automated review of 7fe70f0a was still running at this check. Device acceptance and the rare import-failure path noted below threshold remain outside these automated checks.

## 7. Additional GitHub findings at 4bc1afcf / 4dd43776

- [x] 7.1 Share Unicode coffee-name normalization across SQL scope resolution, fitting, advice and the history cache; verify accented names through Settings and History and remeasure the query. [Controls on 7fe70f0a](https://github.com/janhcla/Decenza/actions/runs/41417057187) detect ASCII matching and stale Visualizer eligibility, with four owner suites green before/after restoration.
- [x] 7.2 Refresh PORTAL eligibility inside the Visualizer pull transaction, preserving clean sync state and rolling back metadata if refresh fails.
- [x] 7.3 Subscribe Settings calibration to successful Visualizer pulls, including TDS-only changes; native/browser live refresh remains part of manual acceptance.
- [x] 7.4 Both owner-boundary regressions passed, and each deliberate defect failed at its intended assertion in CI 41417057187. Full platform validation passed on 7fe70f0a; final text-invariants results are linked in the PR. GitHub was rechecked before publishing the audit.

## 8. Deferred legacy presence backfill

- [x] 8.1 Move the sample scan and optional index off initialization; persist completion atomically with presence updates on the existing DB worker, preserving recorded references.
- [x] 8.2 Request the pass only behind the shared owned/active hardware gate, coalesce repeated requests, and queue Settings calibration after it.
- [x] 8.3 Extend the migration owner test for previews 47-50, startup without scan, UI event delivery under a competing writer, rollback/retry and no repeat after restart. [Six regression controls on functional source 1a5643e3](https://github.com/janhcla/Decenza/actions/runs/41423944384) failed at their intended assertions; dbmigration, belkaportal and shotreviewactions passed before and after restoration.
- [ ] 8.4 Run current platform/text/OpenSpec validation, update the existing PR and recheck GitHub comments.

- [x] 8.5 Address new GitHub review 5734465691: refresh Shot Review outcomes after Visualizer pulls while preserving the editing form; the existing late-outcome regression now executes the real merge/callback/outcome handlers, rejects stale replies and preserves the draft.

- [ ] 8.6 Address GitHub review 5734886935: explicitly reset History estimate roles when calibration disappears; verify clearing the last paired reference removes a retained estimate.
