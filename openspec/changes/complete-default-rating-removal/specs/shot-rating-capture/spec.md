## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Post-shot review SHALL surface a low-friction rating row above the metadata fold

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
