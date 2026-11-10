# Tasks

## 1. Follow the pitcher until it is lifted

- [x] 1.1 In `PreparationController`, stop closing the milk request in `handleMilkResult`; close it in `refreshPass` when the load leaves the scale after a capture in this request. Set the milk `rearmDelta` to 2 g.
- [x] 1.2 `PostShotReviewPage.qml`: show the captured weight, not "Weighing…", while the request is open and the capture stands.
- [x] 1.3 `tst_preparationcontroller`: add `toppedUpMilkReplacesTheCaptureUntilThePitcherIsLifted`; move the "request closed" assertions in three existing tests from the capture to the lift.
- [x] 1.3a Review fixes: the lift close ignores a tare or dropout and logs at INFO; a pitcher change drops the standing capture; a re-settle within 2 g is not a new capture; the review button does not announce a capture twice. Tests: three `pitcherChangeKeepsAttemptOpenAndScaleBaseline` rows, an early lift, a disconnect, the 2 g boundary.
- [x] 1.4 Build and run the full suite through Qt Creator (ask first). Verify: `succeeded: true`, `QML diagnostics gate passed`, no failures.
- [x] 1.3b Home's milk-captured message uses the shared `StatusToast` at window level instead of a banner pinned under the top bar.
- [x] 1.3c Steam page: a "Play a sound when a weight is captured" switch under the weight-timed toggle, writing the Brew dialog bell's preference.
- [x] 1.3d Shared `StatusToast`: sits above the bottom bar, uses the body font, and takes the selected-pill colours; error toasts keep red on neutral. Seen on Home by Jeff with a milk capture.
- [x] 1.3e Replace the five hand-drawn toasts (profile picker, library panel, Steam page, Recipes page, translation errors) with `StatusToast`; `show(text, isError)` styles each showing. The Brew dialog's recipe error is an inline banner and stays.
- [x] 1.3f Codex review: the lift is judged from the raw reading against the captured load, so a tare or dropout between capture and lift cannot leave the request open for a cup; the capture-sound switch sends one write at a time and reports a refusal.
- [x] 1.3g Codex review: after a tare or dropout with the captured pitcher on, the capture stands and nothing more is weighed until the lift (the detector's zero is then the loaded pitcher).
- [x] 1.5 Real-scale check by Jeff on the Mac build (built 9:40 AM MT 2026-11-10, USB scale, Small pitcher 25.0 g): pitcher set down, 71.1 g captured, topped up, 313.3 g captured and timed at 85 s, lift logged "Milk sample ended: pitcher lifted, 313.3 g stands"; toast seen above the bottom bar. Steam was not started, and the tare/dropout paths were exercised by the unit tests only. The tablet gets it with the next beta.

## Workflow follow-up

- Open a PR, run `/pr-review-toolkit:review-pr`, read the `text-invariants` run.
- `openspec archive follow-milk-until-pitcher-lifted --yes` as the PR's final commit, then squash-merge.
