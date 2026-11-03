# Change: Arm milk capture only on request

## Why

Milk auto-capture was armed whenever the preparation mode was `steam`. The mode stays `steam` after any steam, after the review-page milk button and after the steam page opens, so the next load on the scale was taken for milk. The tablet log of 2026-11-01/02 shows the espresso cup captured twice (206.8 g and 211.8 g) seconds before a shot, each rewriting the next steam's time (51 s and 53 s instead of the 65 s preset).

## What Changes

- Milk is captured only for a sample the user asks for, from one of two places: the review-page milk button, or the steam pitchers on the idle screen (the Steam row, or the compact Steam popup on the idle page).
- The selected pitcher must be weight-timed (weight-timed steaming on, not a heater-off preset) with a saved empty-pitcher weight.
- Each offer collects one sample. It ends on capture, when the offering surface closes (pitchers hidden, idle page or review page left), or when any operation other than a steam-wand purge starts. A purge stays on the idle page, so the Steam row and its offer stay open through it.
- Selecting the steam mode, opening the steam page and finishing a steam no longer arm capture.
- The Steam widget's Home row and its bar popup share one pitcher picker. Picking a pitcher in the popup keeps it open, as on Home; starting steam or a purge closes it.
- Selecting a different pitcher in the Steam row or popup asks for a new sample, since a capture is the old pitcher's. Leaving the idle page closes the Steam row and popup; nothing that leaves the idle page up closes them.
- The legacy HTTP `milk-start`/`milk-cancel` routes are removed: they had no callers, and their fixed owner could never be released.
- On the review page, tapping the button while a sample is offered cycles the pitcher instead of cancelling; the offer stays open for the new pitcher.
- The host keeps one attempt per offer owner (client id + surface) and releases a client's offers when its remote session closes, so a closed browser cannot leave capture armed. An offer from a client that is not connected is refused. Withdrawing during a running steam does not restore the earlier milk.
- The live net-milk weight shows on the selected pitcher's pill only; the other pills weighed the same load against their own empty weights.
- Steam time comes only from captured milk. A load resting on the scale at steam start, a pitcher lifted before it settled, and the steam page's live expected-time preview no longer change it. Offering a sample keeps the earlier capture; selecting another pitcher clears it. A restart returns to the selected pitcher's own time.

## Impact

- Affected specs: `weight-timed-steaming`, `live-milk-readout`
- Affected code: `src/controllers/preparationcontroller.{h,cpp}`, `src/controllers/maincontroller.{h,cpp}`, `src/remote/steamservice{.rep,source.cpp}`, `src/remote/machinecontrolservicesource.cpp`, `qml/pages/SteamPage.qml`, `src/core/preparationstate.cpp`, `qml/components/HomePresetActions.qml`, `qml/pages/IdlePage.qml`, `qml/components/layout/items/SteamItem.qml`, `qml/components/SteamPitcherPicker.qml` (new), `qml/components/PresetPillRow.qml`, `CMakeLists.txt`, `src/webclient/CMakeLists.txt`, `qml/pages/PostShotReviewPage.qml`, `qml/components/MilkCapture.qml`, `src/core/preparationstate.h`, `src/remote/preparationservice{.rep,.h,.cpp}`, `src/remote/preparationservicesource.{h,cpp}`, `src/network/shotserver_session.cpp`, `src/network/sessionactionrequest.{h,cpp}`, `docs/CLAUDE_MD/SHOTSERVER.md`, `src/core/steamcalculations.h`, `src/core/settings_brew.{h,cpp}`, `src/main.cpp`, `tests/tst_preparation{controller,service,state}.cpp`, `tests/tst_mcptools_presets.cpp`, `tests/tst_sessionactionrequest.cpp`
