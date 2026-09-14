## MODIFIED Requirements

### Requirement: ProfileSelector overflow action

The overflow action surface on profile picker cards — on `ProfileSelectorPage` and on the recipe wizard's profile step — SHALL expose a contextual auto-load action delivered through an accessible modal Dialog (not a popup Menu).

#### Scenario: Action available for Selected-list profiles
- **WHEN** the user opens the overflow dialog for a card whose profile is in the Selected list
- **THEN** the dialog shows a Set / Disable Auto-Load button

#### Scenario: Label and action reflect current state
- **WHEN** the card's profile is the current auto-load
- **THEN** the button is labelled "Disable Auto-Load" AND activating it clears `autoLoadProfileFilename`

#### Scenario: Setting on a different row replaces the prior auto-load
- **WHEN** the user activates "Set Auto-Load" on a card whose profile is not the current auto-load
- **THEN** `autoLoadProfileFilename` is set to that card's filename AND any prior auto-load is no longer marked

#### Scenario: Action hidden for non-Selected profiles
- **WHEN** the user opens the overflow dialog for a card whose profile is not in the Selected list
- **THEN** the dialog does not show an auto-load button

### Requirement: Auto-load row marker

The picker card representing the current auto-load profile SHALL show a visible marker so the user can identify it at a glance, in both hosts.

#### Scenario: Pin icon visible on the auto-load row
- **WHEN** the card's profile filename equals `autoLoadProfileFilename`
- **THEN** the `pin.svg` icon is visible beside the profile title, colored `Theme.primaryColor`

#### Scenario: Pin icon has an accessible name
- **WHEN** an accessibility screen reader focuses the pin icon
- **THEN** the reader announces "Auto-load profile"

#### Scenario: Marker hidden for non-auto-load rows
- **WHEN** the card's filename does not equal `autoLoadProfileFilename`
- **THEN** the pin icon is not rendered

### Requirement: ProfileSelector status strip

A strip at the top of `ProfileSelectorPage`, above the picker, SHALL surface the configured auto-load and allow tuning the revert minutes, visible only when an auto-load is configured and resolves to a Selected-list profile. The strip SHALL NOT appear in the recipe wizard host. The strip's text SHALL be sized via `Theme.captionFont` so it respects the user's `customFontSizes.captionSize` accessibility override.

#### Scenario: Strip visible when configured
- **WHEN** `autoLoadProfileFilename` is non-empty AND resolves to a Selected-list profile
- **THEN** the strip appears above the picker's search and chip rows showing: pin icon, "Auto-load:" label, profile title, "revert after" label, a numeric input for minutes, and a clear button

#### Scenario: Strip hidden when no auto-load is set
- **WHEN** `autoLoadProfileFilename` is `""`
- **THEN** the strip is not rendered

#### Scenario: Editing revert minutes from the strip
- **WHEN** the user changes the value in the strip's numeric input
- **THEN** `Settings.app.autoLoadRevertMinutes` is updated live AND any in-progress inactivity countdown is reset to the new value

#### Scenario: "off" rendering at zero
- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the strip's minute input displays "off" instead of "0 min" (the startup and wake-from-sleep triggers still fire)

#### Scenario: Clear button disables auto-load
- **WHEN** the user activates the strip's clear button
- **THEN** `autoLoadProfileFilename` is cleared AND a toast confirms "Auto-load disabled" AND the strip disappears

#### Scenario: Strip absent in the wizard
- **WHEN** the recipe wizard's profile step opens while an auto-load is configured
- **THEN** no auto-load strip is shown
