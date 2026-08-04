# custom-widget-history-actions Specification

## Purpose
TBD - created by archiving change add-history-filter-widget-actions. Update Purpose after archive.
## Requirements
### Requirement: The Custom widget offers four context-filtered History actions

The Custom layout widget's action catalog SHALL include four actions that open Shot
History with a filter already applied, in addition to the existing unfiltered
"Go to History":

- **Go to History (this recipe)** — filtered to the currently active recipe.
- **Go to History (this bean)** — filtered to the current bean's brand and type, so the
  same coffee across every bag of it.
- **Go to History (this bag)** — filtered to the active bag exactly.
- **Go to History (this profile)** — filtered to the currently loaded profile.

Each SHALL be assignable to a Custom widget's tap, long-press and double-click gesture on
the same terms as every other action, and SHALL be available in the same page contexts as
the existing "Go to History" action.

The bean and bag actions SHALL remain distinct: the bean action answers "how does this
coffee behave", the bag action answers "how is this bag behaving", and neither SHALL be
implemented as the other.

#### Scenario: Recipe action opens History filtered to the active recipe

- **WHEN** a user activates a Custom widget configured with "Go to History (this recipe)" while a recipe is active
- **THEN** Shot History opens listing only shots made with that recipe
- **AND** the filter banner names the recipe and offers its Clear control

#### Scenario: Bean action spans bags of the same coffee

- **WHEN** a user activates a Custom widget configured with "Go to History (this bean)" while a bean is selected
- **THEN** Shot History opens listing shots recorded with that bean's brand and type, including shots pulled from earlier bags of the same coffee
- **AND** the filter banner names the bean and offers its Clear control

#### Scenario: Bag action is confined to one bag

- **WHEN** a user activates a Custom widget configured with "Go to History (this bag)" while a bag is active
- **THEN** Shot History opens listing only shots recorded against that bag
- **AND** shots from a different bag of the same coffee are absent

#### Scenario: Profile action opens History filtered to the loaded profile

- **WHEN** a user activates a Custom widget configured with "Go to History (this profile)" while a profile is loaded
- **THEN** Shot History opens listing only shots pulled with that profile
- **AND** the filter banner names the profile and offers its Clear control

#### Scenario: Action assignable to any gesture

- **WHEN** a user assigns one of the four actions to a Custom widget's long-press or double-click gesture
- **THEN** that gesture opens the correspondingly filtered Shot History

### Requirement: Filter values are read from live app state at activation time

Each action SHALL resolve its filter when the widget is activated, from the app's current
state, never from a value stored on the widget. The widget's stored configuration SHALL be
the action identifier alone, carrying no recipe, bean, bag or profile identity.

The recipe action SHALL filter by the active recipe's **id** and the bag action by the
active bag's **id**, so a rename cannot orphan the filter and two records sharing a name
stay distinct; the corresponding names SHALL be passed for the banner label only, never as
a query term. The bean action SHALL filter by the current bean's brand and type. The
profile action SHALL filter by the loaded profile's name.

#### Scenario: Widget follows a change of active recipe

- **WHEN** the active recipe changes and the user then activates a Custom widget configured with "Go to History (this recipe)"
- **THEN** Shot History is filtered to the newly active recipe, with no re-configuration of the widget

#### Scenario: Widget follows a change of active bag

- **WHEN** the user selects a different bag and then activates a Custom widget configured with "Go to History (this bag)"
- **THEN** Shot History is filtered to the newly active bag

#### Scenario: Renamed recipe still matches its shots

- **WHEN** the active recipe has been renamed since its shots were pulled and the user activates the recipe action
- **THEN** Shot History still lists those shots
- **AND** the filter banner shows the recipe's current name

#### Scenario: Widget configuration carries no identity

- **WHEN** a layout containing one of these widgets is exported and loaded on another device
- **THEN** the widget filters by that device's own active recipe, bean, bag or profile

### Requirement: An action with no context to filter on opens History unfiltered

When the state an action filters on is absent — no active recipe, no bean selected, no bag
active, or no profile loaded — activating the action SHALL open Shot History unfiltered
rather than doing nothing, and SHALL NOT show a filter banner. The widget SHALL NOT be
disabled or hidden in that state.

#### Scenario: No active recipe

- **WHEN** a user activates the recipe action with no recipe active
- **THEN** Shot History opens showing all shots, with no filter banner

#### Scenario: No active bag

- **WHEN** a user activates the bag action with no bag active
- **THEN** Shot History opens showing all shots, with no filter banner

#### Scenario: Partially known bean

- **WHEN** a user activates the bean action while the current bean has a brand but no type recorded
- **THEN** Shot History opens filtered on the brand alone rather than on an empty type

### Requirement: Activating a History action while History is showing re-filters in place

When Shot History is already the current page, activating any of these actions — or the
unfiltered "Go to History" — SHALL apply the requested filter to the page that is showing
rather than being discarded. The unfiltered action SHALL clear an active filter. No
duplicate Shot History page SHALL be pushed onto the stack.

#### Scenario: Status-bar widget tapped while History is open

- **WHEN** a user is viewing Shot History and activates a status-bar Custom widget configured with one of the filtered History actions
- **THEN** the visible Shot History list re-filters to that action's filter and the banner updates
- **AND** the navigation stack does not gain a second Shot History page

#### Scenario: Unfiltered action clears an active filter

- **WHEN** a user is viewing a filtered Shot History and activates a widget configured with the plain "Go to History"
- **THEN** the filter is cleared and the full list is shown

### Requirement: Both widget editors offer the four actions

The four actions SHALL appear in the in-app Custom widget editor's action picker and in
the ShotServer web layout editor's action picker, with the same labels and the same
gesture coverage, so a widget configured on one surface reads and edits correctly on the
other.

#### Scenario: In-app picker lists the actions

- **WHEN** a user opens the action picker for a Custom widget in the in-app layout editor
- **THEN** the four context-filtered History actions are listed alongside "Go to History"

#### Scenario: Web picker lists the actions

- **WHEN** a user opens the action picker for a Custom widget in the web layout editor
- **THEN** the four context-filtered History actions are listed with the same labels

#### Scenario: Cross-surface round trip

- **WHEN** a widget is configured with one of these actions in the web editor and then opened in the in-app editor
- **THEN** the in-app editor shows that action selected, not a blank or unknown action

