# graph-settings-domain Specification

## Purpose
TBD - created by archiving change add-graph-flow-scale-menu. Update Purpose after archive.
## Requirements
### Requirement: Graph Settings Domain

Graph display preferences SHALL be exposed as properties of a `SettingsGraph` domain
sub-object with change notification, so that consumers bind to them rather than sampling them.

#### Scenario: A change reaches every consumer
- **WHEN** any graph display preference is changed from any surface
- **THEN** the live shot graph, the history/shot-detail graph, the post-shot review graph, the
  comparison graph and table, and the last-shot chart widget SHALL all reflect the new value
- **AND** no consumer SHALL require a refresh handler, watch list, or direct property
  assignment to observe it

#### Scenario: Storage keys are preserved
- **WHEN** a device that predates the domain is upgraded
- **THEN** every preference SHALL retain the value stored under its existing key
- **AND** no preference SHALL reset to its default as a result of the move

#### Scenario: One write per user action
- **WHEN** the user toggles a series from the legend or the comparison table
- **THEN** exactly one setting SHALL be written
- **AND** no consumer's property SHALL be assigned directly as part of that action

### Requirement: Single Source Of Truth For The Series List

The set of graph series — label, colour, settings property, tooltip, and the advanced and
post-shot-only gates — SHALL be defined in exactly one place.

#### Scenario: Adding a series
- **WHEN** a new series is added to the shared definition
- **THEN** the legend, the comparison table, and the last-shot chart's cache identity SHALL
  all include it without any of them being edited

#### Scenario: No consumer enumerates series independently
- **WHEN** a consumer needs the set of series or their settings keys
- **THEN** it SHALL derive them from the shared definition rather than restating them

### Requirement: Advanced Mode Is A Graph Preference

Advanced mode SHALL be a property of the graph settings domain, not a per-page mirror.

#### Scenario: Advanced mode is consistent across pages
- **WHEN** advanced mode is changed from any surface that offers it
- **THEN** the live espresso screen, shot detail, post-shot review and shot comparison pages
  SHALL all reflect it
- **AND** no page SHALL hold its own copy of the value

### Requirement: Shared Graph Presentation Components

Presentation shared by more than one graph or page SHALL live in one component.

#### Scenario: Right-axis label column
- **WHEN** the live graph and the history graph both draw a right-axis label column
- **THEN** both SHALL use the same component
- **AND** a change to its modes, formatting or accessibility SHALL take effect on both

#### Scenario: Graph options control
- **WHEN** the shot detail, post-shot review and shot comparison pages present the graph
  options control
- **THEN** all three SHALL use the same component

#### Scenario: Option cards
- **WHEN** a labelled checkbox option is presented in the extraction view selector or the
  graph options menu
- **THEN** both SHALL use the same card component

