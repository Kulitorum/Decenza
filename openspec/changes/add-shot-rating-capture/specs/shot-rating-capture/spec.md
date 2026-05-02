# shot-rating-capture — Delta

## ADDED Requirements

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

### Requirement: Inferred-good shots SHALL be auto-rated with `enjoymentSource == "inferred"`

The post-shot analysis pipeline SHALL evaluate "inferred-good" criteria for every shot saved with `enjoyment0to100 == 0`. When ALL of the following hold, the shot SHALL be auto-rated:

- `verdictCategory == "clean"` (no truncation, no skip-first-frame),
- `channelingSeverity == "none"`,
- `grindDirection == "onTarget"`,
- ratio within `0.1` of the profile's `targetRatio` (when set), OR yield within `0.5g` of the profile's target weight when no ratio target,
- duration within ±15% of the profile's median duration over the most recent 5 *user-rated* shots on the same profile, falling back to ±15% of `targetDurationSec` when no user-rated history exists.

Auto-rating sets:

- `enjoyment0to100 = 75`,
- `enjoymentSource = "inferred"`.

Manually rated shots SHALL never be overwritten by the inferred-good evaluator. When the user manually rates an inferred shot via the editor or `QuickRatingRow`, the write SHALL set `enjoymentSource = "user"` and replace the inferred score with the user's value.

#### Scenario: Inferred-good criteria all match → auto-rate

- **GIVEN** a saved shot with `enjoyment0to100 == 0`
- **AND** detector results: `verdictCategory == "clean"`, `channelingSeverity == "none"`, `grindDirection == "onTarget"`
- **AND** ratio = 2.05 (target ratio = 2.0; within 0.1)
- **AND** duration = 33s (median of last 5 rated shots = 32s; within ±15%)
- **WHEN** the post-shot analysis pipeline runs
- **THEN** the shot's `enjoyment0to100` SHALL be persisted as `75`
- **AND** `enjoymentSource` SHALL be persisted as `"inferred"`

#### Scenario: One criterion fails → no auto-rate

- **GIVEN** the same shot but with `channelingSeverity == "transient"`
- **WHEN** the pipeline runs
- **THEN** `enjoyment0to100` SHALL remain `0`
- **AND** `enjoymentSource` SHALL remain `"none"`

#### Scenario: User rating is never overwritten

- **GIVEN** a shot with `enjoyment0to100 == 90` and `enjoymentSource == "user"`
- **WHEN** the inferred-good evaluator runs (e.g., on a recompute trigger)
- **THEN** the rating SHALL remain `90` with `enjoymentSource == "user"`

#### Scenario: User manually re-rates an inferred shot

- **GIVEN** a shot with `enjoyment0to100 == 75` and `enjoymentSource == "inferred"`
- **WHEN** the user taps the medium-face icon on `QuickRatingRow` (60)
- **THEN** the shot's `enjoyment0to100` SHALL be persisted as `60`
- **AND** `enjoymentSource` SHALL be persisted as `"user"`

### Requirement: `enjoymentSource` SHALL persist as a `ShotProjection` field with values `"none" | "user" | "inferred"`

`ShotProjection` SHALL gain an `enjoymentSource` field with three string values:

- `"none"` — no rating recorded; `enjoyment0to100` is `0`.
- `"user"` — rating set by the user (manual editor, conversational reply, or `QuickRatingRow`).
- `"inferred"` — rating set by the inferred-good evaluator.

The schema migration on `shot_history` SHALL add `enjoyment_source TEXT NOT NULL DEFAULT 'none'` and back-fill `'user'` for rows where `enjoyment > 0`. The migration SHALL be idempotent (guarded by column existence).

`ShotHistoryStorage::updateShotMetadataStatic` SHALL accept `enjoymentSource` in its metadata map and persist it. When the metadata map carries `enjoyment` but not `enjoymentSource`, the storage layer SHALL default `enjoymentSource` to `"user"` (any explicit user-driven write is a user rating; only the inferred-good evaluator passes `"inferred"`).

#### Scenario: Schema migration back-fills user-rated rows

- **GIVEN** a pre-migration `shot_history` table containing rows with `enjoyment` values 0, 75, and 0
- **WHEN** the migration runs
- **THEN** all rows SHALL have an `enjoyment_source` column
- **AND** the row with `enjoyment == 75` SHALL have `enjoyment_source == 'user'`
- **AND** the rows with `enjoyment == 0` SHALL have `enjoyment_source == 'none'`

#### Scenario: Re-running the migration is idempotent

- **GIVEN** a `shot_history` table that already has the `enjoyment_source` column populated
- **WHEN** the migration runs again (e.g., next app launch)
- **THEN** the migration SHALL detect the column exists and skip the ALTER + UPDATE
- **AND** existing `enjoyment_source` values SHALL NOT be touched

#### Scenario: User write defaults enjoymentSource to "user"

- **GIVEN** a metadata-update payload `{enjoyment: 80, espressoNotes: "fine"}` (no `enjoymentSource` key)
- **WHEN** `ShotHistoryStorage::updateShotMetadataStatic` runs
- **THEN** the persisted `enjoyment_source` SHALL be `'user'`

#### Scenario: Inferred write sets enjoymentSource to "inferred"

- **GIVEN** a metadata-update payload `{enjoyment: 75, enjoymentSource: "inferred"}`
- **WHEN** `ShotHistoryStorage::updateShotMetadataStatic` runs
- **THEN** the persisted `enjoyment_source` SHALL be `'inferred'`

### Requirement: `bestRecentShot` SHALL surface a `confidence` field and prefer user ratings over inferred

The `bestRecentShot` block in the dialing-context envelope (and the in-app advisor's user-prompt envelope) SHALL gain a `confidence` field with two values:

- `"user_rated"` — the resolved candidate has `enjoymentSource == "user"`.
- `"inferred"` — the resolved candidate has `enjoymentSource == "inferred"`.

When BOTH user-rated and inferred candidates exist in the 90-day window, the user-rated candidate SHALL win regardless of score. Inferred candidates SHALL be considered only when no user-rated candidate exists in the window. Within each tier, the highest-scoring candidate SHALL win.

When the resolved candidate's `enjoymentSource == "none"`, the candidate SHALL NOT be eligible for `bestRecentShot` — the existing gate of `enjoyment > 0` is preserved, and a `none`-source row cannot have `enjoyment > 0` by construction.

#### Scenario: User-rated candidate beats higher-scored inferred candidate

- **GIVEN** the 90-day window contains one shot with `enjoyment = 70, enjoymentSource = 'user'` and one shot with `enjoyment = 85, enjoymentSource = 'inferred'`
- **WHEN** the `bestRecentShot` block is built
- **THEN** the resolved candidate SHALL be the score-70 shot
- **AND** `confidence` SHALL be `"user_rated"`

#### Scenario: Only inferred candidates → highest inferred wins

- **GIVEN** the 90-day window contains no user-rated shots, two inferred shots scored 75 and 80
- **WHEN** the block is built
- **THEN** the resolved candidate SHALL be the score-80 shot
- **AND** `confidence` SHALL be `"inferred"`

#### Scenario: System prompt teaches confidence semantics

- **GIVEN** the espresso `shotAnalysisSystemPrompt` output
- **WHEN** the prompt is rendered
- **THEN** it SHALL contain teaching for `bestRecentShot.confidence`
- **AND** SHALL state that `inferred` SHOULD be treated as a hint that requires user confirmation before strong anchoring

### Requirement: `currentBean` and `dialInSessions[].shots[]` SHALL surface `enjoymentSource` only when "inferred"

The `currentBean` block SHALL emit `enjoymentSource` ONLY when the resolved shot's source is `"inferred"`. When the resolved shot's source is `"user"` or `"none"`, the field SHALL be omitted (the LLM does not need teaching when the rating is user-grounded or absent — the existing `tastingFeedback.hasEnjoymentScore` boolean covers the absence case).

Per-shot entries in `dialInSessions[].shots[]` SHALL similarly emit `enjoymentSource: "inferred"` only on shots with that source. User-rated and unrated shots SHALL NOT carry the field.

#### Scenario: currentBean omits enjoymentSource for user-rated resolved shot

- **GIVEN** a resolved shot with `enjoymentSource == "user"` and `enjoyment0to100 == 80`
- **WHEN** the `currentBean` block is built
- **THEN** `currentBean` SHALL NOT carry an `enjoymentSource` key

#### Scenario: currentBean carries enjoymentSource on inferred resolved shot

- **GIVEN** a resolved shot with `enjoymentSource == "inferred"` and `enjoyment0to100 == 75`
- **WHEN** the `currentBean` block is built
- **THEN** `currentBean.enjoymentSource` SHALL be `"inferred"`

#### Scenario: dialInSessions per-shot enjoymentSource is sparse

- **GIVEN** a session of 3 shots: shot A user-rated, shot B inferred-rated, shot C unrated
- **WHEN** `dialInSessions[].shots[]` is built
- **THEN** only shot B's entry SHALL carry `enjoymentSource: "inferred"`
- **AND** shots A and C SHALL NOT carry the field
