## MODIFIED Requirements

### Requirement: Persistent Override Storage
The system SHALL store temperature and yield overrides in QSettings for persistence across app sessions. The yield override SHALL be stored as a `YieldSpec` — a value plus a `none` | `absolute` | `ratio` mode (`yield-anchor`) — not as a bare gram number.

"Active" is tracked by a genuine boolean flag (`hasTemperatureOverride`, `hasBrewYieldOverride`) that reflects whether a deliberate override is currently in effect — not merely whether an override value has ever been set during the session. For the yield, "active" SHALL be defined as `mode != none` and SHALL NEVER be inferred by comparing a resolved gram value against the profile's target weight — a ratio that happens to derive exactly the profile's target is still a deliberate, active anchor.

Overrides SHALL be cleared — the flag set false, not just the value resynced to a new default — when a recipe is activated (before its own overrides apply), or when the user taps "Clear" in the BrewDialog.

**On a profile switch the yield override SHALL be cleared when its mode is `absolute`, or when its mode is `ratio` and the incoming profile's `beverage_type` is not espresso.** A gram target describes the profile it was set against and is meaningless on another; a ratio is profile-independent across espresso profiles (1:2 is 1:2 on any espresso profile) and SHALL survive a switch to one, re-deriving against the current dose. A dose ratio means nothing to tea, filter or the other non-espresso types, so those profiles SHALL start from their own target; a ratio the user sets after loading one SHALL apply. Loading an espresso profile with no yield override left SHALL re-arm the active bag's saved ratio, if it has one. The temperature override SHALL continue to clear unconditionally on a profile switch.

Loading a shot or favorite that carries its own frozen override value SHALL only mark the flag active when that frozen value genuinely differs from the freshly-loaded profile's own default (the same threshold the Shot Plan display uses), so a frozen value that happens to already match the current profile never falsely reports as an active override.

#### Scenario: Overrides persist between app sessions
- **WHEN** the user sets temperature or yield overrides in the BrewDialog
- **THEN** the values are immediately saved to QSettings
- **AND** when the app is restarted, the overrides are restored from QSettings — including the yield's mode
- **AND** the overrides remain active until explicitly cleared

#### Scenario: An absolute yield override clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}`, the active bag saves no ratio, and the user switches to a different profile
- **THEN** the yield override is cleared from QSettings and `hasBrewYieldOverride` becomes false
- **AND** the IdlePage shot plan returns to the new profile's target weight with no highlight

#### Scenario: A ratio yield override survives a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user switches to a different espresso profile
- **THEN** the anchor remains `{2.0, ratio}` and `hasBrewYieldOverride` stays true
- **AND** the target re-derives against the current dose on the new profile

#### Scenario: A ratio yield override clears on a switch to a non-espresso profile
- **WHEN** the session anchor is `{2.5, ratio}` and the user switches to a tea profile
- **THEN** `hasBrewYieldOverride` becomes false and the stop-at-weight target is the tea profile's own `target_weight`

#### Scenario: A bean's saved ratio is re-armed when returning to espresso
- **WHEN** the active bag saves `{2.0, ratio}` and the user switches from a tea profile to an espresso profile
- **THEN** the anchor is `{2.0, ratio}` and `hasBrewYieldOverride` is true

#### Scenario: Temperature still clears on a profile switch
- **WHEN** a temperature override is active and the user switches profiles
- **THEN** `hasTemperatureOverride` becomes false, unchanged from before this change

#### Scenario: Overrides cleared via BrewDialog
- **WHEN** the user taps "Clear" in the BrewDialog
- **THEN** all overrides are removed from QSettings and the override flags become false
- **AND** the Settings properties are reset to default values

#### Scenario: Overrides cleared on recipe activation
- **WHEN** a recipe is activated
- **THEN** the override flags reflect only that recipe's own stored overrides (or false, if it has none) — not a leftover flag from whatever was active before

#### Scenario: A ratio deriving the profile's own target still reads as active
- **WHEN** the session anchor is `{2.0, ratio}`, the dose is 18 g, and the active profile's `target_weight` is 36 g
- **THEN** `hasBrewYieldOverride` is true and `brewByRatioActive` is true
- **AND** a subsequent dose change still re-derives the target

#### Scenario: Deactivation alone leaves the live setup untouched
- **WHEN** the active recipe is deactivated without switching profile or bag
- **THEN** the live brew values — including any override the recipe applied — remain in effect (deactivation drops the recipe association, not the dialed setup); the next profile or recipe switch clears them as above

#### Scenario: A frozen shot value matching the current profile is not flagged
- **WHEN** a shot or favorite is loaded whose saved `temperatureOverride` happens to equal the freshly-loaded profile's own default temperature
- **THEN** `hasTemperatureOverride` is false and the Shot Plan shows no highlight
