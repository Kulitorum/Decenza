# layout-widget-gesture-overrides Specification

## Purpose
Lets each built-in action widget (Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles, History, Favorites and Settings) carry a per-instance long-press or double-click action drawn from the Custom-widget catalog. Covers the reserved-gesture rule for widgets whose tap runs an operation, unchanged default behaviour, identical rendering in both widget formats, and matching gesture editing in both layout editors.
## Requirements
### Requirement: Built-in action widgets accept per-instance gesture overrides

The ten built-in action widgets (Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles, History, Favorites and Settings) SHALL accept a per-instance long-press and/or double-click action. An override SHALL be stored per widget INSTANCE through the existing item-property mechanism, with no new settings and no schema change, so two copies of one type MAY carry different gestures.

#### Scenario: Long-press override on a built-in widget

- **WHEN** a user assigns "Go to History (this bean)" to the Beans widget's long-press and long-presses it
- **THEN** Shot History opens filtered to the current bean
- **AND** the widget keeps its normal appearance and live state

#### Scenario: Two instances differ

- **WHEN** two Beans widgets are placed and only one is given a gesture override
- **THEN** only that instance responds to the gesture differently; the other behaves as default

#### Scenario: Same catalog as the Custom widget

- **WHEN** the gesture picker is opened for a built-in widget
- **THEN** it offers the same actions, with the same labels and context filtering, as the Custom widget's picker

### Requirement: The gesture picker SHALL offer the Custom widget's action catalog

The actions offered for a built-in widget SHALL be the same catalog the Custom widget uses, with the same labels and the same page-context filtering, so an action available on one is available on the other.

#### Scenario: Built-in and Custom pickers match

- **WHEN** the gesture picker is opened for a built-in widget and for the Custom widget in the same context
- **THEN** both SHALL offer the same action labels

### Requirement: A widget whose page is only reachable by gesture keeps one gesture for it

Widgets whose tap runs an operation (Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles) reach their page ONLY through long-press and double-click. Exactly ONE of those two gestures SHALL be overridable. Once the user overrides one, the other SHALL stay bound to opening the page and SHALL NOT be overridable. The user SHALL choose which of the two gestures carries the override.

#### Scenario: Overriding one gesture reserves the other

- **WHEN** a user assigns an action to the Steam widget's long-press
- **THEN** the double-click slot becomes non-editable and is shown as opening the Steam page

#### Scenario: The choice of gesture is the user's

- **WHEN** a user instead assigns an action to the Steam widget's double-click
- **THEN** that is accepted, and long-press becomes the reserved slot

#### Scenario: Clearing an override releases the other slot

- **WHEN** a user clears the only gesture override on a one-slot widget
- **THEN** both gestures return to opening the page, and either slot may be overridden again

#### Scenario: Two-slot widgets take both

- **WHEN** a user assigns actions to both long-press and double-click on the History widget
- **THEN** both are accepted, because tap already opens Shot History

#### Scenario: The page is never stranded

- **WHEN** any one-slot widget carries a gesture override
- **THEN** its page remains reachable from that widget by the reserved gesture

### Requirement: History, Favorites and Settings SHALL keep both gestures overridable

Widgets whose tap already opens their page (History, Favorites and Settings) SHALL have BOTH gestures overridable, with no reserved slot.

#### Scenario: Settings widget takes both gestures

- **WHEN** a user assigns actions to both long-press and double-click on the Settings widget
- **THEN** both SHALL be accepted and neither SHALL be reserved

### Requirement: The editor SHALL show the reserved gesture as reserved

The editor SHALL show the reserved gesture visibly, labelled with the destination it opens. It SHALL NOT offer the reserved gesture for editing, and SHALL NOT accept and discard an override or silently omit the slot.

#### Scenario: Reserved gesture names its destination

- **WHEN** a one-slot widget carries an override on one gesture
- **THEN** the other gesture SHALL be shown as reserved and labelled with the page it opens

### Requirement: Defaults are unchanged until an override is stored

A widget instance with no stored gesture override SHALL behave exactly as it does today, on
every gesture. Existing layouts, saved library items, and layouts imported from another
device SHALL NOT change behaviour as a result of this capability.

An override SHALL replace only the gesture it is assigned to. The widget's tap behaviour,
appearance, live state and highlight rules SHALL be unaffected.

#### Scenario: Untouched widget is untouched

- **WHEN** a layout saved before this change is loaded
- **THEN** every built-in widget behaves exactly as it did, with no override in effect

#### Scenario: Tap is never affected

- **WHEN** a Steam widget carries a long-press override
- **THEN** tapping it still toggles the steam preset row exactly as before

### Requirement: Overrides apply in both render formats

These widgets render two ways: compiled through the Custom widget's renderer in the center and action zones, and as their own dedicated component elsewhere. A stored gesture override SHALL take effect identically in both. In the compiled path, stored per-instance properties SHALL take precedence over the compiled defaults, and the compiled merge SHALL NOT discard them.

#### Scenario: Compiled format honours the override

- **WHEN** a widget carrying a gesture override is placed in a zone that renders the compiled format
- **THEN** the overridden gesture runs the stored action, not the compiled default

#### Scenario: Dedicated format honours the override

- **WHEN** the same widget is placed in a zone that renders its dedicated component
- **THEN** the overridden gesture runs the same stored action

#### Scenario: Moving a widget between zones preserves behaviour

- **WHEN** a widget with an override is moved from a zone using one format to a zone using the other
- **THEN** its gesture behaviour is unchanged

### Requirement: Both editors expose gesture overrides

The in-app layout editor and the ShotServer web layout editor SHALL both offer gesture
editing for these widget types, with the same actions, the same reserved-slot presentation,
and the same stored result. A widget configured in one editor SHALL read back correctly in
the other.

The types SHALL be marked as having per-instance options, so the existing has-options
indicator appears on them.

#### Scenario: Indicator appears on a built-in widget

- **WHEN** the layout editor shows a Beans widget
- **THEN** it carries the same has-options affordance as other configurable widgets

#### Scenario: Cross-surface round trip

- **WHEN** a gesture override is set in the web editor and the widget is then opened in the in-app editor
- **THEN** the in-app editor shows that action selected, and the same slot reserved

#### Scenario: Reserved slot presented consistently

- **WHEN** a one-slot widget with an override is opened in either editor
- **THEN** both show the reserved gesture as non-editable and name the page it opens

