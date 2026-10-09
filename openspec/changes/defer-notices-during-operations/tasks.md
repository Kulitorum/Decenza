## 1. Hold

- [x] 1.1 Add `machineOperating` (operation phases plus DE1 Steam state), rebuild `operationActive` on it, and add `noticeMustWait()`.
- [x] 1.2 Route the update, charging, scale, BLE-error, local-network and decent-machine notices through `noticeMustWait()`. Drop recipe-activation failures while it holds.

## 2. Start and end of an operation

- [x] 2.1 On start, close and re-queue open notices. The helper is shared with the screensaver.
- [x] 2.2 On end, drain on the next tick, and only when no modal dialog is up. Every listed dialog drains on close, except the startup crash report (its close opens the auto-relaunch prompt).

## 3. BLE errors

- [x] 3.1 Carry `raisedDuringOperation` through queue, dequeue, direct open and re-queue, and exempt such errors from the stale skip.
- [x] 3.2 Let a permission error take over a queued generic one.

## 4. Verification

- [ ] 4.1 On a device: with a saved scale off, wake from the group head into a shot. The scale notice appears after the shot, not over it.
- [ ] 4.2 On a device: an update check during steam warm-up and during steaming. The prompt appears when steaming ends.
