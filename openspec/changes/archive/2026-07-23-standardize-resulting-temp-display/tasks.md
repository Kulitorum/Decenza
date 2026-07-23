## 1. Implementation (landed in PR #1616)

- [x] 1.1 BrewDialog Temp Delta sub-indicator shows the resulting temp (frames shifted by the dialed offset), `hasOverride=false`, highlight retained.
- [x] 1.2 ShotPlanText live-plan branch shows the effective resulting temp (`eff - profileTemp` shift, `hasOverride=false`), highlight via `_tempOverride`.

## 2. Spec sync

- [x] 2.1 `brew-overrides`: MODIFIED "Brew Dialog" (sub-indicator shows resulting temps, no tag) and "Shot Plan Display" (resulting temps, highlight, no tag; scenarios updated).
- [x] 2.2 Confirmed `recipe-quick-switch` already specs resulting-temp for recipe cards (no delta needed) and `recipe-aware-brew-settings` is unaffected (stepper reading `0°` and the highlight scheme unchanged).

## 3. Verification

- [x] 3.1 Qt Creator build clean; full suite green (no C++ behavior change).
- [ ] 3.2 Visual check in the running app: Brew Dialog sub-indicator and live Shot Plan show the resulting temp and highlight on override; recipe at offset 0 reads unchanged.
