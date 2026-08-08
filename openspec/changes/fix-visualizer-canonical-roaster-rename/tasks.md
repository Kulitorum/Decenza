> Implementation shipped on branch `fix/visualizer-canonical-roaster-rename` ([#1781](https://github.com/Kulitorum/Decenza/pull/1781)) before this change was written up; the boxes below record what landed, not work still to schedule. The unchecked items are genuinely outstanding.

## 1. Establish the facts

- [x] 1.1 Reproduce the rename end to end against a live account through the app's own UI, not by inference
- [x] 1.2 Confirm no correct canonical id exists for the affected roaster (`q=Stavanger` → 0 rows) and that the canonical endpoints are read-only
- [x] 1.3 Read `Shot#refresh_coffee_bag_fields` at `upstream/main` and record which branch fires for a shot with and without a server-side `coffee_bag`
- [x] 1.4 Record the shot endpoints of record: what the list omits, what the detail returns, which fields are permitted for update, and the published rate limits

## 2. Storage invariant

- [x] 2.1 Add the conflict predicate over the blob, comparing the record's pristine names and treating an empty name on either side as no conflict
- [x] 2.2 Fail the predicate closed on an unparseable blob, opposite to every other mutator in that header, and say why at the call site
- [x] 2.3 Add link stripping that removes only the link keys and keeps every descriptive field and the product URL
- [x] 2.4 Enforce at both storage write paths (insert and update) so the bag editor, MCP and the web editor inherit one rule
- [x] 2.5 Bundle the two same-typed name arguments and the two out-params into types, so a transposition is a compile error rather than a silent no-op

## 3. Migration and queue

- [x] 3.1 Add the `bean_repair_pending` column on `shots`
- [x] 3.2 Apply the predicate to stored bags in one pass, propagate the unlink to each shot's own snapshot, and flag the uploaded shots
- [x] 3.3 Keep the unlink when the column could not be added; defer the whole pass when the column's existence cannot be determined
- [x] 3.4 Read the queue on the DB worker, distinguishing a failed read from an empty queue

## 4. Repair pass

- [x] 4.1 Drain the queue at startup and on bag changes, with re-entry dropped and re-drained when a snapshot was missed
- [x] 4.2 Pace every request — not every shot — from the server's published limits, and read shots with `essentials` so they do not drag chart data
- [x] 4.3 Decide per shot from the read: nothing to write when the shot has a server bag, link-clear only when local names are incomplete, names otherwise
- [x] 4.4 Read the PATCH response back and report a write the server discarded rather than counting it as a repair
- [x] 4.5 Abandon on rate limiting, on an invalid credential, and on an authorization refusal on the READ; settle one shot on an authorization refusal or a missing shot on the WRITE
- [x] 4.6 Report names restored, links cleared and shots declined separately, and claim "resumes next boot" only when work is genuinely still queued

## 5. Export gate

- [x] 5.1 Apply the same predicate on the shot metadata PATCH and the canonical link call, so a historical snapshot cannot export a borrowed id

## 6. Verification

- [x] 6.1 Unit-test the predicate (borrowed record, pristine-vs-working keys, unknown proves nothing, corrupt blob), both storage write paths with negative cases, and the migration's data pass
- [x] 6.2 Unit-test the per-shot plan and the bag-id derivation, including a field that is present but not a string
- [x] 6.3 Prove the new tests can fail by injecting each regression and watching them go red
- [x] 6.4 Exercise the whole path against a live account — migration, unlink, queue, repair, restore — and diff the account and database against pre-test snapshots
- [x] 6.5 Run an Android CI build, since the local suite only covers macOS
- [ ] 6.6 Exercise the authorization-refusal branches and the migration's defer path, which need fault injection not possible against a live account

## 7. Documentation and close-out

- [x] 7.1 Update `docs/CLAUDE_MD/BEAN_BASE.md` with the invariant, the queue, the endpoints of record and the rule about reading the deployed server rather than a working tree
- [x] 7.2 Record that no wiki manual entry is needed — this is a bug fix with no new user-facing surface
- [ ] 7.3 Merge [#1781](https://github.com/Kulitorum/Decenza/pull/1781)
- [ ] 7.4 Archive this change with `openspec archive fix-visualizer-canonical-roaster-rename` as the last commit on the branch
