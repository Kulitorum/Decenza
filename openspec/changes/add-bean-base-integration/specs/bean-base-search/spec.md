## ADDED Requirements

### Requirement: Search bar visibility is gated on API key presence and existing link state

The BeanInfoPage SHALL render the Bean Base search bar only when at least one of the following is true: (a) `Settings.beanbase.beanBaseApiKey` is non-empty, or (b) the current bean preset / DYE record carries a non-empty `beanBaseId`. When neither condition holds, the page SHALL render identically to a build without Bean Base integration.

#### Scenario: User has no API key and no existing link
- **WHEN** the BeanInfoPage opens with `Settings.beanbase.beanBaseApiKey` empty and `dyeBeanBaseId` empty
- **THEN** no Bean Base UI is rendered above the Bean section
- **AND** the Roaster + Coffee fields behave exactly as in a build without this change

#### Scenario: User has no API key but a linked preset
- **WHEN** the BeanInfoPage opens with `Settings.beanbase.beanBaseApiKey` empty and `dyeBeanBaseId` non-empty
- **THEN** a "✓ Linked to Bean Base" indicator is rendered above the Bean section
- **AND** the search input is NOT rendered
- **AND** no Unlink or Replace affordance is offered (user cannot mutate the link without a key)

#### Scenario: User has an API key but no link
- **WHEN** the BeanInfoPage opens with a non-empty API key and `dyeBeanBaseId` empty
- **THEN** the search input "Search Loffee Labs Bean Base" is rendered at the top of the Bean section
- **AND** typing into it triggers debounced Bean Base searches

#### Scenario: User has both key and link
- **WHEN** the BeanInfoPage opens with both a non-empty API key and `dyeBeanBaseId` set
- **THEN** the search input is rendered with the linked bean's display name pre-filled
- **AND** a "✓ Linked", "Unlink", and "🔗 open URL" affordance is visible
- **AND** typing into the search input transitions back to "search mode" (the link is cleared and the dropdown re-opens)

### Requirement: Selecting a search result populates DYE fields per a fixed mapping

When the user picks a Bean Base entry from the dropdown, the system SHALL apply its fields to the active DYE record using the documented field-mapping table.

#### Scenario: Roaster and Coffee become verified
- **WHEN** the user picks a Bean Base entry with `roaster = "Prodigal Coffee"` and `roast-name = "Buenos Aires Caturra - Colombia, washed"`
- **THEN** `dyeBeanBrand` is set to "Prodigal Coffee" and `dyeBeanType` is set to the roast name
- **AND** the Roaster and Coffee `StyledTextField`s are rendered with `enabled: false` and a "verified" visual treatment
- **AND** the `↑` distinct-values suggestion arrow is hidden on those two fields

#### Scenario: Roast level is filled but stays editable
- **WHEN** the picked entry has `degree = "Medium"`
- **THEN** `dyeRoastLevel` is set to "Medium"
- **AND** the Roast level dropdown remains enabled so the user can override

#### Scenario: Roast date is not touched
- **WHEN** the user picks a Bean Base entry that has a `date` field
- **THEN** `dyeRoastDate` is NOT modified (Bean Base `date` is the bean release date, not the user's bag roast date)

#### Scenario: Cached attributes flow to upload and advisor without on-page UI
- **WHEN** a Bean Base entry is applied
- **THEN** the entry's `origin`, `region`, `variety`, `process`, `producer`, `min-elev`, `max-elev`, `tasting-tag`, `tasting` (notes), `link`, `image`, `type`, `general-tag` are cached on the active preset
- **AND** these cached values are sent on the next Visualizer upload (per `visualizer-bag-linkage`)
- **AND** these cached values are injected into the next AI advisor prompt (per `ai-advisor` modifications)

### Requirement: Unlinking preserves field values

When the user explicitly unlinks a previously-matched Bean Base entry, the system SHALL clear `beanBaseId`, `beanBaseRoasterId`, and the cached attribute fields, but SHALL preserve the user-visible `dyeBeanBrand`, `dyeBeanType`, and `dyeRoastLevel` values.

#### Scenario: User taps Unlink
- **WHEN** the user taps the Unlink action on a linked bean
- **THEN** `dyeBeanBaseId` and `dyeBeanBaseRoasterId` are cleared
- **AND** cached origin/variety/process/etc. fields are cleared
- **AND** `dyeBeanBrand` and `dyeBeanType` retain their current display values
- **AND** the Roaster and Coffee fields become enabled and lose the "verified" treatment

### Requirement: Search respects free-tier rate budget

The Bean Base client SHALL debounce user typing and enforce a minimum 3-second gap between requests sent to `/beans`, regardless of typing speed.

#### Scenario: User types rapidly
- **WHEN** the user types "prodigal espresso" with no pause longer than 200 ms
- **THEN** at most one request is sent for the in-progress query, fired no sooner than 800 ms after the last keystroke
- **AND** subsequent requests for the same in-progress text are coalesced into the latest version

#### Scenario: User changes the query within the cooldown window
- **WHEN** a request has just been sent and the user changes the query within 3 seconds
- **THEN** the new request is queued and fires when 3 seconds have elapsed since the prior send
- **AND** if multiple new queries arrive during the cooldown, only the most recent one is fired

#### Scenario: Repeated query hits the cache
- **WHEN** the user types a query string that was already searched in this session
- **THEN** results are rendered from the cache without sending a new request

### Requirement: Loffee Labs branding in the search label

The system SHALL render the search field label as the verbatim string "Search Loffee Labs Bean Base", matching Visualizer's UI. This label SHALL NOT be abbreviated to "Search Bean Base" or otherwise shortened.

#### Scenario: Label is unabbreviated
- **WHEN** the search bar is visible in any state
- **THEN** the label reads "Search Loffee Labs Bean Base"
