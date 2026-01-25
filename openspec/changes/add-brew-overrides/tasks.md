## 1. Settings Layer (Session-Only Overrides)
- [x] 1.1 Add `brewDoseOverride`, `brewYieldOverride`, `brewGrindOverride` Q_PROPERTYs with `has*` flags
- [x] 1.2 Implement `clearAllBrewOverrides()` method
- [x] 1.3 Implement `brewOverridesToJson()` serialization (includes temperature override)
- [x] 1.4 Clear grind and dose overrides when a bean preset is applied

## 2. Controller Logic
- [x] 2.1 Add `activateBrewWithOverrides(dose, yield, temperature, grind)` to MainController
- [x] 2.2 Save temp/weight to profile via `getCurrentProfile()` + `uploadProfile()` (same pattern as ProfileEditor)
- [x] 2.3 On profile switch: clear only target weight and temperature overrides (not grind/dose)
- [x] 2.4 Clear brew-by-ratio mode in `loadProfile()` when switching profiles
- [x] 2.5 Populate dose from shot history in `loadShotWithMetadata()`
- [x] 2.6 Capture overrides JSON in `onShotEnded()` before saving
- [x] 2.7 Do NOT clear overrides after shot ends (overrides persist between shots)
- [x] 2.8 Pass overrides JSON to shot history save

## 3. Shot History Storage
- [x] 3.1 Add `brew_overrides_json` column (DB migration v2)
- [x] 3.2 Update `saveShot()` to accept and store `brewOverridesJson`
- [x] 3.3 Update `getShotRecord()` / `getShot()` to return `brewOverridesJson`
- [x] 3.4 Update `getShotsFiltered()` to include `grinder_setting` in list results
- [x] 3.5 Update import/export to handle `brew_overrides_json`

## 4. BrewDialog UI
- [x] 4.1 Create `BrewDialog.qml` with temperature, dose, grind, ratio, yield inputs
- [x] 4.2 Show "Base Recipe" header (profile name + bean info)
- [x] 4.3 Temperature input with profile-default indicator and "Save to profile" button
- [x] 4.4 Dose input with "Get from scale" button and low-dose warning
- [x] 4.5 Grind text input (hidden when Beans feature not enabled)
- [x] 4.6 Yield edit updates ratio automatically (yield / dose)
- [x] 4.7 "Save yield to profile" button
- [x] 4.8 "Clear" button resets to profile defaults: temp=profile, dose=18g, grind=bean default, ratio=profileTargetWeight/18
- [x] 4.9 OK button calls `activateBrewWithOverrides()`

## 5. IdlePage Integration
- [x] 5.1 Add shot plan summary text line (profile, temp, bean, grind, dose, yield)
- [x] 5.2 Show temperature override as arrow notation (e.g., "88 → 90°C")
- [x] 5.3 Make shot plan text clickable to open BrewDialog
- [x] 5.4 Display brew overrides vs DYE defaults (grind, dose, yield)
- [x] 5.5 Hide grind in shot plan when Beans feature (visualizerExtendedMetadata) is disabled

## 6. StatusBar Integration
- [x] 6.1 Replace `BrewRatioDialog` reference with `BrewDialog`

## 7. IdlePage Temperature Display
- [x] 7.1 Show effective target temperature (override or profile) in status section
- [x] 7.2 Show "(override)" label and blue coloring when temp override is active

## 8. Shot Plan Visibility Settings
- [x] 8.1 Add `showShotPlan` (bool, default: true) and `showShotPlanOnAllScreens` (bool, default: false) Q_PROPERTYs to Settings
- [x] 8.2 Add toggles in Settings → Options tab for "Show shot plan" and "Show on all screens"
- [x] 8.3 Guard IdlePage shot plan text with `Settings.showShotPlan` visibility binding
- [x] 8.4 Add shot plan text to StatusBar (between page title and indicators) visible when both settings are enabled
- [x] 8.5 The StatusBar shot plan reuses the same text logic and opens BrewDialog on tap
- [x] 8.6 Hide IdlePage's inline shot plan when "Show on all screens" is active (avoid duplication)
