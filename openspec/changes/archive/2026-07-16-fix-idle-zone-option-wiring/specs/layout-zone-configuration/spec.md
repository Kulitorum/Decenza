## MODIFIED Requirements

### Requirement: Item distribution option

Each **bar** zone SHALL support an item-distribution option with at least the values `packed` (default, current behaviour), `equalWidth` (every widget gets an equal-width cell regardless of content width), and `spaced` (evenly spaced / justified). The default SHALL preserve current behaviour so existing layouts are unchanged.

The **center** zones (`centerStatus`, `centerTop`, `centerMiddle`) SHALL NOT support item distribution, and the editors SHALL NOT offer the control for them. A center zone sizes every item from a fixed cell (capped so its action buttons never stretch), which is exactly what `equalWidth` and `spaced` require it to abandon; with the cap kept, both values would be indistinguishable from each other and would have no effect at all on a zone containing only action buttons.

#### Scenario: Default preserves current behaviour

- **WHEN** a zone has no distribution option set (existing layouts)
- **THEN** it SHALL lay out widgets exactly as it does today

#### Scenario: Equal-width distribution

- **WHEN** a bar zone's distribution is `equalWidth`
- **THEN** every widget SHALL occupy an equal-width cell across the zone, independent of its content width

#### Scenario: Spaced distribution

- **WHEN** a bar zone's distribution is `spaced`
- **THEN** widgets SHALL be evenly distributed across the zone's width

#### Scenario: Center zones do not offer distribution

- **WHEN** a user opens zone options for `centerStatus`, `centerTop`, or `centerMiddle` in either editor
- **THEN** no item-distribution control SHALL be offered
- **AND** any distribution value already stored for that zone SHALL be ignored, including by the previews

### Requirement: Zone alignment option

Each zone — bar **and** center — SHALL support an alignment option (`left` / `center` / `right`) that positions its widgets when they do not fill the zone width. The default SHALL preserve current behaviour.

#### Scenario: Alignment positions un-filled content

- **WHEN** a zone's widgets do not fill the available width
- **AND** the zone alignment is set to `left`, `center`, or `right`
- **THEN** the widgets SHALL be positioned accordingly within the zone

#### Scenario: Alignment is ignored under full-width distribution

- **WHEN** a bar zone uses `equalWidth` or `spaced` distribution (which already fill the width)
- **THEN** the alignment option SHALL have no visible effect and SHALL NOT cause layout errors

#### Scenario: Center zone alignment

- **WHEN** a center zone's alignment is set to `left` or `right`
- **THEN** its row SHALL be justified to that edge on the idle screen, not only in the previews

## ADDED Requirements

### Requirement: The idle screen honors every offered zone option

For every zone, the idle screen SHALL honor each per-zone option the editors offer for that zone. An option that is offered, persisted, and reflected in a preview SHALL take effect in the rendered home screen; conversely, an option a zone cannot express SHALL NOT be offered for it in either editor, nor approximated by either preview.

This forbids the dead-control state in which a user sets an option, sees it confirmed by the in-app and web previews, and observes no change on the idle screen.

#### Scenario: Bar zones honor distribution, alignment, and style

- **WHEN** `topLeft`, `topRight`, `bottomLeft`, `bottomRight`, `lowerMidBar`, or `statusBar` has a distribution, alignment, or style option set
- **THEN** the idle screen SHALL render that zone accordingly

#### Scenario: Center zones honor alignment and style

- **WHEN** `centerStatus`, `centerTop`, or `centerMiddle` has an alignment or style option set
- **THEN** the idle screen SHALL render that zone accordingly

#### Scenario: An unsupported option is offered nowhere

- **WHEN** a zone cannot express a given option
- **THEN** neither editor SHALL offer that control for the zone
- **AND** neither preview SHALL depict an effect for it
