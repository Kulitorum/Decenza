## ADDED Requirements

### Requirement: An icon depicts its subject, not its category

An icon SHALL be a drawing of the thing it names. It SHALL NOT be a generic stand-in for the
category the thing belongs to.

This is the distinction that separated the one successful emoji substitution from the failures: a
coffee cup for coffee is the subject; a toolbox for a grinder is the category. A category
stand-in is recognisable as *something*, which is why it survives review — it fails only when a
user looks for the specific thing and does not find it.

#### Scenario: An icon for a domain-specific object

- **WHEN** an icon represents something specific to espresso — a grinder, a portafilter basket, a
  brew curve, puck prep
- **THEN** it depicts that object or its direct output, not a generic symbol for tools, charts or
  equipment

#### Scenario: Judging a proposed replacement

- **WHEN** a replacement is proposed for an existing icon
- **THEN** it is assessed against what the existing icon DRAWS, not against the icon's filename or
  the label of the control it sits on

### Requirement: Icons are recolourable

Icons SHALL carry no baked-in colour that the application cannot override, so that a single asset
renders correctly against any theme colour and any state colour.

Two independent needs, and the second is the one that is easy to miss: themes recolour icons for
light and dark and for user-defined palettes; and individual controls recolour their BACKGROUND to
signal state — active, highlighted, warning — so an icon must be recoloured to stay legible against
whatever its container is currently doing.

#### Scenario: Theme change

- **WHEN** the interface switches between light and dark, or a custom palette is applied
- **THEN** every icon remains legible against the new background

#### Scenario: A control changes background to signal state

- **WHEN** a control's background colour changes to indicate that it is active, selected or in a
  warning state
- **THEN** its icon remains legible against that new background

#### Scenario: An icon on a light surface

- **WHEN** an icon is drawn on a light surface
- **THEN** it is not rendered in a colour that makes it invisible against that surface

### Requirement: Icons are legible at the sizes actually used

Icons SHALL be legible at the smallest size at which they are displayed, not only at the size they
were drawn.

#### Scenario: Small chrome

- **WHEN** an icon is displayed in navigation or status chrome at around 20 pixels
- **THEN** it is identifiable at that size without magnification

#### Scenario: Detail that does not survive

- **WHEN** an icon carries detail that becomes indistinct at its smallest displayed size
- **THEN** the detail is removed or simplified rather than shipped

### Requirement: The icon set is visually consistent

All icons in the set SHALL share one visual language. A change of style SHALL be applied to the
whole set before it ships, never to part of it.

A set in two styles reads as unfinished rather than as variety, and the mismatch is more noticeable
than either style is on its own.

#### Scenario: A style change is proposed

- **WHEN** a new visual treatment is applied to some icons
- **THEN** no release ships until every icon in the set uses that treatment, or the change is
  abandoned

#### Scenario: A concept has no equivalent in a proposed new source

- **WHEN** a proposed replacement source cannot supply an icon for some concept the app needs
- **THEN** that source is not adopted piecemeal for the concepts it does cover

### Requirement: Icon changes are judged visually before they ship

A change to an icon SHALL be evaluated by rendering it and looking at it, at the sizes and on the
backgrounds where it is used, in both themes.

Textual analysis of icon files and their call sites has repeatedly produced confident wrong answers
about how they render — about which are tinted, about what they depict, and about whether they are
visible. Rendering answered each of those correctly.

#### Scenario: Evaluating a redrawn icon

- **WHEN** an icon is redrawn
- **THEN** it is compared side by side with the icon it replaces, at its smallest and largest
  displayed sizes, on both light and dark backgrounds

#### Scenario: A replacement is not clearly better

- **WHEN** a redrawn icon is not clearly better than the one it replaces
- **THEN** the existing icon is kept
