## Context

This change comes from a code read of v2.0.8 (`main` at 91f1e7d, Oct 2026): 258 QML files, about 103k lines. It covered:

- the daily brew loop;
- the profile, recipe, bean and equipment screens;
- settings, first run and error flows;
- the theme and visual system.

Contrast figures are computed from the default palettes in `settings_theme.cpp`. Nothing was measured on a device, so every finding below is a pointer to code to confirm, not a measured defect. Line numbers are as of 91f1e7d.

The reference point is the Meticulous machine's own screen ([usage manual](https://meticuloushome.com/pages/usage-manual)). It is not a feature model, because Decenza is far more capable. It shows how small an everyday surface can be:

- Pick a profile, then "Hold to start".
- While brewing: one pressure needle, four numbers and the current stage name.
- At the end: weight, time and "remove cup". The graph is one turn of the dial away.
- Errors carry one action ("Tap to retry").
- A ten-item menu, with Advanced settings and factory reset at the bottom.
- Profile building and setup live in the phone app.

## Goals / Non-Goals

**Goals**
- The everyday path takes two taps or fewer, and every affordance on it is visible.
- No dead ends: every problem message offers a way forward.
- Nothing covers a running shot.
- The screen is legible at arm's length, in both shipped themes, for any finger size.
- A user can predict where a setting lives.

**Non-Goals**
- Removing features or power-user paths. Gestures stay available as opt-in through the existing gesture-override system.
- Renaming "bag" (decided in #1993) or collapsing the multi-page recipe wizard (decided in #1610).
- Any BLE, profile-engine or database change.
- Copying Meticulous's look.

## Decisions

### D1: One obvious start
Selecting and starting are different acts and must look different. The selected pill shows ▶, or "Press group head" when a GHC owns the start. A start the app refuses always says why.

### D2: A running machine owns the screen
`operationActive` already exists (`main.qml:403`). Notices raised during an operation queue exactly as they do for the screensaver.

### D3: Every problem message has one way forward
Each problem message gets a button that fixes the problem, or a button that goes to where it can be fixed, named as the destination is actually named.

### D4: Show the result, then the next step
After a shot, the first thing on screen is the result and a fast rating. Everything else is one tap down. This follows Meticulous's green end screen.

### D5: Three tiers: everyday, set once, advanced
Settings and editors show the first two tiers by default. Advanced items sit behind one remembered switch and stay searchable.

### D6: One word, one meaning
"Favorites", "recipe" and the star icon each name exactly one thing.

### D7: Legible and hittable regardless of window size or theme
Window scaling may shrink drawing, but not hit areas below `Theme.touchTargetMin`. Contrast is checked for every shipped theme, graphs included.

## Findings

Effort: S = hours, M = days, L = a week or more.

### A. Home screen and daily brew loop

| ID | Finding | Evidence | Proposal | Effort |
|---|---|---|---|---|
| A1 | Update, charging and scale notices are deferred for the screensaver (scale notices also wait for wake), never for a running operation. They can open over a shot or steaming | `main.qml:2304-2306`, `:2065-2071`, `:1794-1796`; `operationActive` (`:403`) gates only the auto-sleep and auto-load timers | Queue while `operationActive` | S |
| A2 | The "Shot Stopped" dialog (shot aborted, saved scale missing) sends users to "Settings → Bluetooth", which does not exist (the tab is "Connections"). The only workaround it offers is forgetting the scale. All three scale dialogs offer only OK | `main.qml:1986-1990`, `:1871-1880`, `:1930-1939`; `SettingsTabs.qml:18` | Reconnect / Open Connections / brew once without scale | S–M |
| A3 | A second tap on the selected pill starts the shot, with no visual cue. When the machine is not ready the tap logs, and announces only in accessibility mode. A recipe re-tap blocked by a GHC only logs | `IdlePage.qml:490-513`, `:1127-1140`; `RecipesItem.qml:49-69`; `PresetPillRow.qml:458-486` | ▶ on the selected pill; visible reason toast | S |
| A4 | The seven action tiles (Recipes, Beans, Steam, Hot Water, Flush, Espresso, Equipment) reserve a double-tap by default, so each single tap waits 300 ms (outside accessibility mode) | `settings_network.cpp:894-904`, `LayoutItemDelegate.qml:309-317`, `CustomItem.qml:501`, `AccessibleTapHandler.qml:141-148` | No default double-tap; long-press opens the page | S |
| A5 | Holding Sleep quits the app (`allowQuit` defaults to true). An ungated 80×80 area: holding the top-right corner for 5 s saves a "[Simulated shot]" into real history and clears the current DYE notes/TDS/EY | `SleepItem.qml:17`, `:84-108`; `IdlePage.qml:43-78`; `maincontroller.cpp:4858-5001` | Default off; debug-only | S |
| A6 | Fresh install: "Espresso" appears nowhere on the home screen (the espresso widget sits in the bottom bar as "Profiles"). With zero recipes, tapping Recipes opens an empty row with no hint. A starter recipe is only made for upgrading users who accept the offer | `settings_network.cpp:321-340`, `EspressoItem.qml:147-149`, `maincontroller.cpp:2134-2135` | Empty state "+ New recipe / Brew with <profile>" | S–M |
| A7 | Bottom-bar popups close after selecting, so switch-and-brew takes four taps | `EspressoItem.qml:293`, `RecipesItem.qml:342`, `SteamItem.qml:246` | Keep open until start or an outside tap | S |
| A8 | "Heating" uses the error colour. Machine status cannot be tapped | `MachineStatusItem.qml:26-29`, `:106`, `:150` | Warning colour plus target; tap to reconnect | S–M |
| A9 | The post-shot review shows about 35 controls. Rating buttons are 36×28 with 12 px text, under `touchTargetMin` (44). Auto-close defaults to Never | `RatingInput.qml:100-119`, `Theme.qml:1042`, `PostShotReviewPage.qml:390` | Quick-rate card first, rest under Details | M |
| A10 | The Espresso page back arrow (and Esc, Space, Backspace) stops the shot. The STOP button is shown only on headless machines. Skip-step is the same button style as ±5/±10 g and sits right next to them | `EspressoPage.qml:119-127`, `:620-626`, `:703-752`, `:1011-1066` | A red "Stop"; a distinct, spaced Skip | S |
| A11 | The stop-reason banner (shown after every shot) is four hard-coded English strings, with text colour "black" | `main.qml:2506-2513`, `:2564-2568` | Translate; show the result | S |
| A12 | Six hand-rolled toasts share one position and layer, at 13 px | `main.qml:4763-5053`, `StatusToast.qml:49` | One queued toast at body size | S |

### B. Profiles, recipes, beans, equipment

| ID | Finding | Evidence | Proposal | Effort |
|---|---|---|---|---|
| B1 | The idle "Favorites" widget (star icon) opens Auto-Favorites. The same word and star mark starred profiles | `AutoFavoritesItem.qml:37-63`, `ProfileCard.qml:182-191`, `ProfilePicker.qml:458-464` | Rename Auto-Favorites; give it its own icon | S |
| B2 | The profile editor says "recipe": a new D-Flow profile is titled "D-Flow / New Recipe", and leaving it asks about "unsaved changes to this recipe" (the noun is inserted untranslated) | `ProfileSelectorPage.qml:214-216`, `RecipeEditorPage.qml:697`, `UnsavedChangesDialog.qml:71` | "Profile" throughout; translate the item noun | S |
| B3 | Dose, yield, grind and equipment are editable on the recipe, the bag and the profile, with a precedence the user cannot see | `ChangeBeansDialog.qml:2428-2539`, `RecipeWizardPage.qml:2787-2917`, `docs/CLAUDE_MD/RECIPES.md` | Provenance tag per value in Brew Settings | M |
| B4 | Changing the profile, bag, equipment, pitcher or vessel deactivates the active recipe silently (six swap watchers among 13 call sites). No QML reacts to `activeRecipeChanged` | `maincontroller.cpp:2695`; watchers at `:1699`, `:1729`, `:1752`, `:1850`, `:1857`, `:1893` | Toast plus restore | S |
| B5 | "Edit" loads the profile onto the machine before the editor opens. Just inspecting a profile changes the active profile and drops the active recipe (B4). Edits themselves upload on exit (Try/Save) | `ProfilePicker.qml:719-722`; `RecipeEditorPage.qml:174-187`, `:706-707` | Open the editor without loading; load on Try/Save | M |
| B6 | Which editor opens depends on the title prefix ("D-Flow / "). Rename prefills the whole title selected, so typing replaces the prefix and silently changes the editor. `convertCurrentProfileToAdvanced` / `switchToAdvancedEditor` have no UI caller | `main.qml:4067-4080`, `ProfilePicker.qml:995-999`, `:1074`; `profilemanager.cpp:3495` | Explicit editor type; convert with a warning | M |
| B7 | Profile cards have no curve preview (long-press only), tap loads immediately, and search matches titles only | `ProfileCard.qml:205-258`, `ProfileSelectorPage.qml:64-66` | Sparkline; tap previews, Load commits | M |
| B8 | ValueInput ± buttons are `sc(24)` wide and hold-repeat at a fixed 80 ms. Pressure and flow step by 0.01 (6→9 bar is 300 presses). A faster inline drag exists (20 px per step; drag down for ×10/×100), and so does typing (tap → popup → double-tap), but neither has a visible affordance | `ValueInput.qml:234`, `:269-275`, `:298-404`, `:881-886`; `SimpleProfileEditorPage.qml:456`, `:521` | Keypad toggle, acceleration, 0.1 step, 44+ px | S–M |
| B9 | Wizard temperature is an offset (±°), while tea uses an absolute temperature. Three input styles share one screen | `RecipeWizardPage.qml:2917-2994` | Absolute temperature shown; offset stored | S |
| B10 | One bag dialog has four titles; the Beans page lists bags; tapping an equipment card does not select it | `ChangeBeansDialog.qml:1189-1193`, `EquipmentPage.qml:14` | One title per action; tap to use | M |

### C. Settings, first run, errors

| ID | Finding | Evidence | Proposal | Effort |
|---|---|---|---|---|
| C1 | 12 release tabs, about 250 controls, grouped by implementation rather than by task. Examples: dark/light lives under Machine; Auto-Sleep under Screensaver; factory reset inside the web-server card; firmware under About | `SettingsTabs.qml:17-31`, `SettingsMachineTab.qml:732`, `SettingsScreensaverTab.qml:314`, `SettingsHistoryDataTab.qml:520-744` | Task-based groups (tasks 3.5) | L |
| C2 | No global advanced tier: heater calibration, raw logs, MAC addresses and Tailscale ACLs sit beside everyday items | `SettingsCalibrationTab.qml:1009-1093`, `SettingsConnectionsTab.qml:974`, `SettingsAITab.qml:1577-1680` | "Show advanced settings" switch | M |
| C3 | First run is one text dialog. Language defaults to "en" whatever the device locale. A never-paired scale is not auto-connected, so new users brew on estimated weight unknowingly | `main.qml:984-986`, `:3153-3175`; `translationmanager.cpp:45` | Four-step skippable onboarding | M |
| C4 | Jargon in tab names and labels: "MQTT", "Lang & Access", "Glass chrome", "Multiplier" | `SettingsTabs.qml:27`, `SettingsPage.qml:180` | Plain names; one-line card descriptions | S |
| C5 | AI provider setup exists on two screens with different wording | `AISettingsPage.qml:62-65`, `SettingsAITab.qml:188` | One screen | S |
| C6 | Settings search indexes 54 cards. Searching "fahrenheit", "celsius" or "units" finds nothing. Nothing checks that the index covers every card | `SettingsSearchIndex.js` | Full index plus a check | S |
| C7 | BLE permission errors are classified by English substrings in an already-translated message, then replaced by untranslated English. In other languages, the dialog loses its "open settings" buttons | `main.qml:1764-1771`, `blemanager.cpp:1230-1288` | Error codes | S |
| C8 | One-tap resets and deletes with no confirmation or undo. Replace-mode restore deletes all shots with no automatic backup | `SettingsCalibrationTab.qml:116`, `SettingsConnectionsTab.qml:1571-1576`, `SettingsThemesTab.qml:396-428` | Confirmation or undo; backup first | S–M |

### D. Visual system and accessibility

| ID | Finding | Evidence | Proposal | Effort |
|---|---|---|---|---|
| D1 | Light theme (no background preset): the plot area is `Qt.darker(white, 1.6)` = #9f9f9f, with hard-coded white grid and axes. Phase labels are 80% white on a #aaaaaa chip. Curves measure 1.1–2.3:1 | `DecenzaGraphsTheme.qml:31-42`, `ShotGraph.qml:446`, `HistoryShotGraph.qml:708` | Light plot background; derive line colours from `textColor` | S |
| D2 | Light theme buttons: primary text #4e85f4 on #d1daee measures 2.5:1; destructive measures 1.36:1 | `settings_theme.cpp:425`, `:471`; `AccessibleButton.qml:69` | Always use `contrastColorFor(fill)` | S |
| D3 | Shot-history "Load" is `primaryContrastColor` on the warning orange: white (1.9:1) in dark, blue in light. It is also a raw Rectangle+MouseArea | `ShotHistoryPage.qml:1045-1061` | Contrast foreground, or a neutral button | S |
| D4 | Quality issues are 8 px dots, the same red for three different problems (orange for grind) | `ShotHistoryPage.qml:1002-1027` | Text badges | S |
| D5 | `Theme.scale` = min(w/960, h/600) with no floor. On a landscape phone it is about 0.69, so ± buttons fall to roughly 16 dp | `main.qml:1039-1046` | A hit-area floor | M |
| D6 | 680 `pixelSize: Theme.scaled(<n>)` sites (22 distinct sizes, 142 at ≤11) bypass the font roles. Core review labels are 10 px, and font-size overrides do not reach them | `PostShotReviewPage.qml:1897` and others | Map onto the roles; add a check | M–L |
| D7 | `Theme.borderColor` on the surface measures 1.35:1, and `StyledComboBox` has no border at rest | `StyledComboBox.qml:44-47` | Control-border token of at least 3:1 | S |
| D8 | Rotated phase labels overlap and hide the curves. The legend does not explain dashed = target | `ShotGraph.qml:409-460`, `HistoryShotGraph.qml:692-716`; fix exists in `ComparisonGraph.qml:505-521` | Port the packing; add a legend key | S |
| D9 | In shot history, tapping a row toggles compare selection. Opening the shot needs a long-press or the small ">" button. With a screen reader the default action opens it | `ShotHistoryPage.qml:1137-1141`, `:1109-1132`, `:790` | Tap opens; a checkbox selects | M |

### Already good, keep it
- Operation pages appear whoever started the operation (app or group head), and back is always bottom-left.
- The popup queue waits out the screensaver and ignores brief scale drops. One touch on the screensaver wakes both machine and scale.
- The review page autosaves with Undo. Grind picker wheels come from shot history. The shared profile picker shows "Recommended for <bean>" with reasons.
- Factory reset has two confirmation steps. Restore defaults to merge. The firmware update flow is clear.
- Accessibility discipline is strong (`AccessibleButton` everywhere, announced shot rows). The comparison graph tells shots apart by line pattern, not colour.

## Related issues

| Issue | Covered by |
|---|---|
| #1547 recipe page layout (grind hidden) | B3, B9 |
| #1608, #1972 add bag flow | B10 |
| #1609, #1793 recipe-first home | A3, A6 |
| #1610 recipe wizard feedback | B9 (wizard stays multi-page) |
| #1630 last-shot graph space | D8 |
| #1668 Shot Review vs Shot Detail | A9 |
| #1794 milk recipe → steam | A9 (next-step card) |
| #1799 keyboard pops up unasked | tasks 2.8 |
| #1987 flush ±5 s, quick ratio presets | A7 follow-up |
| #1993 terminology consistency | B1, B2, B10 |
| #1153, #1633 theme controls and saving | D2, D7 |

## Risks / Trade-offs

- **Muscle memory.** Removing the default double-tap and regrouping settings changes where things are. Mitigations: opt-in gesture overrides already exist, search reveals moved items, and the regroup happens once, with the manual updated in the same PRs.
- **Hiding advanced items.** Community members who help others say "go to X". Mitigations: the switch is remembered, search always reaches advanced items, and the manual names the switch.
- **Not loading on Edit (B5).** Some users may rely on Edit also selecting the profile. The editor's existing Try action covers that explicitly.
- **Light theme.** Fixing it touches the shared theme engine. The existing contrast tests should be extended to the graph colours rather than relied on as they are.

## Open Questions

1. Is "Coffee (bag) / Gear / Profile / Recipe" acceptable as the user-facing model, keeping "bag" as the item noun and making the recipe the only home of dose, yield, temperature and grind (B3)?
2. Should a fresh install get a starter recipe built from the default profile (A6)?
3. What should the post-shot review's default auto-close be (A9)?
4. Should the advanced tier be one global switch, or per tab (C2)?
5. Should Edit stop loading the profile onto the machine (B5), leaving Try/Save to load it?
