## ADDED Requirements

### Requirement: Shot endpoints of record

These are the observable facts the bean-repair pass depends on, verified against the deployed server (`miharekar/visualizer`, `upstream/main`) and against a live account. They supersede the spike finding in `changes/archive/2026-06-20-bean-bag-inventory/design.md` that `GET /api/shots/{id}` does not return `coffee_bag_id`.

- `GET /api/shots/{id}` SHALL be treated as returning `coffee_bag_id` **only when the shot has a bag**: the field is built as `coffee_bag_id: coffee_bag&.id` inside a hash that is compacted, so a bag-less shot OMITS the key rather than sending null. Absent and null both mean "no bag"; a value that is present but not a string means the response was not understood and SHALL NOT be read as "no bag".
- `canonical_coffee_bag_id` SHALL NOT be expected in any shot response — it is not among the shot's serialized attributes — so no read can confirm that an unlink took effect.
- `GET /api/shots` (the list) SHALL NOT be used to answer any question about a shot's bean identity: it renders only `{clock, id, updated_at}`, so the bean fields are absent rather than empty, and reading the missing keys as empty strings makes every uploaded shot look like a disagreement.
- Requests SHALL be paced against the published limits of 50 per minute per IP and 200 per 10 minutes per IP and per user. Shot upload shares that budget, so an unpaced background pass can rate-limit a user's actual espresso uploads.

#### Scenario: A bag-less shot omits the key

- **WHEN** a shot with no server-side coffee bag is read
- **THEN** `coffee_bag_id` SHALL be absent from the response
- **AND** the system SHALL treat that as "no bag"

#### Scenario: An unreadable coffee_bag_id is not "no bag"

- **WHEN** `coffee_bag_id` is present but is not a string
- **THEN** the system SHALL treat the response as unusable and leave the shot queued, rather than acting on an assumed absence

### Requirement: Borrowed-record shot repair is a recorded queue, not a library scan

Shots renamed by a borrowed canonical record SHALL be repaired from the flag recorded at the unlink, never by comparing the local library against the server. A discovery pass built on the shot list read absent bean fields as empty, took that for a disagreement, and queued an entire library on a live account.

The pass SHALL read each queued shot before writing anything, and SHALL write only on a real difference — trim- and case-insensitive, since a whitespace or capitalisation difference is not worth a write to a user's cloud account. It SHALL clear a shot's flag only when the server has confirmed that shot needs nothing further; nothing else SHALL clear a flag, so an interrupted or failed pass simply resumes later.

#### Scenario: Only flagged shots are considered

- **WHEN** a repair pass runs
- **THEN** it SHALL read only shots flagged at an unlink
- **AND** a shot the server already agrees with SHALL settle without any write

#### Scenario: A failed read leaves the shot queued

- **WHEN** a shot's read fails, or returns a body that cannot be parsed or lacks the bean fields
- **THEN** no write SHALL be made for that shot and its flag SHALL remain set

### Requirement: What the repair may write to a shot

Before writing, the pass SHALL decide per shot from the read:

- When the shot has a server-side coffee bag, the pass SHALL write NOTHING. Such a shot takes its identity from that bag on every touch, so nothing sent to the shot survives — and the attempt is not free, because the server re-derives the shot's roast date into the user's display format as a side effect. Any residual problem for such a shot belongs to the bag, not the shot.
- When the shot's local bean brand or bean type is empty, the pass SHALL NOT send bean names — sending an empty name blanks a real value on the user's account — and SHALL instead clear the borrowed link alone, which needs no names and is what stops the server re-deriving that shot's identity.
- Otherwise the pass SHALL send the local bean names together with an explicit clear of the canonical link.

An HTTP success status SHALL NOT be taken as proof the values took: the server may overwrite them from a bag between the request and the save, and it answers with the assigned attributes either way. The pass SHALL read the response back and report a write the server discarded, rather than counting it as a repair.

#### Scenario: A shot with a server bag is declined

- **WHEN** a queued shot is read and has a `coffee_bag_id`
- **THEN** no request SHALL be sent for it
- **AND** its flag SHALL be cleared, since no future pass could do more

#### Scenario: Incomplete local names never blank the server's

- **WHEN** a queued shot's local bean brand or bean type is empty and the shot has no server bag
- **THEN** the request SHALL clear the canonical link and SHALL NOT contain the bean names

#### Scenario: A discarded write is not counted as a repair

- **WHEN** a write returns success but the response shows the server kept different bean names
- **THEN** the outcome SHALL be reported as not applied
- **AND** the pass SHALL NOT claim the shot was corrected

### Requirement: Repair pass failure vocabulary

A refusal that cannot differ per shot SHALL abandon the pass rather than repeat itself against every remaining shot; the flags keep the remainder for a later launch. Rate limiting and an invalid credential are such refusals. So is an authorization refusal on the READ, because the read path performs no per-shot authorization — a refusal there is account- or network-wide and settling shots against it would clear the whole queue for a condition unrelated to any shot.

A refusal on the WRITE that names one shot — it is not this account's shot, or it no longer exists — SHALL settle that shot alone and continue, since it can never succeed and leaving it flagged re-sends the same refusal on every launch forever.

A pass SHALL report itself incomplete only when work was genuinely left queued, and SHALL distinguish shots whose names were restored, shots whose link was cleared, and shots it declined to touch.

#### Scenario: Rate limiting stops the pass

- **WHEN** the server rate-limits a request
- **THEN** the pass SHALL stop
- **AND** every shot not yet settled SHALL remain flagged for a later launch

#### Scenario: An authorization refusal on the read stops the pass

- **WHEN** a read is refused as unauthorized
- **THEN** the pass SHALL stop rather than settle that shot, because the read path authorizes nothing per shot

#### Scenario: An authorization refusal on the write settles one shot

- **WHEN** a write is refused as unauthorized or the shot is gone
- **THEN** that shot alone SHALL be settled and the pass SHALL continue
