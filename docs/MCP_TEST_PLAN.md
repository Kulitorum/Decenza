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

---

## 1. Machine State (read-only, no state changes)

### 1.1 machine_get_state
```
Call: machine_get_state
Expect: phase exists, connected=true, waterLevelMl > 0, firmwareVersion non-empty
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
Expect: filename non-empty, editorType non-empty, modified is boolean, targetWeightG > 0
Save: note the filename and editorType as ORIGINAL_PROFILE and ORIGINAL_EDITOR
```

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
Verify: profiles_get_active → targetTemperature = ORIGINAL_TEMP+2, modified=true
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
Verify: profiles_get_active → targetTemperature = ORIGINAL_TEMP+2
```

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
Verify: profiles_get_active → editorType="advanced", targetTemperature = ADV_TEMP
```

### 4.8 profiles_save — save in place
```
Call: profiles_set_active (filename: "default", confirmed: true)
Call: profiles_edit_params (tempStart: 91, tempPreinfuse: 91, tempHold: 91, tempDecline: 91, confirmed: true)
Verify: profiles_get_active → modified=true
Call: profiles_save (confirmed: true)
Expect: success=true, filename="default"
Verify: profiles_get_active → modified=false
Note: this creates a user override of the built-in "default" — cleaned up in 4.11
```

### 4.9 profiles_save — Save As
```
Call: profiles_save (filename: "_mcp_test_tmp", title: "MCP Test Temp", confirmed: true)
Expect: success=true, filename="_mcp_test_tmp"
Note: this creates a temporary profile — cleaned up immediately in 4.10
```

### 4.10 profiles_delete — user profile
```
Call: profiles_delete (filename: "_mcp_test_tmp", confirmed: true)
Expect: success=true, message contains "deleted"
Verify: profiles_list → no profile with filename "_mcp_test_tmp"
```

### 4.11 profiles_delete — built-in revert
```
Call: profiles_delete (filename: "default", confirmed: true)
Expect: success=true (either "deleted" for user copy or "reverted" for built-in)
Verify: profiles_set_active (filename: "default", confirmed: true) → success (built-in still exists)
Note: this cleans up the user override created in 4.8, restoring "default" to built-in state
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
  Expect: success=true, editorType=type
  Verify: profiles_get_params → editorType matches type
  Cleanup: profiles_delete (filename from create response, confirmed: true)
```

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

### 5.2 settings_set — DYE metadata
```
Call: settings_set (dyeBeanBrand: "MCP Test Brand", dyeGrinderSetting: "99", confirmed: true)
Expect: success=true, updated includes "dyeBeanBrand" and "dyeGrinderSetting"
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
Expect: count=3, each shot has id, profileName, duration, enjoyment
Save: note first shot id as SHOT_ID, second shot id as SHOT_ID_2
```

### 6.2 shots_list — filtered
```
Call: shots_list (profileName: "D-Flow", limit: 3)
Expect: count > 0, all returned shots have profileName containing "D-Flow"
```

### 6.3 shots_get_detail
```
Call: shots_get_detail (shotId: SHOT_ID)
Expect: id=SHOT_ID, pressure array non-empty, flow array non-empty, temperature array non-empty
```

### 6.4 shots_compare
```
Call: shots_compare (shotIds: [SHOT_ID, SHOT_ID_2])
Expect: response contains data for both shot IDs
```

### 6.5 shots_update — enjoyment and notes
```
Call: shots_get_detail (shotId: SHOT_ID)
Save: ORIGINAL_ENJOYMENT = enjoyment, ORIGINAL_NOTES = espressoNotes
Call: shots_update (shotId: SHOT_ID, enjoyment: 85, notes: "MCP test run")
Expect: success=true, message contains shot ID
Cleanup: shots_update (shotId: SHOT_ID, enjoyment: ORIGINAL_ENJOYMENT, notes: ORIGINAL_NOTES)
```

### 6.6 shots_update — full metadata
```
Call: shots_get_detail (shotId: SHOT_ID)
Save: ORIGINAL_DOSE = doseWeight, ORIGINAL_BARISTA = barista
Call: shots_update (shotId: SHOT_ID, doseWeight: 18.5, barista: "MCP Test")
Expect: success=true, updated includes "dose_weight" and "barista"
Verify: shots_get_detail (shotId: SHOT_ID) → doseWeight=18.5
Cleanup: shots_update (shotId: SHOT_ID, doseWeight: ORIGINAL_DOSE, barista: ORIGINAL_BARISTA)
```

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

### 7.2 dialing_suggest_change
```
Call: dialing_suggest_change (parameter: "grind", suggestion: "Test suggestion", rationale: "MCP test")
Expect: status="suggestion_displayed", parameter="grind"
```

### 7.3 settings_set — DYE metadata (replaces dialing_apply_change)
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

### State Capture
```
Call: settings_get (category: "preferences")
Save: ORIGINAL_AUTO_SLEEP = autoSleepMinutes, ORIGINAL_KEEP_STEAM = keepSteamHeaterOn

Call: settings_get (category: "accessibility")
Save: ORIGINAL_A11Y_ENABLED = accessibilityEnabled, ORIGINAL_TICK_VOLUME = tickVolume

Call: settings_get (category: "screensaver")
Save: ORIGINAL_DIM_PERCENT = dimPercent

Call: settings_get (category: "mqtt")
Save: ORIGINAL_MQTT_ENABLED = mqttEnabled

Call: settings_get (category: "update")
Save: ORIGINAL_AUTO_CHECK = autoCheckUpdates
```

### 11.1 settings_get — category filter
```
Call: settings_get (category: "preferences")
Expect: autoSleepMinutes present, keepSteamHeaterOn present, themeMode present
Expect absent: mqttEnabled, screensaverType, accessibilityEnabled (wrong category)
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

### 11.6 settings_set — preferences
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
| Settings Parity | 12 | Category filter, all categories, write+verify+restore |
| **Total** | **62** | |
