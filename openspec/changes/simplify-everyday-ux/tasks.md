Each numbered item is meant to be its own small PR. Finding IDs (A1, B3, …) refer to the tables in `design.md`.

## 1. Phase 1: make the everyday path safe and legible

- [ ] 1.1 Defer notices during operations (A1). The update prompt, charging-mismatch warning and scale notices go to `queuePopup()` when `root.operationActive`, not only when `screensaverActive`. Drain the queue on the return to Idle.
- [ ] 1.2 Scale-missing dialogs (A2):
  - Replace "Settings → Bluetooth" with the real tab name.
  - Add "Reconnect", "Open Connections" and "Brew without scale this once".
  - Make the mid-session disconnect notice non-modal.
- [ ] 1.3 Visible start (A3):
  - The selected pill shows "▶" (or "Press group head" on GHC machines).
  - A blocked start shows a toast with the reason in every accessibility mode.
- [ ] 1.4 Remove the default `doubleclickAction` from the seven action tiles (A4). The pages stay reachable by long-press, and users who want double-tap can still set it per widget.
- [ ] 1.5 Default `allowQuit` to false on the Sleep widget, and gate the 5-second fake-shot corner to debug or simulation builds (A5).
- [ ] 1.6 Toast on implicit recipe deactivation, naming the recipe, with a one-tap restore (B4).
- [ ] 1.7 Light-theme contrast (D1, D2, D3):
  - Plot background close to the surface colour.
  - Grid, axes and phase labels derived from `Theme.textColor`.
  - Button foregrounds always run through `contrastColorFor(fill)`.
  - Shot-history "Load" readable on its fill.
- [ ] 1.8 BLE permission errors carry an error code. QML branches on the code, not on English substrings, and the replacement text is translated (C7).
- [ ] 1.9 Confirm or undo for flow-calibration reset, per-profile stop-at-weight reset, steam-health reset, forget-scale and delete-theme. Automatic backup before a replace-mode restore (C8).
- [ ] 1.10 Translate the stop-reason banner and include the result ("Stopped at 36.4 g · 28.1 s") (A11).

## 2. Phase 2: the daily loop

- [ ] 2.1 Post-shot review (A9):
  - A "Quick rate" card first: rating buttons of at least 56 px, the taste chips, and a large Done.
  - Measurements, beans, equipment, uploads, debug and delete collapse under "Details".
  - Revisit the "Never" auto-close default.
  - Coordinate with #1668 (merge Shot Review and Shot Detail).
- [ ] 2.2 ValueInput (B8):
  - The popup opens on a numeric keypad, with a visible Keypad/Stepper toggle like `GrindPickerDialog`'s.
  - Hold repeat accelerates (×10 after about 1 s), and the existing drag gesture gets a visible affordance.
  - Pressure and flow coarse step 0.1, with 0.01 as the fine step.
  - ± buttons at least `touchTargetMin` wide.
- [ ] 2.3 First-run home (A6):
  - With zero recipes, the Recipes row shows "+ New recipe" and "Brew with <current profile>".
  - Decide whether a new install gets a starter recipe (open question 2).
- [ ] 2.4 Espresso page (A10):
  - The back control reads "Stop" (red) on every machine.
  - Skip-step is visually distinct from +10 g and spaced from it.
- [ ] 2.5 Bottom-bar popups stay open after a selecting tap, and close on start or on an outside tap (A7).
- [ ] 2.6 Machine status (A8):
  - Heating in the warning colour, with the target temperature.
  - Tapping Disconnected reconnects or opens Connections.
  - Readiness shown as a subtitle on the start tile.
- [ ] 2.7 Unify the six hand-rolled toasts in `main.qml` into one queued `StatusToast` at body font size (A12).
- [ ] 2.8 Keyboard only opens on an explicit focus, not on page entry (#1799).
- [ ] 2.9 Recipe wizard temperature (B9): the user enters an absolute temperature (shown next to the profile's), and the offset is stored. One input style per screen. "No coffee" comes first in the bag list. The flow stays multi-page (#1610).

## 3. Phase 3: structure (maintainer decisions first)

- [ ] 3.1 Terminology (B1, B2):
  - Auto-Favorites gets its own name and icon.
  - The profile editor never says "recipe".
  - `UnsavedChangesDialog` translates its item noun.
- [ ] 3.2 Value provenance (B3). Brew Settings shows where each value comes from (recipe / bag / profile), and editing a value says which layer it writes to.
- [ ] 3.3 Profile editing (B5, B6):
  - Opening the editor does not change the machine's active profile; Try or Save loads it.
  - Show the current editor type, with "Convert to Advanced" behind a one-way warning.
  - A rename keeps the editor type.
  - The New Profile dialog describes each type in one line.
- [ ] 3.4 Profile list (B7): a curve thumbnail per card; on tablets, tap previews and "Load" commits; same-title profiles say their source in words.
- [ ] 3.5 Settings information architecture (C1, C2, C4, C5):
  - Regroup the tabs as Basics / Machine / Brewing / Home Screen / Data & Sharing / Integrations / About.
  - Add a "Show advanced settings" switch, off by default, with search still revealing advanced items.
  - Rename the "MQTT" and "Lang & Access" tabs.
  - One AI setup screen instead of two.
- [ ] 3.6 Settings search (C6): index every setting, add a check that each settings card has an index entry, and highlight the row instead of the whole card.
- [ ] 3.7 Onboarding (C3), skippable:
  - Language and units pre-filled from `QLocale`.
  - Machine search with a wake hint.
  - Scale pairing, or "I don't have one".
  - First drink.
- [ ] 3.8 Beans and equipment (B10): one dialog title per action; tapping an equipment card means "use this"; advanced bag fields go under "More" (#1608, #1972).

## 4. Phase 4: visual system

- [ ] 4.1 Hit-area floor (D5): a `Theme` helper so a hit area never drops below the minimum after scaling. Apply it to ValueInput, ProfileCard actions, GraphChip, combo boxes and list-row actions.
- [ ] 4.2 Typography (D6):
  - Map `pixelSize: Theme.scaled(<n>)` sites onto the Theme font roles.
  - No meaningful text below 13 px at reference scale.
  - Add a text-invariants check against new literal sizes.
- [ ] 4.3 Add a `Theme.controlBorderColor` token of at least 3:1, and give `StyledComboBox` a visible boundary (D7).
- [ ] 4.4 Shot graphs (D8):
  - Port `ComparisonGraph`'s phase-label packing to `ShotGraph` and `HistoryShotGraph`.
  - Make the label boxes translucent.
  - Add a "dashed = target" legend key (#1630).
- [ ] 4.5 Shot history (D4, D9):
  - Quality issues as short text badges.
  - Tap opens the shot, and selection for comparison uses a checkbox.
  - Load and Recipe move to the shot page or an overflow menu.

## 5. Per phase

- [ ] 5.1 Add spec deltas for the capabilities each PR modifies.
- [ ] 5.2 Update the wiki manual, shorter where a gesture no longer needs explaining.
- [ ] 5.3 Update the ShotServer counterpart where an in-app page has one.
