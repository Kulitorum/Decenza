# Tasks

## 1. Measurement and storage

- [x] 1.1 Implement mass integration, gap and scale safeguards; retain existing analyzer tests.
- [x] 1.2 Record serving-source and mounting provenance; remove log inference, slow-onset detectors and note injection; extend storage/capture regressions.
- [x] 1.3 Move backfill to one migration, scope read-only calibration and refresh eligibility on dose edits; extend migration/storage coverage.
- [x] 1.4 Measure realistic and multiplied database costs and record them at the query call site.

## 2. Shared presentation

- [x] 2.1 Gate history labels on PORTAL data, translate all new text through TranslationManager and remove generic settings keywords; verify text invariants and QML compilation.
- [x] 2.2 Keep only the shared structured AI block and remove the optional assertion-free real-shot diagnostic branch; retain executable payload assertions.
- [ ] 2.3 Publish the prepared calibration entry in the separate wiki repository. The patch is removed here; wiki commit 89bc054 is prepared locally, but GitHub denied janhcla write access (403). Maintainer publication is required.

## 3. Integration validation

- [ ] 3.1 Re-run after incorporating main 621853e6. Previous source b1dae020 passed [CI 41342483399](https://github.com/janhcla/Decenza/actions/runs/41342483399): Linux 205/205, no retries, QML gate 357/357, Android and Wasm builds passed.
- [x] 3.2 [Regression controls 41342881887](https://github.com/janhcla/Decenza/actions/runs/41342881887) passed: six deliberate regressions failed at the intended assertions, with all four owner suites green before and after restoration.
- [ ] 3.3 Native/browser interaction and physical-sensor acceptance remain unverified. No current build was started on a paired host or installed on the live tablet; perform this manual acceptance before release.

## Workflow follow-up

- Archive this change as the final PR commit before an explicitly requested merge.
