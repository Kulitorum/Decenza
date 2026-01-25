## 1. Temperature Override Delta (MainController)
- [x] 1.1 In `uploadCurrentProfile()` (maincontroller.cpp:1062-1074): compute `delta = overrideTemp - m_currentProfile.espressoTemperature()` and apply `steps[i].temperature += delta` instead of absolute assignment
- [x] 1.2 Set `modifiedProfile.setEspressoTemperature(overrideTemp)` (already correct — reflects new base)
- [x] 1.3 Update `groupTemp` to use override value for `setShotSettings()` (already correct)

## 2. Profile Editor Global Temperature Delta (QML)
- [x] 2.1 In `ProfileEditorPage.qml` (line 489-497): compute delta from current first-frame temperature (`profile.steps[0].temperature`) and apply `profile.steps[i].temperature += delta` instead of absolute assignment
- [x] 2.2 Update `profile.espresso_temperature` to the new first-frame value (newValue)

## 3. Validation
- [ ] 3.1 Verify with a multi-temp profile (e.g., adaptive_v2.json: frames at [93,93,88,88]) that override preserves offsets
- [ ] 3.2 Verify profile editor preserves offsets when changing global temp
- [ ] 3.3 Verify uniform-temp profiles still work correctly (delta produces same result as absolute)
