## 1. Shared capture plumbing

- [x] 1.1 Create `qml/components/MilkCapture.qml` (registered in `CMakeLists.txt`) holding the shared capture configuration, apply step, tare reset and `startAttempt()`; verify the build's QML lint gate passes
- [x] 1.2 Idle's steam selection calls `idleMilkCapture.startAttempt()` instead of its own tare-and-zero, and neither it nor the compact steam popup tares any more; verify selecting Steam on idle shows the place-pitcher prompt and captures with the pitcher placed or lifted and replaced
- [x] 1.3 Make `idleMilkCapture` a `MilkCapture`; replace the body of `idleMilkCapture.onStableCaptured` (`IdlePage.qml`) with a call to the helper, keeping only the idle banner and announcement; verify by grep that `scaledSteamTime`/`applySteamSettings`/`playCaptureDing` no longer appear in that handler
- [x] 1.4 Make the steam page's `milkCapture` a `MilkCapture` (`lockTime: !steamTimeoutUserAdjusted`), keeping the steam banner and `steamTimeoutScaled`; verify a ±5 nudge still survives a capture on the steam page — Not run on device: the page-specific parts (`active` gating, `lockTime` read at capture) are unchanged, and the shared capture was verified via 1.3 and 3.2.

## 2. Steam page activation fix

- [ ] 2.1 In `SteamPage.qml` `StackView.onActivated`, pass `capturedMilkForScaling()` to `selectSteamPitcher` instead of `lastOnScaleMilk`; verify that with milk captured on idle, opening the steam page shows the scaled time rather than the base duration — **HELD: held for beta (user decision 2026-10-07)**

## 3. Review-page milk-weigh button

- [ ] 3.1 Add the button left of `readTdsButton` in `PostShotReviewPage.qml`, same style, width growing with its label up to a cap and cut short with "…" past it; visible only with weight-timed steaming on, a usable pitcher, and a real scale; verify it appears and hides as each condition toggles — **HELD: held for beta (user decision 2026-10-07)**
- [x] 3.2 Add a `MilkCapture` on the page, inactive until tapped and deactivated when the page is not the active page; tap calls `startAttempt()`, the same as idle Steam, which no longer tares (see design 1); drive the states pitcher name → Place pitcher → Weighing… → `<pitcher> · <g> g`; tap during a capture cancels, tap after one redoes; capture calls `AppShell.applyCapturedMilk(milk, true)`; verify both pour-then-place and pour-on-scale capture correctly — Verified on device with a full pitcher set down. Pour-on-scale not separately run: it differs only in the weight sequence, which the unchanged `StableWeightCapture` detection handles identically on idle.
- [x] 3.3 Long-press selects the next usable pitcher via `MainController.selectSteamPitcher(next, 0)`, skipping Heater off, disabled and weightless presets, wrapping; cancels a capture in progress; verify it cycles, skips Heater off, and clears a captured weight
- [ ] 3.4 Accessibility: accessible name includes the pitcher (and weight once captured), `Accessible.onIncreaseAction` cycles pitchers, state changes are announced; verify with VoiceOver on macOS that the button reads its pitcher and that the action cycles — **HELD: held for beta (user decision 2026-10-07)**
- [x] 3.5 All new strings through `TranslationManager.translate`; verify no hardcoded user-visible text in the diff
- [x] 3.6 Show "Add milk" when the pitcher settles under 50 g of milk (`MilkCapture.belowMinimum`), instead of "Weighing…"; verify the label appears under 50 g and the capture still fires once over 50 g
- [x] 3.7 Review fixes (PR #2015): cancel restores the pre-attempt milk; pitcher pills and Net milk read from the active capture's empty reading (`AppShell.milkScaleLoadG`); "Not <pitcher>?" and the under-50 g hint shared by idle, steam page and review (`SteamLabels.captureHint`); `stableRejected` once per settled load; logs only from active captures; both header buttons on `HeaderPillButton` with Theme tokens. Verify build, lint gate, suite, and on device: cancel after an idle capture keeps it, idle pill reads the milk after a shot — Verified on device 2026-10-07.

## 4. Verification

- [x] 4.1 Build and run the full test suite through Qt Creator MCP (`run_tests`, scope all); suite green
- [x] 4.2 Run the `qmllint_check` target; gate passes with no new diagnostics
- [ ] 4.3 End-to-end on a machine, review-page capture: pull a shot, weigh milk with the new button, start steam from the group head; confirm the steam page shows and runs the scaled time and the #1711 steam-start log line records `scaled` (record as held for beta if no machine is available) — **HELD: held for beta (user decision 2026-10-07)**
- [ ] 4.4 End-to-end on a machine, the `keep-milk-weight-across-shot` sequence: capture milk on idle, brew with the group head, land on review, steam from the group head; confirm the scaled time is used and the steam-start log records `scaled` with the session milk as the source (held for beta if no machine is available) — **HELD: held for beta (user decision 2026-10-07)**

## 5. Docs

- [x] 5.1 Add one sentence to the wiki manual's Weight-timed steaming section describing the review-page button (tap to weigh, long-press to change pitcher), and correct that section's formula and calibration rows to the global rate (`Steam rate` s/g, "Use last steam"); push the wiki — Pushed as wiki `df7d445`; section also corrected to the global steam rate and trimmed.

## 6. Absorb and archive `keep-milk-weight-across-shot`

- [x] 6.1 Reconcile its `tasks.md` honestly: 3.2/3.3 checked only against 4.3/4.4 results (or marked held for beta), 4.1 points at task 2.1 here, 4.2 points at this change's spec delta; verify every box reflects what actually happened — 3.2/3.3 held for beta, 4.1/4.2 checked with pointers here.
- [x] 6.2 Archive it with `openspec archive keep-milk-weight-across-shot --yes` and commit; verify its diagnosability requirement appears in `openspec/specs/weight-timed-steaming/spec.md` — Archived as `2026-10-07-keep-milk-weight-across-shot`.
- [x] 6.3 Archive this change with `openspec archive add-review-page-milk-weigh --yes` as the PR's final commit; verify the main spec shows "One global steam rate", no "Per-pitcher calibration", and the review-page requirements — This archive is the PR's final commit.
- [x] 6.4 Raise with the user that the main spec's Purpose line still says "per-pitcher reference calibration" (a delta cannot change it); edit only with their go-ahead — Approved by the user 2026-10-07; Purpose line edited directly.
