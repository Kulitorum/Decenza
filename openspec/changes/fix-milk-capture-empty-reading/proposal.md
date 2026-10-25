## Why

Since capture attempts stopped taring the scale (add-review-page-milk-weigh), milk weighing fails whenever the empty scale does not read zero (#2058). The capture forgot the empty reading each time it disarmed, so a pitcher already on the scale when Steam was selected, or the review-page button tapped, became the zero and weighed 0 g. The steam-start and pitcher-pill fallbacks subtracted the pitcher from the raw reading, so on a scale reading −450 g empty they found no milk and steam ran the fixed time.

## What Changes

- The milk and bean captures learn the empty reading whenever the scale is connected, not only while a capture attempt is armed. Arming measures a load already on the scale from it. A tare, a scale switch or a disconnect still clears it.
- Every net-milk reading taken from the scale (steam-start time, last on-scale milk, pitcher selection, the pills and the Net milk readout) is measured above that learned empty reading, falling back to the raw reading only before one has been learned.

## Impact

- Specs: `weight-timed-steaming` (Capture attempts do not tare).
- Code: `StableWeightCapture`, `PreparationController`, `PreparationState::loadOnScaleG`, `MainController::selectSteamPitcher`, `SteamServiceSource`, `SteamLabels.qml`.
