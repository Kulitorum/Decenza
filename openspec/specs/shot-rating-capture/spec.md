# shot-rating-capture Specification

## Purpose

Capture a taste rating (`enjoyment0to100`) for as many shots as possible with the lowest friction, so downstream features (dialing advisor, best-shot selection, recipe recommendations) have user-grounded signal. Two complementary entry paths exist:

- **Layer 1 — Conversational capture.** When the AI advisor asks the user how a shot tasted and the user replies with a numeric score (e.g., `"82"` or `"82, balanced and sweet"`), the score is parsed and persisted to the linked shot, with any trailing prose stored as `espressoNotes`.
- **Layer 2 — Quick rating row.** `PostShotReviewPage.qml` shows a low-friction three-icon row (high/medium/low → 80/60/40) above the metadata fold for unrated shots, with a per-shot dismiss control so the nudge never becomes nag-ware.

Ratings written by either path are user ratings — the system never infers a score from detector output. The precision slider in the metadata editor remains available for users who want a number other than 40/60/80.
## Requirements
### Requirement: Conversational user replies SHALL persist ratings back to the shot
When the user replies to a turn whose prior assistant message asked for taste feedback, the reply contains a parseable numeric score, and the turn has a non-zero `shotId`, the score SHALL be persisted to `ShotProjection.enjoyment0to100` for that shot. The remaining reply text (score removed) SHALL be persisted to `espressoNotes` when non-empty. When `shotId == 0`, the reply SHALL NOT be persisted; the linkage is the load-bearing precondition.

#### Scenario: User replies with a bare score → persisted

- **GIVEN** a conversation turn where `shotIdForTurn(latest user)` returns 8473
- **AND** the prior assistant message asked "How did this shot taste? Please give a 1-100 score and a couple of notes."
- **WHEN** the user replies `"82"`
- **THEN** `ShotProjection(8473).enjoyment0to100` SHALL be persisted as `82`
- **AND** `espressoNotes` SHALL NOT be overwritten (no remaining text)

#### Scenario: User replies with score + notes → both persisted

- **GIVEN** the same setup
- **WHEN** the user replies `"82, balanced and sweet"`
- **THEN** `ShotProjection(8473).enjoyment0to100` SHALL be persisted as `82`
- **AND** `espressoNotes` SHALL be persisted as `"balanced and sweet"` (leading/trailing punctuation and whitespace trimmed)

#### Scenario: User replies with prose only → no persistence

- **GIVEN** the same setup
- **WHEN** the user replies `"really good, much better than last time"`
- **THEN** `ShotProjection(8473).enjoyment0to100` SHALL NOT change
- **AND** `espressoNotes` SHALL NOT change
- **AND** no warning SHALL be logged (this is normal LLM-asks-for-number-user-gives-prose flow)

#### Scenario: shotId absent → no persistence

- **GIVEN** the same setup but `shotIdForTurn(latest user)` returns 0
- **WHEN** the user replies `"82"`
- **THEN** no DB write SHALL occur
- **AND** no warning SHALL be logged (this is normal for legacy conversations)

### Requirement: Score parsing is permissive but conservative
A bare integer token in [1, 100] SHALL count as a score. The token MAY be followed by `/100`, `out of 100` or `%`, which SHALL be consumed and discarded. Decimal scores SHALL be accepted and rounded to the nearest integer. Non-numeric tokens SHALL NOT be inferred as scores, and out-of-range numbers (`0`, `150`, `-5`) SHALL NOT be accepted. When a message contains several numeric tokens, the FIRST in-range token SHALL be the score.

#### Scenario: Prose praise yields no score
- **WHEN** the reply is "really good", "loved it" or "better than last time"
- **THEN** no score is parsed

### Requirement: Rating writes go through the metadata path
Rating and notes writes SHALL go through the existing `ShotHistoryStorage::updateShotMetadataStatic` path. A failed write SHALL log a warning, and the conversation flow SHALL continue uninterrupted.

#### Scenario: Write failure does not stop the conversation
- **WHEN** the metadata write fails
- **THEN** a warning is logged and the conversation continues

### Requirement: A saved shot SHALL be unrated, and no setting SHALL supply a rating
A shot that has just been pulled SHALL persist with `enjoyment0to100 == 0` (unrated). A rating SHALL originate only from a person, via one of this capability's capture paths, the AI taste intake, or `shots_update enjoyment0to100`.

#### Scenario: Freshly pulled shot saves unrated

- **WHEN** a shot completes and is saved to history
- **THEN** the persisted `enjoyment0to100` SHALL be `0`
- **AND** the value SHALL NOT depend on any key present in the settings store

#### Scenario: Legacy rating keys are evicted from the store

- **GIVEN** a settings store upgraded from a build that had the default-rating
  feature, carrying `shot/defaultRating` and/or `dye/espressoEnjoyment`
- **WHEN** `Settings` is constructed
- **THEN** both keys SHALL be absent from the store afterwards
- **AND** constructing `Settings` again SHALL be a no-op

#### Scenario: Unrated shot leaves Visualizer enjoyment unset

- **GIVEN** a saved shot with `enjoyment0to100 == 0`
- **WHEN** it is uploaded or PATCHed to Visualizer
- **THEN** `espresso_enjoyment` SHALL be sent as `null` (or omitted on create),
  never as literal `0`
- **AND** the shot SHALL therefore display as Unrated, not as "Rated 0/100"

### Requirement: No setting SHALL supply a shot rating
No setting SHALL supply a shot rating. `Settings` SHALL NOT have a default-rating or sticky enjoyment field, and the shot-save path SHALL NOT read a rating from `Settings` in any form. The keys `shot/defaultRating` and `dye/espressoEnjoyment` SHALL be removed from the settings store, not merely left unread.

#### Scenario: Stale rating setting cannot stamp a shot
- **WHEN** a shot finishes after a former rating key held a value
- **THEN** the shot saves unrated

### Requirement: Migration 16 SHALL reset inferred ratings to unrated
The one-time migration that drops `enjoyment_source` SHALL reset every row with `enjoyment_source = 'inferred'` to `enjoyment = 0`, and SHALL NOT read `shot/defaultRating` or any other setting to choose that value. Rows a person rated SHALL be left untouched, including a rating taken from a since-removed default.

#### Scenario: Inferred rows reset regardless of any stale default

- **GIVEN** a database at schema version 15 with an `enjoyment_source` column
- **AND** a stale `shot/defaultRating` value present in the settings store
- **WHEN** migration 16 runs
- **THEN** every `enjoyment_source = 'inferred'` row SHALL have `enjoyment == 0`
- **AND** the stale setting value SHALL have had no effect on the result

#### Scenario: User-rated rows survive the migration

- **GIVEN** a row with `enjoyment = 90` and `enjoyment_source = 'user'`
- **WHEN** migration 16 runs
- **THEN** that row's `enjoyment` SHALL remain `90`

### Requirement: Post-shot review SHALL surface a rating row above the metadata fold
`PostShotReviewPage.qml` SHALL display a rating row above the metadata editor: a "How was this shot?" label and the shared `RatingInput` component. The row SHALL be shown for every shot, rated or not, with no visibility gate and no dismiss control.

#### Scenario: Rating row visible on an unrated shot

- **GIVEN** a shot with `enjoyment0to100 == 0`
- **WHEN** the user opens `PostShotReviewPage` for that shot
- **THEN** the `"How was this shot?"` label and `RatingInput` SHALL be visible
- **AND** the shot SHALL remain unrated until the user acts

#### Scenario: User taps a preset → score persisted

- **GIVEN** the rating row is visible on an unrated shot
- **WHEN** the user taps the `75` preset
- **THEN** `editEnjoyment` SHALL be set to `75`
- **AND** the value SHALL be persisted through the page's autosave path

#### Scenario: Displaying the row does not rate the shot

- **GIVEN** a shot with `enjoyment0to100 == 0`
- **WHEN** `PostShotReviewPage` is opened and closed without the user touching
  the rating row
- **THEN** no rating SHALL be written
- **AND** the shot SHALL still satisfy the taste-intake gate as unrated

### Requirement: RatingInput offers coarse and precise entry
`RatingInput` SHALL offer preset buttons for 25, 50, 75 and 100 that each write their value directly, and a continuous 0-100 slider for any other number. It SHALL support keyboard adjustment (arrows by 1, PageUp/PageDown by 25). Setting a value by any path SHALL update `editEnjoyment` and persist through the page's autosave path.

#### Scenario: Keyboard adjusts the rating
- **WHEN** the rating control has focus and the user presses an arrow key or PageUp/PageDown
- **THEN** the value changes by 1 or 25 respectively and persists through autosave

### Requirement: Displaying the rating row never rates a shot
`RatingInput` SHALL NOT emit a value on construction or when its bound value changes programmatically. Only a deliberate user gesture SHALL write a rating.

#### Scenario: Programmatic value change emits nothing
- **WHEN** the bound value of `RatingInput` changes programmatically
- **THEN** no value is emitted and no rating is written

### Requirement: RatingInput follows project conventions
`RatingInput` SHALL be registered in `CMakeLists.txt`'s `qt_add_qml_module` list, styled via `Theme.*`, translated via `TranslationManager.translate(...)` or `Tr`, and accessible per `docs/CLAUDE_MD/ACCESSIBILITY.md`: `Accessible.role: Slider`, an `Accessible.name` carrying the current value, `Accessible.focusable: true`, and a description naming both interaction methods.

#### Scenario: Control is announced as a slider
- **WHEN** a screen reader focuses `RatingInput`
- **THEN** it is announced as a slider with its current value and both interaction methods

