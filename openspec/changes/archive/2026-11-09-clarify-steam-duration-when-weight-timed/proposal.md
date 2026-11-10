# Proposal

## Why

With weight-timed steaming on and a rate calibrated, a pitcher preset's Duration is only a fallback: the weighed milk sets the steam time. The Steam page shows the Duration row identically in both modes, so a user who weighs milk cannot tell the number they are editing is not the one that runs (Kulitorum/Decenza#2148, point A).

## What Changes

- The Duration row on the Steam page gains a one-line hint while weight timing is active (toggle on, rate calibrated, heater-on preset) saying the value is used when milk is not weighed.
- The same wording is added to the Duration control's accessible name in that state.
- Duration stays editable and is not greyed out: it still runs whenever no milk was captured, and a deliberately fixed preset (a long hot-chocolate steam) depends on it.
- The Steam page's preset wording drops "pitcher": the row heading becomes "Presets", the dialogs "Add Preset" and "Edit Preset", the name field "Preset name". Strings about the physical pitcher (the "Milk pitcher" weight row, placement prompts) keep "pitcher".
- Those two dialogs' 15 strings collapse to five new ones plus the shared Cancel / Save / Add / Delete keys. Screen-reader names stop restating the dialog ("Cancel editing pitcher preset" becomes "Cancel"); the name field, which takes focus on open, speaks the dialog title instead.
- No new required fields, no change to how steam time is computed.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `weight-timed-steaming`: adds a requirement that the preset Duration states its fallback role while weight timing is active, and toggling weight timing does not shift the page.

## Impact

- `qml/pages/SteamPage.qml` only (shared native/Wasm QML, so the web client picks it up). Reads `SteamService.state.milkAutoCaptureEnabled` and `steamSecondsPerGram`, both already published; no C++, service or settings change.
- Six new translation keys (the hint and five reworded strings; a reused key would keep showing its existing translation of the old wording). Fifteen page-local keys lose their readers.
- Manual: the three places where the wiki Manual says "pitcher preset" (recipe milk settings, Steam how-to, quick start) say "preset". No new section: the hint states its own meaning on screen.
