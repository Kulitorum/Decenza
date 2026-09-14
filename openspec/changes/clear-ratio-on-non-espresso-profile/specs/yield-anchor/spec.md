## MODIFIED Requirements

### Requirement: A ratio anchor survives a profile change; an absolute one does not

When a profile is loaded, the system SHALL clear a session anchor whose mode is `absolute` (a gram target describes the profile it was set against). It SHALL preserve one whose mode is `ratio` when the incoming profile is espresso (a ratio is profile-independent across espresso profiles), and SHALL clear it when the incoming profile's `beverage_type` is anything other than espresso (a dose ratio means nothing to tea, filter or the other non-espresso types). An empty `beverage_type` SHALL count as espresso. A ratio the user sets after the non-espresso profile is loaded SHALL apply normally. When an espresso profile is loaded and no anchor remains, the system SHALL re-arm the active bag's saved ratio, if it has one.

#### Scenario: Ratio persists across a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user loads a different espresso profile
- **THEN** the anchor remains `{2.0, ratio}` and the target re-derives against the current dose

#### Scenario: Absolute clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}`, the active bag saves no ratio, and the user loads a different profile
- **THEN** the anchor clears and the new profile's `target_weight` applies

#### Scenario: A bean's saved ratio returns on the way back to espresso
- **WHEN** the active bag saves `{2.0, ratio}`, the dose is 18 g, and the user loads a tea profile and then an espresso profile
- **THEN** the anchor is `{2.0, ratio}` and the target resolves to 36 g

#### Scenario: Ratio clears on a switch to a non-espresso profile
- **WHEN** the session anchor is `{2.5, ratio}` and the user loads a profile whose `beverage_type` is `tea_portafilter`
- **THEN** the anchor clears and the tea profile's own `target_weight` applies (0 = no weight stop)

#### Scenario: A ratio dialed on a non-espresso profile applies
- **WHEN** a non-espresso profile is loaded, the dose is 18 g, and the user then sets the anchor `{2.5, ratio}`
- **THEN** the target resolves to 45 g
