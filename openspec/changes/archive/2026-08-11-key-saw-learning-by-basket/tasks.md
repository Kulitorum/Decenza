## 1. Three-dimensional SAW key

- [x] 1.1 Add basket-key normalization (lowercase, non-alphanumeric runs to `-`, `(none)` sentinel for
      no basket) and extend `sawPairKey()` to take basket brand/model, emitting the three-segment key
- [x] 1.2 Thread basket brand/model through the per-pair API — `sawLearnedLagFor`, `getExpectedDripFor`,
      `sawLearningEntriesFor`, `sawModelSource`, `addSawLearningPoint`, `perProfileSawHistory`,
      `sawPendingBatch`, `addSawPerPairEntry`
- [x] 1.3 Implement the one-time `seedSawBucketsFromPreBasketKeys()` copy (flag
      `saw/basketKeyMigrated`): copy each pre-basket bucket into the baskets THAT PROFILE was
      pulled with, tag entries `inherited`, skip untried combinations and profiles absent from the
      window, never overwrite a basket that has data, leave the two-segment keys in place, set the
      flag only on a demonstrated-successful read, and clear it in `resetSawLearning()` and
      `sawLearningImport()`
- [x] 1.4 Widen `recomputeGlobalSawBootstrap` to take contributions from every per-basket bucket on
      the scale type, skipping buckets whose newest median is `inherited` AND the two-segment source
      bucket (frozen, so it would vote its own past forever) so one seeded batch cannot vote once per
      basket; key and IQR fencing unchanged
- [x] 1.5 Make `resetSawLearningForProfile` clear every basket bucket plus the pre-basket bucket for
      the `(profile, scale)` pair, and update the MCP tool's description to match — no new tool
- [x] 1.6 Extend `tst_saw_settings`: two baskets on one profile+scale learn independently, `(none)` is
      its own bucket, a basket switch mid-batch leaves the pending batch with its original basket, no
      new write ever lands on a two-segment key, and the copy carries buckets forward, carries pending batches
      without committing, untried combinations and absent profiles are skipped, a basket with its own
      data is never overwritten, an empty basket set over a store with buckets is refused, a partial
      run leaves the flag unset, no equipment recorded uses the
      no-basket value, and inherited medians do not vote in the bootstrap

- [x] 1.7 Source the combinations from the shot history, not the equipment inventory: add
      `ShotHistoryStorage::requestRecentProfileBasketPairs(limit=500)` (bounded query joining
      `shots.equipment_id` to `equipment_items` where `kind='basket'`, returning distinct
      (profile title, basket) pairs) and wire it in `MainController`, mapping titles through
      `ProfileManager::titleToFilename()`

## 2. Wire the basket into the shot path

- [x] 2.1 Read the active basket brand/model from `SettingsDye` and latch it into the SAW model
      snapshot at `espressoCycleStarted` in `main.cpp`
- [x] 2.2 Use the latched value (not a fresh read) when writing the learning point at
      `sawLearningComplete`
- [x] 2.3 Extend the per-shot `[SAW][Learning] Model:` and `Accuracy:` lines with the basket, per
      `docs/CLAUDE_MD/LOGGING.md` (existing `[SAW]` subsystem, tier unchanged)

## 3. Calibration tab

- [x] 3.1 Name the profile, scale and basket the displayed value belongs to in the SAW card (the
      profile was missing before this change), with a dye dependency on the lag/tier bindings so a
      basket switch does not leave the new name beside the old number
- [x] 3.2 Offer a profile-scoped reset that clears every basket for the active profile and scale,
      visible when that scope has data (not when its tier happens to be winning), and put the
      full wipe behind a confirmation
- [ ] 3.3 Open the Calibration tab in a running build and confirm the suffix and reset wording (QML has
      no test harness)

## 4. Documentation and close-out

- [x] 4.1 Update `docs/CLAUDE_MD/SAW_LEARNING.md`: three-part key, storage schema table, read-path
      chain, the one-time copy and its failure gating, logging table, and the measured basket data
      from proposal.md as the evidence section
- [x] 4.2 Record the transport measurement in `SAW_LEARNING.md` as the reason the three Half Decent
      Scale transports stay separate — the split currently has no stated justification beyond a
      `scaletypeids.h` comment, and the next reader will otherwise re-propose the merge
- [x] 4.3 Reconcile the `kSawMinMediansForGraduation` drift — the spec documented 2, the code is 1 —
      in the main spec text so the documented value matches the code
- [x] 4.4 Check whether the wiki manual documents the Calibration tab's SAW card; update it only if it
      does, and keep it to a few sentences
- [x] 4.5 Run the full test suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before building,
      Qt Creator is shared
- [ ] 4.6 Open the PR, then run `/pr-review-toolkit:review-pr` and address findings
- [ ] 4.7 Archive this change with spec sync as the final commit on the PR branch
