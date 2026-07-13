## ADDED Requirements

### Requirement: Overlay chip group in screensaver Display settings
For each screensaver background that supports an overlay (Videos & Images, 3D Pipes, Strange Attractors, Shot Map), the Display settings card SHALL offer a chip group with one chip per overlay item: Clock, Water Level, Shot Plan, Battery, Link Button. Tapping a chip SHALL toggle that item on/off. Clock is scoped per background type (matching its existing storage); Water Level, Shot Plan, Battery, and Link Button are each a single global setting shared across every background. The Flip Clock background SHALL NOT offer a Clock chip. The Turn Screen Off background SHALL NOT offer any overlay chips.

#### Scenario: Enabling an overlay item
- **WHEN** a user taps the Water Level chip while the Videos & Images background is selected
- **THEN** Water Level becomes enabled, and shows the same enabled state when the user switches to any other background (Pipes, Strange Attractors, Shot Map)

#### Scenario: Clock chip absent on Flip Clock
- **WHEN** a user selects the Flip Clock background in the Type dropdown
- **THEN** the Display settings card does not offer a Clock chip

#### Scenario: Existing Clock setting preserved on upgrade
- **WHEN** an existing user upgrades and previously had a background's per-type "Show Clock" boolean set to true
- **THEN** the Clock chip for that background shows as enabled after upgrade, with no other action required

### Requirement: Water Level overlay
When enabled for the active background, the screensaver SHALL render a Water Level readout in a compact icon row along the top of the screen, right-aligned, using the same live data and compact rendering as the home screen's water level widget.

#### Scenario: Water level shown while machine is asleep
- **WHEN** the screensaver is active with Water Level enabled and the DE1 is connected
- **THEN** the current water level (from the DE1's water level sensor) is displayed in the top-right icon row

### Requirement: Shot Plan overlay
When enabled for the active background, the screensaver SHALL render the current Shot Plan (profile, dose, temperature) in its compact form in the top-right icon row, using the same data as the home screen's Shot Plan widget.

#### Scenario: Shot plan shown while machine is asleep
- **WHEN** the screensaver is active with Shot Plan enabled and a profile/recipe is selected
- **THEN** the top-right icon row shows the compact Shot Plan summary for that selection, independent of whether the DE1 is connected

### Requirement: Battery overlay
When enabled for the active background, the screensaver SHALL render the tablet's own battery level in the top-right icon row, using the same data as the home screen's battery widget.

#### Scenario: Battery level shown regardless of BLE connection
- **WHEN** the screensaver is active with Battery enabled, regardless of DE1 or scale connection state
- **THEN** the top-right icon row shows the tablet's current battery percentage

### Requirement: Configurable Link Button
Users SHALL be able to configure exactly one Link Button per background, with a Label and a URL. When enabled, the button SHALL render bottom-left on the screensaver. Tapping it SHALL open the configured URL in the system's default browser and SHALL NOT wake the DE1 or scale, and SHALL NOT trigger the screensaver's normal wake-on-tap dismissal.

#### Scenario: Configuring the link button
- **WHEN** a user enables the Link Button chip and enters a Label and URL
- **THEN** the button renders bottom-left on that background's screensaver showing the configured Label

#### Scenario: Tapping the link button does not wake the machine
- **WHEN** the screensaver is active with the Link Button enabled and the user taps the button
- **THEN** the configured URL opens in the system's default browser, and the DE1 and scale remain asleep and the screensaver remains active

#### Scenario: Tapping elsewhere on the screensaver still wakes the machine
- **WHEN** the screensaver is active with the Link Button enabled and the user taps anywhere outside the button
- **THEN** the machine wakes as it does today

### Requirement: Anti-burn-in positional drift
Each overlay anchor point (Clock, the top icon row, the Link Button) SHALL slowly drift by a small number of pixels within its corner or edge over time, without requiring any user-facing setting.

#### Scenario: Overlay position drifts over an extended idle period
- **WHEN** the screensaver remains active for an extended period with one or more overlay items enabled
- **THEN** each enabled item's on-screen position varies by a few pixels over time rather than remaining at an exact fixed pixel position
