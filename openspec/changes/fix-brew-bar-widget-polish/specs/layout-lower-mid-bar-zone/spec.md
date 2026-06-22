## ADDED Requirements

### Requirement: Lower-mid-bar widgets do not overlap

The `lowerMidBar` zone SHALL lay out its widgets so they never visually overlap or stack on top of one another, across the zone's supported distribution, alignment, item-size, and scale states. In particular, a size-selecting control, a "Ratio" label, and a ratio pill placed together in the bar SHALL each occupy their own space and remain legible.

#### Scenario: No overlap across layout states

- **WHEN** the `lowerMidBar` zone renders one or more widgets in any supported distribution / alignment / item-size / scale combination
- **THEN** each widget SHALL occupy a distinct, non-overlapping region of the bar

#### Scenario: Size controls, ratio label, and ratio pill are distinct

- **WHEN** the `lowerMidBar` contains size-selecting controls together with a "Ratio" label and ratio pill
- **THEN** all three SHALL be laid out side by side without stacking on top of each other
- **AND** each SHALL remain fully visible and legible

#### Scenario: Scaled content stays within the bar

- **WHEN** the `lowerMidBar` zone applies a non-default content scale
- **THEN** the scaled widgets SHALL remain aligned to the zone and SHALL NOT overlap neighbouring widgets
