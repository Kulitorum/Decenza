# layout-widget-gesture-overrides Specification

## Purpose
TBD - created by archiving change add-widget-gesture-overrides. Update Purpose after archive.
## Requirements
### Requirement: Built-in action widgets accept per-instance gesture overrides

The following widget types SHALL accept a per-instance long-press and/or double-click
action chosen from the Custom-widget action catalog: Recipes, Beans, Steam, Hot Water,
Equipment, Flush, Profiles, History, Favorites, Settings.

An override SHALL be stored per widget INSTANCE, so two copies of the same widget type can
carry different gestures. It SHALL be stored through the existing item-property mechanism,
requiring no new settings and no schema change.

The set of actions offered SHALL be the same catalog the Custom widget uses, filtered by
the same page contexts, so an action available on one is available on the other.

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

### Requirement: A widget whose page is only reachable by gesture keeps one gesture for it

Widget types whose tap runs an operation rather than opening a page — Recipes, Beans,
Steam, Hot Water, Equipment, Flush, Profiles — reach their page ONLY through long-press and
double-click, both of which open it today. For these, EXACTLY ONE of the two gestures SHALL
be overridable. Once the user overrides one, the other SHALL remain bound to opening the
widget's page and SHALL NOT be overridable.

The editor SHALL show the reserved gesture as reserved — visibly present, not offered for
editing, and labelled with the destination it opens — rather than accepting an override and
discarding it, or silently omitting the slot.

Widget types whose TAP already opens their page — History, Favorites, Settings — have no
such constraint, and BOTH gestures SHALL be overridable.

The user SHALL choose WHICH of the two gestures carries the override; neither is fixed.

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

These widgets render two ways: compiled to the Custom widget's renderer in the center and
action zones, and as their own dedicated component elsewhere. A stored gesture override
SHALL take effect in BOTH, identically.

In the compiled path, stored per-instance properties SHALL take precedence over the
compiled defaults. (Today the compiled merge rebuilds the item from its type and id and
copies only compiled keys, so a stored property is discarded — that is the specific defect
this requirement closes.)

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

