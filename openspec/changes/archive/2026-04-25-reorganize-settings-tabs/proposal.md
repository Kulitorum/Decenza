# Change: Reorganize Settings Tabs and Add Settings Search

## Why

The Settings page has 14 tabs with settings placed wherever there was room rather than where users would look for them. The Preferences tab alone has become a catch-all with ~19 cards spanning machine hardware, display, calibration, app modes, and services. Settings like "Enable Server" appear in two tabs (History and Data). The tab count (14) exceeds what users can scan without scrolling, and there's no way to search for a specific setting.

Adding a settings search is the single highest-impact improvement — it makes organizational questions secondary because users who know what they want bypass tabs entirely.

## What Changes

### Current Tab Structure (14 tabs + 1 conditional)

| # | Tab | Items | Problem |
|---|-----|-------|---------|
| 0 | Connections | Machine BLE/USB, Scale BLE/USB, Refractometer | Already full |
| 1 | Preferences | Theme mode, Auto-Sleep, Post-Shot Review, Refill Kit, Offline Mode, Launcher Mode, Shot Map, Per-Screen Scale, Battery Charging, Flow Cal, Ignore SAV, Water Level, Virtual Scale, Auto-Wake, SAW Cal, Heater Cal, etc. | **Catch-all; ~19 unrelated cards** |
| 2 | Screensaver | Screensaver type + per-type config | OK |
| 3 | Visualizer | Account + upload settings | OK |
| 4 | AI | Provider, MCP server, Discuss Shot | OK |
| 5 | Accessibility | TTS, tick sounds, extraction announcements | Small, rarely visited |
| 6 | Themes | Color editor, presets | Already busy |
| 7 | Layout | Idle page widget editor | Already busy |
| 8 | Language | Language picker + translation tools | Small, rarely visited |
| 9 | History | Shot history link, DE1 import, **Remote Access** | Server toggle duplicated from Data |
| 10 | Data | **Server toggle (duplicate)**, Security, Backup, Device Migration, Factory Reset | Overlaps History |
| 11 | MQTT | MQTT config, Home Assistant | OK |
| 12 | Update | Version, auto-update, beta toggle, release notes | OK but small |
| 13 | About | Credits, donate | OK but small |
| 14 | Debug | Resolution, simulation (debug builds only) | OK |

### Proposed Tab Structure (12 tabs + 1 conditional)

| # | Tab | Contents | Source |
|---|-----|----------|--------|
| 0 | **Connections** | Unchanged | Connections |
| 1 | **Machine** | Renamed from Preferences. Calibration items removed, rest stays: Theme Mode, Auto-Sleep, Auto-Wake, Shot Review Timer, Refill Kit, Simulation Mode, Launcher Mode, Shot Map, Screen Zoom, Battery Charging, Water Level, Water Refill Threshold | Preferences (minus calibration) |
| 2 | **Calibration** | Flow Calibration, Weight Stop Timing, Heater Calibration, Virtual Scale, Prefer Weight over Volume | Preferences (calibration items) |
| 3 | **History & Data** | Three columns: Shot History, Backup, Server & Data. Single server toggle. Device Migration as dialog. | History + Data merged |
| 4 | **Themes** | Unchanged | Themes |
| 5 | **Layout** | Unchanged | Layout |
| 6 | **Screensaver** | Unchanged | Screensaver |
| 7 | **Visualizer** | Unchanged | Visualizer |
| 8 | **AI** | Unchanged | AI |
| 9 | **MQTT** | Unchanged | MQTT |
| 10 | **Language & Access** | Language picker + translation tools + Accessibility settings (TTS, tick sounds, extraction announcements) | Language + Accessibility merged |
| 11 | **About** | Version + update controls + release notes (full width) + credits + donate | Update + About merged |
| 12 | **Debug** | Unchanged (debug builds only, always last) | Debug |

### What Moves Where (complete mapping)

| Setting | From | To | Rationale |
|---------|------|----|-----------|
| Flow Calibration | Preferences | **Calibration** | Measurement tuning |
| SAW Calibration (→ "Weight Stop Timing") | Preferences | **Calibration** | Measurement tuning |
| Heater Calibration | Preferences | **Calibration** | Measurement tuning |
| Virtual Scale (FlowScale) | Preferences | **Calibration** | Fallback measurement device |
| Ignore SAV with Scale (→ "Prefer Weight over Volume") | Preferences | **Calibration** | Measurement override behavior |
| All other Preferences items | Preferences | **Machine** (renamed) | Stay in place, tab renamed |
| Server toggle (duplicate) | History + Data | **History & Data** (single) | Eliminate duplication |
| About content | About tab | **About** (merged with Update) | Too small for own tab |
| Accessibility settings | Accessibility tab | **Language & Access** | Both set-once, rarely visited |

### Setting Card Renames (clarity improvements)

| Current Name | New Name | Rationale |
|---|---|---|
| Per-Screen Scale | **Screen Zoom** | "Scale" means weighing device in this app — ambiguous |
| Close Shot Review Screen | **Shot Review Timer** | Describes what it is (a timer), not an action |
| Unlock GUI | **Simulation Mode** | Match the card title, say what it does |
| Ignore Stop-at-Volume with Scale | **Prefer Weight over Volume** | User-facing behavior, not technical jargon |
| Stop-at-Weight Calibration | **Weight Stop Timing** | It's about timing lag, not measurement accuracy |

### What Gets Added

| Item | Description |
|------|-------------|
| **Settings Search** | Search icon in tab bar opens a modal Dialog with text field and filtered results. Tapping a result navigates to the correct tab and highlights the target card. |
| **Search Index** | JS array of `{ tabIndex, cardId, title, description, keywords }` for all searchable settings |

### What Stays on Operation Pages (confirmed correct)

| Setting | Page | Why it stays |
|---------|------|-------------|
| Steam presets (pitcher, duration, flow, temp) | SteamPage | Adjusted while steaming, needs live feedback |
| Hot water presets (vessel, volume, temp, flow) | HotWaterPage | Contextual adjustment during operation |
| Flush presets (flow, duration) | FlushPage | Contextual adjustment during operation |
| Bean/DYE metadata | BeanInfoPage, PostShotReviewPage | Pre/post-shot workflow, not a preference |
| Profile selection | IdlePage, ProfileSelectorPage | Changes per shot |

## Decisions

**Decision 1: Rename Preferences → Machine, don't create Display or Espresso tabs.** The Preferences tab's problem is its name and the calibration clutter, not its size. With calibration items extracted and search added, ~12 coherent machine/app-behavior cards under a clear name is fine. Creating thin tabs (Display with 2-3 cards, Espresso with 2 cards) introduces new problems.

**Decision 2: New Calibration tab.** Flow Cal, SAW Cal, Heater Cal, Virtual Scale, and Ignore SAV with Scale are all "dialing in measurement accuracy." They're the most technical cards in Preferences and the same user visits all of them during setup. Extracting them makes Machine more approachable.

**Decision 3: Don't move settings into already-full tabs.** Connections, Themes, Layout, and About are already well-designed and dense. Moving overflow into them degrades pages that work. Settings search reduces the importance of perfect placement.

**Decision 4: Settings Search via modal Dialog.** A search icon in the tab bar opens a modal Dialog (not a dropdown — dropdowns are inaccessible with TalkBack/VoiceOver). Results are `AccessibleButton` delegates. Tapping a result closes the dialog, switches to the tab, scrolls to the card, and briefly highlights it. A static JS search index keeps maintenance simple.

**Decision 5: Merge History + Data.** Eliminates duplicate server toggle. Three-column layout: Shot History | Backup | Server & Data. Device Migration as dialog (matches existing TOTP/ZIP extraction patterns).

**Decision 6: Merge Update + About.** About content (credits, donation) in the left column below update toggles. Release notes keep the full right column — no shrinkage. Tab renamed to "About."

**Decision 7: Merge Language + Accessibility → Language & Access.** Both are set-once, low-traffic tabs. Combined tab uses the existing short label "Access" from the Accessibility tab. With search, users typing "screen reader" or "TalkBack" find it regardless of tab name.

**Decision 8: Tab ordering by user mental model.** Four clusters: Setup (Connections → Machine → Calibration → History & Data), Customize (Themes → Layout → Screensaver), Services (Visualizer → AI → MQTT), System (Language & Access → About). Most-visited tabs first within each cluster. Debug always last when visible.

**Decision 9: Within-tab card ordering by frequency.** Machine: Power & Schedule (left) → App Behavior (middle) → Water & Features (right). Calibration: Flow Cal → Weight Stop Timing → Heater Cal → Virtual Scale → Prefer Weight over Volume. Most-adjusted settings at top of each column.

**Decision 10: Rename confusing setting cards.** Five cards get clearer names: "Per-Screen Scale" → "Screen Zoom" (avoids confusion with weighing scales), "Close Shot Review Screen" → "Shot Review Timer", "Unlock GUI" → "Simulation Mode", "Ignore Stop-at-Volume with Scale" → "Prefer Weight over Volume", "Stop-at-Weight Calibration" → "Weight Stop Timing". Translation keys updated accordingly.

## Cross-Tab References to Update

| File | Current text | New text |
|------|-------------|----------|
| `ConversationOverlay.qml` | "Settings → Shot History" | "Settings → History & Data" |
| `SettingsDataTab.qml` | "Shot History settings" | "History & Data" |
| Any reference to "Preferences" tab | "Preferences" | "Machine" |
| Any reference to "Accessibility" tab | "Accessibility" | "Language & Access" |

## Impact

- Affected code:
  - `qml/pages/SettingsPage.qml` — tab bar restructure (14 → 12 tabs), add search icon + search dialog
  - `qml/pages/settings/SettingsPreferencesTab.qml` — renamed to `SettingsMachineTab.qml`, calibration cards removed
  - `qml/pages/settings/SettingsCalibrationTab.qml` — new file with 5 calibration cards
  - `qml/pages/settings/SettingsShotHistoryTab.qml` — merged with Data
  - `qml/pages/settings/SettingsDataTab.qml` — merged with History, remove duplicate server toggle
  - `qml/pages/settings/SettingsUpdateTab.qml` — merge with About
  - `qml/pages/settings/SettingsAboutTab.qml` — merge into About
  - `qml/pages/settings/SettingsLanguageTab.qml` — receive Accessibility content
  - `qml/pages/settings/SettingsAccessibilityTab.qml` — merged into Language & Access
  - `qml/pages/settings/SettingsSearchDialog.qml` — new search dialog
  - `qml/components/SettingsSearchIndex.js` — new search index
  - `qml/main.qml` — update `goToSettings()` tab indices for deep-links
  - New files: `SettingsCalibrationTab.qml`, `SettingsSearchDialog.qml`, `SettingsSearchIndex.js`
  - Renamed files: `SettingsPreferencesTab.qml` → `SettingsMachineTab.qml`
  - Removed files: `SettingsAboutTab.qml` (merged), `SettingsAccessibilityTab.qml` (merged)
  - String updates: cross-tab reference strings in QML files
