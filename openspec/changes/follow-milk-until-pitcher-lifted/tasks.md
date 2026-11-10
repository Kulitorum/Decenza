# Tasks

## 1. Follow the pitcher until it is lifted

- [x] 1.1 In `PreparationController`, stop closing the milk request in `handleMilkResult`; close it in `refreshPass` when the load leaves the scale after a capture in this request. Set the milk `rearmDelta` to 2 g.
- [x] 1.2 `PostShotReviewPage.qml`: show the captured weight, not "Weighing…", while the request is open and the capture stands.
- [x] 1.3 `tst_preparationcontroller`: add `toppedUpMilkReplacesTheCaptureUntilThePitcherIsLifted`; move the "request closed" assertions in three existing tests from the capture to the lift.
- [x] 1.3a Review fixes: the lift close ignores a tare or dropout and logs at INFO; a pitcher change drops the standing capture; a re-settle within 2 g is not a new capture; the review button does not announce a capture twice. Tests: three `pitcherChangeKeepsAttemptOpenAndScaleBaseline` rows, an early lift, a disconnect, the 2 g boundary.
- [x] 1.4 Build and run the full suite through Qt Creator (ask first). Verify: `succeeded: true`, `QML diagnostics gate passed`, no failures.
- [x] 1.3b Home's milk-captured message uses the shared `StatusToast` at window level instead of a banner pinned under the top bar.
- [ ] 1.5 Device check, rides the next beta: pour with the pitcher on the scale, pause, pour more, lift. Verify: two captures, the second weight is the one steamed, and the message shows bottom-centre clear of the bars.

## Workflow follow-up

- Open a PR, run `/pr-review-toolkit:review-pr`, read the `text-invariants` run.
- `openspec archive follow-milk-until-pitcher-lifted --yes` as the PR's final commit, then squash-merge.
