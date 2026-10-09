# layout-widget-instance-config Specification

## Purpose
Defines how individual layout widget instances hold their own persistent, per-instance configuration — opened via an explicit visible affordance (plus long-press) in the in-app and web editors — covering the Scale Weight data mode, readout display mode, Sleep widget's quit/icon options, the Shot Plan display-item list and its sentence/stacked/steam-plan toggles, and the visible has-options indicator that marks which widget types are configurable.

## Requirements

### Requirement: Per-instance widget configuration in the editors
The layout editors SHALL allow a configurable widget instance to be opened for editing and SHALL persist per-instance settings using the existing item-property mechanism (`setItemProperty` / `getItemProperties` and the `/api/layout/item` endpoints). Two instances of the same widget type SHALL be able to hold different settings.

#### Scenario: Visible affordance opens the instance editor in-app

- **WHEN** a user activates the visible options affordance on a configurable widget chip in the in-app layout editor
- **THEN** an editor popup SHALL open exposing that widget's configurable properties for that instance

#### Scenario: Long-press still opens the instance editor in-app

- **WHEN** a user long-presses a configurable widget instance in the in-app layout editor
- **THEN** the same instance editor popup SHALL open

#### Scenario: Web gear button opens the instance editor

- **WHEN** a user clicks the gear on a configurable widget chip in the web layout editor
- **THEN** the instance editor SHALL open loaded with that instance's current properties
- **AND** this SHALL hold whether or not the chip was already selected

#### Scenario: Repeated chip clicks never dead-end the options flow

- **WHEN** a user opens a widget's options, closes the editor, and activates the options affordance again
- **THEN** the instance editor SHALL reopen — no alternating click SHALL be a no-op

#### Scenario: Editor closes with its widget

- **WHEN** the widget whose instance editor is open is deselected or removed
- **THEN** the instance editor SHALL close

#### Scenario: Settings persist per instance

- **WHEN** a user changes a configurable property on one instance and saves
- **THEN** that instance SHALL retain its setting across app restarts
- **AND** other instances of the same widget type SHALL be unaffected

### Requirement: The instance editor has an explicit affordance
The instance editor SHALL be openable through an explicit, visible affordance, not a hidden gesture alone. In the in-app editor the affordance SHALL be visible on the widget, and long-press SHALL be retained as an additional shortcut. In the web editor the has-options gear SHALL itself be an activatable control that opens the instance editor. Activating the affordance SHALL open the editor regardless of the chip's selection state.

#### Scenario: Options affordance works on a repeated click
- **WHEN** the user clicks a chip's options affordance repeatedly
- **THEN** each click opens the instance editor and selection toggling does not swallow or invert the open action

### Requirement: An open instance editor closes with its widget
An open instance editor SHALL close when its widget is deselected or removed, so the editor is never open for a widget that is no longer selected.

#### Scenario: Deselecting the widget closes its editor
- **WHEN** the widget of an open instance editor is deselected or removed
- **THEN** the editor closes

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
The `machineStatus`, `temperature`, `steamTemperature`, `waterLevel`, `clock`, `scaleWeight`, `batteryLevel`, `scaleBattery`, `doseWeight` and `milkWeight` widgets SHALL each support a per-instance `displayMode` of `text` (default) or `icon`, as declared in the layout readout capability schema. In `icon` mode the widget SHALL render a tinted icon ahead of its value. In `text` mode it SHALL render exactly as it does today.

#### Scenario: Default is today's rendering

- **WHEN** one of these widgets has no `displayMode` set (existing layouts)
- **THEN** it SHALL render identically to today

#### Scenario: Icon mode renders an icon

- **WHEN** a widget's `displayMode` is `icon`
- **THEN** it SHALL render its icon ahead of the value, tinted to the surrounding text/contrast color

#### Scenario: Newly-optioned readouts honor the mode

- **WHEN** a `doseWeight` or `milkWeight` instance has `displayMode` set to `icon`
- **THEN** it SHALL render its icon ahead of the value like the other readouts

#### Scenario: Works in any zone

- **WHEN** an icon-mode widget is placed in any zone (status bar, a bottom bar, the lower-mid bar, etc.)
- **THEN** it SHALL render its icon form there — the mode is not gated by a zone style

#### Scenario: Two instances differ

- **WHEN** the same widget type is placed twice with different `displayMode` values
- **THEN** each instance SHALL render according to its own mode

#### Scenario: Edited via the unified readout editor

- **WHEN** a user opens one of these widgets' options in the in-app or web layout editor
- **THEN** the unified readout options editor SHALL expose the `displayMode` choice for that instance (and for `scaleWeight`, its existing `dataMode` and ratio-suffix options as well)
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: Disconnected state

- **WHEN** the widget's device is disconnected
- **THEN** it SHALL show its existing placeholder ("—"/"--") in either mode, without errors

### Requirement: Icon-default readouts keep their default and profileName has no mode
Widgets whose default rendering already includes an icon (`batteryLevel`, `scaleBattery`) SHALL treat that rendering as the default and offer the other form via the mode. `profileName` SHALL NOT expose `displayMode`, because the capability schema declares it color-only.

#### Scenario: profileName offers no display mode
- **WHEN** a `profileName` widget is configured
- **THEN** no `displayMode` option is offered for it

### Requirement: The display mode is stored per instance and applies in any zone
The mode SHALL be read from the item's stored properties (`modelData`), SHALL persist per instance, and SHALL apply in any zone the widget is placed in.

#### Scenario: Mode is read from stored properties
- **WHEN** a readout widget with `displayMode` set to `icon` is placed in any zone
- **THEN** it renders in icon mode from its stored item properties

### Requirement: Configurable quit option for the sleep widget

The `sleep` widget SHALL have per-instance `allowQuit` (long-press quits) and `confirmQuit` (that long-press asks first, `quit-confirmation`) options, editable from either layout editor and persisted per instance. The defaults SHALL preserve current behaviour: quit enabled, no confirmation. `confirmQuit` SHALL have no effect while `allowQuit` is off.

#### Scenario: Default keeps quit available

- **WHEN** a `sleep` widget has no `allowQuit` or `confirmQuit` set (existing layouts)
- **THEN** long-press-to-quit SHALL behave exactly as it does today

#### Scenario: Removing the quit option

- **WHEN** a user disables `allowQuit` on a Sleep instance in either editor
- **THEN** that Sleep instance SHALL sleep on tap but SHALL NOT quit on long-press
- **AND** the "long-press to quit" accessibility hint SHALL be dropped for that instance
- **AND** the setting SHALL persist for that instance only

#### Scenario: Asking before quitting

- **WHEN** a user enables `confirmQuit` on a Sleep instance whose `allowQuit` is on
- **THEN** that instance's long-press SHALL open the quit confirmation instead of quitting at once
- **AND** the setting SHALL persist for that instance only

#### Scenario: Long-press opens the sleep editor in-app

- **WHEN** a user long-presses a `sleep` widget in the in-app layout editor
- **THEN** an editor SHALL open exposing the quit-option toggles for that instance

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
Which widget types have configurable options, and which option keys each type supports, SHALL be defined by the layout readout capability schema (`layout-readout-capability-schema`) and consumed by every site that needs them, in both editors. Adding a configurable widget type or option key SHALL require updating only the schema.

#### Scenario: Indicator and open behavior agree

- **WHEN** the editors render and a widget type is configurable per the capability schema
- **THEN** that type SHALL both display the has-options indicator AND respond to the open-options affordance/gesture

#### Scenario: Non-configurable type is inert everywhere

- **WHEN** a widget type is not configurable per the capability schema
- **THEN** it SHALL neither show the indicator nor open an instance editor in either editor

#### Scenario: Editor sections follow the schema

- **WHEN** the in-app or web editor opens a configurable readout instance
- **THEN** the option controls presented SHALL be exactly those the schema declares for that type

#### Scenario: Action widgets are configurable

- **WHEN** the editors render a Beans, Steam or History widget
- **THEN** it displays the has-options indicator and opens an instance editor offering its gesture overrides

#### Scenario: Slot rule is derived, not hard-coded

- **WHEN** a one-slot widget's reserved gesture is presented in either editor
- **THEN** both surfaces derive the rule and the destination name from the schema, and neither carries its own list of which widgets reserve which gesture

### Requirement: Action widgets are configurable with schema-derived slot rules
The built-in ACTION widgets (Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles, History, Favorites and Settings) SHALL be configurable types under this rule, carrying gesture-override option keys (`layout-widget-gesture-overrides`). Their per-type slot rule, meaning how many gestures may be overridden and which destination the reserved gesture opens, SHALL be declared in the same schema and not hard-coded in either editor.

#### Scenario: Reserved slot presentation follows the schema
- **WHEN** an action widget's reserved slot is shown in either editor
- **THEN** the slot is derived from the schema, so the two editors cannot drift

### Requirement: Shot plan display option set
The `shotPlan` widget SHALL expose, in both editors, an **ordered display-item list** (`shotPlanItems`): a JSON array of keys drawn from `doseYield`, `profile`, `temperature`, `roaster`, `coffee`, `grind` and `roastDate`. The list SHALL define both which items are shown and their order. Profile and Temperature SHALL be independent items.

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

### Requirement: Shot plan defaults reproduce the previous rendering
The default item list SHALL be `["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]`, with Roast date off by default. With Sentence style on, that default SHALL reproduce the widget's previous default rendering.

#### Scenario: Defaults reproduce the previous rendering
- **WHEN** a shot plan widget has no stored item list and no legacy overrides
- **THEN** it renders as before the option set existed

### Requirement: Sentence and stacked shot plan options
A **Sentence style** boolean (`shotPlanSentence`, default ON) SHALL select sentence or fragment rendering. A **Stacked details** boolean (`shotPlanStacked`, default OFF) SHALL, in sentence mode, move the detail tail onto its own line or lines, and the in-app toggle SHALL be disabled while Sentence style is OFF. A **Steam plan** boolean (`shotPlanShowSteamPlan`, default ON) SHALL gate the page-aware steam swap, unchanged.

#### Scenario: Stacked details is disabled for fragments
- **WHEN** Sentence style is OFF
- **THEN** the in-app Stacked details toggle is disabled

### Requirement: In-app shot plan editor
The Shot Plan settings popup SHALL present the item list as a "Shown" row of chips, reorderable by drag with an explicit remove affordance per chip, and an "Available" row of unused items, activated to add. Reordering SHALL have an accessible fallback of per-chip move controls when a screen reader is active. Edits SHALL apply only on Save, and Cancel SHALL discard them. The popup SHALL show a live preview of the plan as configured.

#### Scenario: Cancel discards chip edits
- **WHEN** the user reorders or removes chips and then taps Cancel
- **THEN** the stored item list is unchanged

### Requirement: Web shot plan editor
The web layout editor SHALL present the same item list with add, remove and reorder controls, plus the same toggles, reading and writing the same keys.

#### Scenario: Web editor round-trips the item list
- **WHEN** an item list is saved in the web editor and opened in the in-app editor
- **THEN** the same items appear in the same order

### Requirement: Legacy booleans derive the item list
When `shotPlanItems` is absent, both editors and the widget SHALL derive the list from the legacy booleans in canonical order, honoring each legacy default: `shotPlanShowDoseYield` maps to `doseYield`, `shotPlanShowProfile` to `profile` and `temperature`, `shotPlanShowRoaster` to `roaster`, `shotPlanShowCoffee` to `coffee`, `shotPlanShowGrind` to `grind`, and `shotPlanShowRoastDate` (default OFF) to `roastDate`.

#### Scenario: Legacy profile boolean expands to two chips
- **WHEN** `shotPlanItems` is absent and `shotPlanShowProfile` is ON
- **THEN** the derived list contains both `profile` and `temperature`

### Requirement: Legacy keys are read-only and editors round-trip
Derivation SHALL apply only when `shotPlanItems` is absent. A stored empty list is a valid show-nothing configuration and SHALL be honored. The legacy display booleans SHALL be read but never written by the new editors. Both editors and the C++ configurable-type gate SHALL accept the same keys, so a configuration set in one editor round-trips through the other.

#### Scenario: An emptied item list stays empty
- **WHEN** the user removes every item from the shot plan list
- **THEN** the stored list is empty and no legacy boolean is used to repopulate it

### Requirement: Web readout options match the in-app editor's presentation
The web layout editor SHALL present readout widget options in a dedicated editor with the same structure, wording and explanatory hints as the in-app readout options editor: labelled sections per option key, and the same choice labels. Its sections SHALL continue to derive from the shared readout capability schema. Unlabelled inline chip controls SHALL NOT be the only way to edit readout options.

#### Scenario: Web readout editor shows labeled sections

- **WHEN** a user opens the options editor for a readout widget (e.g. Scale Weight) on the web
- **THEN** each option the type supports SHALL appear under a labeled section matching the in-app editor
- **AND** choices SHALL use the same descriptive labels as the in-app editor

#### Scenario: Sections still derive from the capability schema

- **WHEN** an option key is added to a type's entry in the readout capability schema
- **THEN** the web readout options editor SHALL show the corresponding section without a separate web-side type list being edited
