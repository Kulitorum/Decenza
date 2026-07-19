## MODIFIED Requirements

### Requirement: Name auto-suggestion from bean and drink type
The wizard SHALL suggest a recipe name composed from the bean (coffee/tea name), the drink type's short label, and the recipe's profile (e.g. "Yirgacheffe Latte · Cremina" — never a label containing a slash or parenthetical), applied only while the name field is empty or still holds the previous suggestion — never over a user edit. The suggestion SHALL update whenever the bean, drink type, or profile selection changes while the field still holds a prior suggestion.

The profile token SHALL be cleaned before use: the `D-Flow/` or `A-Flow/` editor-membership title prefix SHALL be removed, and the profile SHALL NOT be appended when its trailing word repeats the drink-type word already present (case-insensitive), mirroring the bean stutter rule. When no profile is selected (a hot-water tea recipe), no profile token SHALL be appended.

When the bean name already ends with the drink-type word (case-insensitive), the suggestion SHALL NOT append the drink-type word again.

When another non-archived recipe already carries the suggested name (same bean, drink type, and cleaned profile), the wizard SHALL append a qualifier drawn from the first differing dial-in axis — the yield (ratio or target weight), else the dose — so the suggestion stays distinct. The wizard SHALL NOT disambiguate with a bare numeric counter.

#### Scenario: Suggestion follows selections
- **WHEN** the user picks a bean and drink type without typing a name
- **THEN** the name field shows the suggestion, and changing the bean, drink type, or profile updates it

#### Scenario: Profile is included from the first recipe
- **WHEN** the user builds a Latte for the bean "Yirgacheffe" on the profile "Cremina"
- **THEN** the suggestion is "Yirgacheffe Latte · Cremina"

#### Scenario: Editor prefix is stripped
- **WHEN** the selected profile's title is "D-Flow/Extractamundo"
- **THEN** the appended profile token is "Extractamundo", not "D-Flow/Extractamundo"

#### Scenario: No stuttered profile word
- **WHEN** an espresso recipe uses the profile "Blooming Espresso"
- **THEN** the suggestion does not repeat "Espresso" after the profile (no "… Espresso · Blooming Espresso")

#### Scenario: No stuttered type word
- **WHEN** the bean "Milk Blend Espresso" is picked for an espresso recipe
- **THEN** the suggestion is "Milk Blend Espresso", not "Milk Blend Espresso Espresso" (before any profile token)

#### Scenario: Short label in names
- **WHEN** the user picks Latte/Cappuccino for the bean "Gran Bar"
- **THEN** the suggestion uses "Latte", not "Latte / Cappuccino"

#### Scenario: Hot-water tea has no profile token
- **WHEN** the user builds a "Just hot water" tea recipe (no profile)
- **THEN** the suggestion is composed from the bean and drink type only, with no profile token

#### Scenario: Collision falls to a dial-in qualifier
- **WHEN** a recipe named "Yirgacheffe Espresso · Cremina" already exists and the user builds another with the same bean, drink type, and profile but a different yield
- **THEN** the suggestion appends the differing yield (ratio or target weight), not a numeric counter

#### Scenario: User edit wins
- **WHEN** the user types their own name and then changes the profile
- **THEN** the typed name is unchanged
