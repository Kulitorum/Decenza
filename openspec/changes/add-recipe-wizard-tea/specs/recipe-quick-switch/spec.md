# recipe-quick-switch Specification (delta)

## ADDED Requirements

### Requirement: Recipe pills show a drink-type icon
Recipe pills in the idle widget and recipe lists SHALL show a small icon for the recipe's drink type (stored value, derived from blocks when absent), rendered as an SVG image (never a Unicode glyph per QML conventions).

#### Scenario: Mixed pill row is scannable
- **WHEN** the idle widget shows an espresso, a latte, and a tea recipe
- **THEN** each pill carries its distinct drink-type icon
