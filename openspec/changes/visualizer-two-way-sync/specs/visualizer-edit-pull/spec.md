## ADDED Requirements

### Requirement: Shot edits made on Visualizer are pulled into Decenza

While Visualizer is connected and automatic update is on, the application SHALL periodically (at startup, every 30 minutes and when an account is connected) list the shots changed on visualizer.coffee since the last complete pass (`GET /api/shots?updated_after=<cursor>&sort=updated_at`) and read each one linked to a local shot. For each pulled field — bean brand, bean type, roast date, roast level, grind setting and rpm, dose, yield, TDS, EY, enjoyment, notes, barista, taste — whose remote value differs from the local value, the local value SHALL be replaced, unless that field was edited locally and not yet sent. A missing or null remote value SHALL NOT clear a local value. Grinder identity and the canonical bean link SHALL NOT be pulled. The cursor SHALL be kept per account and advanced only after a complete pass; the first pass on an account SHALL look back 14 days. Requests SHALL be paced at the shared Visualizer API interval.

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

#### Scenario: Pass fails part-way
- **WHEN** a pass stops on an offline network or a refused request
- **THEN** the cursor is not advanced and the next pass reads the same shots again, writing nothing already applied

### Requirement: Coffee bag archive state syncs both ways

The application SHALL keep, per synced bag, the `archived_at` visualizer.coffee was last known to hold. Each pass SHALL read every bag's `archived_at` from the bag list; when it differs from the known value, an archive SHALL mark the local bag finished, a restore SHALL return it to inventory, and the new value SHALL be recorded. A bag push SHALL carry `archived_at` — the current time when the bag is finished here and active there, `null` when it is in inventory here and archived there — only when the two disagree, and SHALL record the value Visualizer returns. Pulled changes SHALL NOT be pushed back.

#### Scenario: Archived on Visualizer
- **WHEN** the user archives a synced bag on visualizer.coffee and the next pass runs
- **THEN** the bag is marked finished in Decenza, as if Bag Finished had been tapped

#### Scenario: Finished in Decenza
- **WHEN** the user taps Bag Finished on a synced bag in Decenza
- **THEN** the bag is archived on visualizer.coffee

#### Scenario: An edit does not undo an unpulled archive
- **GIVEN** a bag archived on Visualizer since the last pass
- **WHEN** the user edits its tasting notes in Decenza before the next pass
- **THEN** the push carries the notes and no `archived_at`, and the bag stays archived there

### Requirement: Other coffee bag state on Visualizer is pulled into Decenza

During the same pass, the application SHALL read each synced bag still in inventory (`GET /api/coffee_bags/:id`). A new `frozen_date` SHALL be taken together with its `defrosted_date`; otherwise a differing `defrosted_date` SHALL be taken. Descriptive fields SHALL fill only local blanks. A bag with an unsent local edit SHALL keep its fields. A bag photo that one side has and the other lacks SHALL be copied to the other; neither side's photo SHALL be replaced.

#### Scenario: Frozen on Visualizer
- **WHEN** the user taps Freeze on a synced bag on visualizer.coffee
- **THEN** the local bag's frozen date becomes that date and its defrost date is cleared

#### Scenario: Photo only in Decenza
- **WHEN** a synced bag has a photo in Decenza and none on Visualizer
- **THEN** the photo is uploaded to the Visualizer bag
