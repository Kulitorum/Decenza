## 1. Hold state in CupFillView

- [x] 1.1 Add `extractionSeen` (bool) and `heldWeight` (real) properties to `qml/components/CupFillView.qml`, with a phase-change handler that sets `extractionSeen` on entry to `Preinfusion` or `Pouring` and captures `heldWeight` from `currentWeight` on the transition out of the espresso cycle
- [x] 1.2 Clear `extractionSeen` and `heldWeight` on entry to a new espresso cycle (`EspressoPreheating`, and the machine-driven straight-to-`Preinfusion` case)
- [x] 1.3 Add the derived `holding` and `displayWeight` readonly properties described in `design.md`

## 2. Route rendering through the held value

- [x] 2.1 Extend `hasExtraction` in `cupGeometry()` to `hasExtraction || extractionSeen`, keeping the pre-flow empty-cup behaviour intact when nothing has been latched
- [x] 2.2 Compute `fillRatio` from `displayWeight` instead of `currentWeight`
- [x] 2.3 Replace the `currentWeight <= 0` early return in the liquid canvas, and the `currentWeight`-dependent guards in the effects canvas, with `displayWeight`
- [x] 2.4 Bind the view's weight text to `displayWeight`
- [x] 2.5 Confirm `animTimer.running` still evaluates false during the hold, so the held frame stays static

## 3. Verify

- [x] 3.1 Ask before building; build via `mcp__qtcreator__build`, confirming the active project is the intended checkout first
- [x] 3.2 Run the full test suite via `mcp__qtcreator__run_tests` (scope `all`) — no CI job builds or tests a PR
- [x] 3.3 Manual: run a normal shot with the cup fill view selected; the cup and weight hold from end of extraction until the shot review page appears
- [x] 3.4 Manual: lift the cup during the hold; fill, crema and weight text are unchanged
- [x] 3.5 Manual: stop a shot early — not applicable: the page navigates to the shot review immediately, so the view is destroyed before any hold window exists. Recorded in `design.md`; no code change
- [x] 3.6 Manual: abort during preheat; the cup stays empty
- [x] 3.7 Manual: leave a cup on the scale and open the espresso page before a shot; the cup renders empty

## 4. Land

- [ ] 4.1 Open a PR (never push to `main`)
- [ ] 4.2 Run the automated `/pr-review-toolkit:review-pr` review and address the findings
- [ ] 4.3 Archive this change and sync specs as the final commit on the same PR, then squash-merge and delete the branch
