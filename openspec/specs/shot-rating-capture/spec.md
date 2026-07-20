# shot-rating-capture Specification

## Purpose

Capture a taste rating (`enjoyment0to100`) for as many shots as possible with the lowest friction, so downstream features (dialing advisor, best-shot selection, recipe recommendations) have user-grounded signal. Two complementary entry paths exist:

- **Layer 1 — Conversational capture.** When the AI advisor asks the user how a shot tasted and the user replies with a numeric score (e.g., `"82"` or `"82, balanced and sweet"`), the score is parsed and persisted to the linked shot, with any trailing prose stored as `espressoNotes`.
- **Layer 2 — Quick rating row.** `PostShotReviewPage.qml` shows a low-friction three-icon row (high/medium/low → 80/60/40) above the metadata fold for unrated shots, with a per-shot dismiss control so the nudge never becomes nag-ware.

Ratings written by either path are user ratings — the system never infers a score from detector output. The precision slider in the metadata editor remains available for users who want a number other than 40/60/80.
## Requirements
### Requirement: Conversational user replies SHALL persist ratings back to the shot

When the user replies to a conversation turn whose prior assistant message asked for taste feedback, AND the user's reply contains a parseable numeric score (1-100), AND the conversation turn pair has a non-zero `shotId` recorded (per #1053's per-turn linkage), the score SHALL be persisted to `ShotProjection.enjoyment0to100` for that shot. The remaining text of the reply (with the score token removed) SHALL be persisted to `espressoNotes` when non-empty.

The score parser SHALL be permissive but conservative:

- A bare integer token in `[1, 100]` SHALL count as a score.
- The token MAY be followed by `/100`, `out of 100`, or `%` (the suffix consumed and discarded).
- Decimal scores (e.g., `82.5`) SHALL be accepted and rounded to the nearest integer.
- Non-numeric tokens SHALL NOT be inferred as scores. `"really good"`, `"loved it"`, `"better than last time"` SHALL all yield no score.
- Out-of-range numbers (`0`, `150`, `-5`) SHALL NOT be accepted.
- When the message contains multiple numeric tokens, the FIRST in-range token SHALL be taken as the score.

The write SHALL go through the existing `ShotHistoryStorage::updateShotMetadataStatic` path. Failures SHALL log a warning; the conversation flow SHALL continue uninterrupted.

When `shotId == 0` (legacy conversation, free-form follow-up, or pre-#1053 saved conversations) the reply SHALL NOT be persisted. The linkage is the load-bearing precondition.

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

### Requirement: A saved shot SHALL be unrated, and no setting SHALL supply a rating

A shot that has just been pulled has not been tasted, so it SHALL persist with
`enjoyment0to100 == 0` (unrated). A rating SHALL originate only from a person, via
one of the capture paths in this capability, the AI taste intake, or
`shots_update enjoyment0to100`.

No setting SHALL supply a shot rating. There SHALL be no "default shot rating"
setting and no sticky enjoyment field in `Settings`, and the shot-save path SHALL
NOT read a rating from `Settings` in any form. A rating held in settings is
per-session state that lands on whichever shot happens to finish next, which is
the defect this requirement exists to prevent: `dyeEspressoEnjoyment` was read at
save time and reset to 0 only afterwards, so the last value it held stamped
exactly one further shot before self-healing — invisible in testing, and enough to
suppress that shot's taste intake permanently.

The keys `shot/defaultRating` and `dye/espressoEnjoyment` SHALL be removed from
the settings store rather than merely left unread, so no stale rating remains
available to leak.

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

### Requirement: Migration 16 SHALL reset inferred ratings to unrated

The one-time migration that drops `enjoyment_source` SHALL reset every row with
`enjoyment_source = 'inferred'` to `enjoyment = 0`. It SHALL NOT read
`shot/defaultRating` or any other setting to choose that value.

Resetting these rows does not destroy user data: `enjoyment_source = 'inferred'`
means the app computed the score and no person chose it. Rows carrying a rating a
person set — including one produced by the user's own configured default while
that feature existed — SHALL be left untouched.

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

`PostShotReviewPage.qml` SHALL display a rating row above the metadata editor
consisting of a `"How was this shot?"` label and the shared `RatingInput`
component. The row SHALL be shown for every shot, rated or not — there is no
visibility gate and no dismiss control, because the row is one line of chrome
rather than a nudge that needs suppressing.

`RatingInput` SHALL offer both a coarse and a precise path to the same value:

- preset buttons for 25 / 50 / 75 / 100, each writing its value directly,
- a continuous slider over 0-100 for any other number,
- keyboard adjustment (arrows by 1, PageUp/PageDown by 25).

Setting a value by any path SHALL update `editEnjoyment` and persist through the
page's autosave path. `RatingInput` SHALL NOT emit a value on construction or when
its bound value changes programmatically — only a deliberate user gesture writes a
rating, so that displaying the row on an unrated shot cannot itself rate it.

The component SHALL follow project conventions:

- registered in `CMakeLists.txt`'s `qt_add_qml_module` file list,
- styled via `Theme.*` (no hardcoded colors / spacings),
- text via `TranslationManager.translate(...)` or `Tr` components,
- accessibility per `docs/CLAUDE_MD/ACCESSIBILITY.md`: `Accessible.role: Slider`,
  an `Accessible.name` carrying the current value, `Accessible.focusable: true`,
  and a description naming both interaction methods.

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

