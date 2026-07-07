# bag-detail-editing Delta

## ADDED Requirements

### Requirement: All bag fields are editable in the bag editor, linked or not

The bag editor (ChangeBeansDialog form, create and edit modes) SHALL expose a "Bean details" section with editable fields: product URL, origin, region, farm, producer, variety, elevation, process, harvest, quality score, place of purchase, and tasting notes. Every field — including identity (roaster, coffee name) and roast level — SHALL be editable regardless of canonical link state: a canonical link autofills and shows a "verified" badge, but never locks a field (matching Visualizer's own bag editor). Editing never breaks the link.

#### Scenario: Editing a canonical-linked bag's details
- **WHEN** the user opens the bag editor for a bag linked to a canonical entry and changes the tasting notes and elevation
- **THEN** the edited values SHALL be saved on the bag
- **AND** the canonical link (`beanBaseId`) SHALL remain intact

#### Scenario: Correcting a linked bag whose roaster entry is stale
- **WHEN** the roaster has updated the coffee (e.g. new crop name) but the canonical DB carries only the older entry, and the user edits the linked bag's coffee name and details
- **THEN** the edits SHALL be saved with the canonical link intact and the badge still shown
- **AND** the blob's working identity keys (`roasterName`, `roastName`) and the bag columns SHALL both reflect the edit

#### Scenario: Adding details to a manual bag
- **WHEN** the user opens the bag editor for a bag with no canonical link and enters origin, variety, and process
- **THEN** the values SHALL be saved and rendered on the bag card attribute line and details popup exactly as canonical data would be

#### Scenario: Prefill from canonical data
- **WHEN** the bag carries canonical-supplied detail values
- **THEN** the Bean details fields SHALL open prefilled with those values as editable text, not read-only confirmation

### Requirement: Bean details section is collapsed by default

The Bean details section SHALL render collapsed, showing the existing one-line summary (origin · variety · process) when any detail value exists, and SHALL expand to the full field set on demand. An empty section header SHALL still be shown so details can be added to a bag that has none.

#### Scenario: Bag with no details
- **WHEN** the bag editor opens for a bag whose blob carries no detail values
- **THEN** the collapsed Bean details section SHALL be visible and expandable so values can be entered

### Requirement: A product URL can be added or corrected

The Bean details section SHALL include the product URL (`link`). When saved, the URL SHALL feed the existing bag-image resolution (`og:image` fetch and file cache) and the details popup's open-at-roaster affordance.

#### Scenario: Adding a URL to a bag without one
- **WHEN** the user enters a product URL for a bag whose blob has no `link` and saves
- **THEN** the blob SHALL carry the URL
- **AND** bag-image resolution SHALL be attempted for the bag using the new URL

### Requirement: Pristine canonical snapshot enables revert

On the first edit-save of a linked blob without a `canonical` key, the pre-edit flat values SHALL be copied into a `canonical` sub-object before edits apply — the working values are pristine until the first edit by construction, so this single lazy-capture path covers new links and bags linked before this feature alike. Flat top-level keys remain the working copy consumers read; the `canonical` sub-object is never modified by edits.

#### Scenario: Linked bag gets its snapshot on first edit
- **WHEN** a linked bag (new or pre-feature) is edited and saved for the first time
- **THEN** the pre-edit flat values SHALL be copied into `canonical` before the edits are applied
- **AND** subsequent edits SHALL leave `canonical` untouched

### Requirement: Revert to Bean Base data

When a bag is linked and its working values differ from the `canonical` snapshot, the editor SHALL offer a "Revert to Bean Base data" action. Reverting SHALL restore every canonical-supplied value (identity, roast level, details) over the working keys and remove working detail keys the canonical entry lacked — including a user-added URL — after a confirmation stating that local edits are discarded. A revert is a save: it persists like any edit and triggers the Visualizer edit-push.

#### Scenario: Revert restores canonical values
- **WHEN** the user edited a linked bag's coffee name and tasting notes, then taps Revert and confirms
- **THEN** the name and tasting notes SHALL return to the canonical entry's values
- **AND** the bag row, blob working keys, and display surfaces SHALL all reflect the canonical values

#### Scenario: Revert removes user additions canonical lacked
- **WHEN** the user added a URL the canonical entry did not carry, then reverts
- **THEN** the `link` key SHALL be removed along with the other local edits

#### Scenario: Revert hidden when nothing differs
- **WHEN** a linked bag's working values equal its `canonical` snapshot
- **THEN** no revert affordance SHALL be shown

#### Scenario: Manual bag has no revert
- **WHEN** the bag has no canonical link
- **THEN** no revert affordance SHALL be shown

### Requirement: Edited details merge into the beanBaseData blob

Saving the bag editor SHALL merge edited fields into the bag's existing `beanBaseData` blob, preserving untouched keys (`id`, `visualizerCanonicalId`, `canonicalRoasterId`, the `canonical` snapshot, `description`, legacy `image`). Identity edits SHALL update the blob's working `roasterName`/`roastName` alongside the bag columns. Fields cleared by the user SHALL be removed from the blob (absent, not empty string). New blob keys `farm`, `qualityScore`, and `placeOfPurchase` complement the existing detail keys.

#### Scenario: Merge preserves the link and snapshot
- **WHEN** a linked bag's fields are edited and saved
- **THEN** the blob's `id`, `canonicalRoasterId`, and `canonical` snapshot SHALL be unchanged
- **AND** only the edited working keys SHALL differ

#### Scenario: Clearing a field removes the key
- **WHEN** the user clears the region field and saves
- **THEN** the blob SHALL NOT contain a `region` key
- **AND** the details popup SHALL omit the region row (zero footprint per field)

#### Scenario: Downstream consumers see edited data
- **WHEN** a shot is saved after bag details were edited
- **THEN** the shot's `beanbase_json` snapshot, the AI advisor bean context, and MCP `shots_get_detail` SHALL carry the edited values
