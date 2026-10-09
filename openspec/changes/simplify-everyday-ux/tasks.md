Each numbered item is meant to be its own small PR. Finding IDs (A1, B3, …) refer to the tables in `design.md`.

## 1. Phase 1: make the everyday path safe and legible

- [ ] 1.1 Defer notices during operations (A1). In review as Kulitorum/Decenza#2038: while `machineOperating` (the operation phases plus steam warm-up), the update prompt, charging-mismatch warning, scale notices, BLE errors and network notices go to `queuePopup()`, as behind the screensaver, and the queue drains when the operation ends with no other dialog open. A firmware flash is left out: it has no Stop button, and its AwaitingReboot state lasts until the user power-cycles.
- [ ] 1.2 Scale-missing dialogs (A2). The "Shot Stopped" part is in review as Kulitorum/Decenza#2037; the rest remains:
  - Replace "Settings → Bluetooth" with the real tab name, and give "Shot Stopped" "Reconnect" and "Open Connections" (Kulitorum/Decenza#2037).
  - Give "No Scale Found" the same two actions.
  - Add "Brew without scale this once", which needs a one-shot bypass in the controller.
  - Make the mid-session disconnect notice non-modal.
- [ ] 1.3 Visible start (A3):
  - The selected pill shows "▶" (or "Press group head" on GHC machines).
  - A blocked start shows a toast with the reason in every accessibility mode.
- [ ] 1.4 Remove the default `doubleclickAction` from the seven action tiles (A4). The pages stay reachable by long-press, and users who want double-tap can still set it per widget.
- [ ] 1.5 Make an accidental quit impossible, and gate the 5-second fake-shot corner to debug or simulation builds (A5). The default stays: no built-in layout has a Quit widget, so Sleep's long-press is the only in-app exit. Every in-app quit asks for confirmation instead. In review as Kulitorum/Decenza#2039 (one table for the Sleep defaults, fake-shot gate) and Kulitorum/Decenza#@CONFIRM_PR@ (the confirmation).
- [ ] 1.6 Toast on implicit recipe deactivation, naming the recipe, with a one-tap restore (B4).
- [ ] 1.7 Light-theme contrast (D1, D2, D3):
  - Plot background close to the surface colour.
  - Grid, axes and phase labels derived from `Theme.textColor`.
  - Button foregrounds always run through `contrastColorFor(fill)`.
  - Shot-history "Load" readable on its fill.
  - Live and goal series colours that meet 3:1 on the light plot background (today's light goal colours `#40d898`, `#6898e8`, `#f07080` measure about 1.8–2.9:1 even on white), covered by the existing palette contrast tests.
- [ ] 1.8 BLE permission errors carry an error code (in review as Kulitorum/Decenza#@BLE_PR@, built on Kulitorum/Decenza#2038). QML branches on the code, not on English substrings, and the replacement text is translated (C7).
- [ ] 1.9 Confirm or undo for flow-calibration reset, per-profile stop-at-weight reset, steam-health reset, forget-scale and delete-theme. Automatic backup before a replace-mode restore (C8).
- [ ] 1.10 Translate the stop-reason banner and include the result ("Stopped at 36.4 g · 28.1 s") (A11).

## 2. Phase 2: the daily loop

- [ ] 2.1 Post-shot review (A9):
  - A "Quick rate" card first: rating buttons of at least 56 px, the taste chips, and a large Done.
  - Measurements, beans, equipment, uploads, debug and delete collapse under "Details".
  - Revisit the "Never" auto-close default.
  - Build on the single shot page that shipped in `2026-10-08-unify-shot-page` (spec `shot-page`, which closes the ask in #1668), keeping its section order.
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
- [ ] 3.6 Settings search (C6): index every setting, add a check that each settings card has an index entry, and highlight the row instead of the whole card. The card-level part (the three missing cards and the check) is in review as Kulitorum/Decenza#2036; row-level entries and the row highlight remain.
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

## 5. Phase 5: lighter and smoother (measure first)

- [ ] 5.1 Baseline (D8). Extend `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md` with:
  - cold start to first usable frame (`am start -W` plus the existing startup checkpoints);
  - tap-to-first-frame for Steam, Hot Water, Settings, the recipe wizard, ChangeBeans and the post-shot review;
  - a main-thread lag probe (max and p99 per shot), read next to `[SAW-Latency]`;
  - GC logs during a shot.

  Record the numbers on the reference tablet with the workflow's `qml_profiling` build, and run bloaty on the unstripped CI library.
- [ ] 5.2 Package, small changes (E1, E2, E6, E7):
  - Exclude the unused Quick Controls styles, the `qmltooling` plugins and the unused image formats.
  - `QGuiApplication` instead of `QApplication`.
  - Drop the unreferenced splash PNGs, and `-threshold 0` for rcc.
  - Add a CI step that lists the APK and fails if an excluded library returns.
- [ ] 5.3 Package, maintainer calls (E3, E4, E5, E8): Tailscale as a separate build or on-demand load; FFmpeg on Android; Quick3D/ShaderTools if Graphs does not need them; bytecode-only QML if 5.1 supports it.
- [ ] 5.4 Navigation guard held until a frame known to have begun after the push (E9). Reuse the two-tick `FrameAnimation` pattern in `LastShotChartRenderer.qml`, not `frameSwapped`, which fires on the render thread and may belong to a frame already in flight.
- [ ] 5.5 Lazy, synchronous Loaders for the hidden views of Steam, Hot Water, Flush and the recipe wizard steps, and `OnDemandLoader` for the rare dialogs in `main.qml`. Seed `Theme.scale` before children evaluate (E10, E11).
- [ ] 5.6 Shot end (E12):
  - Take a complete value snapshot of the samples on the main thread, as `saveShot()` already does for its other fields (`ShotSaveData`).
  - Run only the analysis, JSON and compression (level 6) of that snapshot inside `runOnDbThread`, never reads of the live `ShotDataModel`, which a following shot may clear.
  - ChangeBeansDialog queries only when it opens.
- [ ] 5.7 Log file cost (E13), measured first. The synchronous write is deliberate: the next launch reads the log tail after a crash, so lines buffered on a worker thread would be lost exactly when they matter. If 5.1 shows the cost, keep one open handle with a flush per line instead of open/append/close, and move only trimming off the caller's thread.
- [ ] 5.8 Goal curves dirty only on goal/frame change, and per-token cached substitution in `CustomItem` (E14).
- [ ] 5.9 Lazy TTS; MemoryMonitor every 5 min and never during an operation; `sourceSize` on screensaver images (E15).
- [ ] 5.10 Review the `BleHelper` heap-utilization and forced-GC logic against the GC logs from 5.1, and remove it if it does not help (E16).
- [x] 5.11 ~~Unify the 26 conflicting translation fallbacks (E17).~~ Already done: `check_translation_key_conflicts.py` passes for 3,592 keys. The finding came from a stale comment.
- [ ] 5.12 Frame cap or off for continuous effects on low-end devices (E18).
- [ ] 5.13 Re-run 5.1 and record before and after for each item in the PR that lands it.

## 6. Per phase

- [ ] 6.1 Add spec deltas for the capabilities each PR modifies.
- [ ] 6.2 Update the wiki manual, shorter where a gesture no longer needs explaining.
- [ ] 6.3 Update the ShotServer counterpart where an in-app page has one.
