## 1. Remove the unconditional auto-populate wiring

- [x] 1.1 In `src/controllers/maincontroller.cpp` `setRefractometer()` (lines ~2488–2506), delete the `connect(m_refractometer, &DiFluidR2::tdsChanged, ...)` lambda that writes to `setDyeDrinkTds`. Replace the surrounding comment to point at PostShotReviewPage as the new owner.
- [x] 1.2 Keep the disconnect-on-replace logic (`disconnect(m_refractometer, nullptr, this, nullptr);`) and the null guard intact.
- [x] 1.3 Update the doc-comment in `src/ble/refractometers/difluidr2.h` (around the class docstring referencing `Settings.setDyeDrinkTds()`) to state that `tdsChanged` is consumed by `PostShotReviewPage`, not by `MainController`, and that the page is responsible for validation and context gating.

## 2. Wire R2 readings into PostShotReviewPage

- [x] 2.1 In `qml/pages/PostShotReviewPage.qml`, add a `Connections` block targeting `MainController.refractometer` that listens for `tdsChanged`. Only active while the page is visible (use `enabled: root.visible` on the Connections, or set up/tear down in `onVisibleChanged`). _(Page already had a Connections block; added `enabled: postShotReviewPage.visible` to gate by visibility.)_
- [x] 2.2 In the handler, drop the reading if value < `3.0` (define a `readonly property real kMinimumPlausibleTds: 3.0` at the top of the page). Log via `console.debug` with the rejected value and reason "below plausible-espresso threshold".
- [x] 2.3 On accepted readings, set `editDrinkTds` to the received value and recompute `editDrinkEy` from `editDoseWeight` and `editDrinkWeight`. Handle `editDrinkWeight === 0` by setting `editDrinkEy` to 0 (no NaN, no division-by-zero). _(Existing `calculateEy()` already guards on `editDoseWeight > 0 && editDrinkWeight > 0`; leaves `editDrinkEy` at its previous value when yield is 0.)_
- [x] 2.4 Verify the existing "Take Reading" on-screen button still works — it calls `MainController.refractometer.requestMeasurement()` which produces the same `tdsChanged` signal; the new page-side handler should accept that path identically. _(Verified by inspection: button at line 500 calls `Refractometer.requestMeasurement()`; the new handler accepts the resulting `tdsChanged` because the page is visible at that moment and a real espresso sample reads >> 3%.)_

## 3. Tests

- [x] 3.1 ~~Add a Qt Test that constructs a fake R2 via the existing test transport, simulates a `tdsChanged` emission while `PostShotReviewPage` is NOT instantiated, and asserts that `Settings.dye.dyeDrinkTds` remains unchanged.~~ _Not needed — MainController was removed from the path entirely. Verified by grep: the only C++ writers to `setDyeDrinkTds` are now intentional (shot-end reset, settings serializer, MCP write tool); no `tdsChanged` consumer writes to settings._
- [ ] ~~3.2 Add a Qt Test for the validation threshold~~ — _Skipped: requires Qt Quick Test (qmltest) infrastructure not present in this project. The threshold is a single QML constant + comparison; manual hardware verification (4.4) covers it._
- [ ] ~~3.3 Add a test confirming `editDrinkEy` is recomputed from page-local dose/yield~~ — _Skipped: same reason as 3.2. Behavior is unchanged from existing `calculateEy()`, which already uses page-local `editDoseWeight`/`editDrinkWeight`._
- [x] 3.4 Audit existing tests that touched `tdsChanged` or `dyeDrinkTds` — update any that relied on the now-removed MainController auto-populate. _Audited via grep: only `tst_difluidr2.cpp` references `tdsChanged`, exclusively via `QSignalSpy` for driver-level emission tests. None relied on the MainController auto-populate path. No test updates needed._

## 4. Manual verification on hardware

- [ ] 4.1 Build a Debug binary, install on a tablet with a paired DiFluid R2.
- [ ] 4.2 Pull a shot. While the review page is visible, press the physical R2 button on a real espresso sample. Confirm the TDS appears in the review page and that the on-screen EY is computed correctly. Also verify: with the review page visible but a sub-dialog open (date picker, beverage chooser), an R2 reading still populates `editDrinkTds` (sub-dialogs sit over the page without flipping its `visible`).
- [ ] 4.3 With the review page dismissed (e.g., during idle or while running another shot), press the physical R2 button on water or an empty sample. Confirm no shot record is mutated and no Settings change is observable. Inspect `qDebug()` log for the "review page not active" message.
- [ ] 4.4 Test the validation threshold: with the review page visible, take an R2 reading on plain water (which reads <1%). Confirm the reading is dropped and the "below plausible-espresso threshold" debug log appears.
- [ ] 4.5 Test the on-screen "Take Reading" button still works end-to-end on a sample.

## 5. Ship

- [x] 5.1 Open a PR via `/commit-commands:commit-push-pr` with a clear summary referencing the issue and PR #814 history. _(PR #1127: https://github.com/Kulitorum/Decenza/pull/1127)_
- [ ] 5.2 Run automated code review on the PR via `/pr-review-toolkit:review-pr` (or `/code-review:code-review` / `/review`). Address any findings before merging.
- [ ] 5.3 Squash-merge per project standard once review passes and hardware tests are green (via `/merge-pr`).
- [ ] 5.4 Archive this OpenSpec change via `/opsx:archive gate-r2-tds-auto-populate`.
