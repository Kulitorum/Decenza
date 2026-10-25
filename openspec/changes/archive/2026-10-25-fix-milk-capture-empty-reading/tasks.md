## 1. Implementation

- [x] 1.1 `StableWeightCapture` takes `Arming` (Off / Tracking / Armed); Tracking learns the empty reading without capturing
- [x] 1.2 `PreparationController` tracks while the scale is usable and arms only for an attempt
- [x] 1.3 `PreparationState::loadOnScaleG` is the one load-above-empty reading; steam-start, last on-scale milk, pitcher selection, the steam service and the pill/Net milk readouts use it

## 2. Validation

- [x] 2.1 Engine test: a load placed while tracking is captured from the learned empty reading once armed
- [x] 2.2 Controller test: pitcher on an un-zeroed scale before arming, from the idle Steam row and from the review-page button
- [x] 2.3 Controller test: steam started before the capture settles is scaled from the load above the empty reading
- [x] 2.4 Full suite green through Qt Creator (207/207)
- [ ] 2.5 On-machine check with a scale that reads non-zero when empty — HELD for beta, not passed: confirm with the #2058 reporter's Skale 2 (reads −370 to −470 g empty) on both the idle Steam row and the review-page milk button
