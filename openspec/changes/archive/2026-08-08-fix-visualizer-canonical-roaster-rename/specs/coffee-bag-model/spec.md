## ADDED Requirements

### Requirement: shots table gains bean_repair_pending column

A schema migration SHALL add a `bean_repair_pending INTEGER NOT NULL DEFAULT 0` column to `shots`, set when a bag's borrowed canonical link is dropped and cleared once visualizer.coffee has confirmed that shot needs nothing further (see `visualizer-coffee-management`). The column SHALL survive backup restore and device transfer like every other shot column.

#### Scenario: Migration adds the column

- **WHEN** the schema migration runs on an existing database
- **THEN** every existing shot SHALL have `bean_repair_pending = 0`

#### Scenario: The column is the only durable record of the queue

- **WHEN** the app is closed mid-repair, or a repair pass is abandoned
- **THEN** the still-flagged shots SHALL remain flagged and be retried on a later launch

### Requirement: Stored bags carrying a borrowed canonical record are unlinked by migration

A schema migration SHALL apply the identity check to every stored bag and unlink those whose own roaster/coffee name a different coffee than the record they point at. Detection is offline and SHALL be conservative: it can only prove a conflict while the stored snapshot still carries the record's own names, and an empty name on either side SHALL NOT be treated as a disagreement.

Because each shot carries its own copy of the snapshot and the upload path reads that copy rather than the bag, the unlink SHALL propagate to the shots of every bag it fixes; otherwise every subsequent upload re-asserts the borrowed id and the bag fix changes nothing. Uploaded shots of those bags SHALL additionally be flagged for repair, since those are exactly the shots the server may already have renamed.

The unlink is the correctness fact and SHALL be applied even if the repair-queue column could not be added; only the cloud repair is lost in that case. Where the migration cannot determine whether the column exists, it SHALL defer to a later launch rather than unlink without queueing, because a version stamp is permanent and would retire the repair for that database forever.

#### Scenario: A stored borrowed link is dropped

- **WHEN** the migration runs on a database holding a bag whose roaster names a different coffee than its canonical record
- **THEN** the bag's canonical id SHALL be cleared and its snapshot stripped of the link keys
- **AND** every descriptive field and the product URL SHALL be kept

#### Scenario: A correctly linked bag is untouched

- **WHEN** the migration runs on a database holding a bag whose names still match its record
- **THEN** the bag SHALL keep its canonical id

#### Scenario: The unlink reaches the shots' own snapshots

- **WHEN** a bag is unlinked by the migration
- **THEN** the shots of that bag SHALL have the link keys removed from their own stored snapshots

#### Scenario: Only uploaded shots are queued for repair

- **WHEN** a bag with both uploaded and never-uploaded shots is unlinked
- **THEN** only the uploaded shots SHALL be flagged `bean_repair_pending`
- **AND** the never-uploaded shots SHALL NOT be flagged, since the server cannot have renamed a shot it does not have
