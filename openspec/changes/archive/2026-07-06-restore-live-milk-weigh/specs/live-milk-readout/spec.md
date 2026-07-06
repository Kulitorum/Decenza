# live-milk-readout

## ADDED Requirements

### Requirement: Live net-milk suffix on steam preset pills
The idle-page steam preset pill row SHALL append a live net-milk suffix `" (Xg)"` to each pill, where `X = round(max(0, scaleWeight − pitcherWeightG))`, updating as the scale weight changes. The suffix SHALL be shown for a pill only when that preset has a saved empty-pitcher weight (`pitcherWeightG > 0`), a scale is connected, and the scale is not the FlowScale fallback. Disabled ("Off") presets SHALL never show a suffix.

#### Scenario: Weigh milk on the pills (1.8.0 flow)
- **WHEN** a pitcher preset has a saved pitcher weight of 300 g, steam presets are shown, and the user places that pitcher with milk on the connected scale so it reads 450 g
- **THEN** the preset pill shows the suffix " (150g)" and the number tracks live as milk is added or removed

#### Scenario: No saved pitcher weight
- **WHEN** a preset has no saved empty-pitcher weight (`pitcherWeightG` absent or 0)
- **THEN** that pill shows no suffix

#### Scenario: No real scale
- **WHEN** no scale is connected, or the active scale is the FlowScale fallback
- **THEN** no pill shows a suffix

#### Scenario: Reading below the pitcher weight clamps to zero
- **WHEN** the scale reading is less than the saved pitcher weight (e.g. the pitcher was lifted, or the scale was tared with the pitcher on it)
- **THEN** the suffix shows " (0g)" rather than a negative number

### Requirement: Readout is display-only and independent of weight-timed steaming
The live net-milk readout SHALL NOT write any settings, tare the scale, set `sessionMeasuredMilkG`, or alter the programmed steam duration. It SHALL NOT apply the 50–1500 g validity window used by `SettingsBrew::netMilkForPitcher()` for steam-time scaling, and it SHALL render identically whether the weight-timed steaming toggle is on or off.

#### Scenario: Weight-timed steaming disabled
- **WHEN** `milkAutoCaptureEnabled` is off and a saved-weight pitcher with milk rests on the scale
- **THEN** the suffix shows the net milk, and steam still uses the preset's fixed duration

#### Scenario: Small milk amount below the scaling floor
- **WHEN** the net milk is 30 g (below the 50 g floor `netMilkForPitcher()` enforces for scaling)
- **THEN** the suffix shows " (30g)"

#### Scenario: Capture pipeline unaffected
- **WHEN** weight-timed steaming is enabled and the settle detector captures a milk weight while the suffix is visible
- **THEN** the captured value and the scaled steam time are identical to what they would be without the suffix rendered

### Requirement: Consistent readout across steam pill surfaces
The compact-mode steam widget's preset popup and the idle-page steam pill row SHALL use the same suffix math and the same display gates, so the same scale state produces the same number on both surfaces.

#### Scenario: Same state, same number
- **WHEN** the same pitcher with the same milk rests on the scale and the user views the idle-page pill row or the compact steam popup
- **THEN** both show the same " (Xg)" suffix
