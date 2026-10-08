# visualizer-edit-pull Specification

## Purpose

Covers the two-way sync of Visualizer data into Decenza: periodic pulls of shot
edits, coffee bag archive and field state, and how fresh Visualizer-linked
screens are. Pulled values never overwrite an unsent local edit, and pulled
changes are not pushed back.

## Requirements

### Requirement: Shot edits made on Visualizer are pulled into Decenza

While Visualizer is connected and automatic update is on, the application SHALL
periodically list the shots changed on visualizer.coffee since the last complete
pass and read each one linked to a local shot. For each pulled field whose
remote value differs from the local value, the local value SHALL be replaced,
unless that field was edited locally and not yet sent. A missing or null remote
value SHALL NOT clear a local value.

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

### Requirement: Shot pulls run on a schedule and are paced

A pass SHALL run at startup, every 30 minutes, and when an account is connected.
Requests SHALL be paced at the shared Visualizer API interval.

#### Scenario: Pass runs at startup and every 30 minutes

- **WHEN** Visualizer is connected and automatic update is on
- **THEN** a pass runs at startup and then every 30 minutes

### Requirement: Bean fields and grinder identity are not pulled from shots

Grinder identity, the canonical bean link and the bean fields (brand, type,
roast date, roast level) SHALL NOT be pulled from a shot. Visualizer rewrites a
bag-linked shot's bean fields from its coffee bag, so their values there are not
edits.

#### Scenario: Bean link and grinder identity are not pulled

- **WHEN** a pass reads a shot whose grinder identity or bean link differs on Visualizer
- **THEN** the local grinder identity and bean link are unchanged

### Requirement: The shot pull cursor advances only after a complete pass

The cursor SHALL be kept per account and advanced only after a complete pass in
which every pulled write was saved. A shot that cannot be read SHALL be skipped
and logged; a failure affecting the whole account SHALL end the pass without
advancing the cursor. The first pass on an account, and one with more changed
shots than a pass reads, SHALL look back 14 days.

#### Scenario: First pass looks back 14 days

- **WHEN** an account is synced for the first time
- **THEN** the pass reads shots changed in the last 14 days

### Requirement: Pulled shot changes stay local and yield to newer pushes

A read that a push of the same item overtook SHALL be dropped. A pulled shot
change SHALL reach the other upload destinations as a local edit would, and
SHALL NOT be pushed back to Visualizer.

#### Scenario: A pulled change is not pushed back

- **WHEN** a pass applies a change pulled from Visualizer
- **THEN** no push to Visualizer is made for it, and other upload destinations receive it as a local edit

#### Scenario: A push overtakes a read

- **WHEN** a push of a shot completes while a read of that shot is in flight
- **THEN** the read result for that shot is dropped

### Requirement: Coffee bag archive state syncs both ways

The application SHALL keep, per synced bag, the `archived_at` visualizer.coffee
was last known to hold. Each pass SHALL read every bag's `archived_at` from the
bag list. When it differs from the known value, an archive SHALL mark the local
bag finished, a restore SHALL return it to inventory, and the new value SHALL be
recorded. Pulled changes SHALL NOT be pushed back.

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

### Requirement: An unseen archive state is recorded, not acted on

A bag whose archive state has never been seen SHALL have it recorded without
acting on it.

#### Scenario: Unseen archive state is only recorded

- **WHEN** a pass first reads `archived_at` for a bag whose archive state was never seen
- **THEN** the value is recorded and the local bag is not changed

### Requirement: A bag push carries archived_at only when the two sides disagree

A bag push SHALL carry `archived_at` only when the two sides disagree: the
current time when the bag is finished here and active there, or `null` when it
is in inventory here and archived there. The value Visualizer returns SHALL be
recorded.

#### Scenario: Push carries the archive state when they disagree

- **WHEN** the user finishes a synced bag in Decenza while it is active on Visualizer
- **THEN** the push carries `archived_at` as the current time, and the value Visualizer returns is recorded

### Requirement: Other coffee bag state on Visualizer is pulled into Decenza

Each synced bag in inventory SHALL be read in the same pass (`GET
/api/coffee_bags/:id`). A field (roast date, roast level, frozen and defrosted
dates, notes, descriptive attributes) whose remote value differs from the value
Visualizer was last known to hold SHALL be taken when the local value still
equals that last-known value, and kept if changed locally too. Remote values
SHALL be recorded as seen. Pulled changes SHALL NOT be pushed back.

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

### Requirement: A never-seen bag field only fills a blank

A field never seen before SHALL only fill a local blank. The decision SHALL be
made against the bag row as it stands when the write runs.

#### Scenario: An unseen field fills only a blank

- **WHEN** a pass first reads a remote value for a bag field Decenza has never seen
- **THEN** the local field is filled only if it is blank

### Requirement: Bag photos fill only the missing side

A bag photo that one side has and the other lacks SHALL be copied to the other.
Neither side's photo SHALL be replaced.

#### Scenario: Photo only on Visualizer

- **WHEN** a synced bag has a photo on Visualizer and none in Decenza
- **THEN** the photo is copied into Decenza, and neither photo is replaced

### Requirement: Synced data is fresh when viewed

A screen showing a Visualizer-linked shot (post-shot review, shot detail, the
web shot page) SHALL read that shot from Visualizer when it opens. The bag
editor SHALL read its bag when it opens. The bean inventory (app and web) SHALL
read every bag's archive state and the bags in use, unless they were read in the
last 3 minutes.

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

### Requirement: An open editor takes pulled changes into untouched fields

An editor SHALL save only the fields edited in it. While open, it SHALL take a
pulled change into any field not yet edited. An undo SHALL NOT restore a value
that a pull replaced.

#### Scenario: A pulled change lands in an untouched field

- **WHEN** an editor is open and a pull changes a field the user has not edited
- **THEN** that field shows the pulled value

### Requirement: Background requests share one pacer

Background requests SHALL share one pacer, so concurrent passes together keep to
the request interval.

#### Scenario: Concurrent passes share the interval

- **WHEN** two passes run at the same time
- **THEN** their combined requests keep to the request interval
