## 1. Complete the outcome inventory

- [ ] 1.1 Expand `source-audit.md` into a branch-level inventory of every Visualizer entry, terminal exit and signal consumer, including guards and duplicate decisions; verify each row identifies its existing behavior, emitter, proposed severity and validation path.
- [ ] 1.2 Resolve ownership and duplicate forwarding for each row; verify a `[Visualizer]` query can contain the complete operation without re-emitting storage/profile events or changing existing consumers.

## 2. Share diagnostic context

- [ ] 2.1 Implement callback-owned operation context with stable IDs, elapsed time, stage, parent and known record IDs using the shared formatter; verify overlapping actions, rejected reentry and late callbacks keep independent identities and one terminal event each.
- [ ] 2.2 Centralize bounded field and URL summaries without duplicating AI formatter bodies; verify sensitive payloads, share-code query values, credentials, oversized IDs and newlines cannot enter the new fields, and existing AI diagnostics still pass if shared code moves.

## 3. Upgrade Visualizer result paths

- [ ] 3.1 Instrument live/history upload and metadata update boundaries, preserving retry budgets and signal/writeback behavior; verify missing credentials, transient-then-success, exhausted failure and missing returned shot ID yield truthful INFO/WARN outcomes with unchanged request counts.
- [ ] 3.2 Instrument connection tests and paginated shot lists; verify explicit missing-credential rejection, network failure, malformed response, page ceiling, valid empty and success outcomes remain visible through the Visualizer severity filter.
- [ ] 3.3 Instrument single/share-code/renamed profile imports and shared-shot discovery; verify parse/fetch/save failures and successful saves are distinct, with safe diagnostics and unchanged import signals.
- [ ] 3.4 Carry import identity through duplicate overwrite, rename, skip and cancellation, and through batch imports; verify the result waits for the actual decision and batch counts distinguish success, skips and partial failure.
- [ ] 3.5 Instrument history recovery from preflight and list pages through download, parse and database insertion; verify one terminal summary includes total/imported/skipped/failed counts and partial recovery is WARN.
- [ ] 3.6 Instrument post-upload coffee management, repair, canonical linking and queued bag updates as their own related operations; verify capability limitations, pending retry, unexpected failure and later success do not rewrite a successful upload result.
- [ ] 3.7 Replace covered request/response dumps, parse snippets and remote error prose with safe summaries; verify a fake server echoing secrets and notes produces none of that content in the main log while UI errors and dedicated diagnostic files retain existing behavior.

## 4. Verify integration on Mac

- [ ] 4.1 Run focused production-path regressions through Qt Creator MCP and record exact test/run results; verify terminal count, severity, operation/record context, signals and request counts for the completed inventory, using fake replies rather than live service mutations.
- [ ] 4.2 Build the app and run the full Mac suite through Qt Creator MCP, plus affected text/QML gates and strict OpenSpec validation; record outcomes and fix failures before declaring local validation complete.
- [ ] 4.3 Compare a complete persisted local validation log with Visualizer INFO/WARN selections and record event counts, duplicate/fallback census and bounded output size; verify historical entries remain readable and document the exercised versus source-only branches. Any app launch must target this checkout after confirming no duplicate instance.

## 5. Documentation and review

- [ ] 5.1 Update LOGGING.md and VISUALIZER.md to describe actual outcomes and safe context, correcting stale Network/raw-prefix and severity-helper guidance; verify examples match the current helper APIs and keep later app-wide candidates in the audit.
- [ ] 5.2 Prepare the short wiki update by extending the prior reviewed patch as needed; verify it describes user-visible log filtering/outcomes in 3–5 sentences and remains queued for publication with the feature.
- [ ] 5.3 Review the final implementation against every scenario and the no-behavior-change boundary; verify all checked tasks have evidence, all held items remain outstanding, and the PR description links the validation record.

## 6. Inherited work held by the user

These tasks are carried from the archived #1930 change. They are not Mac-test
failures or authorization to run builds. See `validation-holds.md` for origin and
release conditions; do not check them merely because this proposal or PR is ready.

- [ ] 6.1 After the user lifts the beta-build hold, verify Android, iOS and other platform beta builds containing #1930 and the follow-up; record platform, revision, run URL and result, and evaluate any failures.
- [ ] 6.2 Once an updated mobile build is available under the user's workflow, retrieve a representative charging/mismatch log using the DE1 MCP; verify request-versus-observation wording and registered context without changing charging control to manufacture a case.
- [ ] 6.3 Publish the prepared wiki guidance with the shipped feature and verify the rendered page; retain its held status while publication remains queued.
