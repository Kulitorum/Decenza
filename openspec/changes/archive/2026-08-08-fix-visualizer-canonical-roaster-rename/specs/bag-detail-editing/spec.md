## MODIFIED Requirements

### Requirement: All bag fields are editable in the bag editor, linked or not

The bag editor (ChangeBeansDialog form, create and edit modes) SHALL expose a "Bean details" section with editable fields: product URL, origin, region, farm, producer, variety, elevation, process, harvest, quality score, place of purchase, and tasting notes. Every field — including identity (roaster, coffee name) and roast level — SHALL be editable regardless of canonical link state: a canonical link autofills and shows a "verified" badge, but never locks a field (matching Visualizer's own bag editor).

A **detail** edit SHALL never break the link. An **identity** edit MAY: the canonical id is a claim that this bag IS that roaster's product, so when an edit leaves the bag's roaster or coffee naming a different coffee than the record does, the link SHALL be dropped rather than kept and corrected. The comparison SHALL use the record's pristine names (the `canonical` snapshot where present, since the working identity keys are themselves user-editable), and an empty name on either side SHALL NOT be treated as a disagreement. Dropping the link removes only the link keys; every descriptive field and the product URL — the data the user linked FOR — SHALL be kept as the user's own.

This is not a UI restriction. visualizer.coffee rewrites a shot's `bean_brand` and `bean_type` from the linked canonical record, so a bag that keeps a record naming another roaster's coffee renames every shot it has ever pulled, in the cloud, while the app keeps showing the right name.

#### Scenario: Editing a canonical-linked bag's details

- **WHEN** the user opens the bag editor for a bag linked to a canonical entry and changes the tasting notes and elevation
- **THEN** the edited values SHALL be saved on the bag
- **AND** the canonical link (`beanBaseId`) SHALL remain intact

#### Scenario: Correcting the roaster to one the record does not name

- **WHEN** the user edits a linked bag's roaster from the record's roaster to their own (the borrowed-record case: the same coffee, scraped from a different roaster, was the only match)
- **THEN** the edit SHALL be saved
- **AND** the canonical link SHALL be dropped, because the record no longer describes this bag
- **AND** every descriptive value and the product URL SHALL be kept

#### Scenario: Correcting a linked bag whose roaster entry is stale

- **WHEN** the roaster has updated the coffee (e.g. new crop name) but the canonical DB carries only the older entry, and the user edits the linked bag's coffee name to the new one
- **THEN** the edits SHALL be saved and the blob's working identity keys (`roasterName`, `roastName`) and the bag columns SHALL both reflect the edit
- **AND** the canonical link SHALL be dropped, because the bag now names a coffee the record does not — the app cannot tell a renamed crop from a different product, and the safe answer is the one that cannot rename the user's cloud history

#### Scenario: Adding details to a manual bag

- **WHEN** the user opens the bag editor for a bag with no canonical link and enters origin, variety, and process
- **THEN** the values SHALL be saved and rendered on the bag card attribute line and details popup exactly as canonical data would be

#### Scenario: Prefill from canonical data

- **WHEN** the bag carries canonical-supplied detail values
- **THEN** the Bean details fields SHALL open prefilled with those values as editable text, not read-only confirmation
