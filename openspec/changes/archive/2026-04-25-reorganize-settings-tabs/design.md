## Context

The Settings page has 14 tabs that grew organically. The Preferences tab became a dumping ground for anything that didn't obviously belong elsewhere, accumulating ~19 cards spanning hardware, display, calibration, app modes, and features. Server controls are duplicated across History and Data tabs. There is no way to search for a setting — users must scan through tabs linearly.

This is primarily a UI reorganization. No settings are added or removed — only moved between tabs. The C++ Settings class and all Q_PROPERTY bindings remain unchanged. The one new feature is a settings search dialog.

## Goals / Non-Goals

- **Goals:**
  - Every setting has exactly one home (eliminate duplication)
  - Each tab has a clear mental model (a user can predict which tab holds a given setting)
  - Tab names are self-explanatory without domain knowledge
  - Users can find any setting via search without knowing which tab it's in
  - Don't degrade well-designed, already-full tabs by dumping overflow into them

- **Non-Goals:**
  - Adding new settings or removing existing ones
  - Changing the Settings C++ class or property names
  - Redesigning individual setting cards/widgets
  - Changing navigation patterns (still tab-based)
  - Mobile-specific layout changes (responsive layout is a separate concern)
  - Moving settings into Connections, Themes, Layout, or other already-full tabs

## Decisions

### Decision: Rename Preferences → Machine, extract Calibration

The Preferences tab's core problem is its name (tells users nothing) and the mix of unrelated concerns. Rather than creating multiple thin tabs, we:

1. **Rename** Preferences to Machine — most content is already machine behavior
2. **Extract** the 5 calibration/measurement cards into a new Calibration tab
3. **Keep** Theme Mode, Per-Screen Scale, and other app-behavior cards in Machine

This leaves Machine with ~12 cards across 3 columns — manageable with search, and coherent under the "Machine" label.

**What moves to Calibration:**

| Setting | Why it's calibration |
|---------|---------------------|
| Flow Calibration | Measures flow accuracy |
| Stop-at-Weight Calibration | SAW lag tuning |
| Heater Calibration | Temperature accuracy |
| Virtual Scale (FlowScale) | Fallback measurement device |
| Ignore SAV with Scale | Which measurement wins |

### Decision: Don't move settings into already-full tabs

Connections, Themes, Layout, and About are well-designed and dense. Moving Preferences overflow into them solves one problem by creating others. With settings search, perfect organizational placement matters less — users who know what they want find it instantly.

### Decision: Settings search via modal Dialog

A dropdown list is not accessible — TalkBack/VoiceOver cannot trap focus inside it. A modal Dialog solves this naturally.

**Placement:** Search icon on the right end of the tab bar.

**Flow:**
1. User taps search icon → modal Dialog opens
2. Text field at top of dialog, scrollable results list below
3. Results filter live as user types
4. Each result is an `AccessibleButton` showing setting name + parent tab badge
5. Tapping a result → dialog closes → switches to tab → scrolls to card → briefly highlights it
6. Standard dialog close / back gesture to dismiss

**Search index:** A static JS array in `SettingsSearchIndex.js`:
```js
[
    { tabIndex: 1, cardId: "autoSleepCard",
      title: "Auto-Sleep",
      description: "Put the machine to sleep after inactivity",
      keywords: ["timeout", "power", "idle", "sleep"] },
    // ...
]
```

The `keywords` field catches synonyms — searching "power" finds Auto-Sleep even though "power" isn't in the title. When someone adds a new setting card, they add one entry to the index.

**Why navigate-to-card instead of rendering settings inline:** Rendering actual setting widgets outside their tab context is complex (bindings, state, lazy loading). Navigate + highlight is simple, maintainable, and matches iOS/Android Settings.

### Decision: Merge History + Data, eliminate duplicate server toggle

Current state: `shotServerEnabled` toggle exists in both History tab ("Remote Access") and Data tab ("Share Data"). Merging eliminates the duplicate.

**Merged layout — three columns ordered by visit frequency:**

| Shot History | Backup | Server & Data |
|---|---|---|
| "Shot History →" button | Daily Backup config | Enable Server toggle |
| Total shots count | Backup time selector | Server URL + status |
| *divider* | Backup location | Enable Security toggle |
| Import from DE1 App | Storage permission (Android) | TOTP setup / reset |
| Overwrite toggle | "Backup Now" button | *divider* |
| Import buttons (DE1, ZIP, Folder) | *divider* | Data summary (shots/profiles) |
| Import progress | "Restore from Backup" | *divider* |
| | Restore button | "Import from Another Device" → dialog |
| | | *divider* |
| | | Factory Reset (bottom, destructive) |

Note: This is a set-it-and-forget-it tab. Once configured, users go to History and Favorites pages directly to view data. The tab is positioned in the Setup cluster accordingly.

### Decision: Device Migration as dialog flow

The 5-state wizard (idle → searching → devices found → TOTP auth → manifest/import) is a rare operation. A dialog matches existing patterns (TOTP setup, ZIP extraction) and keeps the common case clean (just a button in the Server & Data column).

### Decision: Merge Update + About

About content (credits, donation) goes in the left column below update toggles. Release notes keep the full right column — no shrinkage. Tab renamed to "About."

```
Left column                    Right column
┌─────────────────────┐       ┌──────────────────────────────┐
│ Decenza v1.6.1      │       │ Software Updates (if avail.) │
│ Build 3194          │       │ ● You're up to date          │
│                     │       │ [Check Now]                  │
│ Auto-check  [toggle]│       │                              │
│ Beta        [toggle]│       │ Release Notes — v1.6.1       │
│ (hidden on iOS)     │       │ ┌────────────────────────────┐│
│                     │       │ │ ## Changes                 ││
│   ── divider ──     │       │ │ ...                        ││
│                     │       │ │ (scrollable, full height)  ││
│ Built by Michael    │       │ │                            ││
│ Holm (Kulitorum)... │       │ │                            ││
│                     │       │ └────────────────────────────┘│
│ Donations welcome   │       │                              │
│ but never expected. │       │                              │
│ [Donate via PayPal] │       │                              │
│ PayPal QR code      │       │                              │
│                     │       │                              │
│ Credits text        │       │                              │
└─────────────────────┘       └──────────────────────────────┘
```

### Decision: Merge Language + Accessibility → Language & Access

Both are set-once, low-traffic tabs. Neither is large enough to justify prime tab-bar real estate. Combined tab name "Language & Access" preserves discoverability for both concerns. With settings search, users typing "TalkBack" or "screen reader" find it regardless.

**Layout:** Language selection on one side, accessibility toggles on the other. Language content is the primary section (more users visit it), accessibility below or in the right column.

## Tab Ordering

Tabs are grouped by user mental model into four clusters, most-used first within each:

| Position | Tab | Cluster | Rationale |
|---|---|---|---|
| 0 | Connections | Setup | First thing every user needs |
| 1 | Machine | Setup | Primary settings after connecting |
| 2 | Calibration | Setup | Fine-tuning after basics work |
| 3 | History & Data | Setup | Set-once data configuration |
| 4 | Themes | Customize | Most popular customization |
| 5 | Layout | Customize | Related to Themes |
| 6 | Screensaver | Customize | Related to Layout |
| 7 | Visualizer | Services | External integration |
| 8 | AI | Services | External integration |
| 9 | MQTT | Services | External integration |
| 10 | Language & Access | System | Set once, rarely revisited |
| 11 | About | System | Least visited, natural end |
| 12 | Debug | (debug only) | Always last when visible |

Key changes from current order:
- **History & Data moves up** (from positions 9-10 to 3) — it's in the Setup cluster, configured early then forgotten
- **Accessibility moves down and merges** — important but set-once; current position 5 is prime real estate wasted on a tab most users visit once
- **Screensaver moves next to Layout** — both are "how my idle screen looks"
- **Services cluster together** (Visualizer, AI, MQTT) instead of being scattered

## Within-Tab Card Ordering

### Machine Tab (~12 cards, 3 columns)

Grouped by column theme, most-used at top of each:

| Left: Power & Schedule | Middle: App Behavior | Right: Water & Features |
|---|---|---|
| Auto-Sleep | Theme Mode | Water Level Status |
| Auto-wake Schedule | Shot Review Timer | Water Refill Threshold |
| Battery/Charging | Screen Zoom | Refill Kit |
| | Simulation Mode | Shot Map |
| | Launcher Mode (Android) | |

### Calibration Tab (5 cards)

Ordered by adjustment frequency:

1. **Flow Calibration** — most frequently adjusted, has auto-cal toggle
2. **Weight Stop Timing** (was "Stop-at-Weight Calibration") — SAW lag, tuned as users dial in
3. **Heater Calibration** — set once, rarely revisited
4. **Virtual Scale** — fallback device config, set and forget
5. **Prefer Weight over Volume** (was "Ignore Stop-at-Volume with Scale") — behavioral override, single toggle

### History & Data Tab (3 columns)

See merged layout table above. Columns ordered left-to-right by visit frequency.

### About Tab (merged)

Left column: Version info → update toggles → divider → About content (story, donate, credits). Right column: Update status + release notes (full height, scrollable, unchanged).

### Language & Access Tab

Language selection as the primary section (visited by more users), accessibility settings as secondary section. Both small enough to fit without scrolling.

## Setting Card Renames

Five setting cards are renamed for clarity. The underlying Q_PROPERTY names and Settings keys are unchanged — only the user-facing labels and translation keys change.

| Current Name | New Name | Problem | Fix |
|---|---|---|---|
| Per-Screen Scale | **Screen Zoom** | "Scale" means weighing device throughout the app | Use unambiguous "Zoom" |
| Close Shot Review Screen | **Shot Review Timer** | Reads like an action button, not a setting | Describe what it is (a timer) |
| Unlock GUI | **Simulation Mode** | Cryptic — "unlock" what? | Match the card title the toggle lives inside |
| Ignore Stop-at-Volume with Scale | **Prefer Weight over Volume** | Long, requires understanding SAV + scale interaction | Describe the behavior from user perspective |
| Stop-at-Weight Calibration | **Weight Stop Timing** | "Calibration" implies adjusting accuracy; it's actually lag timing | Say what you're actually tuning |

## Risks / Trade-offs

- **Muscle memory disruption**: Existing users know where settings are. Mitigation: pre-1.0 beta app with ~30 active users; better to fix now than after wider adoption. Settings search provides an escape hatch for users who can't find something.
- **Search index maintenance**: New settings require a search index entry. Mitigation: one line per setting, simple format, easy to forget but low consequence (setting still exists in its tab, just not searchable until indexed).
- **Tab count still 12**: Not dramatically fewer than 14. Mitigation: the real win is search + coherent naming, not count reduction. The two merges (History+Data, Language+Access) eliminate genuine problems (duplicate toggle, wasted tab-bar space).

## Open Questions

None — all decisions finalized through review.
