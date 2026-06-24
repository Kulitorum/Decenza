## 1. Anchor unification (bug fix)

- [x] 1.1 Add a single `ProfileManager` method that applies a temperature override as a delta from `espressoTemperature` (the live-brew anchor), returning/uploading the shifted frames, so the math lives in one place. *(Added `framesShiftedToTemperature()` helper + `applyTemperatureToProfile()`.)*
- [x] 1.2 Change the BrewDialog "Update Profile" handler to use that method (anchor on `espressoTemperature`) instead of `steps[0].temperature`.
- [x] 1.3 Confirm the live-brew path and the save path now compute an identical delta for the same input. *(Both call `framesShiftedToTemperature()`; covered by `tst_ProfileManager::applyTemperatureUsesEspressoTemperatureAnchor`.)*

## 2. Distinct-temp data on ProfileManager

- [x] 2.1 Expose the active profile's distinct frame temperatures for QML binding. *(Realized as `Q_INVOKABLE temperatureDisplay(anchorTemp, hasOverride, overrideTemp)` which reads the frames internally and returns the formatted string — cleaner than a raw list given the C++ formatter; QML binds without rebuilding a QVariantMap.)*
- [x] 2.2 Ensure reactivity. *(Binding passes `profileTargetTemperature` (NOTIFY currentProfileChanged) + `Settings.brew` override props, so it re-evaluates on profile and override changes.)*

## 3. Shared adaptive formatter

- [x] 3.1 Create a shared pure formatter for N=1 (single/arrow), N=2 (mid-dot list), N≥3 (first…last ellipsis) + signed delta tag. *(Implemented as pure, unit-testable C++ `TemperatureDisplay::format` in `src/profile/temperaturedisplay.{h,cpp}` — see note below.)*
- [x] 3.2 Register the new source in `CMakeLists.txt` (app + `PROFILE_SOURCES` for tests).
- [x] 3.3 Internationalize composed text; keep `·`/`…` separators as literals; format numbers per convention. *(Formatter output is numbers/symbols only; surrounding labels in BrewDialog go through `TranslationManager`.)*

## 4. Shot plan widget

- [x] 4.1 Replace the single-value/arrow temperature segment in `ShotPlanText.qml` with the shared formatter.
- [x] 4.2 Verify multi-temp profiles render list/ellipsis even with no override, and override shows post-shift values + delta tag.

## 5. Brew Settings dialog

- [x] 5.1 Change the BrewDialog temperature subtext to the adaptive structure (single / list / ellipsis); keep the stepper as the single anchor control.
- [x] 5.2 When an override is pending, show that the shift applies to all steps and preview the resulting temperatures.
- [x] 5.3 The "Update Profile" preview is the same shifted display shown in the subtext (answering "which temps get updated? all of them, by Δ").

## 6. Tests

- [x] 6.1 Unit-test the formatter: N=1/2/≥3, override/no-override, negative delta, ramp edge case, empty-frames fallback, fractional temps. *(`tst_temperaturedisplay.cpp`.)*
- [x] 6.2 Unit-test anchor parity: a profile where `espressoTemperature ≠ steps[0]` shifts from the `espressoTemperature` anchor. *(`tst_ProfileManager::applyTemperatureUsesEspressoTemperatureAnchor`.)*
- [x] 6.3 Build clean (Qt Creator) and run the full autotest suite green.

## 7. Verify

- [x] 7.1 Manually verified in the running app (iterated via screenshots): the Brew Settings "Temp Delta:" control reads 0°/±N°, the "Profile: 90 · 88°C" reference line renders with the spaced mid-dot at the larger font, and the label sizes like "Equipment:" without clipping.

---

### Implementation notes / deviations from design

- **Formatter in C++, not QML/JS.** The design *leaned* toward a JS helper to keep i18n in QML, but the formatter's output carries no translatable words (only numbers, °C, `·`, `…`, `→`, and the signed delta tag), and the C++ alternative — listed as a considered option in design.md — is unit-testable by the existing Qt Test suite (tasks 6.1/6.2). It is exposed to QML via `ProfileManager.temperatureDisplay(...)`.
- **Task 2** realized as the `temperatureDisplay()` invokable rather than a raw distinct-temp `Q_PROPERTY`, since the only consumer is the formatter.
