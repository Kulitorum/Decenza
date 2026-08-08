## MODIFIED Requirements

### Requirement: Keyless canonical identity round-trips to Visualizer

The search path SHALL remain keyless (no account or API key), and the `id` / `visualizerCanonicalId` of an entry SHALL be the Visualizer canonical UUID that Decenza stores locally and sends back on shot upload as `shot[canonical_coffee_bag_id]`, so the same canonical id links the bean in both systems.

That round-trip carries a precondition, because the id is an IDENTITY claim and not a details pointer: a canonical record is a ROASTER'S PRODUCT, and visualizer.coffee rewrites a shot's `bean_brand` and `bean_type` from the linked record. Decenza SHALL store and export the id only while the local bag's own roaster and coffee still name that record. When they do not — the **borrowed record** case, where the same coffee scraped from another roaster was the only match because the user's roaster is absent from the canonical database — the link SHALL be dropped rather than corrected: the canonical endpoints are read-only, so no correct id exists for such a bag and unlinked is the only correct state.

A shot's stored snapshot is a historical record and SHALL NOT be rewritten to match, so the export path SHALL apply the same check independently and withhold a borrowed id at upload time.

#### Scenario: Canonical UUID is the stored and uploaded identity

- **WHEN** a user links a bean from a canonical search entry
- **THEN** the stored snapshot's `id`/`visualizerCanonicalId` equals the API's `id`
- **AND** that id is sent on shot upload as `shot[canonical_coffee_bag_id]`

#### Scenario: A borrowed record is never stored as a link

- **WHEN** a bag is saved whose roaster or coffee names a different coffee than the record it points at — whether by picking a near-match and typing over the prefilled roaster, or by renaming a bag that was already linked
- **THEN** the canonical id SHALL NOT be stored
- **AND** the descriptive values and product URL from that record SHALL be kept as the user's own data

#### Scenario: A borrowed id already in a shot snapshot is withheld from export

- **WHEN** a shot whose stored snapshot carries a canonical id is uploaded or its metadata is updated, and that snapshot's record names a different coffee than the shot's own bean fields
- **THEN** `canonical_coffee_bag_id` SHALL be omitted from the payload
- **AND** the shot's stored snapshot SHALL be left unchanged

#### Scenario: An unverifiable snapshot is treated as borrowed

- **WHEN** the stored snapshot cannot be parsed
- **THEN** the link SHALL be treated as conflicted and withheld, because the permissive answer exports an unverified identity claim into the user's cloud history
