# MCP Tool Test Plan

Verifies all MCP tools work correctly as a second UI to the QML app. Each test records the tool called, expected behavior, and pass/fail result.

Last run: 2026-03-20 (simulator mode, 37 tools registered)

## 1. Machine State (read)

### 1.1 machine_get_state
- **Action**: Read machine state
- **Expected**: Returns phase, connection, water level, firmware
- **Result**: PASS — returned Ready phase, waterLevelMl=872, firmwareVersion=SIM-1.0

### 1.2 machine_get_telemetry
- **Action**: Read live telemetry
- **Expected**: Returns pressure, flow, temp, weight values
- **Result**: PASS — returned pressure=0, flow=0, temperature=93, mixTemperature=92.5

## 2. Machine Control

### 2.1 machine_wake
- **Action**: Wake machine from sleep
- **Expected**: Machine transitions to Idle then Ready
- **Result**: PASS — "Wake command sent"

### 2.2 machine_sleep
- **Action**: Put machine to sleep
- **Expected**: Machine transitions to Sleep
- **Result**: PASS — "Sleep command sent"

### 2.3 machine_stop
- **Action**: Stop current operation (only valid during operation)
- **Expected**: Returns success or appropriate error if not in operation
- **Result**: PASS — returned error "No operation in progress" (correct)

### 2.4 machine_skip_frame
- **Action**: Skip to next frame (only valid during espresso)
- **Expected**: Returns appropriate error if not in espresso
- **Result**: PASS — returned error "No extraction in progress" (correct)

### 2.5 machine_start_espresso / steam / hot_water / flush
- **Action**: Start operations (headless DE1 only)
- **Expected**: Returns error on simulator (no headless support)
- **Result**: NOT TESTED — skipped (simulator, not headless)

## 3. Profile Management (read)

### 3.1 profiles_list
- **Action**: List all profiles
- **Expected**: Returns array with filename, title, editorType for each
- **Result**: PASS — 94 profiles, all have editorType (pressure/flow/dflow/aflow/advanced)

### 3.2 profiles_get_active
- **Action**: Get current profile
- **Expected**: Returns filename, title, editorType (not empty), modified, targetWeight, targetTemperature
- **Result**: PASS — editorType="pressure" for Default, "advanced" for Adaptive v2

### 3.3 profiles_get_detail
- **Action**: Get full profile JSON for a specific profile
- **Expected**: Returns complete profile with steps/frames
- **Result**: PASS — Blooming Espresso returned with 6 frames, notes, metadata

### 3.4 profiles_get_params — pressure editor
- **Action**: Load Default profile, get params
- **Expected**: Returns only pressure-relevant fields
- **Result**: PASS — returned espressoPressure, pressureEnd, limiter, preinfusion, hold, decline, per-step temps. No D-Flow/A-Flow fields.

### 3.5 profiles_get_params — flow editor
- **Action**: Load Flow profile for straight espresso, get params
- **Expected**: Returns flow fields instead of pressure fields
- **Result**: PASS — returned holdFlow=2, flowEnd=1.2, limiterValue=8.6. No espressoPressure/pressureEnd.

### 3.6 profiles_get_params — dflow editor
- **Action**: Load D-Flow / Q, get params
- **Expected**: Returns fill/infuse/pour params only
- **Result**: PASS — returned fillTemperature=84, pourFlow=1.8, pourPressure=10. No simple profile fields.

### 3.7 profiles_get_params — aflow editor
- **Action**: Load A-Flow / default-medium, get params
- **Expected**: Returns fill/infuse/pour + A-Flow-specific fields
- **Result**: PASS — returned rampTime=10, rampDownEnabled=false, flowExtractionUp=true, secondFillEnabled=false

### 3.8 profiles_get_params — advanced editor
- **Action**: Load Adaptive v2, get params
- **Expected**: Returns full profile data with frames
- **Result**: PASS — returned editorType="advanced", 7 frames with all step details, profile_notes, metadata

## 4. Profile Editing (write)

### 4.1 profiles_edit_params — recipe profile
- **Action**: Edit Default temperature via profiles_edit_params
- **Expected**: Temperature changes, profile marked modified
- **Result**: PASS — targetTemperature changed to 92, modified=true, editorType="pressure"

### 4.2 profiles_edit_params — advanced profile
- **Action**: Edit Adaptive v2 espresso_temperature via profiles_edit_params
- **Expected**: espresso_temperature changes, all 7 frames preserved
- **Result**: PASS — espresso_temperature=95, all 7 frames intact with original per-frame temperatures

### 4.3 settings_set — temperature on recipe profile
- **Action**: Change espressoTemperature on Default via settings_set
- **Expected**: Frames regenerated, profile modified
- **Result**: PASS — targetTemperature updated to 90

### 4.4 settings_set — temperature on advanced profile
- **Action**: Change espressoTemperature on Adaptive v2 via settings_set
- **Expected**: espresso_temperature updated, all 7 frames preserved
- **Result**: PASS — temperature changed, editorType still "advanced", frames not destroyed

### 4.5 dialing_apply_change — temperature on advanced profile
- **Action**: Change espressoTemperature on Adaptive v2 via dialing_apply_change
- **Expected**: Frames preserved, uses uploadProfile()
- **Result**: PASS — temperature changed to 94, editorType still "advanced"

### 4.6 profiles_save — save in place
- **Action**: Edit profile, then save
- **Expected**: Profile saved to disk, modified flag cleared
- **Result**: PASS — modified=false after save

### 4.7 profiles_save — Save As
- **Action**: Save current profile with filename="mcp_test_copy", title="MCP Test Copy"
- **Expected**: New profile created
- **Result**: PASS — "Profile saved as: MCP Test Copy"

### 4.8 profiles_delete — user profile
- **Action**: Delete mcp_test_copy
- **Expected**: Profile deleted
- **Result**: PASS — "Profile deleted: mcp_test_copy"

### 4.9 profiles_delete — built-in revert
- **Action**: Delete Default (which had a saved user override)
- **Expected**: Local override removed, reverts to built-in
- **Result**: PASS — "Profile deleted: default"

### 4.10 profiles_set_active — built-in profile
- **Action**: Switch to blooming_espresso (built-in)
- **Expected**: Profile loads (profileExists finds built-in via m_availableProfiles)
- **Result**: PASS — "Profile activation queued: blooming_espresso"

## 5. Settings

### 5.1 settings_get
- **Action**: Read all settings
- **Expected**: Returns temperature, weight, steam, water, DYE fields
- **Result**: PASS — returned 20 fields including espressoTemperature, targetWeight, steam, water, DYE

### 5.2 settings_set — DYE metadata
- **Action**: Set bean brand and grinder setting
- **Expected**: Fields updated, returned in updated list
- **Result**: PASS — updated=["dyeBeanBrand","dyeGrinderSetting"]

## 6. Shot History

### 6.1 shots_list
- **Action**: List recent shots (limit 3)
- **Expected**: Returns array of shot summaries
- **Result**: PASS — 3 shots returned with id, profileName, dose, yield, duration, enjoyment

### 6.2 shots_list — with filter
- **Action**: List shots filtered by profileName="D-Flow"
- **Expected**: Only matching shots returned
- **Result**: PASS — 3 D-Flow shots returned (757 total matching)

### 6.3 shots_get_detail
- **Action**: Get full detail of shot 899
- **Expected**: Returns time-series data
- **Result**: PASS — returned pressure[], flow[], temperature[], weight[] arrays + phases, debug log

### 6.4 shots_compare
- **Action**: Compare shots 899 and 898
- **Expected**: Returns summary for both
- **Result**: PASS — returned comparison data (large response)

### 6.5 shots_set_feedback
- **Action**: Set enjoyment=90 and notes on shot 899
- **Expected**: Feedback saved
- **Result**: PASS — "Feedback saved for shot 899"

## 7. Dial-In

### 7.1 dialing_get_context
- **Action**: Get full dial-in context for most recent shot
- **Expected**: Returns shot summary, history, profile knowledge, bean metadata
- **Result**: PASS — returned shot, dialInHistory, profileKnowledge, currentBean, currentProfile, shotAnalysis

### 7.2 dialing_suggest_change
- **Action**: Suggest a grind change
- **Expected**: Returns suggestion with status
- **Result**: PASS — status="suggestion_displayed"

### 7.3 dialing_apply_change — metadata
- **Action**: Change grinder setting and bean brand
- **Expected**: Settings updated, no profile corruption
- **Result**: PASS — applied=["grinderSetting","dyeBeanBrand"]

## 8. Scale

### 8.1 scale_get_weight
- **Action**: Read current weight
- **Expected**: Returns weight and flow rate
- **Result**: PASS — weight=0, flowRate=0 (no cup on simulated scale)

### 8.2 scale_tare
- **Action**: Tare the scale
- **Expected**: Scale zeroed
- **Result**: PASS — "Scale tared"

### 8.3 scale_timer_start / stop / reset
- **Action**: Start, stop, reset timer
- **Expected**: Timer operations succeed
- **Result**: PASS — all three returned success

## 9. Devices

### 9.1 devices_list
- **Action**: List discovered BLE devices
- **Expected**: Returns device list (may be empty in simulator)
- **Result**: PASS — count=0, devices=[] (simulator mode, expected)

### 9.2 devices_connection_status
- **Action**: Get connection status
- **Expected**: Returns DE1 and scale connection state
- **Result**: PASS — machineConnected=true, bleAvailable=true

### 9.3 devices_scan
- **Action**: Start BLE scan
- **Expected**: Scan initiated
- **Result**: PASS — "BLE scan started"

## 10. Debug

### 10.1 debug_get_log
- **Action**: Read debug log with offset/limit
- **Expected**: Returns log lines with pagination
- **Result**: PASS — returned 5 lines, totalLines=5940, hasMore=true

## Summary

| Category | Tests | Passed | Skipped |
|----------|-------|--------|---------|
| Machine State | 2 | 2 | 0 |
| Machine Control | 5 | 4 | 1 (start ops — simulator) |
| Profile Read | 8 | 8 | 0 |
| Profile Edit | 10 | 10 | 0 |
| Settings | 2 | 2 | 0 |
| Shot History | 5 | 5 | 0 |
| Dial-In | 3 | 3 | 0 |
| Scale | 3 | 3 | 0 |
| Devices | 3 | 3 | 0 |
| Debug | 1 | 1 | 0 |
| **Total** | **42** | **41** | **1** |
