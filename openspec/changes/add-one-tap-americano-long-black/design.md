## Context

- A recipe's hot-water block already stores `order` (`after` | `before`), set by the wizard, MCP `recipe_update` and the web form. Nothing reads it at brew time: `Recipe::deriveDrinkType` uses it only to label the recipe.
- Activation already pushes the vessel's amount, temperature and flow into the live hot-water settings (`applyHotWaterSettings`), so the DE1 is configured for the second step before the drink starts. Swapping the vessel deactivates the recipe, so a sequence never pours a vessel other than the recipe's.
- A recipe start from the app goes through `MainController::dispatchHomeRecipeStart`, which ends in `m_device->startEspresso()`. GHC and MCP starts bypass it.
- `DE1Device::startHotWater()` / `startEspresso()` write `GHC_MODE` and request the state directly. The app's own start gate (`MachineControlStartPolicy::machineCanStart`, which refuses when the GHC is active) sits above them in the control service. The sequencer calls the device directly, so the firmware decides.
- Hot water auto-tares 200 ms after its flow starts (`MachineState`, `m_hotWaterTarePending`). An espresso stopped by weight is still settling for up to 10 s after the stop (`ShotTimingController::startSettlingTimer`). Without a change here, the tare reads as a cup lift.
- `startEspresso()` defers up to `PROFILE_UPLOAD_SETTLE_MS` (500 ms) after a profile upload. Activation uploads long before the hot water ends, so a long black's espresso request is not deferred in practice. Even when it is, 500 ms is inside the 5 s window.

## Goals / Non-Goals

**Goals:**
- One place decides whether a recipe sequences and which step comes first. The sequencer, the pill start and `deriveDrinkType` all call it.
- The sequence follows machine events, so a GHC, MCP or web start behaves like a pill tap.

**Non-Goals:**
- No firmware-version or GHC gate (decided with the maintainer: a refused request costs nothing, and long black on GHC machines needs no code change when Decent ships the firmware).
- No retry, and no chaining beyond two steps.
- Milk-and-water recipes.

## Decisions

### D1. One predicate on the recipe blocks
`RecipeEditFields::drinkSequence(profileTitle, steamJson, hotWaterJson)` returns `None`, `WaterAfter` or `WaterBefore`. It is header-only beside `hotWaterActive`, so the sequencer and its tests need no storage library. `deriveDrinkType` calls it for its americano/long-black branch, so the label and the behaviour cannot disagree. Per `RECIPES.md`, machine behaviour reads the blocks, never the stored `drink_type`. The stored type is presentation, and a legacy row may have none.
*Alternative:* key on `drink_type`. Rejected: the stored type is user intent and can be empty, while the blocks are what the machine will actually do.

### D2. A `DrinkSequenceController` driven by `MachineState`
A new controller in `src/controllers/`, created by `MainController`.
- **Arm** on `MachineState::shotFlowStarted(phase)`: if phase is Espresso pouring and the active recipe's sequence is `WaterAfter`, the follow-on is hot water. If phase is HotWater and the sequence is `WaterBefore`, the follow-on is espresso. The sequence is read from MainController's active-recipe cache at that moment.
- **Fire** at the later of the shot's settled end (`ShotTimingController::shotProcessingReady`, americano only) and `IDLE_DELAY_MS` (4 s) after the DE1 reports Idle; then disarm. The 4 s is the maintainer's choice to let the cup finish dripping, and it sits inside firmware 1359's 5 s window. Real shots on the tablet (21 stop-by-weight shots, 2026-10-30) settled 1.7-2.4 s after the stop and reached Idle 4.4-6.1 s after it, so the delay is normally the later event and settling has finished 6-8 s before the water. The delay is a QTimer started at Idle and stopped on drop, skip or fire.
- **Drop** on any other phase after the flow (Sleep, Disconnected, another operation), on Skip, and on firing.

Arming on flow start, not on the request, is what makes a shot stopped before it pours produce no follow-on, and makes GHC-started steps count.
*Alternative:* sequence in QML from the operation pages. Rejected: MCP and GHC starts would not sequence, and the shared native/Wasm UI would have to carry machine logic.

### D3. A long black starts with hot water
`dispatchHomeRecipeStart` calls `startHotWater()` when `RecipeEditFields::drinkSequence` is `WaterBefore`, and `startEspresso()` otherwise. For a long black it first tells the sequencer to expect that hot water: a long black arms only on the hot water its recipe tap started, so hot water for tea with a long black recipe selected does not pull a shot. An americano arms on any espresso flow, since an espresso with that recipe selected is the drink. Everything before that call (activation, applied checks, completion signal) is unchanged. The completion result still says "dispatched".

### D4. Hot water ends settling, before its tare
`ShotTimingController` gains one entry, called from the HotWater flow-start edge. That edge comes before the 200 ms tare.
- With a clean average captured (`m_lastCleanSettlingAvg > 0`), the weight is set to it and settling completes through the normal completion path, so learning runs.
- Otherwise it saves what a cup lift saves (`abandonedSettleWeight` with no disturbance: the last reading, or the settle base if below it), skips learning, and logs hot water as the reason. The reading at hot water's flow start is real drip, so it is not discarded for the stop weight.

`endShot` makes the same call when hot water is already pouring by the time the shot's queued end arrives, which only a hand-started hot water at the Idle report can produce: a sequenced americano waits for the settle. This applies to any hot water during settling, not only sequenced pours. A manual hot-water tap in that window would corrupt the shot the same way today.
*Trade-off:* a clean average needs only 250 ms of stability (`SETTLING_CLEAN_CAPTURE_MS`), where a full settle needs 1000 ms (`SETTLING_STABLE_MS`). Learning from it accepts a slightly earlier reading so that americano drinkers keep SAW learning. The alternative was skipping learning on every americano shot.

### D5. Skip through the shared UI services
The controller exposes `pendingStep` (`none` | `hotWater` | `espresso`) and `skip()`. They reach the Espresso and Hot Water pages through `MachineControlService` (a `.rep` property, the facade, and a `skip-drink-step` command validated by the native source against the operation context), following `SHARED_UI.md`. Each page shows one compact `AccessibleButton`: "Skip" followed by the water or espresso icon (`iconAfterText`), with the accessible name "Skip water" or "Skip espresso".

### D6. Logging
A `[Recipes]` line at INFO through the registered `DIAG_INFO(RECIPES, "DrinkSequenceController")` for arm, fire, skip and drop, each naming the recipe and the step. A request the DE1 does not act on within `START_TIMEOUT_MS` logs at WARN ("did not start within 3 s of the request, so the DE1 likely refused it"), with the firmware build and GHC state, which is enough to diagnose without a gate.

### D7. The simulator models the firmware's GHC rule
In simulation the app's own controls stand in for GHC taps, so the simulator cannot tell a tap from the sequencer's request by looking at BLE. The sequencer therefore calls `DE1Device::startUnattended(state)`. On hardware that is exactly `startHotWater()` / `startEspresso()`. In simulation with the GHC active (the Debug headless toggle off), it asks `DE1Simulator::acceptsUnattendedStart`, which models build 1359: HotWater within 5 s of an espresso returning to Idle, nothing else. A refusal is logged, as hardware without the window would leave it. Long black is refused on GHC until Decent ships its window, which is then one more condition there.

### D8. The simulator ends a shot the way the DE1 does
A stop during preinfusion or pouring now enters Espresso/Ending, as the profile running out already did, and Ending lasts `ENDING_SECONDS` (5 s, the measured median) before Idle. Before this the simulator went straight to Idle on a stop and ended Ending when pressure bled off. That let hot water start before the espresso's queued end reached `endShot`, an order a real DE1 does not produce.

### D9. One drink on screen
`drinkSequenceActive` (sequencer `isActive()`, carried by `MachineControlService`) runs from arming until the second step has ended, or is skipped, dropped or refused. It deliberately outlasts the second step's start: in the simulator the DE1-driven property was observed arriving before the queued phase change, so a flag that ended at the start would let a shell open the review before the second step's page. Refusal has no event, since the DE1 answers a refused request with silence, so `START_TIMEOUT_MS` (3 s) bounds the wait for the requested state. It is the one time-based decision here, and its log line names the likely cause.
The two shared presentation components hold on it, so both shells inherit the behaviour: `EspressoCompletionCoordinator` defers leaving the shot page and opening the review until the drink ends; `OperationCompletionOverlay` holds the first step's completion message, drops it if the second step starts (its page replaces this one), and shows it only if the machine is still idle when the drink ends.

### D10. Cup view shows the whole drink
`OperationNavigationPolicy.machineOperationPage(phase, phases, drinkInCupView)` maps HotWater to the Espresso page while a drink is active and the cup view is selected. `AppShell.drinkInCupView` is the one place that flag is computed, and every caller passes it. `CupFillView` gains a water input: it tracks the water's peak during HotWater (the scale is tared at the water's start), keeps it across the espresso for a long black, and stops the coffee peak from following the scale during the water. The cup is drawn half coffee, half water (the maintainer's choice: true ratios leave a shot at ~10% of the cup, which barely moves), each half filling by its own progress; the weight text reports real grams. The liquid lightens toward a diluted brown by the water share, an americano's crema fades as water is added while a long black's is kept, and the stream is pale and translucent while water flows. The page's Stop calls `stopHotWater` during HotWater.

## Risks / Trade-offs

- [The cup must catch both outlets. The 5 s window leaves no time to move it.] → Accepted: this is the drink the firmware was built for. Skip covers the occasional exception.
- [On firmware without the window, a GHC machine may react to the request in an unexpected way rather than ignore it.] → Check on a pre-1359 DE1 during validation, and record what the machine does in the manual entry if it is visible.
- [Hot water run to warm a cup with a long black active pulls a shot.] → Skip on the Hot Water page, and the line naming the follow-on is visible for the whole pour.
- [A refused request leaves no in-app feedback.] → The INFO log line covers diagnosis. A user-facing notice would be a guess about firmware behaviour we have not observed.

## Migration Plan

No data migration: `order` already exists on every hot-water block. Rollback is reverting the code. Stored recipes are untouched.
