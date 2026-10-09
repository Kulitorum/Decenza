## ADDED Requirements

### Requirement: A running operation owns the screen

While an operation is active (espresso, steam, hot water, flush, descale, clean, firmware flash), the app SHALL NOT open a modal dialog over the operation page for a notice unrelated to that operation. Such notices SHALL be queued and shown when the machine returns to idle, the same way they are queued while the screensaver is active. Confirmations the operation itself raises (cancelling it, or the firmware-flash exit warning) are exempt: deferring them would defeat their purpose.

#### Scenario: Update found mid-shot
- **WHEN** the update checker requests its prompt while a shot is pouring
- **THEN** no dialog opens over the Espresso page AND the prompt opens after the machine returns to idle

#### Scenario: Quitting during a firmware flash
- **WHEN** the user tries to close the app while firmware is flashing
- **THEN** the firmware-flash exit warning opens immediately

#### Scenario: Scale drops out while steaming
- **WHEN** the scale disconnects during steaming
- **THEN** the Steam page stays fully visible and operable AND the notice is shown non-modally or after steaming ends

### Requirement: The start action is visible, and a blocked start says why

An everyday control that starts an operation SHALL show that it does so before it is pressed. When the app cannot start the operation it SHALL tell the user why, visibly, in every accessibility mode.

#### Scenario: Selected recipe pill
- **WHEN** a recipe or profile pill is selected on the idle screen and a further tap would start a shot
- **THEN** the pill shows a start affordance (for example a ▶ label)

#### Scenario: Machine not ready
- **WHEN** the user taps a start control while the machine is disconnected, out of water, or controlled from the group head
- **THEN** a visible message names the reason and, where one exists, the next step (for example "Start from the group head")

#### Scenario: Machine still heating
- **WHEN** the user taps a start control while the machine is heating
- **THEN** the start is accepted and queued as today (`MachineState::isReady()` includes Heating on purpose), and the screen says it will start when heated

### Requirement: Every problem message has a way forward

A dialog or banner that reports a problem SHALL offer at least one action that addresses it, and any place it names (a settings tab, a button) SHALL exist under that name.

#### Scenario: Saved scale missing at shot start
- **WHEN** a shot cannot use the saved scale
- **THEN** the message offers reconnecting, opening the Connections settings, and continuing this once without the scale

#### Scenario: Translated error
- **WHEN** the app language is not English and a Bluetooth or Location permission error occurs
- **THEN** the dialog shows the same recovery buttons it shows in English

### Requirement: Changes the user did not ask for are announced

When an action implicitly changes other state the user relies on, the app SHALL say so at the moment it happens and SHALL offer undo where the change is reversible.

#### Scenario: Recipe deactivated by a profile change
- **WHEN** the active recipe is deactivated because the user changed the profile, bag, equipment or pitcher
- **THEN** a toast names the recipe that was deactivated and offers to restore it

### Requirement: Destructive actions are deliberate

An action that deletes data or resets learned or calibrated values SHALL either ask for confirmation or offer undo, and SHALL NOT be reachable only through a hidden gesture.

#### Scenario: Resetting a calibration
- **WHEN** the user taps a reset for flow calibration, stop-at-weight learning or steam health
- **THEN** the value is not lost without a confirmation or an undo window

#### Scenario: Holding the Sleep button
- **WHEN** the user holds the Sleep widget in the default layout
- **THEN** the app does not quit

#### Scenario: Developer shortcuts
- **WHEN** a gesture writes simulated data (such as a fake shot) to history
- **THEN** it is available only in a debug or simulation build

### Requirement: One word, one meaning

A user-visible term SHALL name exactly one concept across the app, its web pages and its manual.

#### Scenario: Favorites
- **WHEN** the app uses the word "Favorites" or a star icon
- **THEN** it refers to starred profiles only, and Auto-Favorites uses a different name and icon

#### Scenario: Recipe
- **WHEN** the app uses the word "recipe"
- **THEN** it refers to a drink recipe, never to a profile or the profile editor

### Requirement: Everyday controls are easy to hit

Interactive controls on everyday surfaces (idle screen, operation pages, post-shot review, value steppers, list rows) SHALL have a hit area of at least `Theme.touchTargetMin`, and that minimum SHALL hold after window scaling.

#### Scenario: Phone in landscape
- **WHEN** the window is smaller than the 960×600 reference size
- **THEN** the hit areas of steppers, rating buttons and list-row actions do not shrink below the minimum

### Requirement: Readable in every shipped theme, without relying on colour alone

Text and control boundaries SHALL meet WCAG 2 contrast (4.5:1 for body text, 3:1 for large text and UI component boundaries) in every built-in theme, including the shot graphs. State SHALL NOT be conveyed by colour alone.

#### Scenario: Light theme graph
- **WHEN** a shot graph is drawn in the default light theme
- **THEN** curves, grid, axes and phase labels meet the contrast above against the plot background

#### Scenario: Quality issues in shot history
- **WHEN** a shot has more than one quality issue
- **THEN** the history row tells them apart by text or icon, not by dot colour

### Requirement: The package carries only what the app uses

A release package SHALL NOT contain libraries, plugins or resources that no code path in that build uses, and CI SHALL detect an excluded library returning.

#### Scenario: Unused Quick Controls styles
- **WHEN** the Android release APK is built
- **THEN** it contains the Material and Basic style libraries only

#### Scenario: Release build
- **WHEN** a release build is packaged
- **THEN** it contains no `qmltooling` debug plugins and no unreferenced images

### Requirement: Optional features cost nothing until switched on

A feature that is off by default SHALL NOT load its runtime, bind system services or start background work while it is off.

#### Scenario: Remote MCP connector off
- **WHEN** the remote connector is disabled
- **THEN** the Tailscale runtime is not loaded into the process

#### Scenario: Accessibility off
- **WHEN** accessibility mode is off
- **THEN** no text-to-speech engine is bound

### Requirement: The main thread stays free while the machine is running

While an operation is active, the main thread SHALL NOT perform synchronous disk writes for logging, whole-object-tree walks, or per-sample rebuilding of data that did not change.

#### Scenario: Logging during a shot
- **WHEN** a log line is written during a shot
- **THEN** the file write happens on a background thread

#### Scenario: Goal curves during a shot
- **WHEN** a new sample arrives and the goal values and frame have not changed
- **THEN** the goal-curve point lists are not rebuilt

### Requirement: A tap reaches the screen the user saw

A tap SHALL only be delivered to a control that was on screen when the user tapped.

#### Scenario: Tap during a page build
- **WHEN** the user taps while a page is still being built
- **THEN** the tap is not delivered to a control on the newly built page
