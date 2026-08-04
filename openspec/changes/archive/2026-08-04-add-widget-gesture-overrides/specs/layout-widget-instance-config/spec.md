## MODIFIED Requirements

### Requirement: Single source of truth for configurable widget types

Which widget types have configurable options, and which option keys each type supports, SHALL be defined by the layout readout capability schema (see `layout-readout-capability-schema`) and consumed by every site that needs it — the in-app has-options indicator, the in-app open-options gesture/affordance, the in-app options editor's section selection, and the web editor's indicator, open affordance, and option forms. Adding a new configurable widget type or a new option key SHALL require updating only the schema for the editors' behavior to stay consistent.

The built-in ACTION widgets — Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles, History, Favorites, Settings — SHALL be configurable types under this rule, carrying gesture-override option keys (see `layout-widget-gesture-overrides`). Their per-type slot rule — how many gestures may be overridden, and which destination the reserved gesture opens — SHALL be declared in the same schema rather than hard-coded in either editor, so the reserved-slot presentation is derived and cannot drift between surfaces.

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
