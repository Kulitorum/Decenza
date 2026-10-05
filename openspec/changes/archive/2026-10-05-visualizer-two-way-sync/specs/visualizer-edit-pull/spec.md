## ADDED Requirements

### Requirement: Shot edits made on Visualizer are pulled into Decenza

While Visualizer is connected and automatic update is on, the application SHALL periodically (at startup, every 30 minutes and when an account is connected) list the shots changed on visualizer.coffee since the last complete pass (`GET /api/shots?updated_after=<cursor>&sort=updated_at`) and read each one linked to a local shot. For each pulled field — grind setting and rpm, dose, yield, TDS, EY, enjoyment, notes, barista, taste — whose remote value differs from the local value, the local value SHALL be replaced, unless that field was edited locally and not yet sent. A missing or null remote value SHALL NOT clear a local value. Grinder identity, the canonical bean link and the bean fields (brand, type, roast date, roast level) SHALL NOT be pulled: Visualizer rewrites a bag-linked shot's bean fields from its coffee bag, so their values there are not edits. The cursor SHALL be kept per account and advanced only after a complete pass in which every pulled write was saved; the first pass on an account, and an account with more changed shots than one pass reads, SHALL look back 14 days. A shot that cannot be read SHALL be skipped and logged; a failure that affects the whole account SHALL end the pass. A read that a push of the same item overtook SHALL be dropped. A pulled shot change SHALL reach the other upload destinations as a local edit would, and SHALL NOT be pushed back to Visualizer. Requests SHALL be paced at the shared Visualizer API interval.

#### Scenario: Journal edit comes back
- **GIVEN** a shot uploaded from Decenza
- **WHEN** the user changes its grind setting and rating in Visualizer's Journal and the next pass runs
- **THEN** the local shot carries the new grind setting and rating

#### Scenario: An unsent local edit wins
- **GIVEN** the user changed a shot's rating in Decenza with automatic update off
- **WHEN** a pass reads a different rating for that shot from Visualizer
- **THEN** the local rating is kept, and other fields that differ are taken from Visualizer

#### Scenario: Absence is not a clear
- **WHEN** a pulled shot has no barista or CVA scores on Visualizer
- **THEN** the local barista and taste are unchanged

#### Scenario: Bean fields rewritten by Visualizer
- **GIVEN** a shot linked to a Coffee Management bag, whose roast date Visualizer stores in the user's display format
- **WHEN** a pass reads the shot
- **THEN** the local bean brand, type, roast date and roast level are unchanged

#### Scenario: Pass fails part-way
- **WHEN** a pass stops on an offline network or a refused request
- **THEN** the cursor is not advanced and the next pass reads the same shots again, writing nothing already applied

#### Scenario: One shot will not read
- **WHEN** one changed shot's read fails with a response specific to that shot
- **THEN** the pass skips it, logs it, and reads the rest

### Requirement: Coffee bag archive state syncs both ways

The application SHALL keep, per synced bag, the `archived_at` visualizer.coffee was last known to hold. Each pass SHALL read every bag's `archived_at` from the bag list; when it differs from the known value, an archive SHALL mark the local bag finished, a restore SHALL return it to inventory, and the new value SHALL be recorded. A bag whose archive state has never been seen SHALL have it recorded without acting on it. A bag push SHALL carry `archived_at` — the current time when the bag is finished here and active there, `null` when it is in inventory here and archived there — only when the two disagree, and SHALL record the value Visualizer returns. Pulled changes SHALL NOT be pushed back.

#### Scenario: Archived on Visualizer
- **WHEN** the user archives a synced bag on visualizer.coffee and the next pass runs
- **THEN** the bag is marked finished in Decenza, as if Bag Finished had been tapped

#### Scenario: Finished in Decenza
- **WHEN** the user taps Bag Finished on a synced bag in Decenza
- **THEN** the bag is archived on visualizer.coffee

#### Scenario: First sight is recorded, not acted on
- **GIVEN** a bag archived on Visualizer before this sync existed and still in use in Decenza
- **WHEN** the first pass reads it
- **THEN** the bag stays in inventory in Decenza

#### Scenario: An edit does not undo an unpulled archive
- **GIVEN** a bag archived on Visualizer since the last pass
- **WHEN** the user edits its tasting notes in Decenza before the next pass
- **THEN** the push carries the notes and no `archived_at`, and the bag stays archived there

### Requirement: Other coffee bag state on Visualizer is pulled into Decenza

During the same pass, the application SHALL read each synced bag still in inventory (`GET /api/coffee_bags/:id`). A pulled field (roast date, roast level, frozen and defrosted dates, notes, and the descriptive attributes) whose remote value differs from the value Visualizer was last known to hold SHALL be taken when the local value still equals that last-known value, and kept when it was also changed locally. A field never seen SHALL only fill a local blank. Either way the remote value SHALL be recorded as seen. The decision SHALL be made against the bag row as it stands when the write runs. A bag photo that one side has and the other lacks SHALL be copied to the other; neither side's photo SHALL be replaced. Pulled changes SHALL NOT be pushed back.

#### Scenario: Frozen on Visualizer
- **WHEN** the user taps Freeze on a synced bag on visualizer.coffee
- **THEN** the local bag's frozen date becomes that date and its defrost date is cleared

#### Scenario: A local clear is not undone
- **GIVEN** a bag whose tasting notes the user cleared in Decenza
- **WHEN** a pass reads the bag before that clear has reached Visualizer, or after
- **THEN** the local tasting notes stay empty

#### Scenario: Photo only in Decenza
- **WHEN** a synced bag has a photo in Decenza and none on Visualizer
- **THEN** the photo is uploaded to the Visualizer bag

### Requirement: Synced data is fresh when viewed

A screen showing a Visualizer-linked shot (post-shot review, shot detail, the web shot page) SHALL read that shot from Visualizer when it opens; the bag editor SHALL read its bag; the bean inventory (in the app and on the web) SHALL read every bag's archive state and the bags in use, unless the bags were read in the last 3 minutes. An editor SHALL save only the fields edited in it, and while open SHALL take a pulled change into any field not yet edited; an undo SHALL NOT restore the value a pull replaced. Background requests SHALL share one pacer, so concurrent passes together keep to the request interval.

#### Scenario: Review page opened after a Journal edit
- **GIVEN** the user changed a shot's rating in Visualizer's Journal a minute ago
- **WHEN** the user opens that shot's review page in Decenza
- **THEN** within a few seconds the page shows the new rating, without waiting for the next pass

#### Scenario: Editing one field does not revert another
- **GIVEN** the review page is open and a pull changes the shot's grind setting
- **WHEN** the user then changes the rating
- **THEN** the grind field shows the pulled value, and the save writes and sends only the rating

#### Scenario: Undo after a pull
- **GIVEN** the review page is open, the user changed the rating, and a pull then changed the grind setting
- **WHEN** the user taps Undo
- **THEN** the rating reverts and the grind setting keeps the pulled value

#### Scenario: Shot list shrinks mid-pass
- **WHEN** a shot is deleted on Visualizer while a pass is paging the changed-shot list
- **THEN** the pass does not advance its cursor, so no changed shot is skipped
