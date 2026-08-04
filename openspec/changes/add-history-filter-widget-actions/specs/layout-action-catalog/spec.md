## ADDED Requirements

### Requirement: The Custom widget action catalog is declared in one place

The Custom layout widget's action catalog — the set of assignable actions with, per
action: its action id string, its label (translation key + English fallback), and the page
contexts it is offered in — SHALL be declared in a single C++ table, beside the existing
widget catalog and readout capability schema. Adding, renaming or removing an action SHALL
require no per-surface list edits beyond that table (the action's runtime behaviour in
`CustomItem.executeActionString()` is still separate).

#### Scenario: One edit updates every picker

- **WHEN** an action's entry is added to or changed in the catalog table
- **THEN** the in-app Custom widget editor's action picker and the web layout editor's action picker both reflect it without either surface being edited

### Requirement: All action-catalog consumers derive from the single table

The in-app Custom widget editor's action list (`CustomEditorPopup.getFilteredActions()`)
and the web layout editor's `ACTIONS` array SHALL both consume the single catalog — the
web editor receiving it as injected JSON, by the same mechanism as the widget catalog. The
hand-maintained copies in those two files SHALL be removed along with their
keep-in-sync comments.

Actions whose picker entry is dynamically expanded at selection time (the profile picker
behind "Load Profile") SHALL keep that behaviour; only the catalog entry itself moves.

#### Scenario: No hand-synced action copies remain

- **WHEN** the action catalog changes in C++
- **THEN** no other code location must be edited for the in-app editor and the web editor to stay consistent

#### Scenario: Page-context filtering preserved

- **WHEN** the action picker is opened for a widget on a page whose context excludes some actions
- **THEN** only actions whose catalog contexts include that page are offered, exactly as before the catalog was centralized

#### Scenario: Dynamic profile action still expands

- **WHEN** a user picks the "Load Profile" action in the in-app editor
- **THEN** the profile list is still offered and the chosen profile is still encoded into the stored action string

### Requirement: Action labels remain translatable in-app

Catalog labels SHALL be stored as translation key + English fallback pairs. The in-app
picker SHALL resolve them through `TranslationManager.translate(key, fallback)`,
preserving the existing translation keys and live language-switch behaviour. The web
editor SHALL use the English fallbacks, matching its current English-only presentation.

#### Scenario: Language switch updates action labels

- **WHEN** the app language changes while the Custom widget editor is open
- **THEN** the action picker's labels update to the new language without reopening the editor

#### Scenario: Existing translation keys preserved

- **WHEN** the catalog is populated from the previously hand-listed actions
- **THEN** each action keeps the translation key it already used, so no existing translation is orphaned
