# MCP Tool Test Plan

Sequential runbook to verify all MCP tools work correctly as a second UI to the QML app. Each test has explicit tool calls, expected results, and cleanup steps. Tests must be run in order — later tests depend on state from earlier ones.

## Prerequisites

- App running (simulator or production)
- MCP connected (`/mcp` in Claude Code)
- Machine in Ready or Sleep state (not mid-operation)

## State Capture (run before tests)

Save these values — they will be restored at the end:
```
Call: profiles_get_active
Save: ORIGINAL_PROFILE = filename, ORIGINAL_MODIFIED = modified

Call: profiles_get_params
Save: ORIGINAL_TEMP (tempStart for recipe, espresso_temperature for advanced)

Call: settings_get
Save: ORIGINAL_BRAND = dyeBeanBrand, ORIGINAL_GRIND = dyeGrinderSetting
Save: ORIGINAL_BEAN_TYPE = dyeBeanType, ORIGINAL_ROAST = dyeRoastLevel

Call: machine_get_state
Save: ORIGINAL_PHASE = phase (if Sleep, will wake for tests then re-sleep at end)
```

If ORIGINAL_MODIFIED is true, warn the user — tests will modify the profile and the unsaved changes will be lost.

## Run Log

| Date | Tools | Passed | Skipped | Failed | Notes |
|------|-------|--------|---------|--------|-------|
| 2026-03-20 | 37 | 41 | 1 | 0 | Initial run, simulator mode |
| 2026-04-30 | 33 | 56 | 1 | 0 | Simulator, MCP 2025-11-25 negotiated. Plan refreshed for PR #976 field renames, `profiles_save` readonly refusal, `profiles_create` empty-filename, removal of `preferences` category, and §4.10 active-delete behavior. |
| 2026-05-01 | 33 | 56 | 1 | 0 | Plan refreshed for PR #984: `shots_get_detail`/`shots_compare` default to summary mode (#979); `enjoyment0to100`/`drinkTdsPct`/`drinkEyPct` everywhere on shot reads (#980); `profiles_delete` returns actionable error on active profile (#983). |

---

## 1. Machine State (read-only, no state changes)

### 1.1 machine_get_state
```
Call: machine_get_state
Expect: phase exists, connected=true, waterLevelMl > 0, firmwareVersion non-empty, activeProfile non-empty
```

### 1.2 machine_get_telemetry
```
Call: machine_get_telemetry
Expect: temperatureC > 0, pressureBar/flowMlPerSec/scaleWeightG are numbers
```

## 2. Machine Control

### 2.1 machine_sleep
```
Call: machine_sleep (confirmed: true)
Expect: success=true
```

### 2.2 machine_wake
```
Call: machine_wake (confirmed: true)
Expect: success=true
Verify: machine_get_state → phase is "Ready" or "Idle" (not "Sleep")
Note: machine must be awake for remaining tests. Will restore ORIGINAL_PHASE at end.
```

### 2.3 machine_stop (no operation running)
```
Call: machine_stop (confirmed: true)
Expect: error containing "No operation" or similar
```

### 2.4 machine_skip_frame (no extraction running)
```
Call: machine_skip_frame (confirmed: true)
Expect: error containing "No extraction" or similar
```

### 2.5 machine_start_espresso / steam / hot_water / flush
```
SKIP if simulator mode (not headless DE1)
On real headless DE1:
  Call: machine_start_espresso
  Expect: success=true, then machine_get_state shows espresso phase
  Call: machine_stop (confirmed: true)
  Expect: success=true
```

## 3. Profile Read — All 5 Editor Types

### 3.1 profiles_list
```
Call: profiles_list
Expect: count > 0, each profile has filename, title, editorType
Verify: editorType values include at least "pressure", "flow", "dflow", "aflow", "advanced"
```

### 3.2 profiles_get_active
```
Call: profiles_get_active
Expect: filename non-empty, editorType non-empty, modified is boolean,
        targetWeightG > 0, targetTemperatureC > 0, readOnly is boolean
Save: note the filename and editorType as ORIGINAL_PROFILE and ORIGINAL_EDITOR
```
Note: target temperature field is `targetTemperatureC` (suffix), not `targetTemperature`.

### 3.3 profiles_get_detail
```
Call: profiles_get_detail (filename: "blooming_espresso")
Expect: title="Blooming Espresso", steps is array with length > 0, espresso_temperature > 0
```

### 3.4 profiles_get_params — pressure
```
Call: profiles_set_active (filename: "default", confirmed: true)
Call: profiles_get_params
Expect: editorType="pressure"
Expect present: espressoPressure, pressureEnd, preinfusionTime, holdTime, simpleDeclineTime, tempStart, tempHold, limiterValue
Expect absent: fillTemperature, pourFlow, rampTime, steps
```

### 3.5 profiles_get_params — flow
```
Call: profiles_set_active (filename: "flow_profile_for_straight_espresso", confirmed: true)
Call: profiles_get_params
Expect: editorType="flow"
Expect present: holdFlow, flowEnd, preinfusionTime, holdTime, simpleDeclineTime, tempStart, limiterValue
Expect absent: espressoPressure, pressureEnd, fillTemperature, pourFlow, rampTime, steps
```

### 3.6 profiles_get_params — dflow
```
Call: profiles_set_active (filename: "d_flow_q", confirmed: true)
Call: profiles_get_params
Expect: editorType="dflow"
Expect present: fillTemperature, fillPressure, fillFlow, infusePressure, infuseTime, pourTemperature, pourFlow, pourPressure
Expect absent: preinfusionTime, espressoPressure, holdFlow, rampTime, steps
```

### 3.7 profiles_get_params — aflow
```
Call: profiles_set_active (filename: "a_flow_default_medium", confirmed: true)
Call: profiles_get_params
Expect: editorType="aflow"
Expect present: (all dflow fields) + rampTime, rampDownEnabled, flowExtractionUp, secondFillEnabled
Expect absent: preinfusionTime, espressoPressure, steps
```

### 3.8 profiles_get_params — advanced
```
Call: profiles_set_active (filename: "adaptive_v2", confirmed: true)
Call: profiles_get_params
Expect: editorType="advanced"
Expect present: steps (array with 7 elements), espresso_temperature, profile_notes, preinfuse_frame_count
Expect absent: fillTemperature, pourFlow, preinfusionTime, espressoPressure
```

## 4. Profile Editing

### Setup: switch to pressure profile
```
Call: profiles_set_active (filename: "default", confirmed: true)
Call: profiles_get_params
Save: note tempStart as ORIGINAL_TEMP
```

### 4.1 profiles_edit_params — recipe profile
```
Call: profiles_edit_params (tempStart: ORIGINAL_TEMP+2, tempPreinfuse: ORIGINAL_TEMP+2, tempHold: ORIGINAL_TEMP+2, tempDecline: ORIGINAL_TEMP+2, confirmed: true)
Expect: success=true, editorType="pressure", modified=true
Verify: profiles_get_active → targetTemperatureC = ORIGINAL_TEMP+2, modified=true
```

### 4.2 Restore temperature
```
Call: profiles_edit_params (tempStart: ORIGINAL_TEMP, tempPreinfuse: ORIGINAL_TEMP-2, tempHold: ORIGINAL_TEMP-2, tempDecline: ORIGINAL_TEMP-2, confirmed: true)
Expect: success=true
```

### 4.3 settings_set — temperature on recipe profile
```
Call: settings_set (espressoTemperature: ORIGINAL_TEMP+2, confirmed: true)
Expect: success=true, updated includes "espressoTemperature"
Verify: profiles_get_active → targetTemperatureC = ORIGINAL_TEMP+2
```
Note: writing `espressoTemperature` mutates the active profile (creates a user override on a built-in). This matches the QML behavior — the brew temperature lives on the profile.

### 4.4 Restore temperature
```
Call: settings_set (espressoTemperature: ORIGINAL_TEMP, confirmed: true)
```

### 4.5 profiles_edit_params — advanced profile (frame preservation)
```
Call: profiles_set_active (filename: "adaptive_v2", confirmed: true)
Call: profiles_get_params
Save: note steps array length as FRAME_COUNT, espresso_temperature as ADV_TEMP
Call: profiles_edit_params (espresso_temperature: ADV_TEMP+2, confirmed: true)
Expect: success=true, editorType="advanced"
Verify: profiles_get_params → steps has FRAME_COUNT elements (frames NOT destroyed), espresso_temperature = ADV_TEMP+2
```

### 4.6 settings_set — temperature on advanced profile (frame preservation)
```
Call: settings_set (espressoTemperature: ADV_TEMP+3, confirmed: true)
Expect: updated includes "espressoTemperature"
Verify: profiles_get_params → steps still has FRAME_COUNT elements, espresso_temperature = ADV_TEMP+3
```

### 4.7 Restore advanced profile temperature
```
Call: settings_set (espressoTemperature: ADV_TEMP, confirmed: true)
Expect: updated includes "espressoTemperature"
Verify: profiles_get_active → editorType="advanced", targetTemperatureC = ADV_TEMP
```

### 4.8 profiles_save — refusal on read-only built-in
```
Call: profiles_set_active (filename: "default", confirmed: true)
Call: profiles_edit_params (tempStart: 91, tempPreinfuse: 91, tempHold: 91, tempDecline: 91, confirmed: true)
Verify: profiles_get_active → modified=true, readOnly=true
Call: profiles_save (confirmed: true)   # no filename/title → in-place
Expect: error containing "read-only" — built-in profiles cannot be saved in place;
        Save As is required to create a user copy.
Note: §4.3 (espressoTemperature setter) is the path that *does* persist edits on a
      built-in by creating a user override automatically. profiles_save in-place is
      reserved for already-user-owned profiles.
```

### 4.9 profiles_save — Save As
```
Call: profiles_save (filename: "_mcp_test_tmp", title: "MCP Test Temp", confirmed: true)
Expect: success=true, filename="_mcp_test_tmp"
Verify: profiles_get_active → filename="_mcp_test_tmp", modified=false, readOnly=false
Note: Save As makes the new copy active. Cleaned up in 4.10.
```

### 4.10 profiles_delete — user profile (must switch active first)
```
Call: profiles_set_active (filename: "default", confirmed: true)   # leave _mcp_test_tmp
Call: profiles_delete (filename: "_mcp_test_tmp", confirmed: true)
Expect: success=true, message contains "deleted"
Verify: profiles_list → no profile with filename "_mcp_test_tmp"
Note: deleting the currently-active profile is rejected with an actionable error
      ("Cannot delete the currently-active profile '<name>'. Call profiles_set_active
      with a different profile first, then retry.") — always switch active to a
      different profile before delete (#983, PR #984).
```

### 4.11 profiles_delete — built-in revert
```
Call: profiles_delete (filename: "default", confirmed: true)
Expect: success=true, "reverted":true (or "deleted":true if a user override existed)
Verify: profiles_set_active (filename: "default", confirmed: true) → success (built-in still exists)
Note: succeeds even when no user override exists — reports "reverted" no-op.
```

### 4.12 profiles_set_active — built-in profile
```
Call: profiles_set_active (filename: "blooming_espresso", confirmed: true)
Expect: success=true
Verify: profiles_get_active → title="Blooming Espresso"
```

### 4.13 profiles_create — all editor types
```
For each type in [dflow, aflow, pressure, flow, advanced]:
  Call: profiles_create (editorType: type, title: "_MCP Test " + type, confirmed: true)
  Expect: success=true, editorType=type, filename="" (in-memory only — not yet saved)
  Verify: profiles_get_params → editorType matches type
  Cleanup: profiles_set_active (filename: "default", confirmed: true)  # discards unsaved
```
Note: `profiles_create` only creates the profile in memory — call `profiles_save` with
filename/title to persist. Switching active profile discards the unsaved draft.

### 4.14 Verify removed tool — dialing_apply_change
```
Call: dialing_apply_change (grinderSetting: "12", confirmed: true)
Expect: error — tool not found (removed in phase 15)
```

## 5. Settings

### 5.1 settings_get
```
Call: settings_get
Expect: espressoTemperatureC, targetWeightG, steamTemperatureC, waterTemperatureC, dyeBeanBrand all present
Save: note dyeBeanBrand as ORIGINAL_BRAND, dyeGrinderSetting as ORIGINAL_GRIND
```
Note: read fields are unit/scale-suffixed (`espressoTemperatureC`, `targetWeightG`),
write fields drop the suffix (`espressoTemperature`, `targetWeight`). The `keys`
filter on `settings_get` accepts both forms — passing the suffixed read name an
LLM saw in a previous response round-trips correctly.

### 5.1a settings_get — keys filter round-trip
```
Call: settings_get (keys: ["espressoTemperatureC", "targetWeightG"])
Expect: both fields present in response (suffixed alias accepted).
Call: settings_get (keys: ["espressoTemperature"])
Expect: response contains espressoTemperatureC (un-suffixed write name still works).
```

### 5.1b settings_get — unknown category / keys reject
```
Call: settings_get (category: "preferences")
Expect: error="Unknown category 'preferences'", validCategories array present.
Call: settings_get (keys: ["nonexistentKey"])
Expect: error="Unknown settings key(s)", unknownKeys=["nonexistentKey"].
Call: settings_get (keys: ["dyeBeanBrand", "typoo"])
Expect: error, unknownKeys=["typoo"] (mixed valid+invalid is still an error).
```

### 5.2 settings_set — DYE metadata
```
Call: settings_set (dyeBeanBrand: "MCP Test Brand", dyeGrinderSetting: "99", confirmed: true)
Expect: success=true, updated includes "dyeBeanBrand" and "dyeGrinderSetting"
```

### 5.2a settings_set — unknown key reject
```
Call: settings_set (nonexistentKey: 42, confirmed: true)
Expect: error="Unknown settings key(s)", unknownKeys=["nonexistentKey"]. No setters fire.
```

### 5.3 Cleanup: restore DYE
```
Call: settings_set (dyeBeanBrand: ORIGINAL_BRAND, dyeGrinderSetting: ORIGINAL_GRIND, confirmed: true)
Expect: success=true
```

## 6. Shot History

### 6.1 shots_list
```
Call: shots_list (limit: 3)
Expect: count=3, each shot has id, profileName, durationSec, enjoyment0to100,
        doseG, yieldG, targetWeightG, timestamp (ISO 8601).
        Top-level fields: total, offset=0, hasMore=true (if total > 3),
        nextOffset=3 (or null when hasMore=false).
Save: note first shot id as SHOT_ID, second shot id as SHOT_ID_2
```
Note: list summaries use unit/scale-suffixed field names (`durationSec`, `doseG`,
`yieldG`, `enjoyment0to100`) per PR #976. Detail/compare responses now also use
suffixed names (`enjoyment0to100`, `drinkTdsPct`, `drinkEyPct`, `doseWeightG`,
`finalWeightG`) per PR #984 (#980).

### 6.2 shots_list — filtered
```
Call: shots_list (profileName: "D-Flow", limit: 3)
Expect: count > 0, all returned shots have profileName containing "D-Flow"
```

### 6.3 shots_get_detail — summary (default)
```
Call: shots_get_detail (shotId: SHOT_ID)
Expect: id=SHOT_ID, scalars + phaseSummaries + summaryLines + detectorResults present.
        Time-series arrays (pressure, flow, temperature, etc.), debugLog, and
        profileJson are OMITTED in summary mode. Per-detector `gates` blocks
        (implementation thresholds) are OMITTED in both summary and full mode.
        Payload ~3 KB.
```

### 6.3a shots_get_detail — full
```
Call: shots_get_detail (shotId: SHOT_ID, detail: "full")
Expect: pressure array non-empty, flow array non-empty, temperature array non-empty,
        debugLog and profileJson included. Payload ~85 KB.
```
Note: PR #984 (#979) added the `detail: "summary" | "full"` param. `summary` is the
default — use `full` only when curve-aware analysis is needed.

### 6.4 shots_compare — summary (default)
```
Call: shots_compare (shotIds: [SHOT_ID, SHOT_ID_2])
Expect: response contains data for both shot IDs, plus a `changes` block diffing
        consecutive shots (durationSec, grinderSetting, doseG/yieldG, enjoyment0to100).
        ~3 KB per shot — no time-series, no debugLog, no profileJson.
        If both shots share the same profile: top-level `sharedProfile` block
        carries profileName/profileKbId/profileNotes once, and those fields are
        omitted from each shot. When shots span multiple profiles, the per-shot
        fields remain.
```

### 6.4a shots_compare — full
```
Call: shots_compare (shotIds: [SHOT_ID, SHOT_ID_2], detail: "full")
Expect: per-shot full payloads including time-series. ~85 KB per shot — exceeds
        typical LLM context with more than 1-2 shots.
```

### 6.5 shots_update — enjoyment and notes
```
Call: shots_get_detail (shotId: SHOT_ID)
Save: ORIGINAL_ENJOYMENT = enjoyment0to100, ORIGINAL_NOTES = espressoNotes
Call: shots_update (shotId: SHOT_ID, enjoyment: 85, notes: "MCP test run")
Expect: success=true, message contains shot ID, updated includes "enjoyment" and "espressoNotes"
Verify: shots_get_detail → enjoyment0to100=85, espressoNotes="MCP test run"
Cleanup: shots_update (shotId: SHOT_ID, enjoyment: ORIGINAL_ENJOYMENT, notes: ORIGINAL_NOTES)
```
Note: `shots_update` writers use un-suffixed names (`enjoyment`, `notes`); detail
reads return suffixed names (`enjoyment0to100`, `espressoNotes`).

### 6.6 shots_update — full metadata
```
Call: shots_get_detail (shotId: SHOT_ID)
Save: ORIGINAL_DOSE = doseWeightG, ORIGINAL_BARISTA = barista
Call: shots_update (shotId: SHOT_ID, doseWeight: 18.5, barista: "MCP Test")
Expect: success=true, updated includes "doseWeight" and "barista"
Verify: shots_get_detail (shotId: SHOT_ID) → doseWeightG=18.5
Cleanup: shots_update (shotId: SHOT_ID, doseWeight: ORIGINAL_DOSE, barista: ORIGINAL_BARISTA)
```
Note: `shots_update` writers use un-suffixed names (`doseWeight`, `drinkWeight`),
detail readers return suffixed names (`doseWeightG`, `finalWeightG`).

### 6.7 shots_delete — invalid ID (safe test)
```
Call: shots_delete (shotId: 999999, confirmed: true)
Expect: success=true (deletion queued — async, may not error on invalid ID)
Note: do NOT delete real shots in automated tests
```

### 6.8 Verify removed tool — shots_set_feedback
```
Call: shots_set_feedback (shotId: SHOT_ID, enjoyment: 85)
Expect: error — tool not found (replaced by shots_update in phase 15)
```

## 7. Dial-In

### 7.1 dialing_get_context
```
Call: dialing_get_context (history_limit: 2)
Expect: shotId > 0, shot object present, currentBean present, currentProfile present
Expect: profileKnowledge non-empty (if profile has knowledge base)
```

### 7.2 settings_set — DYE metadata (replaces dialing_apply_change)
```
Call: settings_set (dyeGrinderSetting: "99", confirmed: true)
Expect: updated includes "dyeGrinderSetting"
Cleanup: settings_set (dyeGrinderSetting: ORIGINAL_GRIND, confirmed: true)
```

## 8. Scale

### 8.1 scale_get_weight
```
Call: scale_get_weight
Expect: weight is number, flowRate is number
```

### 8.2 scale_tare
```
Call: scale_tare
Expect: success=true
```

### 8.3 scale_timer — start, stop, reset
```
Call: scale_timer_start
Expect: success=true
Call: scale_timer_stop
Expect: success=true
Call: scale_timer_reset
Expect: success=true
```

## 9. Devices

### 9.1 devices_list
```
Call: devices_list
Expect: devices is array (may be empty in simulator), count is number
```

### 9.2 devices_connection_status
```
Call: devices_connection_status
Expect: machineConnected is boolean, bleAvailable is boolean
```

### 9.3 devices_scan
```
Call: devices_scan
Expect: success=true
```

## 10. Debug

### 10.1 debug_get_log
```
Call: debug_get_log (offset: 0, limit: 10)
Expect: totalLines > 0, returnedLines <= 10, hasMore=true (if totalLines > 10)
```

## Cleanup (restore system to original state)

```
Step 1 — Restore active profile:
  Call: profiles_set_active (filename: ORIGINAL_PROFILE, confirmed: true)
  Verify: profiles_get_active → filename = ORIGINAL_PROFILE

Step 2 — Restore DYE metadata:
  Call: settings_set (dyeBeanBrand: ORIGINAL_BRAND, dyeBeanType: ORIGINAL_BEAN_TYPE,
                      dyeRoastLevel: ORIGINAL_ROAST, dyeGrinderSetting: ORIGINAL_GRIND,
                      confirmed: true)

Step 3 — Restore machine phase (if was sleeping):
  If ORIGINAL_PHASE was "Sleep":
    Call: machine_sleep (confirmed: true)

Step 4 — Verify clean state:
  Call: profiles_get_active → filename = ORIGINAL_PROFILE, modified = false
  Call: settings_get → dyeBeanBrand = ORIGINAL_BRAND, dyeGrinderSetting = ORIGINAL_GRIND
  Call: machine_get_state → phase matches ORIGINAL_PHASE (or Ready if was Ready)
```

### What this test plan does NOT leave behind
- No temporary profiles on disk (`_mcp_test_tmp` deleted in 4.10, `default` override reverted in 4.11)
- No modified DYE metadata (restored in cleanup step 2)
- No modified grinder setting (restored in 7.3 cleanup and cleanup step 2)
- No permanently altered shot feedback (restored in 6.5 cleanup)
- No changed machine phase (restored in cleanup step 3)
- Active profile and temperature restored to pre-test values

## 11. Settings Parity (Phase 16)

Valid categories per the `settings_get` schema:
`machine, calibration, connections, screensaver, accessibility, ai, espresso, steam,
water, flush, dye, mqtt, themes, visualizer, update, data, history, language, debug,
battery, heater, autofavorites`. There is **no** `preferences` category — what used
to live there has been split. Most landed in `machine` (sleep, theme, brightness,
auto-wake, refill kit, post-shot review, default rating, …) but not all: tick/TTS
moved to `accessibility`, screensaver-only keys to `screensaver`, update-channel
keys to `update`, language to `language`. Use the table below to find a key's home,
or pass `keys:["…"]` to bypass categories entirely.

### Where do the common settings live? (verified 2026-04-30)

| Category | Notable keys |
|---|---|
| `machine` | `autoSleepMinutes`, `keepSteamHeaterOn`, `themeMode`, `darkThemeName`, `lightThemeName`, `screenBrightness`, `autoWakeEnabled`, `autoWakeStayAwakeEnabled`, `autoWakeStayAwakeMinutes`, `defaultShotRating`, `launcherMode`, `postShotReviewTimeout`, `refillKitOverride`, `steamAutoFlushSeconds`, `steamTwoTapStop`, `waterLevelDisplayUnit`, `waterRefillPoint` |
| `calibration` | `autoFlowCalibration`, `flowCalibrationMultiplier`, `ignoreVolumeWithScale`, `useFlowScale` |
| `connections` | `machineAddress`, `scaleAddress`, `scaleName`, `scaleType`, `showScaleDialogs`, `usbSerialEnabled` |
| `screensaver` | `screensaverType`, `dimDelayMinutes`, `dimPercent`, `cacheEnabled`, `flipClockUse3D`, `imageDisplayDuration`, `pipesSpeed`, `pipesCameraSpeed`, `pipesShowClock`, `videosShowClock`, `attractorShowClock`, `showDateOnPersonal`, `shotMapShape`, `shotMapTexture`, `shotMapShowClock`, `shotMapShowProfiles`, `shotMapShowTerminator` |
| `accessibility` | `accessibilityEnabled`, `ttsEnabled`, `tickEnabled`, `tickSoundIndex`, `tickVolume`, `extractionAnnouncementsEnabled`, `extractionAnnouncementMode`, `extractionAnnouncementInterval` |
| `ai` | `aiProvider`, `discussShotApp`, `discussShotCustomUrl`, `mcpAccessLevel`, `mcpConfirmationLevel`, `mcpEnabled`, `ollamaEndpoint`, `ollamaModel`, `openrouterModel` |
| `espresso` | `currentProfile`, `espressoTemperatureC`, `lastUsedRatio`, `targetWeightG` |
| `steam` | `steamDisabled`, `steamFlowMlPerSec`, `steamTemperatureC`, `steamTimeoutSec` |
| `water` | `hotWaterFlowRateMlPerSec`, `waterTemperatureC`, `waterVolumeMl`, `waterVolumeMode` |
| `flush` | `flushFlowMlPerSec`, `flushSeconds` |
| `dye` | `dyeBarista`, `dyeBeanBrand`, `dyeBeanType`, `dyeBeanWeight`, `dyeDrinkEy`, `dyeDrinkTds`, `dyeDrinkWeight`, `dyeEspressoEnjoyment`, `dyeGrinderBrand`, `dyeGrinderBurrs`, `dyeGrinderModel`, `dyeGrinderSetting`, `dyeRoastDate`, `dyeRoastLevel`, `dyeShotNotes` |
| `mqtt` | `mqttEnabled`, `mqttBaseTopic`, `mqttBrokerHost`, `mqttBrokerPort`, `mqttClientId`, `mqttHomeAssistantDiscovery`, `mqttPublishInterval`, `mqttRetainMessages`, `mqttUsername` |
| `themes` | `activeShader`, `activeThemeName`, `isDarkMode`, `themeNames` (mostly read-only metadata; write via `machine.themeMode`/`darkThemeName`/`lightThemeName`) |
| `visualizer` | `visualizerAutoUpload`, `visualizerClearNotesOnStart`, `visualizerExtendedMetadata`, `visualizerMinDuration`, `visualizerShowAfterShot` |
| `update` | `autoCheckUpdates`, `betaUpdatesEnabled` |
| `data` | `dailyBackupHour`, `shotServerEnabled`, `shotServerPort`, `webSecurityEnabled` |
| `history` | `shotHistorySortDirection`, `shotHistorySortField` |
| `language` | `currentLanguage` |
| `debug` | `hideGhcSimulator`, `simulationMode` |
| `battery` | `batteryPercent` (read-only), `chargingMode`, `isCharging` (read-only) |
| `heater` | `heaterIdleTempC`, `heaterTestFlowMlPerSec`, `heaterWarmupFlowMlPerSec`, `heaterWarmupTimeoutSec` |
| `autofavorites` | `autoFavoritesGroupBy`, `autoFavoritesHideUnrated`, `autoFavoritesMaxItems`, `autoFavoritesOpenBrewSettings` |

Sensitive fields (API keys, passwords, TOTP secrets) are excluded from both
`settings_get` *and* `settings_set` schemas — they cannot be read or written via MCP.

### State Capture
```
Call: settings_get (category: "machine")
Save: ORIGINAL_AUTO_SLEEP = autoSleepMinutes, ORIGINAL_KEEP_STEAM = keepSteamHeaterOn,
      ORIGINAL_THEME_MODE = themeMode

Call: settings_get (category: "accessibility")
Save: ORIGINAL_A11Y_ENABLED = accessibilityEnabled, ORIGINAL_TICK_VOLUME = tickVolume

Call: settings_get (category: "screensaver")
Save: ORIGINAL_DIM_PERCENT = dimPercent

Call: settings_get (category: "mqtt")
Save: ORIGINAL_MQTT_ENABLED = mqttEnabled

Call: settings_get (category: "update")
Save: ORIGINAL_AUTO_CHECK = autoCheckUpdates
```

### 11.1 settings_get — category filter (machine)
```
Call: settings_get (category: "machine")
Expect: autoSleepMinutes, keepSteamHeaterOn, themeMode, darkThemeName, lightThemeName,
        screenBrightness all present
Expect absent: mqttEnabled, screensaverType, accessibilityEnabled (different categories)
Note: an unknown category (e.g. "preferences") returns {} silently — server should
      probably reject it with an error, but currently does not.
```

### 11.2 settings_get — all categories
```
Call: settings_get
Expect: 100+ fields returned across all categories
Expect present: autoSleepMinutes, screensaverType, accessibilityEnabled, mqttEnabled, autoCheckUpdates
```

### 11.3 settings_get — specific keys across categories
```
Call: settings_get (keys: ["autoSleepMinutes", "mqttEnabled", "screensaverType"])
Expect: exactly 3 fields returned
```

### 11.4 settings_get — accessibility category
```
Call: settings_get (category: "accessibility")
Expect: accessibilityEnabled, ttsEnabled, tickEnabled, tickSoundIndex, tickVolume,
        extractionAnnouncementsEnabled, extractionAnnouncementMode, extractionAnnouncementInterval
```

### 11.5 settings_get — screensaver category
```
Call: settings_get (category: "screensaver")
Expect: screensaverType, dimDelayMinutes, dimPercent, pipesSpeed, pipesCameraSpeed,
        pipesShowClock, flipClockUse3D, videosShowClock, cacheEnabled present
```

### 11.6 settings_set — auto-sleep and steam heater
```
Call: settings_set (autoSleepMinutes: 30, keepSteamHeaterOn: false, confirmed: true)
Expect: success=true, updated includes "autoSleepMinutes" and "keepSteamHeaterOn"
Verify: settings_get (keys: ["autoSleepMinutes", "keepSteamHeaterOn"]) → 30, false
Cleanup: settings_set (autoSleepMinutes: ORIGINAL_AUTO_SLEEP, keepSteamHeaterOn: ORIGINAL_KEEP_STEAM, confirmed: true)
```

### 11.7 settings_set — accessibility
```
Call: settings_set (tickVolume: 50, confirmed: true)
Expect: success=true, updated includes "tickVolume"
Verify: settings_get (keys: ["tickVolume"]) → 50
Cleanup: settings_set (tickVolume: ORIGINAL_TICK_VOLUME, confirmed: true)
```

### 11.8 settings_set — screensaver
```
Call: settings_set (dimPercent: 42, confirmed: true)
Expect: success=true, updated includes "dimPercent"
Verify: settings_get (keys: ["dimPercent"]) → 42
Cleanup: settings_set (dimPercent: ORIGINAL_DIM_PERCENT, confirmed: true)
```

### 11.9 settings_set — MQTT
```
Call: settings_set (mqttEnabled: true, mqttBrokerHost: "test.mqtt.local", confirmed: true)
Expect: success=true, updated includes "mqttEnabled" and "mqttBrokerHost"
Verify: settings_get (category: "mqtt") → mqttEnabled=true, mqttBrokerHost="test.mqtt.local"
Cleanup: settings_set (mqttEnabled: ORIGINAL_MQTT_ENABLED, mqttBrokerHost: "", confirmed: true)
```

### 11.10 settings_set — update settings
```
Call: settings_set (autoCheckUpdates: false, confirmed: true)
Expect: success=true
Cleanup: settings_set (autoCheckUpdates: ORIGINAL_AUTO_CHECK, confirmed: true)
```

### 11.11 settings_set — sensitive fields rejected
```
Verify: settings_get does NOT return openaiApiKey, anthropicApiKey, mqttPassword,
        visualizerUsername, visualizerPassword, mcpApiKey
Note: These fields are not in settings_set schema — attempts to set them are silently ignored
```

### 11.12 settings_set — language
```
Call: settings_get (category: "language")
Save: ORIGINAL_LANG = currentLanguage
Call: settings_set (currentLanguage: "de", confirmed: true)
Expect: success=true, updated includes "currentLanguage"
Cleanup: settings_set (currentLanguage: ORIGINAL_LANG, confirmed: true)
```

## Summary

| Category | Tests | Notes |
|----------|-------|-------|
| Machine State | 2 | Read-only |
| Machine Control | 4+1 | 1 skipped in simulator (start ops) |
| Profile Read | 8 | All 5 editor types |
| Profile Edit | 14 | Includes frame preservation, create all types, removed tool checks |
| Settings | 3 | Basic settings (phase 9) |
| Shot History | 8 | shots_update full metadata, shots_delete, removed tool check |
| Dial-In | 3 | dialing_apply_change replaced by settings_set |
| Scale | 3 | |
| Devices | 3 | |
| Debug | 1 | |
| Settings Parity | 12 | `keys:[…]` filter (no `preferences` category), categories, write+verify+restore |
| **Total** | **62** | |

## Field-name reference (PR #976 / #975)

Settings, profile, and shot responses use unit/scale-suffixed reader names but
un-suffixed writer names. Quick lookup:

| Concept | Read field (responses) | Write field (settings_set / shots_update) |
|---|---|---|
| Espresso temp | `espressoTemperatureC` | `espressoTemperature` |
| Steam temp | `steamTemperatureC` | `steamTemperature` |
| Water temp | `waterTemperatureC` | `waterTemperature` |
| Profile target temp | `targetTemperatureC` | (via `tempStart`/`espresso_temperature` on `profiles_edit_params`) |
| Target weight | `targetWeightG` | `targetWeight` |
| Shot duration | `durationSec` | n/a (computed) |
| Shot enjoyment | `enjoyment0to100` | `enjoyment` (`shots_update`) |
| Shot TDS | `drinkTdsPct` | `drinkTds` (`shots_update`) |
| Shot EY | `drinkEyPct` | `drinkEy` (`shots_update`) |
| Shot dose | `doseG` (list) / `doseWeightG` (detail) | `doseWeight` (`shots_update`) |
| Shot yield | `yieldG` (list) / `finalWeightG` (detail) | `drinkWeight` (`shots_update`) |
| Shot notes | `notes` (list) / `espressoNotes` (detail) | `notes` (`shots_update`) |

PR #984 (#980) extended the suffixed-read convention through `ShotProjection`, so
`shots_get_detail` and `shots_compare` now match `shots_list` for `enjoyment0to100`,
`drinkTdsPct`, and `drinkEyPct`. Per #991, all three fields are emitted as `null`
(not `0`) on shots that lack a rating or refractometer measurement, so callers can
distinguish unrated/unmeasured from a deliberate zero. Remaining read/write asymmetries (dose, yield, notes)
are tracked separately — read names use the projection's full member name (`doseWeightG`,
`finalWeightG`, `espressoNotes`) while write names use the legacy short form for
`shots_update`/`settings_set` schemas.

Open follow-ups for the remaining inconsistencies:
- `profiles_get_active` vs `profiles_get_params` naming (#992)
- `keys: [...]` filter on `settings_get` rejects suffixed read names (#985)
