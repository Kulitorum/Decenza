# Tasks

## 1. Setup comparison and proposal

- [x] 1.1 Capture the complete host-owned shot-start setup in memory, associate it with the latest saved shot, and capture a review-entry baseline after restart; test all fields, during-shot changes, normalization, fallback and restored-to-baseline values.
- [x] 1.2 Evaluate and integrate or replace the existing uncommitted yield-mode/anchor guard and tests; verify absolute versus ratio, legacy/default modes, effective target, temperature and profile changes with focused tests.
- [x] 1.3 Build a request-scoped proposal from only net edited eligible fields, with actual before/after values, derived ratio target and disclosed persistent writes; test feedback-only, undo, bag identity and simultaneous eligible edits.

## 2. Shot-only editing and guarded apply

- [x] 2.1 Remove automatic next-shot sticky sync and immediate shot-review bag activation while retaining shot autosave, selector completion, undo and upload holds; test latest and historical shots for no next-shot, recipe or bag side effects.
- [x] 2.2 Implement a shared host preview/apply service with latest-shot and baseline revalidation, correlated completions and fail-closed handling of unavailable data; test changed-before-review, restart fallback, changed-and-restored, newer shot and stale/concurrent clients.
- [x] 2.3 Apply accepted patches without normal bag activation's unrelated imports or write-through; relink an active recipe while preserving its other fields, live settings and anchor mode; test bean-only and combined edits with conflicting bag values, both yield modes and no recipe.
- [x] 2.4 Implement and test an event-driven transactional/serialized commit protocol covering recipe and bag persistence plus live state, including injected storage failure, concurrent edits, disconnect and operation-start races; verify no partial or stale next-shot updates.

## 3. Confirmation and navigation

- [x] 3.1 Add an accessible shared confirmation listing the host proposal and persistent effects, with Keep and Apply choices; verify no offer for rating-only, reverted edits, empty patch or changed setup.
- [x] 3.2 Route Back, system Back, auto-close and stepping away through save, eligibility and confirmation; test that timeout waits for a choice and both choices resume the original navigation once.
- [x] 3.3 Handle pending shot saves, child navigation, deletion, newly saved shots, disconnection and machine-start dismissal; test that shot edits remain saved, no offer is replayed and operation navigation is not blocked.
- [x] 3.4 Update the wiki manual with a short shot-review explanation of the offer and suppression rule; verify it reflects the shipped labels and behavior.

## 4. Integration verification

- [x] 4.0 Review the implementation for unnecessary complexity and simplify it, while preserving the agreed shot-review behavior and its essential safeguards.
- [x] 4.1 Run the affected shot-review, recipe-context, bag/recipe and navigation tests through Qt Creator and build the native app with its QML lint gate; record results.
- [ ] 4.2 Inspect the changed flow and failure path on the running native app and the matching host-served Wasm UI, including an active recipe and bag with different saved yield; record any device or browser limitation.
- [x] 4.3 Validate the OpenSpec change strictly and confirm the modified requirements preserve unrelated existing scenarios.
