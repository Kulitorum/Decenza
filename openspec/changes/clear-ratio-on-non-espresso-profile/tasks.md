## 1. Implementation

- [x] 1.1 Add `Profile::isEspressoBeverageType` (empty or `espresso`, trimmed and case-insensitive).
- [x] 1.2 `SettingsBrew::clearProfileScopedBrewOverrides(bool keepRatioAnchor)`: keep a ratio anchor only when asked.
- [x] 1.3 `ProfileManager::resetBrewOverridesForLoadedProfile` passes `isEspressoBeverageType` of the incoming profile.
- [x] 1.4 Loading an espresso profile with no yield anchor left re-arms the active bag's saved ratio, so espresso → tea → espresso keeps a ratio saved on the bean.

## 2. Tests

- [x] 2.1 Extend `profileSwitchKeepsRatioClearsAbsolute`: a ratio clears onto a tea profile (target = profile's 0 g), a ratio dialed on the tea profile applies, and a bean's saved ratio returns after a tea round trip.
- [x] 2.2 Break each half of the fix and watch its assertions go red.
- [x] 2.3 Full suite green via Qt Creator.

## 3. Docs

- [x] 3.1 `docs/CLAUDE_MD/RECIPES.md` yield paragraph.
- [x] 3.2 Wiki manual, Brew Settings section: "A ratio survives a switch to another espresso profile." Same for FAQ.
