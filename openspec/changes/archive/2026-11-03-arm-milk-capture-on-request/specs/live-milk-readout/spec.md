## MODIFIED Requirements

### Requirement: Live net-milk suffix on steam preset pills
The idle-page steam preset pill row SHALL append a live net-milk suffix `" (Xg)"` to the selected pitcher's pill only, where `X = round(max(0, scaleWeight − pitcherWeightG))`, updating as the scale weight changes. Unselected pills SHALL show no suffix. The suffix SHALL be shown only when the selected preset has a saved empty-pitcher weight (`pitcherWeightG > 0`), a scale is connected, and the scale is not the FlowScale fallback. Disabled ("Off") presets SHALL never show a suffix.

#### Scenario: Weigh milk on the pills (1.8.0 flow)
- **WHEN** the selected pitcher preset has a saved pitcher weight of 300 g, steam presets are shown, and the user places that pitcher with milk on the connected scale so it reads 450 g
- **THEN** the selected pill shows the suffix " (150g)" and the number tracks live as milk is added or removed

#### Scenario: Only the selected pitcher shows a weight
- **WHEN** several presets have saved pitcher weights and the scale reads 450 g
- **THEN** only the selected pill shows a suffix, and selecting another pitcher moves the suffix to it

#### Scenario: No saved pitcher weight
- **WHEN** the selected preset has no saved empty-pitcher weight (`pitcherWeightG` absent or 0)
- **THEN** no pill shows a suffix

#### Scenario: No real scale
- **WHEN** no scale is connected, or the active scale is the FlowScale fallback
- **THEN** no pill shows a suffix

#### Scenario: Reading below the pitcher weight clamps to zero
- **WHEN** the scale reading is less than the selected pitcher's saved weight (e.g. the pitcher was lifted, or the scale was tared with the pitcher on it)
- **THEN** the suffix shows " (0g)" rather than a negative number
