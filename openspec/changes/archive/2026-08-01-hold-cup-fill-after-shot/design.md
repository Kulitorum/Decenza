## Context

`CupFillView` derives everything it draws from two live inputs: `phase` (from `MachineState`) and `currentWeight` (from `MachineState.scaleWeight`, relayed through `EspressoPage.currentWeight`). Both keep moving after the shot ends, and both independently blank the cup:

```
DE1:     Espresso ───────────────────────────────► Idle
phase:   Preheat → Preinfusion → Pouring → Ending → Ready
         └ empty ┘└──── hasExtraction true ──────┘└─ false
fillRatio:  0        0.0 ──────────────────► 1.0     0   ◄── blanks
weight text: 0       0.0 ──────────────────► 36.2   36.2  ◄── still live
page:    ├────────── EspressoPage still mounted ~3 s ─────┤► ShotReview
```

The redraw that shows the empty cup is not the animation timer — that timer's `running` gates on the same phase range and has already stopped by then (deliberately, since `07ab75fc`). It is `onCurrentWeightChanged`, which calls `requestPaint()` and still fires at the scale's ~10 Hz. So the blank lands within ~100 ms of the phase change and then persists for the rest of the stop-overlay window.

The page is mounted for that window because `main.qml`'s `onShotEnded` shows the stop overlay and defers navigation to `stopOverlayTimer`; `onShotEndedShowMetadata` then replaces the page with shot review (or idle, when `postShotReviewTimeout` is 0).

The gate itself came from commit `304769ea` (it landed incidentally inside the #855 settings-split PR, so citing the PR number alone sends a reader to three commits of unrelated domain-object work). Its comment names the case it was defending: a residual scale reading — notably a hot-water pour's frozen weight — drawing a full cup during `EspressoPreheating`, before any coffee exists. That defence must survive this change.

Review found that the defence was in fact **incomplete before this change**, and had been since it was written: `hasExtraction` correctly forced `fillRatio` to 0, but the liquid canvas then adds a `+0.12` crema boost and bails out only on weight, so a resting cup's weight drew a 12%-full cup labelled with that weight. The gate never actually reached the drawing code. Fixed here rather than left as pre-existing, since this change depends on the guarantee it was supposed to provide.

## Goals / Non-Goals

**Goals:**

- The cup keeps showing the coffee it drew, from the end of extraction until the view is destroyed by navigation.
- The hold survives the cup or portafilter being lifted off the scale.
- The cup's weight text agrees with the fill it sits on for the whole hold.
- A partially-extracted shot that was stopped early holds its partial fill.
- A new espresso cycle on a surviving view instance starts empty.

**Non-Goals:**

- Changing what `MachineState.scaleWeight` publishes. Its other readers — `shotserver.cpp:2337`, `mqttclient.cpp:1100`, and four MCP surfaces (`mcptools_machine.cpp:130,235`, `mcptools_scale.cpp:94`, `mcpresources.cpp:77`, `mcptools_presets.cpp:88`) — must keep seeing the live value. (The widget snapshot and SAW learning were named here in an earlier draft and do **not** read it: `machinestatussnapshot.cpp` carries no weight field, and `ShotTimingController` takes weight through `onWeightSample()` straight from the weight processor.)
- Changing the espresso page's info-bar weight readout.
- Reviving `animTimer` for the hold window, or animating anything during it.
- Changing when the page navigates away.

## Decisions

**Latch a flag rather than widening the phase list.** Adding `Idle`/`Ready`/`Heating` to `hasExtraction` is the smaller diff and the wrong one: `Ready` is also the phase in which a user can reach the espresso page *before* a shot (`main.qml` pushes it from the idle screen), so a cup resting on the scale would render a full cup with no coffee in it — exactly the #855 regression. A latch distinguishes "the phase we are in" from "extraction has happened on this view", which is the property actually being asserted.

**Latch on the flow phases — `Preinfusion`, `Pouring` and `Ending` — not `EspressoPreheating`.** The distinction is flow versus pre-flow, not position in the sequence: latching at preheat would reintroduce the residual-weight case in full, while `Ending` cannot be reached without having flowed, and including it covers a view created mid-shot during `Ending` (a chart-to-cup toggle) that would otherwise never latch at all.

**Hold ends by component destruction, not by a rule.** The view lives inside a `Loader` inside the page; `StackView.replace()` destroys it. So "until the screen changes" is the natural lifetime of the latch, and there is nothing to expire. This also keeps the fix inside the no-timers-as-guards rule — no dismissal timer, no countdown, no "how long should the hold be" tuning parameter.

The only clearing rule needed is for the *next* cycle on a surviving instance: clear on entry to the espresso cycle (`EspressoPreheating`, or the machine driving straight to `Preinfusion`). This is the one case where the view outlives a shot.

**Hold the weight locally, not at the source.** Freezing at the source has precedent — `MachineState::scaleWeight()` returns `m_hotWaterFrozenWeight` after a hot-water SAW trigger, for the same reason (a consumer reads it after the phase has already gone Idle). That precedent does not extend here. The hot-water freeze earned source placement because the *completion overlay* — a different component — needed a stable value; here exactly one view needs it, and `scaleWeight` has other readers who want the truth. Centralising would trade a small view change for a change in what every one of them sees.

The consequence to accept: `CupFillView` is now the only place that deliberately shows a weight the scale is no longer reporting, and only during the hold. That divergence is the feature — the cup is showing what was extracted, not what is currently on the scale.

**Track the peak weight; do not sample once at the boundary.** The first implementation captured `heldWeight` at the transition out of the cycle. Review found two defects in that, both fixed by tracking the running peak from first flow instead:

- The puck drips after the stop and the *saved* yield includes it — `finalWeight` in `maincontroller.cpp` comes from the post-settling weight, and settling runs for up to ~10 s (`MAX_PLAUSIBLE_POST_STOP_DRIP_G` is 5.0). A boundary sample froze the cup several grams under the number the shot record, the review page and the page's own live readout all report, with that readout sitting right beside the cup.
- Lifting the cup drives the scale negative, so a boundary sample taken at that instant froze a *negative* weight for the rest of the view's life — an empty cup that putting the cup back could no longer fix. The freeze turned a transient into a permanent wrong result.

Peak-tracking also removes any question about whether the phase handler or a binding on the same signal runs first, since nothing is captured at the boundary any more.

**One held value covers both blank paths.** Routing the fill, the crema and the view's weight text through a single `displayWeight` closes the `hasExtraction` gate *and* the `currentWeight <= 0` early-return in the liquid canvas. Fixing only the phase gate would leave a cup that empties when it is picked up.

**Freeze the frame; do not re-arm the timer.** `animTimer` stopping at the phase boundary is what makes the hold a still image. Restarting it to keep steam wisps drifting would cost 30 fps of canvas repaint for three seconds of decoration, against a rule that asks for a user-felt win before adding machinery. The still frame is the requested behaviour.

## Risks / Trade-offs

- **A shot aborted during preheat never latches, so the cup stays empty.** → Correct: no coffee was extracted. Worth stating because it will look like the fix failing if it is tested by aborting early.
- **The hold is only visible when the page outlives the phase change.** Observed during verification: stopping a shot early navigates to the shot review immediately, rather than waiting out the post-shot stop overlay that creates the ~3 s window on a normally-completed shot. The view is destroyed at once, so the hold has nothing to hold through. The latch is unconditional and costs nothing on that path — no special case was added for it. The mechanism behind the immediate navigation was not investigated; it is out of scope here. → Recorded so the hold is not later "fixed" or extended because it appears inert after an early stop.
- **The held weight is captured at the phase transition, which can precede the final settling drips.** → The number is the same one the cup was already drawing a moment earlier, so the transition is invisible; the shot record and review page remain the authority on final weight.
- **Divergence between the cup's text and the page's info-bar readout during the hold** (the info bar stays live and will drop if the cup is lifted). → Accepted: the info bar is a live-scale instrument, the cup is a picture of the shot. Revisit only if it reads as a bug in practice.
- **No automated coverage.** → QML is verified manually in this project. The manual check is a normal shot, an early-stopped shot, and lifting the cup during the hold, with the cup fill view selected.

## Reviewed and deliberately not changed

- **`Phase::Refill` mid-shot freezes a partial pour.** `Refill` is outside the espresso cycle and is not handled by `main.qml`'s phase-navigation ladder, so the espresso page stays mounted and the cup holds the partial fill. That is what was actually extracted, and the modal refill dialog tells the user what happened; the view has no notion of *why* a cycle ended and giving it one is a larger change than this. `Disconnected` and `Sleep` both navigate away, so they resolve themselves.
- **Toggling chart↔cup during the hold loses it.** The two views share a `Loader`, so the toggle destroys and recreates the instance with nothing latched. The user explicitly changed what they were looking at; seeding a fresh instance from durable state would mean giving the view a data source it does not otherwise need.
- **The page's info-bar readout stays live while the cup holds.** With peak-tracking the two now agree in the normal case, since both follow drip upward; they diverge only when the cup is lifted, where the info bar is doing its job as a live instrument.
- **No accessible name on the cup's weight text.** The page already announces weight from the live value (`EspressoPage.qml`), so the held number is not exposed to assistive technology. Worth fixing, but it is an accessibility gap in the original component rather than something this change introduces.

## Open Questions

None.
