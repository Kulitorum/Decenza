## MODIFIED Requirements

### Requirement: A ratio anchor survives a profile change; an absolute one does not

When a profile is loaded, the system SHALL clear a session anchor whose mode is `absolute` (a gram target describes the profile it was set against). It SHALL preserve one whose mode is `ratio` when the new profile's beverage group matches the previous profile's, and SHALL clear it when the group changes. The groups are espresso (including an empty or unrecognised `beverage_type`), filter (`filter`, `pourover`) and tea (`tea`, `tea_portafilter`), compared trimmed and case-insensitively. A maintenance profile (`cleaning`, `descale`, `calibrate`) SHALL neither use nor clear any brew override and SHALL NOT count as a group change: its own target and temperatures apply, and the overrides return with the next drink profile.

When the load leaves no anchor, the system SHALL arm the active recipe's saved yield, else the active bag's, except on a maintenance profile. A recipe's gram yield equal to the profile's own `target_weight` SHALL NOT be armed. A ratio the user sets after the load SHALL apply normally.

#### Scenario: Ratio persists across a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user loads a different espresso profile
- **THEN** the anchor remains `{2.0, ratio}` and the target re-derives against the current dose

#### Scenario: Absolute clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}`, neither an active recipe nor the active bag saves a yield, and the user loads a different profile
- **THEN** the anchor clears and the new profile's `target_weight` applies

#### Scenario: Ratio clears when the beverage group changes
- **WHEN** the session anchor is `{2.5, ratio}`, neither an active recipe nor the active bag saves a yield, and the user loads a profile whose `beverage_type` is `tea_portafilter`
- **THEN** the anchor clears and the tea profile's own `target_weight` applies (0 = no weight stop)

#### Scenario: Espresso to filter drops the espresso ratio
- **WHEN** the session anchor is `{2.0, ratio}` from an espresso profile and the user loads a `pourover` profile
- **THEN** the dialed ratio is cleared

#### Scenario: The bean's saved yield applies after the switch
- **WHEN** the active bag saves `{300.0, absolute}`, no recipe is active, and the user switches from an espresso profile to a filter profile
- **THEN** the anchor is `{300.0, absolute}`

#### Scenario: A recipe's saved yield outranks the bean's
- **WHEN** the active recipe saves `{2.2, ratio}`, its bag saves `{2.0, ratio}`, and a load leaves no anchor
- **THEN** the anchor is `{2.2, ratio}`

#### Scenario: A ratio dialed after the load applies
- **WHEN** a tea profile is loaded, the dose is 18 g, and the user then sets the anchor `{2.5, ratio}`
- **THEN** the target resolves to 45 g

#### Scenario: A cleaning run neither uses nor clears the ratio
- **WHEN** the session anchor is `{3.0, ratio}` on a filter profile and the user loads a cleaning profile, then a filter profile
- **THEN** the cleaning run stops on its own `target_weight`, and the filter profile's target resolves from `{3.0, ratio}` again

#### Scenario: Replaying a shot with no yield override
- **WHEN** a shot pulled at its profile's own target is loaded from history while the active bag saves a ratio
- **THEN** the session has no yield anchor and the profile's `target_weight` applies
