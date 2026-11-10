# Tasks

## 1. Reconnect ladder

- [x] 1.1 Remove the 5-minute tier; chain the next attempt from `scaleRetryNeeded` when the machine is not operating, the screensaver is off, Bluetooth is available and the app is not suspended.
- [x] 1.2 Log the cadence at INFO on each change (back-to-back / spaced), once per ramp restart.
- [x] 1.3 Remove the ladder restart from the two scale dialogs' OK handlers.

## 2. WiFi-to-Bluetooth fallback

- [x] 2.1 `BLEManager` takes the saved-scale addresses from `Settings`; the fallback connects only a scale on that list and is skipped when no Bluetooth scale is saved.
- [x] 2.2 A declined scale is logged at INFO, collapsed per device.

## 3. Verification

- [x] 3.1 Real-scale check on the Mac (Bluetooth Decent Scale, machine idle): first attempt timed out at 134.0 s, "scanning back-to-back" logged, next attempt 10 ms later, connected 69 s after the drop.
- [x] 3.1b Real-scale check on the Mac, build of 9d57c1867 (WiFi primary `hdstest.local` off, a second Decent Scale on over Bluetooth): fallback connected the saved Bluetooth scale; after it was forgotten, the next timeout logged "no saved Bluetooth scale to fall back to" and "scanning back-to-back", it was not connected or re-saved, and the WiFi scale connected 14 s later when switched on. A second WiFi scale (`hds.local`) seen by the browse was correctly ignored.
- [x] 3.2 Build and full suite through Qt Creator (ask first). Verify: `succeeded: true`, no failures.
- [ ] 3.3 Device check, rides the next beta: tablet, scale off then on while idle and during a shot; log volume with an attempt every 20 s.

## Workflow follow-up

- `/pr-review-toolkit:review-pr` run on the branch; findings addressed in the same PR.
- `openspec archive hunt-for-missing-scale --yes` as the PR's final commit, then squash-merge.
