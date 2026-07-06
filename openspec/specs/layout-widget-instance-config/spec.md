# layout-widget-instance-config Specification

## Purpose
TBD - created by archiving change composable-brew-bar. Update Purpose after archive.
## Requirements
### Requirement: Per-instance widget configuration in the editors

The layout editors SHALL allow a configurable widget instance to be opened for editing and SHALL persist per-instance settings using the existing item-property mechanism (`setItemProperty` / `getItemProperties` and the `/api/layout/item` endpoints). Two instances of the same widget type SHALL be able to hold different settings.

The instance editor SHALL be openable through an **explicit, visible affordance** — not a hidden gesture alone. In the in-app editor this affordance SHALL be visible on the widget (for example an options control on the chip), and long-press SHALL be retained as an additional shortcut. In the web editor the existing visible open/edit affordance SHALL continue to open the instance editor.

#### Scenario: Visible affordance opens the instance editor in-app

- **WHEN** a user activates the visible options affordance on a configurable widget chip in the in-app layout editor
- **THEN** an editor popup SHALL open exposing that widget's configurable properties for that instance

#### Scenario: Long-press still opens the instance editor in-app

- **WHEN** a user long-presses a configurable widget instance in the in-app layout editor
- **THEN** the same instance editor popup SHALL open

#### Scenario: Web editor opens the instance editor

- **WHEN** a user opens a configurable widget instance in the web layout editor
- **THEN** the editor SHALL load that instance's current properties and present controls to change them

#### Scenario: Settings persist per instance

- **WHEN** a user changes a configurable property on one instance and saves
- **THEN** that instance SHALL retain its setting across app restarts
- **AND** other instances of the same widget type SHALL be unaffected

### Requirement: Configurable data mode for the scale weight widget

The existing `scaleWeight` widget SHALL gain a per-instance `dataMode` property with the values `gross`, `netBeans`, `netMilk`, and `contextAware`. The default SHALL preserve the widget's current behaviour so existing layouts are unchanged.

#### Scenario: Default preserves current behaviour

- **WHEN** a `scaleWeight` widget has no `dataMode` set (existing layouts)
- **THEN** it SHALL behave exactly as it does today

#### Scenario: Gross mode

- **WHEN** `dataMode` is `gross`
- **THEN** the widget SHALL display the raw scale weight

#### Scenario: Net beans mode

- **WHEN** `dataMode` is `netBeans`
- **THEN** the widget SHALL display the scale weight minus the dose-cup tare (`Settings.brew.doseCupTareWeight`), clamped at zero

#### Scenario: Net milk mode

- **WHEN** `dataMode` is `netMilk`
- **THEN** the widget SHALL display the scale weight minus the selected steam pitcher's empty weight, clamped at zero

#### Scenario: Context-aware mode

- **WHEN** `dataMode` is `contextAware`
- **THEN** the widget SHALL display net milk while the machine is in a steam context and net beans otherwise

#### Scenario: Mode is selected in the editor

- **WHEN** a user opens a `scaleWeight` instance in either editor
- **THEN** the editor SHALL present the four data modes for selection
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: No scale connected

- **WHEN** no scale is connected
- **THEN** the widget SHALL display a placeholder ("—") regardless of `dataMode`

### Requirement: Per-instance display mode for readout widgets

The `machineStatus`, `temperature`, `steamTemperature`, and `scaleWeight` widgets SHALL each gain a per-instance `displayMode` property with values `text` (default) and `icon`. In `icon` mode the widget SHALL render a tinted icon ahead of its value, using its existing icon asset; in `text` mode it SHALL render exactly as it does today. The mode SHALL be read from the item's stored properties (`modelData`), persist per instance, and apply in any zone the widget is placed in.

#### Scenario: Default is text mode

- **WHEN** one of these widgets has no `displayMode` set (existing layouts)
- **THEN** it SHALL render value-only, identical to today

#### Scenario: Icon mode renders an icon

- **WHEN** a widget's `displayMode` is `icon`
- **THEN** it SHALL render its icon ahead of the value, tinted to the surrounding text/contrast color

#### Scenario: Works in any zone

- **WHEN** an icon-mode widget is placed in any zone (status bar, a bottom bar, the lower-mid bar, etc.)
- **THEN** it SHALL render its icon form there — the mode is not gated by a zone style

#### Scenario: Two instances differ

- **WHEN** the same widget type is placed twice with different `displayMode` values
- **THEN** each instance SHALL render according to its own mode

#### Scenario: Edited via long-press in the editor

- **WHEN** a user long-presses one of these widgets in the in-app or web layout editor
- **THEN** an editor SHALL expose the `displayMode` choice for that instance (and for `scaleWeight`, its existing `dataMode` as well)
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: Disconnected state

- **WHEN** the widget's device is disconnected
- **THEN** it SHALL show its existing placeholder ("—"/"--") in either mode, without errors

### Requirement: Configurable quit option for the sleep widget

The `sleep` widget SHALL gain a per-instance `allowQuit` option controlling whether long-press-to-quit is available. It SHALL be editable by long-pressing the Sleep widget in the layout editor (in-app and web), persisted via the existing item-property mechanism. The default SHALL preserve current behaviour (quit enabled).

#### Scenario: Default keeps quit available

- **WHEN** a `sleep` widget has no `allowQuit` set (existing layouts)
- **THEN** long-press-to-quit SHALL behave exactly as it does today

#### Scenario: Removing the quit option

- **WHEN** a user disables `allowQuit` on a Sleep instance in either editor
- **THEN** that Sleep instance SHALL sleep on tap but SHALL NOT quit on long-press
- **AND** the "long-press to quit" accessibility hint SHALL be dropped for that instance
- **AND** the setting SHALL persist for that instance only

#### Scenario: Long-press opens the sleep editor in-app

- **WHEN** a user long-presses a `sleep` widget in the in-app layout editor
- **THEN** an editor SHALL open exposing the quit-option toggle for that instance

#### Scenario: Toggling the sleep icon

- **WHEN** a user toggles the Sleep widget's `showIcon` option (default on) in either editor
- **THEN** that Sleep instance SHALL show or hide its icon accordingly (off = label only)
- **AND** the setting SHALL persist for that instance only

### Requirement: Visible indicator that a widget has options

Both editors SHALL show a persistent visual indicator on every widget instance whose type is configurable, so a user can tell which widgets have options without selecting or guessing. The indicator SHALL be present whether or not the widget is selected, and absent on widget types that have no options. In the in-app editor the indicator SHALL be an SVG icon asset (not a Unicode glyph) and SHALL carry an accessible name.

#### Scenario: Configurable widget shows the indicator

- **WHEN** a zone contains a widget whose type has configurable options (for example `custom`, `scaleWeight`, `shotPlan`, `sleep`, `machineStatus`, `temperature`, `steamTemperature`, or a screensaver type)
- **THEN** that widget's chip SHALL display the has-options indicator in both editors

#### Scenario: Non-configurable widget shows no indicator

- **WHEN** a zone contains a widget whose type has no configurable options (for example `flush` or `separator`)
- **THEN** that widget's chip SHALL NOT display the has-options indicator

#### Scenario: Indicator is visible without selection

- **WHEN** a configurable widget is not selected
- **THEN** the has-options indicator SHALL still be visible on its chip

### Requirement: Single source of truth for configurable widget types

The set of widget types that have configurable options SHALL be defined in one place and consumed by every site that needs it — the in-app has-options indicator, the in-app open-options gesture/affordance, and the web editor's indicator and open affordance. Adding a new configurable widget type SHALL require updating only that single definition for the editors' "has options" behavior to stay consistent.

#### Scenario: Indicator and open behavior agree

- **WHEN** the editors render and a widget type is defined as configurable in the single source of truth
- **THEN** that type SHALL both display the has-options indicator AND respond to the open-options affordance/gesture

#### Scenario: Non-configurable type is inert everywhere

- **WHEN** a widget type is not in the single source of truth
- **THEN** it SHALL neither show the indicator nor open an instance editor in either editor

### Requirement: Shot plan display option set

The `shotPlan` widget type SHALL expose, in both editors (in-app popup and web layout editor):

- An **ordered display-item list** (`shotPlanItems`): a JSON array of item keys drawn from `doseYield`, `profile`, `temperature`, `roaster`, `coffee`, `grind`, `roastDate`. The list defines both which items are shown and their order. Profile and Temperature SHALL be independent items.
- A **Sentence style** boolean (`shotPlanSentence`, default ON) selecting sentence vs fragment rendering.
- A **Stacked details** boolean (`shotPlanStacked`, default OFF) that, in sentence mode, moves the detail tail onto its own line(s); the in-app toggle SHALL be disabled while Sentence style is OFF (the option has no meaning for fragments).
- A **Steam plan** boolean (`shotPlanShowSteamPlan`, default ON) gating the page-aware steam swap, unchanged.

The default item list SHALL be `["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]` (Roast date not shown by default, matching the only legacy toggle that defaulted OFF), which — together with Sentence style ON — SHALL reproduce the widget's previous default rendering.

**In-app editor**: the Shot Plan settings popup SHALL present the item list as a "Shown" row of chips (drag to reorder, with an explicit remove affordance per chip) and an "Available" row of the unused items (activate to add), plus the two toggles. Reordering SHALL have an accessible fallback (per-chip move controls) when a screen reader is active. Edits SHALL apply only on Save; Cancel SHALL discard them. The popup SHALL show a live preview of the plan as configured.

**Web editor**: the web layout editor SHALL present the same item list with add, remove, and reorder controls plus the two toggles, reading and writing the same keys.

**Migration**: derivation from legacy booleans SHALL apply only when the `shotPlanItems` property is absent — a stored empty list is a valid "show nothing" configuration and SHALL be honored, not treated as unset. When an instance has no `shotPlanItems` property, both editors and the widget SHALL derive the list from the legacy booleans in canonical order — `shotPlanShowDoseYield` → `doseYield`; `shotPlanShowProfile` → `profile` **and** `temperature`; `shotPlanShowRoaster` → `roaster`; `shotPlanShowCoffee` → `coffee`; `shotPlanShowGrind` → `grind`; `shotPlanShowRoastDate` (default OFF) → `roastDate` — honoring each legacy default. The legacy display booleans SHALL be read but never written by the new editors. Both editors and the C++ configurable-type gate SHALL accept the same keys so a configuration set in one editor round-trips through the other.

#### Scenario: Defaults reproduce the previous rendering

- **WHEN** a fresh `shotPlan` instance is added and never configured
- **THEN** it renders the sentence with dose & yield, profile, temperature, roaster, coffee, and grind — identical content to the pre-change default widget

#### Scenario: Legacy booleans derive the item list

- **WHEN** a saved layout has `shotPlanShowRoaster: false`, `shotPlanShowGrind: false`, and no `shotPlanItems`
- **THEN** the widget shows dose & yield, profile, temperature, and coffee in canonical order, and the editor opens with exactly those chips in the Shown row

#### Scenario: Legacy profile boolean expands to two chips

- **WHEN** a saved layout has `shotPlanShowProfile: true` and no `shotPlanItems`
- **THEN** the derived Shown list contains both the Profile and Temperature chips

#### Scenario: Reorder persists per instance

- **WHEN** a user drags the Grind chip to the front of the Shown row and saves
- **THEN** that instance's `shotPlanItems` starts with `grind`, the widget renders grind first, and the order survives an app restart
- **AND** other `shotPlan` instances are unaffected

#### Scenario: Remove and re-add via the Available row

- **WHEN** a user removes the Coffee chip from the Shown row
- **THEN** Coffee appears in the Available row and the widget no longer shows the coffee name after Save
- **WHEN** the user later activates Coffee in the Available row
- **THEN** it is appended to the Shown row

#### Scenario: An emptied item list stays empty

- **WHEN** a user removes every chip from the Shown row and saves
- **THEN** the instance stores an empty `shotPlanItems` list, the widget renders nothing, and reopening either editor shows an empty Shown row — the legacy booleans do not resurrect the default items

#### Scenario: Stacked details round-trips and gates on Sentence style

- **WHEN** a user enables Stacked details with Sentence style ON and saves
- **THEN** the instance stores `shotPlanStacked: true`, the widget renders the tail below the sentence, and the other editor shows the option enabled
- **WHEN** Sentence style is toggled OFF in the in-app editor
- **THEN** the Stacked details toggle is disabled

#### Scenario: Cancel discards chip edits

- **WHEN** a user reorders and removes chips, then taps Cancel
- **THEN** the instance's stored configuration and rendering are unchanged

#### Scenario: Options round-trip between editors

- **WHEN** the item order is changed and Sentence style turned off in the web editor
- **THEN** the in-app editor shows the same chip order and toggle state, and the widget renders fragments in that order

#### Scenario: New editors never write legacy display keys

- **WHEN** either editor saves a shot-plan configuration
- **THEN** only `shotPlanItems`, `shotPlanSentence`, and `shotPlanShowSteamPlan` are written; the `shotPlanShow*` display booleans are not modified

#### Scenario: Accessible reorder fallback

- **WHEN** a screen reader is active and the Shot Plan settings popup is open
- **THEN** each Shown chip exposes move controls that reorder it without drag gestures

