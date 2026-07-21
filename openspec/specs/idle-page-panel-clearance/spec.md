# idle-page-panel-clearance Specification

## Purpose

Transient idle-page panels — the bottom-nav quick-picker popups and the inline category quick-pick carousel — make room for themselves by transiently sliding the OTHER idle content out of the way and restoring it on close, rather than overlapping/clipping it. The opened panel stays anchored to its control; the surrounding content yields, in a direction set by the opening widget's zone, and only when the space it needs is actually occupied. This complements the per-zone layout options (offsets/scales/alignment) without altering saved configuration, and works at any window size and layout composition.

## Requirements

### Requirement: Transient panels open at their anchor

A transient idle-page panel SHALL open at its natural anchored position — a bottom-nav quick-picker adjacent to the button that opened it, the inline quick-pick carousel beneath its category tile. The panel SHALL NOT be repositioned away from that anchor in order to find unoccupied space, and SHALL NOT be shrunk below its natural size to avoid overlapping content.

#### Scenario: Panel opens adjacent to its control

- **WHEN** a bottom-nav quick-picker is opened
- **THEN** the panel SHALL appear adjacent to the button that opened it
- **AND** SHALL remain visually associated with that button regardless of how much other content the layout contains

#### Scenario: Panel is not degraded to fit

- **WHEN** the idle layout is fully populated with widgets, leaving no free space at the panel's anchor
- **THEN** the panel SHALL still open at full size at its anchor
- **AND** SHALL NOT be truncated, scaled down, or relocated to a different part of the screen

### Requirement: Content yields directionally by the opening widget's zone

When a transient panel opens, the panel itself SHALL NOT move; the **other** idle content SHALL yield out of its way, in a direction determined by the opening widget's zone, and only when the space the panel needs is actually occupied. When that space is already free, nothing SHALL move.

The yield direction SHALL be:
- widget in a **top** zone → content **below** it moves down;
- widget in a **center** zone → content **below** it moves down;
- widget in a **bottom** zone → content **above** it moves up.

#### Scenario: Bottom-zone panel pushes content above upward

- **WHEN** a bottom-nav quick-picker (a bottom-zone widget) opens and the space above it is occupied
- **THEN** the content above the panel SHALL slide up by the amount required to free that space
- **AND** the panel and that content SHALL NOT overlap once the slide completes

#### Scenario: Center-zone panel pushes content below downward

- **WHEN** the inline carousel (a center-zone widget) expands and the space below it is occupied
- **THEN** the content below the carousel SHALL slide down by the amount required to free that space
- **AND** the carousel and that content SHALL NOT overlap once the slide completes

#### Scenario: No motion when there is room

- **WHEN** a panel opens and the space it needs is unoccupied
- **THEN** no other idle-page content SHALL move
- **AND** the page SHALL appear exactly as it did before the panel opened, plus the panel

#### Scenario: The opening widget never moves

- **WHEN** any transient panel opens and content yields around it
- **THEN** the widget that opened the panel SHALL remain at its anchored position
- **AND** only the other content SHALL move

#### Scenario: Content restores exactly on close

- **WHEN** the panel closes
- **THEN** any content that slid SHALL return to its exact prior position
- **AND** no residual offset, displacement, or clipping SHALL remain

#### Scenario: A populated layout is handled

- **WHEN** the idle layout is fully populated such that the panel's required space is entirely occupied
- **THEN** the content SHALL slide to free that space rather than the panel overlapping it or being relocated
- **AND** the panel SHALL be fully visible and usable

### Requirement: Transient offset never alters saved layout configuration

The slide SHALL be a transient view offset only. It SHALL NOT modify, re-center, or re-scale any zone's stored configuration.

#### Scenario: User-configured zone options are preserved

- **WHEN** a user has configured per-zone position (`offsets`), `scales`, or alignment, and a panel opens and causes content to slide
- **THEN** the stored zone configuration SHALL be unchanged
- **AND** after the panel closes, every zone SHALL sit exactly where its configuration places it

### Requirement: Panel appearance is unchanged

Making room SHALL be achieved by the transient content offset alone. No scrim, dim, backdrop, or dialog chrome (header, footer, or frame) SHALL be added to any transient idle panel; the existing lightweight floating-card and inline-carousel appearances SHALL be preserved.

#### Scenario: No scrim or backdrop is introduced

- **WHEN** a bottom-nav quick-picker or the inline carousel is open
- **THEN** the content behind it SHALL NOT be dimmed, tinted, or covered by a backdrop
- **AND** the panel SHALL render with the same surface, radius, and chrome it has today

### Requirement: Modal pickers manage focus like a dialog

A modal bottom-nav quick-picker SHALL move focus into itself on open and restore focus to the invoking control on close, so assistive technology stays within the open panel and never traverses content that has been offset or covered. This requirement governs **focus behavior only** and SHALL NOT be implemented by changing the panel's visual treatment.

#### Scenario: Focus enters the panel and returns

- **WHEN** a modal quick-picker is opened
- **THEN** focus SHALL move into the panel's content
- **AND** when it is dismissed, focus SHALL return to the control that opened it

#### Scenario: Screen reader is unaffected by the content offset

- **WHEN** a screen reader is active and a panel opens, causing underlying content to slide
- **THEN** the screen reader SHALL be positioned on the panel's content
- **AND** the transient offset of the background content SHALL NOT be announced as a change the user must navigate
