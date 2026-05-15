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

### Requirement: Post-shot review SHALL surface a low-friction rating row above the metadata fold

`PostShotReviewPage.qml` SHALL display a `QuickRatingRow` component above the metadata editor whenever the resolved shot's `enjoyment0to100 == 0` AND the per-shot dismissed flag (`shotRatingDismissed/<shotId>` in `Settings`) is unset. The row SHALL contain:

- three icon buttons mapped to default scores: high (80), medium (60), low (40),
- tapping any button SHALL set `editEnjoyment` to the mapped default and immediately invoke `saveEditedShot()` (the existing persistence path),
- a small dismiss `×` SHALL set `shotRatingDismissed/<shotId> = true` and hide the row for the current shot,
- once the user has tapped a face OR dismissed, the row SHALL collapse to a single-line "Rated 80 — tap to revise" pill (or hide entirely when dismissed),
- the existing precision slider in the metadata editor SHALL remain available for users wanting a number other than 40/60/80.

The component SHALL follow project conventions:

- registered in `CMakeLists.txt`'s `qt_add_qml_module` file list,
- styled via `Theme.*` (no hardcoded colors / spacings),
- text via `TranslationManager.translate(...)` or `Tr` components,
- accessibility per `docs/CLAUDE_MD/ACCESSIBILITY.md`: each icon button has `Accessible.role`, `Accessible.name` (e.g., `"Rate this shot good"`), `Accessible.focusable: true`, `Accessible.onPressAction`.

The dismissed flag SHALL be a per-shot QSettings key, NOT a column in the shot history table — it is a UI nudge, not a shot attribute.

#### Scenario: Unrated shot → row visible

- **GIVEN** a shot with `enjoyment0to100 == 0` and no `shotRatingDismissed/<shotId>` Settings key
- **WHEN** the user opens `PostShotReviewPage` for that shot
- **THEN** the QuickRatingRow SHALL be visible
- **AND** SHALL display three rating icons + a dismiss control

#### Scenario: User taps the high icon → score persisted, row collapses

- **GIVEN** the row is visible on an unrated shot
- **WHEN** the user taps the high icon
- **THEN** `editEnjoyment` SHALL be set to `80`
- **AND** `saveEditedShot()` SHALL be called
- **AND** the row SHALL collapse to the "Rated 80 — tap to revise" pill

#### Scenario: User dismisses → row hides, persists across reloads

- **GIVEN** the row is visible
- **WHEN** the user taps the dismiss control
- **THEN** `shotRatingDismissed/<shotId>` Settings key SHALL be set to `true`
- **AND** the row SHALL hide
- **AND** opening the same `PostShotReviewPage` again SHALL NOT re-show the row

#### Scenario: Already-rated shot → row not shown

- **GIVEN** a shot with `enjoyment0to100 == 75`
- **WHEN** the user opens `PostShotReviewPage`
- **THEN** the QuickRatingRow SHALL NOT be visible
- **AND** the row SHALL NOT be replaced by a "tap to revise" affordance — the existing metadata-editor slider remains the revision path

