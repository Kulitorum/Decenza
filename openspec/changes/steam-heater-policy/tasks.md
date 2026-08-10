# Tasks

## 1. The single derivation (standalone bug fix — can land first)

- [x] 1.1 Add one helper returning `(heaterOn, targetTemperatureC)` from the settings, the effective pitcher, the transient flag, the active recipe and live event-permission. Put it where both `MainController` and `ProfileManager` can reach it.
- [x] 1.2 Route `MainController::sendMachineSettings()` through it.
- [x] 1.3 Delete the duplicated derivation in `ProfileManager::uploadCurrentProfile()` (`profilemanager.cpp:2188`) and call the helper. **This is the reported field bug** — a deferred profile upload currently lands after `startSteamHeating()` and re-sends steam = 0.
- [x] 1.4 Route `startSteamHeating`, `turnOffSteamHeater`, `setSteamTemperatureImmediate` and `sendSteamTemperature` through it.
- [x] 1.5 Route `MqttClient`'s steam status derivation (`mqttclient.cpp:1062`) through it.
- [x] 1.6 Test: activation followed by a deferred profile upload leaves the heater state intact (the regression test for the reported bug).
- [x] 1.7 Test/guard: no second derivation of the steam target exists.

## 2. Shared pitcher selection

- [x] 2.1 Add one "select pitcher N and apply its values" action (timeout, flow, temperature, then push to the machine).
- [x] 2.2 Route the MCP path (`mcptools_presets.cpp` `applySteamPitcher`), the idle pill row (`IdlePage.qml`), the SteamItem popup (`SteamItem.qml`), the Steam page, and **recipe activation** through it. Activation currently stores the index only, so it heats to the global temperature rather than the pitcher's.
- [ ] 2.3 Test: activating a recipe whose pitcher carries a non-default temperature commands that temperature.

## 3. The two settings

- [x] 3.1 Replace `keepSteamHeaterOn` in `SettingsBrew` with **Keep warm when idle** and **Let the recipe decide**.
- [x] 3.2 Migration: map `steam/keepHeaterOn` to the first; set the second **on**, for existing installs as well as fresh ones. Idempotent, following the constructor-migration pattern at `settings_brew.cpp:72`. Note this changes behaviour at upgrade for anyone with a parked pitcher-less recipe — release notes must call it out.
- [x] 3.3 `SettingsMachineTab.qml`: two switches replacing the one, in the Steam section.
- [x] 3.4 MCP: replace the `keepSteamHeaterOn` key in `mcptools_write.cpp` and `mcptools_settings.cpp` with the two new keys.
- [x] 3.5 Settings backup: serialize both keys; import maps a legacy `keepHeaterOn` through the same migration (`settingsserializer.cpp`).
- [x] 3.6 Tests: the four setting combinations × {no recipe, milk recipe, pitcher-less recipe} × {standing pitcher real, standing pitcher off}.

## 4. Permission and veto

- [x] 4.1 Implement state-granted vs event-granted permission; event permission is revoked on return to Idle.
- [x] 4.2 Warm at **shot start** when Let the recipe decide is on and the active recipe uses steam.
- [x] 4.3 Scope the return-to-idle turn-off (`main.qml:3557`, `main.qml:869`) to event-granted permission, so it stops racing the state-granted case.
- [x] 4.4 Delete `activeRecipeHasMilk()`, the hold branch in `sendMachineSettings`, and the release in `deactivateRecipe`.
- [x] 4.5 Test: state permission survives a return to Idle; event permission does not.
- [x] 4.6 Selecting a pitcher pill must NOT grant permission — the row is a selection, not a heater switch. Selecting "Heater off" applies the veto; selecting a real pitcher only removes it.
- [x] 4.7 Steaming with "Heater off" selected keeps today's fallback: the live steam settings, which hold the last real pitcher's values. The built-in entry carries no values of its own. Surface a message rather than changing the parameters — a GHC press enters Steam state in firmware and cannot be refused.

## 4b. The steam readout says "Off"

Replaces the temperature outright — the widget shows **Off**, not the current temp, whenever the resolved target is off. Driven by the resolved state, never by `DE1Device.steamTemperature`, which keeps reporting a high number for many minutes while the boiler cools.

- [x] 4b.1 Expose the resolved on/off state to QML (a property on the policy, reachable from the layout items).
- [x] 4b.2 `SteamTemperatureItem.qml:16` — show "Off" in place of the temperature.
- [x] 4b.3 `CustomItem.qml:221` — the `%STEAM_TEMP%` token resolves to "Off".
- [x] 4b.4 `SteamPage.qml:97` and `SettingsMachineTab.qml:287` ("Current:") — same treatment, so the three surfaces cannot disagree.
- [x] 4b.5 Translate the label; it is user-visible text.
- [x] 4b.6 Test: resolved-off with a hot measured boiler reads "Off", not the measured value.

## 4c. Sleep and wake

- [x] 4c.1 Re-resolve and re-send the target on Sleep → Idle (same transition as the auto-load hook, `main.qml:506`). Fixes the case where the transient flag is cleared at sleep (`maincontroller.cpp:215`) and nothing re-asserts, leaving the DE1 holding a stale 0.
- [x] 4c.2 Event-granted permission does not survive sleep.
- [x] 4c.3 An auto-load recipe decides on wake, as any activation would.
- [x] 4c.4 Tests: cold stays cold, warm returns warm, cleared transient flag is re-asserted.

## 5. The built-in "Heater off" entry

- [x] 5.1 Implement the built-in as a **synthetic entry, appended LAST, selected by sentinel `-1`** (see design.md). `steamPitcherPresets()` appends it; the stored array holds only real pitchers, and it carries no stored name — the view translates the label.
- [x] 5.1a `getSteamPitcherPreset()` answers for both the sentinel and the entry's display slot (one past the last stored preset); `steamPitcherCount()` returns the real count for every caller that iterates or indexes the stored list.
- [x] 5.2 Refuse rename, delete, reorder and edit on it in `SettingsBrew`, and hide those affordances in `SteamPage.qml`.
- [x] 5.3 Remove `addSteamPitcherPresetDisabled()`, the add-dialog Off button (`SteamPage.qml:2687`) and the MCP disabled-add path (`mcptools_presets.cpp:163`).
- [x] 5.4 Reserve the name so a user pitcher cannot collide with the built-in's label.
- [x] 5.5 Test: it cannot be created twice, renamed, removed or reordered, on any surface including MCP.

## 6. Recipe pitcher as an override

- [x] 6.1 Resolve the effective pitcher as `active recipe's pitcher (when Let the recipe decide is on) ?? standing pitcher`; stop writing `selectedSteamPitcher` from activation.
- [x] 6.2 Steam block: store the off marker instead of a pitcher name; update `parseSteamBlock`'s documented shape, the MCP schema (`mcptools_recipes.cpp:250`), and the ShotServer editor field (`shotserver_recipes.cpp:741`).
- [x] 6.3 Stop `currentSteamSpecJson()` dropping a heater-off selection (`maincontroller.cpp:2232`) and the wizard's equivalent (`RecipeWizardPage.qml:683`).
- [x] 6.4 The resurrect path (`maincontroller.cpp:1820`) must never create a preset for the off marker or for an unresolvable name.
- [ ] 6.5 Test: activate → deactivate restores the standing pitcher; a recipe carrying the marker never creates a preset.

## 7. Wizard

- [x] 7.1 Offer the built-in entry in `pitcherTileModel()` (`RecipeWizardPage.qml:1144`) instead of filtering disabled entries out.
- [x] 7.2 Show it distinguishably on the steam and summary cards rather than as a pitcher with blank values.
- [ ] 7.3 Test: selecting it stores the marker and round-trips through save/reopen.

## 8. Migration of existing heater-off presets

- [x] 8.1 Remove every preset marked `disabled`.
- [x] 8.2 Remap `selectedSteamPitcher`: surviving presets keep their identity, and a selection that was a removed preset lands on the built-in. **Without this the heater silently turns on at upgrade.**
- [x] 8.3 Rewrite recipes referencing a removed preset to the off marker (`RecipeStorage`), driven from the same migration and safe to re-run.
- [x] 8.4 Leave shot history untouched; make the promote path tolerate an unresolvable pitcher name.
- [x] 8.5 Tests: selection remap (including the on-the-Off-preset case), recipe rewrite, idempotency, and history left alone.

## 9. MCP surface

- [x] 9.1 Bump `McpSurfaceVersion` (`src/mcp/mcpserver.h`).
- [x] 9.2 Update the affected tool descriptions and stay inside the tool budget.

## 10. Documentation

- [x] 10.1 Wiki `Manual.md`: rewrite "Steam Heater Management" (§8) with the two settings and the four-state table; update the recipe activation paragraph and the milk-hold bullet (§ recipes); the presets paragraph; the Machine settings contents list; and the descaling instruction that says to disable the steam heater.
- [x] 10.2 Wiki `FAQ.md`: rewrite "How do I keep the steam heater off…", and add "Why did the steam heater turn off when I picked an espresso recipe?".
- [ ] 10.3 Add `images/settings-steam-heater.png` — a crop of the Steam Heater card showing both switches with their explanations — beside "Steam Heater Management". The markdown reference is in place; the file itself has to be captured on a machine where the app runs (the shell has no Screen Recording permission). Preferred over retaking `settings-machine-1.png`: the tab shot is about the whole tab, and a crop is what the section actually needs.
- [x] 10.4 Remove the unsourced "5–9 minutes" claim from `maincontroller.cpp:1841`, `:2590`, `maincontroller.h:760` and `RecipeWizardPage.qml:3421`, or replace it with a measured figure. It is repeated as fact in five places, each apparently sourced from the others.
