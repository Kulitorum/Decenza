## Why

A ratio yield anchor survives every profile switch (add-yield-ratio-anchor Decision 8). That holds for espresso, but a dose ratio means nothing to a tea or filter profile. Selecting a tea profile after dialing 1:2.5 on espresso stopped the steep at 2.5 × the espresso dose (48.8 g on-device) instead of running the tea profile's own frames, whose `target_weight` is 0 (no weight stop, matching de1app) — [#1941](https://github.com/Kulitorum/Decenza/issues/1941).

## What Changes

- A runtime profile load clears a ratio anchor when the incoming profile's `beverage_type` is not espresso (empty counts as espresso). The profile's own target applies.
- A ratio the user dials after selecting a non-espresso profile applies as normal. Nothing blocks a ratio; the switch just does not carry one over.
- Espresso-to-espresso switches are unchanged: the ratio survives.
- Loading an espresso profile with no yield anchor left re-arms the active bag's saved ratio. Nothing else re-applies the bag rung on a profile change, so without this an espresso → tea → espresso round trip would lose a ratio saved on the bean.

## Capabilities

### Modified Capabilities
- `yield-anchor`: a ratio anchor survives a profile change only onto an espresso profile.
- `brew-overrides`: the profile-switch clearing rule gains the same beverage-type condition.

## Impact

- `src/profile/profile.h` — `Profile::isEspressoBeverageType`.
- `src/core/settings_brew.{h,cpp}` — `clearProfileScopedBrewOverrides(bool keepRatioAnchor)`.
- `src/controllers/profilemanager.cpp` — `resetBrewOverridesForLoadedProfile` passes the incoming profile's beverage type.
- `tests/tst_profilemanager.cpp` — `profileSwitchKeepsRatioClearsAbsolute` covers the tea case.
- Startup restore is unchanged: a persisted ratio on a non-espresso profile can only have been dialed on it.
