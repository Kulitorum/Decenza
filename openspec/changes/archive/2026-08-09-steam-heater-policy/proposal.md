## Why

Five inputs currently compete for one machine value (the DE1's `TargetSteamTemp`), and two independent code paths derive it. They disagree.

- `keepSteamHeaterOn` — persisted, default on.
- `steamDisabled` — session-only, no persistence, cleared from three different places.
- A steam pitcher preset marked `disabled` — the "Off" pitcher, which users create by hand.
- A recipe's `hasMilk` — an implicit heater hold that silently overrides `keepSteamHeaterOn`.
- The selected pitcher's own temperature versus the global steam temperature.

The canonical derivation lives in `MainController::sendMachineSettings()`. `ProfileManager::uploadCurrentProfile()` hand-rolls a second copy that knows only two of the five inputs, so **every profile upload re-sends steam = 0 for a user who has `keepSteamHeaterOn` off**, wiping the milk hold that had just been applied. Recipe activation triggers a profile upload, and that upload is deferred behind `m_uploadInFlight` so it lands *after* `startSteamHeating()` — the heater is commanded on, then immediately turned off again. This is a reported field bug: selecting a milk recipe does not warm the steam heater, and the user has to press Steam to get any heat.

Beyond the bug, the model itself cannot express what users want. The two behaviours "keep the heater standing by" and "let the drink decide" are collapsed into one boolean, so a user who wants both — warm by default, but cold when an espresso recipe is active — has no way to say it. A user who wants neither cannot say that either: today's `keepSteamHeaterOn: false` is silently converted into "follow the recipe" by the milk hold.

Recipe activation also writes the global pitcher selection directly, and deactivation never restores it. A recipe carrying an Off pitcher therefore strands the machine's heater off with no active recipe to explain why.

## What Changes

- **Replace the single `keepSteamHeaterOn` boolean with two independent settings** in Settings → Machine → Steam:
  - **Keep warm when idle** — the baseline: is the heater warm when nothing else says otherwise.
  - **Let the recipe decide** — whether an active recipe's pitcher overrides the standing pitcher.

  They answer different questions and compose into four meaningful states, rather than competing for one.

- **Adopt a permission/veto model** as the single rule. Permission to be warm comes from the settings above or from a deliberate steam action, and nothing else. Any veto wins: the effective pitcher being "Heater off", or the transient widget toggle.

- **One derivation.** A single helper computes `(heaterOn, targetTemperature)`; `sendMachineSettings`, `uploadCurrentProfile`, `startSteamHeating`, `turnOffSteamHeater` and the MQTT status string all call it. `ProfileManager`'s private copy is deleted. This is the fix for the reported bug.

- **Delete the `hasMilk` heater hold.** `activeRecipeHasMilk()`, the hold branch in `sendMachineSettings`, and the release in `deactivateRecipe` all go. `hasMilk` returns to meaning "this drink has milk". Under **Let the recipe decide**, a milk recipe warms the heater **when its shot starts**, not when it is selected — recipe selection is a stale signal, because users park a recipe as the machine's resting state rather than choosing one per drink.

- **BREAKING (behavioural):** a recipe with no pitcher counts as "no milk" under **Let the recipe decide**, so activating it turns the heater off. **Let the recipe decide** defaults **on for everyone**, existing installs included — so a user with a parked pitcher-less espresso recipe will find the steam heater off after updating where it used to be warm. This is deliberate: it is the behaviour the change exists to deliver, and gating it behind a setting nobody finds would deliver it to nobody. It needs calling out in the release notes, and the FAQ carries an entry answering it directly.

- **Replace user-created Off pitchers with one built-in "Heater off" entry** that everyone has, whose displayed name is translated. Users can no longer create additional ones; the add-pitcher dialog's Off button, `addSteamPitcherPresetDisabled()`, and the MCP path that reaches it are removed. Its identity is an off marker, never its name, so a language change cannot break a recipe that references it.

- **Migrate existing Off pitchers away.** Any preset marked `disabled` is removed, `selectedSteamPitcher` is remapped (a user sitting on their Off pitcher lands on the built-in, so no heater silently turns on at upgrade), and recipes referencing a removed preset are rewritten to the off marker. Shot history is left untouched, and the promote-a-shot path tolerates a pitcher name it can no longer resolve without manufacturing a preset.

- **The recipe's pitcher becomes an override, not a write.** While a recipe is active its pitcher supersedes the standing pitcher; deactivating unwinds it. The global selection is no longer overwritten, so nothing strands at Off.

- **Recipe activation retargets the steam temperature to the pitcher's own value**, via the same shared "select this pitcher and apply its values" helper the MCP path, the idle pill row, the SteamItem popup and the Steam page use. Activation is currently the one caller that stores the index without applying the values.

## Capabilities

### New Capabilities
- `steam-heater-policy`: the permission/veto model, the two settings, the built-in "Heater off" entry, the single derivation of the steam target, and the migration off user-created Off pitchers.

### Modified Capabilities
- `recipe-activation`: the `hasMilk` heater hold is removed and replaced by the pitcher override plus shot-start warming; activation applies the pitcher's values rather than only its index.
- `recipe-model`: the steam block carries an off marker instead of relying on a pitcher name that a translation can break.
- `recipe-wizard`: the pitcher picker offers the built-in "Heater off" entry instead of filtering disabled presets out, and stops being able to create one.

## Impact

- **Settings**: `src/core/settings_brew.h/.cpp` — `keepSteamHeaterOn` replaced by two properties; `addSteamPitcherPresetDisabled()` removed; the built-in entry and the one-time migration (following the existing idempotent constructor-migration pattern at `settings_brew.cpp:72`); mutators refuse to rename, delete, reorder or edit the built-in.
- **Controllers**: `src/controllers/maincontroller.cpp` — the single derivation helper, the shared pitcher-select helper, deletion of `activeRecipeHasMilk()` and the hold, the recipe pitcher override layer, shot-start warming.
- **Profile manager**: `src/controllers/profilemanager.cpp:2188` — the duplicated derivation is deleted and replaced with a call to the shared helper.
- **QML**: `qml/pages/settings/SettingsMachineTab.qml` (two switches replacing one); `qml/pages/SteamPage.qml` (add-dialog Off button removed, built-in entry rendered and protected from edit/delete); `qml/pages/RecipeWizardPage.qml` (picker offers the built-in, stops filtering); `qml/main.qml` (the return-to-idle turn-off becomes event-scoped); `qml/pages/IdlePage.qml` and `qml/components/layout/items/SteamItem.qml` (pill rows route through the shared select helper).
- **MCP**: `src/mcp/mcptools_write.cpp` (`keepSteamHeaterOn` replaced by the two keys), `src/mcp/mcptools_settings.cpp` (reads), `src/mcp/mcptools_presets.cpp` (the disabled-add path removed; the select path routes through the shared helper), `src/mcp/mcptools_recipes.cpp` (steam block schema gains the off marker). `McpSurfaceVersion` bumps.
- **Settings backup**: `src/core/settingsserializer.cpp` — the two new keys, and an import path that maps a legacy `keepHeaterOn` plus any `disabled` presets through the same migration.
- **Web**: `src/network/shotserver_recipes.cpp` — the free-text pitcher field gains the off marker; `src/network/mqttclient.cpp` — its status derivation routes through the shared helper.
- **Recipes storage**: `src/history/recipestorage.cpp` — a one-time pass rewriting recipes that reference a removed Off preset.
- **Tests**: the derivation table (all four setting combinations × recipe states × pitcher), the migration (selection remap, recipe rewrite, idempotency), and a guard that no second derivation of the steam target exists.
- **Docs**: wiki manual — `Manual.md` sections on the steam heater, recipe activation, presets, the Machine settings list and the descaling instruction; `FAQ.md` two entries. Tracked in `tasks.md`.
