## MODIFIED Requirements

### Requirement: Settings Search Index
The app SHALL maintain a static JS array of searchable settings entries (`qml/components/SettingsSearchIndex.js`). Each entry SHALL contain `title`, `description` and a `keywords` array, and SHALL route either to a card (`tabId` and `cardId`) or to an `externalRoute` that SettingsPage handles. Every card on a settings tab that has an `objectName` SHALL have an entry, and every entry's `cardId` SHALL match a card's `objectName` in its tab. Cards on debug-only tabs are not indexed. `scripts/check_settings_search_index.py` enforces this on every pull request.

#### Scenario: New setting is searchable
- **WHEN** a developer adds a new setting card with an `objectName` to any tab
- **THEN** they add a corresponding entry to the search index with title, description, and keyword synonyms
- **AND** the per-PR check fails until they do

#### Scenario: Stale entry is caught
- **WHEN** a card's `objectName` is renamed or the card is removed while its index entry remains
- **THEN** the per-PR check fails, because selecting that result would open the tab and highlight nothing
