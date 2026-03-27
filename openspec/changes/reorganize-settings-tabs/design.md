## Context

The Settings page has 13+ tabs that grew organically. The Preferences tab became a dumping ground for anything that didn't obviously belong elsewhere, accumulating 12+ cards spanning hardware, display, scheduling, and calibration. Server controls are duplicated across History and Data tabs. Theme mode selectors are separated from the theme editor. Users can't find settings intuitively.

This is a UI-only reorganization. No settings are added or removed — only moved between tabs. The C++ Settings class and all Q_PROPERTY bindings remain unchanged.

## Goals / Non-Goals

- **Goals:**
  - Every setting has exactly one home (eliminate duplication)
  - Each tab has a clear mental model (a user can predict which tab holds a given setting)
  - No tab requires scrolling past 3 screens of content (break up oversized tabs)
  - Tab names are self-explanatory without domain knowledge

- **Non-Goals:**
  - Adding new settings or removing existing ones
  - Changing the Settings C++ class or property names
  - Redesigning individual setting cards/widgets
  - Changing navigation patterns (still tab-based)
  - Mobile-specific layout changes (responsive layout is a separate concern)

## Decisions

### Decision: Move settings by mental model, not by technical category

Users think in terms of "what am I configuring?" not "what C++ class does this touch?" Group by user intent:
- **Connections** = "How does the app talk to devices?"
- **Machine** = "How does the espresso machine behave when idle?"
- **Display** = "How does the app look on screen?"
- **Espresso** = "How does the shot workflow behave?"
- **Sharing & Data** = "Where does my data go?"

### Decision: Decompose Preferences into Machine + Display + Espresso

The current Preferences tab mixes three distinct mental models:

| Setting | Mental model | New home |
|---------|-------------|----------|
| Theme mode (follow system, dark/light) | Display | Display or Themes |
| Auto-Sleep | Machine behavior | Machine |
| Post-Shot Review Close | Shot workflow | Espresso |
| Refill Kit | Machine hardware | Machine |
| Offline Mode | Machine connection | Machine |
| Launcher Mode | Device display | Display or Machine |
| Shot Map | Shot metadata | Espresso or Sharing |
| Per-Screen Scale | Display zoom | Display |
| Battery Charging | Machine hardware | Machine |
| Flow Calibration | Machine calibration | Machine |
| Ignore SAV with Scale | Shot workflow | Espresso |
| Water Level | Machine hardware | Machine |
| Water Refill Threshold | Machine hardware | Machine |
| Headless Machine | Machine hardware | Machine |
| Extraction View | Display | Display |
| Steam Heater | Machine hardware | Machine |
| Virtual Scale | Device connection | Connections |
| Auto-Wake Timer | Machine scheduling | Machine |
| SAW Calibration | Machine calibration | Machine |
| Heater Calibration | Machine calibration | Machine |

### Decision: Merge History + Data, eliminate duplicate server toggle

Current state: `shotServerEnabled` toggle exists in both History tab ("Remote Access") and Data tab ("Share Data"). The Data tab additionally has security (TOTP) and the History tab has shot import. These are two halves of the same concern.

Merged tab layout:
- Left column: Shot History link + DE1 Import + Server enable + Security
- Middle column: Backup + Restore
- Right column: Device Migration + Factory Reset

### Decision: Move REST API docs from MQTT to the server/sharing section

The REST API endpoints are part of the HTTP server, not MQTT. They should live next to the server enable toggle.

### Decision: Move theme mode selectors to Display tab (not Themes)

Theme mode selection (follow system, pick dark/light theme) is about display preferences. The Themes tab is a specialized color editor. Users who just want to switch between light and dark mode shouldn't need to find the color editor.

Alternative considered: Move to Themes tab. Rejected because the Themes tab is a creative tool (designing custom color schemes) — mode switching is a simpler display preference.

## Risks / Trade-offs

- **Muscle memory disruption**: Existing users know where settings are. Mitigation: this is a pre-1.0 beta app with ~30 active users; better to fix now than after wider adoption.
- **Tab count change**: Moving from 13 to 9 tabs changes the tab bar scroll behavior. Mitigation: fewer tabs means less scrolling to find the right one.
- **Large diff**: Many QML files are touched. Mitigation: the changes are purely structural (cut-paste of existing card components between files). No logic changes.
- **Merge conflicts with in-flight work**: Other proposals may add settings. Mitigation: this is a reorganization of containers, not content — new settings added before this lands just move to the right tab.

## Migration Plan

1. Create new tab files (Machine, Display)
2. Move card components from Preferences to new tabs (cut-paste, preserve property bindings)
3. Merge History + Data into one tab, remove duplicate server toggle
4. Move Virtual Scale to Connections
5. Move theme mode to Display
6. Move REST API from MQTT to Sharing/Data
7. Create Services tab with summary cards + setup dialogs (AI, MCP, Visualizer, Home Assistant)
8. Merge Update + About (About content in left column of Update, below toggles)
9. Delete emptied tabs (Preferences, About, AI, Visualizer, Home Automation)
10. Update SettingsPage.qml tab bar
11. Test all settings still bind correctly

Each step is independently testable — settings bindings don't change, only which .qml file contains the UI.

## Internal Reorganization

Tabs that keep their content still benefit from internal restructuring.

### Services Tab — Four summary cards with setup dialogs

**Current layout:** AI, Visualizer, and Home Assistant (MQTT) are three separate tabs. All are "connect to an external service" workflows configured once and rarely revisited. AI additionally mixes two unrelated concerns (AI provider for shot analysis vs MCP server for Claude Desktop).

**Proposed layout:** A single Services tab with four status cards. Each card shows connection status and a [Configure...] button that opens a focused setup dialog.

```
┌─ Visualizer ──────────────┐  ┌─ AI Assistant ─────────────┐
│ ● Connected               │  │ ● OpenRouter (GPT-4o)      │
│ user@visualizer.coffee    │  │                            │
│ [Configure...]            │  │ [Configure...]             │
└───────────────────────────┘  └────────────────────────────┘

┌─ MCP Server ──────────────┐  ┌─ Home Assistant ───────────┐
│ ● Enabled (read-only)     │  │ ○ Not configured           │
│                           │  │                            │
│ [Configure...]            │  │ [Set Up...]                │
└───────────────────────────┘  └────────────────────────────┘
```

**Dialog contents (~4-6 fields each):**

| Dialog | Fields |
|--------|--------|
| **Visualizer** | Account login, upload preferences |
| **AI Assistant** | Provider picker, API key, provider-specific fields (model, endpoint), cost info, Test Connection + Continue Chat |
| **MCP Server** | Enable toggle, access level, confirmation settings, Discuss Shot (app picker + custom URL), MCP status |
| **Home Assistant** | MQTT broker config, auto-discovery toggle, Publish Discovery |

**Why:** These services share a common pattern: configure credentials/settings once, then forget about them. Giving each its own tab wastes tab bar space for content users visit maybe twice. Summary cards let users see at a glance which services are connected. Separating AI from MCP into distinct cards/dialogs keeps each dialog focused — AI provider setup is for shot analysis users, MCP is for Claude Desktop power users.

### Connections Tab — Group by device status, not device type

**Current layout:** Two columns: Machine (left), Scale/Refractometer (right). The right column mixes connected device status, discovery, known devices, inline refractometer settings, and the scan log into one long scroll. Virtual Scale (moving here from Preferences) would add to the clutter.

**Proposed layout:** Keep two columns but restructure the right column into clear sections:

**Left column (Machine) — unchanged:**
- USB/BLE status, scan button, firmware, discovered devices, connection log

**Right column (Scales & Refractometer) — reorganized:**

| Section | Contents |
|---|---|
| **Active Devices** | Connected scale (name + battery + tare), Connected refractometer (name + status + auto-read toggle + Read TDS button), Virtual Scale enable toggle |
| **Known Devices** | Saved scale picker (star icon, dropdown, forget), Saved refractometer (name, type badge, forget) |
| **Find Devices** | Scan button, discovered devices list with type badges, scale connection alerts toggle |
| **Log** | Scrolling scan log, Clear + Share Log buttons |

**Why:** Users have three distinct intents when visiting Connections: "check what's connected" (Active), "switch between saved devices" (Known), "pair something new" (Find). The current layout intermixes all three. Clear section headers with dividers make each intent findable.

### Merged History & Data Tab — Four columns, Device Migration gets full space

**Current state:** History tab has shot history + import (left) and server enable + port (right). Data tab has server enable + security (left), backup (middle), device migration (right, 2086 lines total including 6 dialogs). Server toggle is duplicated.

The Device Migration column is the most complex piece: it has 5 distinct states (idle → searching → devices found → TOTP auth prompt → manifest/import with checkboxes and progress). This content expands significantly during operations and should not be squeezed alongside backup/restore.

**Proposed layout:** Four columns — the fourth (Device Migration) gets `Layout.fillWidth: true` so it has room to breathe during operations:

| Shot History (fixed) | Server & Sharing (fixed) | Backup (fixed) | Device Migration (fill) |
|---|---|---|---|
| "Shot History →" button | Enable Server toggle | Backup Time dropdown | "Import from Another Device" |
| Total shots count | Server URL + Copy | Backup status + location | Description text |
| *divider* | Enable Security toggle | Storage permission (Android) | "Search for Devices" + spinner |
| Import from DE1 App | TOTP setup / reset | "Backup Now" button | Device list / single card / dropdown |
| Overwrite toggle | *divider* | *divider* | TOTP auth prompt (when needed) |
| Import buttons (DE1, ZIP, Folder) | REST API endpoints | "Restore from Backup" | Manifest display (when connected) |
| Import progress | (from MQTT tab) | Restore button | Import checkboxes + progress |
| | *divider* | | Manual IP entry |
| | Data summary (shots/profiles) | | |
| | *divider* | | |
| | Factory Reset (bottom) | | |

**Why:** Each column answers one question:
- Col 1: "How many shots do I have and how do I import from de1app?"
- Col 2: "How do I share my data over the network?"
- Col 3: "How do I back up and restore?"
- Col 4: "How do I move from another Decenza device?"

Factory Reset stays in the Server column (bottom, after a divider and spacer) — it's a destructive action related to "your data" and should be far from casual reach. Device Migration gets room to show the multi-state workflow without cramming.

**Alternative considered:** Keep Data and History as separate tabs but just fix the duplicate server toggle. Rejected because the mental models overlap too much ("where is my data?") and users still have to check two tabs.

**Alternative considered:** Three columns with Backup + Migration merged. Rejected because the migration column alone has 5 states with auth prompts, manifests, and import progress — combining it with backup/restore would recreate the "too much in one column" problem.

### Decision: Device Migration as a dialog flow (preferred) vs inline column

The migration workflow has 5 sequential states:
1. **Idle** — "Import from Another Device" button
2. **Searching** — spinner + "Searching..."
3. **Devices found** — single device card or multi-device dropdown + manual IP entry
4. **Authenticating** — TOTP code input (when remote device has security enabled)
5. **Connected** — manifest card with counts, import checkboxes, Import All/Cancel buttons, progress bar

**Option A — Dialog flow (recommended):**

The merged tab shows three columns (Shot History, Server & Sharing, Backup). Below the Backup column or spanning the full width at the bottom, a single card:

```
┌─────────────────────────────────────────────┐
│  Import from Another Device                 │
│  Connect to another Decenza device on your  │
│  WiFi to import settings, profiles, shots.  │
│                                             │
│  [Import from Another Device...]            │
│                                             │
│  Factory Reset (destructive, at bottom)     │
└─────────────────────────────────────────────┘
```

Clicking the button opens a modal `Dialog` that walks through the 5 states:

```
Step 1: Search + device selection
  ┌─────────────────────────────────────┐
  │  Import from Another Device         │
  │                                     │
  │  [Search for Devices]  (spinner)    │
  │                                     │
  │  ┌─ Device Card ──────────────────┐ │
  │  │ Decenza Tablet • 192.168.1.50  │ │
  │  └────────────────────────────────┘ │
  │                                     │
  │  Manual: [192.168.1.x:8888] [Go]   │
  │                                     │
  │              [Cancel]               │
  └─────────────────────────────────────┘

Step 2 (if needed): Authentication
  ┌─────────────────────────────────────┐
  │  Authentication Required            │
  │                                     │
  │  Enter 6-digit code from your       │
  │  authenticator app:                 │
  │                                     │
  │  [ _ _ _ _ _ _ ]  [Verify]         │
  │                                     │
  │        [Cancel]                     │
  └─────────────────────────────────────┘

Step 3: Review + import
  ┌─────────────────────────────────────┐
  │  Connected to: Decenza Tablet       │
  │                                     │
  │  ☑ Profiles (42)                   │
  │  ☑ Shots (1,203)                   │
  │  ☑ Settings                        │
  │  ☑ Media (15)                      │
  │  ☐ AI Conversations (3)            │
  │                                     │
  │  [Import Selected]     [Cancel]     │
  │                                     │
  │  ████████████░░░░  67%  Importing.. │
  └─────────────────────────────────────┘
```

**Advantages:**
- Common case is clean: one button, not an empty column
- Dialog can be sized to the content (wider during manifest, narrower during search)
- Consistent with existing patterns: TOTP setup and ZIP extraction already use modal dialogs
- The multi-step flow naturally maps to a wizard/dialog mental model
- Backup settings remain visible behind the dialog

**Option B — Inline column (current approach, adapted to 4-column layout):**

As described in the four-column layout above. The migration column starts with just the button and description, then grows as the user progresses through states.

**Advantages:**
- No new dialog component needed
- User sees everything at once without mode switches
- Consistent with how the tab currently works

**Disadvantage:**
- The column is mostly empty (just a button + description text) 99% of the time, wasting horizontal space in the common case
- When active, the column dominates attention and the other three columns become irrelevant context

### Merged About Tab — About content in Update's left column

**Current layout:** Update tab has two columns (version + toggles on left, release notes on right), conditionally hidden on iOS. About tab is a separate full-width page with app name, version, credits, donate button, PayPal QR code.

**Proposed layout:** Merge About content into the bottom of Update's left column, separated by a divider. Release notes keep the full right column. Tab renamed to "About" and always visible. Auto-update controls (check now, beta toggle, status) are conditionally shown via `MainController.updateChecker.canCheckForUpdates` — hidden on iOS where Apple handles updates. Release notes are always shown regardless of platform.

Tapping anywhere in the donation area opens a dialog with the full-size PayPal QR code + an "Open PayPal" link, keeping the left column compact.

```
Left column                    Right column
┌─────────────────────┐       ┌──────────────────────────────┐
│ Decenza v1.6.1      │       │ Software Updates (if avail.) │
│ Build 3194          │       │ ● You're up to date          │
│                     │       │ [Check Now] [What's New?]    │
│ Auto-check  [toggle]│       │                              │
│ Beta        [toggle]│       │ Release Notes — v1.6.1       │
│ (hidden on iOS)     │       │ ┌────────────────────────────┐│
│                     │       │ │ ## Changes                 ││
│   ── divider ──     │       │ │ ...                        ││
│                     │       │ │ (scrollable)               ││
│ Built by Michael    │       │ │                            ││
│ Holm (Kulitorum)... │       │ │                            ││
│                     │       │ └────────────────────────────┘│
│ Donations welcome   │       │                              │
│ but never expected. │       │                              │
│ (tap to donate)     │       │                              │
└─────────────────────┘       └──────────────────────────────┘

Donation dialog (on tap):
┌─────────────────────────────────┐
│  Donate via PayPal              │
│                                 │
│  ┌───────────────────────────┐  │
│  │                           │  │
│  │        [QR code]          │  │
│  │                           │  │
│  └───────────────────────────┘  │
│                                 │
│  paypal@kulitorum.com           │
│                                 │
│  [Open PayPal]       [Close]    │
└─────────────────────────────────┘
```

**Why:** The About content (credits + donation teaser) fits in the left column's empty space. The QR code — too large for inline display — lives in a tap-to-open dialog where it can be full-size and easily scannable. The dialog provides both scan and link paths. Tab is always visible (not conditional on update checker), so iOS users see version info, release notes, and donation option.

## Open Questions

1. **Shot Map** — Espresso tab or Sharing tab? (See proposal Decision 3)
2. **Launcher Mode** — Display or Machine? (See proposal Decision 5)
3. **Offline/Simulation Mode** — Machine or Advanced? (See proposal Decision 4)
4. **Should the new Machine tab have sub-sections** (e.g., collapsible groups for "Power & Sleep", "Water", "Calibration") or flat cards like current Preferences?
5. **Tab ordering** — What order feels most natural? Current proposal puts Connections first (setup flow), then Machine, Display, Themes...
