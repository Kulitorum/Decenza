# Phase 3: Settings & Recipe Validation

> **For Claude:** Create both test files, add CMake targets, build and run. RecipeParams expected values from de1app Tcl source (de1app at `/Users/jeffreyh/Documents/GitHub/de1app` or `C:\code\de1app`).

**Rationale:** RecipeParams validation controls whether frames regenerate — a false negative in `frameAffectingFieldsEqual` means stale frames uploaded to the machine. Settings had 283 touches and the DYE migration, bean preset, and import filtering bugs.

---

## Task 1: `tests/tst_recipeparams.cpp` (~20 tests)

**Bug context:** `frameAffectingFieldsEqual` comparator controls frame regeneration. Parameter clamping guards against out-of-range values that could damage the machine. Editor type defaults must match de1app.

**Source deps:** `src/profile/recipeparams.cpp` only. No mocks, no MOC, no friend access.

**CMake addition:**
```cmake
add_decenza_test(tst_recipeparams tst_recipeparams.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/recipeparams.cpp)
```

### Test Cases

#### Serialization

- [ ] `jsonRoundTrip` — set all fields to non-default values, serialize to QJsonObject, deserialize, compare field-by-field
- [ ] `variantMapRoundTrip` — same for QVariantMap (QML integration path)
- [ ] `jsonMissingFieldsUseDefaults` — empty QJsonObject → all fields at defaults
- [ ] `jsonExtraFieldsIgnored` — JSON with unknown keys → no crash, valid result

#### Clamping & Validation

- [ ] `clampPressureRange` — fillPressure=20 → clamped to maximum safe value
- [ ] `clampNegativeValues` — negative flow → clamped to 0
- [ ] `clampZeroValid` — 0 is valid for optional fields (e.g., infuseTime=0 means disabled)
- [ ] `validateReportsOutOfRange` — preinfuseFrameCount=25 → error string returned
- [ ] `validateAcceptsSentinel` — preinfuseFrameCount=-1 is valid sentinel
- [ ] `validateAcceptsNormalValues` — all fields in range → empty error string

#### Editor Type

- [ ] `editorTypeRoundTrip_data` — data-driven: each EditorType string conversion (DFlow, AFlow, Pressure, Flow)
- [ ] `editorTypeDefaultFallback` — unknown string → DFlow
- [ ] `applyEditorDefaultsDFlow` — after applyEditorDefaults(DFlow): preinfuseFrameCount=2 (de1app default)
- [ ] `applyEditorDefaultsPressure` — after applyEditorDefaults(Pressure): verify settings_2a defaults match de1app

#### Frame Regeneration Gating

- [ ] `frameAffectingFieldsEqualWhenOnlyWeightDiffers` — change targetWeight only → returns true (skip regen, weight doesn't affect frames)
- [ ] `frameAffectingFieldsEqualWhenOnlyDoseDiffers` — change doseWeight only → returns true
- [ ] `frameAffectingFieldsEqualFalseWhenFlowDiffers` — change pourFlow → returns false (trigger regen)
- [ ] `frameAffectingFieldsEqualFalseWhenPressureDiffers` — change infusePressure → returns false
- [ ] `frameAffectingFieldsEqualFalseWhenTimeDiffers` — change holdTime → returns false
- [ ] `frameAffectingFieldsEqualFalseWhenTempDiffers` — change espressoTemperature → returns false

---

## Task 2: `tests/tst_settings.cpp` (~20 tests)

**Bug context:** Settings had 283 file touches, 45% fix rate. DYE field migration, bean preset lazy migration, and screen brightness import filtering (bug #495) are the key bug areas. 130+ settings must persist correctly across app restarts.

**Source deps:** CORE_SOURCES. Settings is a QObject (MOC automatic). Uses QSettings which works headlessly with a temp file.

**CMake addition:**
```cmake
add_decenza_test(tst_settings tst_settings.cpp ${CORE_SOURCES})
```

**Test setup pattern:** Each test creates a Settings instance with a unique QSettings scope (e.g., `QSettings::Scope::UserScope` with a test-specific organization/application name) so tests don't interfere with each other or real settings.

### Test Cases

#### Core Property Round-Trip

- [ ] `targetWeightPersists` — set to 36.0, create new Settings instance from same QSettings, verify 36.0 loaded
- [ ] `waterVolumePersists` — set to 300, reload, verify 300
- [ ] `scaleAddressPersists` — set BLE MAC address string, reload, verify exact match
- [ ] `steamTemperaturePersists` — set to 155, reload, verify 155
- [ ] `defaultShotRatingDefault` — fresh Settings → default is 75
- [ ] `booleanSettingDefaults` — fresh Settings → ignoreVolumeWithScale is false, autoTare is true (verify actual defaults)

#### Structured Data

- [ ] `dyeFieldsPersist` — set dyeBeanBrand, dyeGrinderBrand, dyeGrinderBurrs, dyeGrinderModel → reload → all match
- [ ] `favoriteProfilesRoundTrip` — add 3 favorites, reload, verify list contains all 3 in order
- [ ] `favoriteProfilesAddRemove` — add, remove middle, reload, verify remaining 2
- [ ] `beanPresetsRoundTrip` — save complex preset with brand/origin/roast/grinder fields, reload, verify all nested fields

#### Theme & Display

- [ ] `themeModePersists` — set "dark", reload, verify "dark"
- [ ] `themeModeDefault` — fresh Settings → "system" (or whatever the default is)

#### Migration Safety

- [ ] `unknownKeysPreserved` — write QSettings with an unknown key, create Settings, verify the key survives (forward compat)

#### Range Boundaries

- [ ] `steamTemperatureRange` — set to 0, 100, 175, verify accepted; set to -1, 250, verify clamped or rejected
- [ ] `shotRatingRange` — set to 0, 50, 100, verify accepted
- [ ] `targetWeightRange` — set to 0 (disabled), 0.1, 100, verify accepted

#### Settings Import (bug #495)

- [ ] `importFiltersScreenBrightness` — export settings, modify brightness to 0.01, import → brightness NOT applied (or filtered)
- [ ] `importPreservesNonDangerous` — export/import cycle preserves targetWeight, scaleAddress, theme

---

## Verification

After completing both test files:
```bash
cmake -DBUILD_TESTS=ON -B build/tests
cmake --build build/tests
cd build/tests && ctest --output-on-failure
```

Expected: all Phase 1 + 2 + 3 tests pass.
