## Why

An americano or long black recipe already records its hot water and the pour order, but the user still starts the second step by hand. On a machine with an active group head controller (GHC), that means a second tap on the machine. DE1 firmware 1359, built by Decent for Decenza, accepts a BLE hot water request for 5 seconds after an espresso ends without a GHC tap. That makes a one-tap americano possible. A matching window that lets espresso follow hot water (for the long black) is coming in a later firmware.

## What Changes

- An americano recipe (hot water ordered `after`, no milk) becomes one drink. When its espresso ends, the app requests hot water at the later of the shot settling and 4 s into Idle.
- A long black recipe (hot water ordered `before`, no milk) becomes one drink. Starting the recipe from the app starts the hot water first, and when that ends the app requests the espresso.
- The second step is always requested, regardless of firmware or GHC state. Where the firmware refuses it, the drink stops after the first step, as it does today. Long black therefore starts working on GHC machines when Decent ships its firmware, with no Decenza change.
- The first step's operation page names the step that follows and offers Skip, for when the first step was not meant as part of the drink (for example hot water run to warm a cup).
- When hot water starts during the espresso's post-shot settling, settling ends there. A weight that has already held steady is recorded as a normal settle, and SAW keeps learning from it. Otherwise the last reading before the water is saved and learning is skipped, as for a cup lift.
- Out of scope: recipes that combine milk and hot water. When and where their water goes is not settled, so they keep today's behaviour.

## Capabilities

### New Capabilities

- `recipe-drink-sequence`: a recipe with milk-less hot water runs as one two-step drink. Covers which recipes qualify, which step starts first, how the second step is requested and skipped, and when a sequence is dropped.

### Modified Capabilities

- `stop-at-weight-learning`: "Final Weight Capture After Settling" gains the case where hot water starts during settling.

## Impact

- New `src/controllers/` sequencer driven by `MachineState` phase changes, so it runs however the first step was started: recipe pill, Espresso button, GHC, or MCP.
- `MainController::dispatchHomeRecipeStart` starts hot water instead of espresso for a long black.
- `ShotTimingController` ends settling when hot water starts.
- Shared native/Wasm UI: a Skip button on the Espresso and Hot Water pages, through the existing services (`SHARED_UI.md`).
- Wiki manual: a short entry on one-tap americano and long black.
