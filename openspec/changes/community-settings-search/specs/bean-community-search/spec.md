## ADDED Requirements

### Requirement: The Beans page surfaces the active coffee bag's own history and a community link
For the active **coffee** bag, `BeanInfoPage` SHALL show a card with the user's own ranked profiles and last
grind for that bean (from the local shot history) and a button that opens the filtered Visualizer Community page
in a browser. The card SHALL be hidden when the active bag is tea, or when the active bean has no roaster+coffee
set. No community shot data is fetched in-app in this phase; the deep link is the community path.

#### Scenario: Active coffee bag with local history
- **WHEN** a coffee bag is the active bean and the user has logged shots for it
- **THEN** the card shows up to three of the user's profiles for that bean (each with when it was last used) and,
  when available, the last grind dialed for that bean

#### Scenario: Active coffee bag with no local history
- **WHEN** a coffee bag is the active bean and the user has logged no shots for it
- **THEN** the card shows a "no shots logged yet" line and still offers the "Open on Visualizer" button

#### Scenario: Open the community page
- **WHEN** the user taps "Open on Visualizer"
- **THEN** the browser opens `visualizer.coffee/community` filtered by the bean's roaster and coffee name
  (URL-encoded) and `min_enjoyment=70`

#### Scenario: Tea or no bean
- **WHEN** the active bag is tea, or no roaster+coffee is set
- **THEN** the card is not shown (community/canonical data is coffee-only)

### Requirement: A similar-roast grind is never presented as the bean's own
When the only grind the local history can offer for the bean is from a similar roast level rather than this exact
bean, the card SHALL label it as a similar-roast grind, distinct from a grind actually dialed for this bean.

#### Scenario: Similar-roast fallback
- **WHEN** the latest-grind result's match level is `similarRoast`
- **THEN** the card labels the grind as "similar-roast", not as this bean's own grind

### Requirement: Replies from the shared history service are matched to the shown bean
The card SHALL ignore any ranked-profiles or latest-grind reply whose echoed query bean does not match the bean
currently shown, because the history service is a singleton shared with other surfaces.

#### Scenario: Stale reply for another bean
- **WHEN** a reply arrives whose echoed `queryBrand`/`queryType` differ from the card's current bean
- **THEN** the card discards it and does not render it

### Requirement: Only coffee-drink profiles are shown for a coffee bean
The card SHALL list only coffee-drink profiles for the bean, excluding tea and maintenance (cleaning/descale)
profiles, classified by the single-source beverage bucket (`Profile::beverageBucket` via
`ProfileManager::beverageBucketForTitle`, never a mapping copied into QML). The grind line SHALL be hidden when the
latest grind for the bean was dialed under a tea or maintenance profile. A profile whose bucket cannot be resolved
(e.g. a deleted profile) is shown.

#### Scenario: Tea profile used with a coffee bean
- **WHEN** the bean's history includes a shot pulled under a tea or maintenance profile
- **THEN** that profile is not listed, and a grind dialed under it is not shown

#### Scenario: Espresso/filter profiles kept
- **WHEN** the bean's history includes espresso or filter profiles
- **THEN** those are listed normally
