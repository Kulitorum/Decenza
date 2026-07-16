# Spec Delta — taste-intake (NEW capability)

## ADDED Requirements

### Requirement: Tap-only taste intake on first advisor open

The system SHALL present a text-free, tap-only taste intake the first time the AI
advisor is opened for a given shot, when `Settings.ai.tasteIntakeOnAsk` is true.
The intake SHALL contain no free-text input of any kind. It SHALL offer up to
three optional rows — Extraction (`Sour` / `Balanced` / `Bitter`), Body (`Thin` /
`Medium` / `Heavy`), and Overall (the existing `RatingInput`, values
25/50/75/100) — an "Ask" action, and a "Skip" action.

#### Scenario: First open with an unrated, un-tasted shot
- **WHEN** the user opens the advisor for a shot that has no enjoyment score and no taste/body set, with the setting ON
- **THEN** the intake SHALL show all three rows and an "Ask" button
- **AND** no keyboard or text field SHALL be presented

#### Scenario: Setting off
- **WHEN** `Settings.ai.tasteIntakeOnAsk` is false
- **THEN** opening the advisor SHALL go directly to the text conversation with the shot attached, with no intake shown

#### Scenario: Intake gates on first open only
- **WHEN** the user has already been shown the intake for a shot (whether they tapped Ask or Skip)
- **THEN** subsequent opens of the advisor for that same shot SHALL NOT show the intake again

### Requirement: Adaptive display of unfilled axes only

The intake SHALL display only the axes whose values are unset. Overall SHALL be
shown only when `enjoyment0to100 == 0`; Taste only when `taste_balance` is empty;
Body only when `taste_body` is empty. When all three are already set, the intake
SHALL collapse to a single "Ask → What do you think?" action.

#### Scenario: Partially rated shot
- **WHEN** the shot already has an enjoyment score but no taste/body
- **THEN** the intake SHALL show only the Taste and Body rows plus "Ask"

#### Scenario: Fully rated shot collapses to one tap
- **WHEN** the shot already has enjoyment, taste, and body set
- **THEN** the intake SHALL show only a single "Ask → What do you think?" button and no rows

### Requirement: Ask composes a question from taps and attaches the shot

The "Ask" action SHALL persist any tapped values to the shot, compose a
first-person, qualitative natural-language message from the tapped values plus a
"what do you think and how should I adjust" question, and send it with the shot
attached on the same path the AI Advice button uses today. Tapping "Ask" with
nothing selected SHALL send the plain "What do you think?" question.

#### Scenario: Ask with taste and body tapped
- **WHEN** the user taps `Sour` and `Thin` and then "Ask"
- **THEN** `taste_balance` = `sour` and `taste_body` = `thin` SHALL be persisted to the shot
- **AND** the sent message SHALL describe the taste in first person and ask for advice
- **AND** the shot data SHALL be attached to the turn

#### Scenario: Ask with nothing selected
- **WHEN** the user taps "Ask" without selecting any row
- **THEN** the plain "What do you think?" question SHALL be sent with the shot attached
- **AND** no taste/body values SHALL be written

### Requirement: Skip preserves the existing text flow

The "Skip" action SHALL close the intake and enter the normal text conversation
with the shot attached, identical to the pre-existing AI Advice behavior, writing
no taste/body values.

#### Scenario: Skip
- **WHEN** the user taps "Skip"
- **THEN** the text conversation SHALL open with the shot attached and no taste/body values written

### Requirement: Structured taste storage on the shot

The system SHALL store taste as structured shot columns `taste_balance` (values
`sour` / `balanced` / `bitter`, empty = unset) and `taste_body` (values `thin` /
`medium` / `heavy`, empty = unset), added by shots migration 33, with the
empty-string sentinel for unset. Overall SHALL continue to use the existing
`enjoyment0to100`. The same picker and the same columns SHALL be used on the
post-shot review page — there SHALL NOT be a second taste UI or a second storage
representation (e.g. encoding taste into free-text notes).

#### Scenario: Migration on an existing database
- **WHEN** a pre-33 shots database is opened
- **THEN** the `taste_balance` and `taste_body` columns SHALL be added with empty defaults and existing rows SHALL read as unset

#### Scenario: Review-page and advisor share storage
- **WHEN** the user sets taste on the post-shot review page and later opens the advisor for that shot
- **THEN** the Taste row SHALL be treated as already-filled and SHALL NOT be shown in the intake

### Requirement: Structured taste satisfies the advisor's taste-feedback gate

The advisor's "has taste feedback" signal SHALL treat a set `taste_balance` or
`taste_body` as taste feedback present, in addition to a non-zero enjoyment
score. When taste feedback is present in the shot data, the advisor SHALL NOT be
prompted to ask "how did it taste?" for that shot.

#### Scenario: Taste tapped but Overall left unrated
- **WHEN** the user taps only a Taste chip (no Overall rating) and asks
- **THEN** the advisor's first prompt SHALL reflect taste feedback as present
- **AND** the advisor SHALL NOT ask the user how the shot tasted

#### Scenario: Skip leaves feedback absent
- **WHEN** the user skips the intake and the shot has no enjoyment, taste, or body
- **THEN** taste feedback SHALL read as absent and the advisor MAY ask how the shot tasted (reactive Layer-1 capture unchanged)

### Requirement: Tap enums map to Visualizer CVA fields on upload

When a shot with taste taps is uploaded or updated to Visualizer, the system SHALL
translate the tap enums to the SCA CVA descriptive attributes (integer intensity
0–15) on the existing shot request body: `taste_balance` → `acidity` +
`bitterness` (sour = 12/4, balanced = 8/8, bitter = 4/12); `taste_body` →
`mouthfeel` (thin = 4, medium = 8, heavy = 12). Overall continues to map to
`espresso_enjoyment`. The system SHALL NOT set any CVA attribute a tap does not
speak to (`sweetness`, `aftertaste`, `aroma`, `flavor`, `fragrance`). A mapped
CVA field SHALL be written ONLY when the corresponding local taste tap is set,
and SHALL NEVER be sent as null — so a shot the user never taps in Decenza never
has its CVA attributes written or cleared, preserving anything scored by hand in
Visualizer. (The mapping does not read remote CVA state, so for a shot the user
does tap, the tap is authoritative for the mapped fields.) The mapping is
one-directional; CVA values are not reverse-mapped into a tap enum on download.

#### Scenario: Sour tap maps to acidity/bitterness
- **WHEN** a shot with `taste_balance = sour` is PATCHed to Visualizer
- **THEN** the request SHALL set `acidity = 12` and `bitterness = 4`
- **AND** `sweetness`, `aftertaste`, `aroma`, `flavor`, `fragrance` SHALL be absent from the mapped fields

#### Scenario: Untapped shot never clears hand-entered CVA
- **WHEN** a shot the user has NOT tapped in Decenza is PATCHed to Visualizer (e.g. an enjoyment/notes edit)
- **THEN** the request SHALL NOT include `acidity` / `bitterness` / `mouthfeel` (neither a value nor null)
- **AND** any CVA scores the user entered by hand in Visualizer SHALL be preserved

#### Scenario: Body tap maps to mouthfeel
- **WHEN** a shot with `taste_body = heavy` is uploaded to Visualizer
- **THEN** the request SHALL set `mouthfeel = 12`

#### Scenario: Capture-time upload when auto-sync is on
- **WHEN** the user taps a taste chip on an already-uploaded shot and `visualizerAutoUpdate` is enabled
- **THEN** the mapped CVA fields SHALL be PATCHed to Visualizer at capture time via the existing auto-update path, with no separate upload action

#### Scenario: Auto-sync off defers upload
- **WHEN** the user taps a taste chip and Visualizer auto-sync is disabled
- **THEN** the taste SHALL persist locally and upload on the next manual push, like any other shot field
