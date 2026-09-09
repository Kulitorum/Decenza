## 1. Audit and scope

- [x] 1.1 Retrieve the complete real-device week through DE1 MCP, including unformatted lines; classify message families and review fault contexts and actual values.
- [x] 1.2 Review PR #1930, retain useful formatting/outcomes, and document what should be removed or tightened under the clarified goal.

## 2. Reduce existing noise

- [x] 2.1 Remove automatic FD dumps without replacement; verify independent on-demand MCP FD inspection is unchanged.
- [x] 2.2 Make memory logging require sustained growth and retain sampled/on-demand diagnostics; add deterministic trend regressions.
- [x] 2.3 Tighten battery and constant-weight suppression; remove extraction-color and routine settling traces while preserving failures and final results.
- [x] 2.4 Remove AI stage and successful file-receipt chatter; preserve context and one terminal outcome with existing provider behavior.
- [x] 2.5 Remove added Visualizer lifecycle infrastructure and improve existing payload/error messages directly using shared bounded formatters.
- [x] 2.6 Trim redundant function context while retaining fallback context; add regression coverage.

- [x] 2.7 Suppress repeated connection/discovery/forecast outcomes and remove duplicate Steam UI/auto-load receipts; retain actual timer commands and steam-scaling decisions.

## 3. Verify and review

- [x] 3.1 Run the full Mac suite/build through Qt Creator MCP, source gates and strict OpenSpec validation; fix failures and record final results.
- [x] 3.2 Inspect the rebuilt live Mac app and its persisted log with exactly one user-launched app instance; distinguish measured runtime output from historical replay estimates.
- [x] 3.3 Reconcile docs, audit and evidence with the final change; review the diff and update the existing PR title/body.

## 4. Inherited user holds

These are outstanding, not successful checks. See `validation-holds.md`.

- [ ] 4.1 After the beta hold is lifted, verify Android, iOS and other beta builds containing both logging changes.
- [ ] 4.2 When an updated mobile build is available, retrieve a representative charging/mismatch log through DE1 MCP.
- [ ] 4.3 Publish the short wiki guidance with the shipped feature and verify the rendered page.
