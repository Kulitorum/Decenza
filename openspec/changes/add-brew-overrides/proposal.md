# Change: Add Per-Shot Brew Parameter Overrides

## Why
Users want to tweak brew parameters (temperature, dose, yield, grind) for the next shot without permanently modifying the profile. This enables quick experimentation — e.g., "same profile but 1°C hotter" or "same beans but coarser grind" — while keeping the base profile intact. Overrides should be visible before the shot, recorded in history, and restorable from a past shot. If an override is not set, the profile defaults should be used. If a grind setting is not used (because the Beans feature is not used, then it should not be shown)

## What Changes
- New **BrewDialog** replaces the old BrewRatioDialog, adding temperature override, grind setting, and a "Base Recipe" info header
- **Session-only brew overrides** in Settings: dose, yield, grind (temperature override already existed, but should now move under this dialog)
- **Shot plan summary line** on IdlePage showing the configured shot parameters with override indicators
- **Shot plan visibility settings**: "Show shot plan" (enable/disable) and "Show on all screens" (bottom bar on all pages vs IdlePage only) in Settings → Options
- **Override lifecycle**: cleared after shot ends, cleared when switching profiles, cleared when switching beans and populated from shot history
- **Shot history storage**: brew overrides JSON recorded with each shot for traceability
- `MainController::activateBrewWithOverrides()` applies all overrides in one call
- `MainController::setProfileTemperature()` allows saving temp changes to profile

## Impact
- Affected specs: `brew-overrides` (new capability)
- Affected code:
  - `qml/components/BrewDialog.qml` (new)
  - `qml/components/BrewRatioDialog.qml` (modified, will be superseded)
  - `qml/components/StatusBar.qml` (references BrewDialog)
  - `qml/pages/IdlePage.qml` (shot plan line, BrewDialog instance)
  - `src/core/settings.h/.cpp` (brew override properties)
  - `src/controllers/maincontroller.h/.cpp` (activateBrewWithOverrides, setProfileTemperature, lifecycle)
  - `src/history/shothistorystorage.h/.cpp` (brew_overrides_json column, migration v2)
