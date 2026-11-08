# Spec Delta

## MODIFIED Requirements

### Requirement: Quality-ranked search results
Results SHALL be ranked by data quality and recency in six tiers: 0, bags in inventory (`inInventory = true`), labelled "In inventory"; 1, in both shot history and Bean Base canonical (matched on `beanBaseId` or case-insensitive roaster and name), with both labels; 2, Bean Base canonical only; 3, shot history with a `beanBaseId` not in current results; 4, shot history with no canonical link; 5, manual entry, always last.

#### Scenario: Switching to a bag already in inventory (non-inventory contexts)
- **WHEN** the dialog is opened from brew settings, the idle page, post-shot review, or a historical shot, and the user picks a Tier 0 inventory bag
- **THEN** that existing bag SHALL be selected per the context's semantics immediately, with no details form and no new bag created

#### Scenario: Another bag of the same coffee (Add New Bag)
- **WHEN** the dialog is opened from the Beans window's Add New Bag action and the user picks a Tier 0 inventory bag
- **THEN** the creation form SHALL open pre-filled from that bag (identity, canonical link, dose, notes) with the roast date blank and identity editable
- **AND** confirming SHALL create a SEPARATE new bag — two bags of the same coffee, each with its own roast date / freeze state, is supported

#### Scenario: Inventory bag absorbs its own history/canonical match
- **WHEN** a search query matches both an inventory bag and that same coffee's history or canonical entry
- **THEN** exactly one Tier 0 result SHALL be shown for it

#### Scenario: Same coffee in both sources
- **WHEN** a coffee matches both a history entry and a Bean Base entry on `beanBaseId` or roaster+name
- **THEN** exactly ONE merged result SHALL appear at Tier 1 with labels for both sources

#### Scenario: History without canonical ranks below canonical-only
- **WHEN** search returns a history entry with no canonical link and a fresh Bean Base entry
- **THEN** the Bean Base entry SHALL rank above the history entry

#### Scenario: Two near-identical entries separated by link state
- **WHEN** two Bean Base entries for the same coffee are in the same tier, one with a live product
  URL and one whose URL is dead with an archive snapshot
- **THEN** the live-link entry SHALL be ordered above the archived-link entry

#### Scenario: Dead link with no snapshot ranks last within its tier
- **WHEN** an entry's URL is dead and the Internet Archive has no snapshot of it
- **THEN** it SHALL be ordered below every live- and archived-link entry in the same tier

#### Scenario: Results are not delayed by probing
- **WHEN** a search returns results whose link states are not yet known
- **THEN** the results SHALL be displayed immediately in tier-and-recency order
- **AND** SHALL re-order as each link state resolves

### Requirement: Bag details form after picking a result
After selecting any result, the dialog SHALL show a Bag Details form pre-filled with available data. The form SHALL only show fields for which the system does not already have a value, except the Bean details section, which SHALL always be present (collapsed) with canonical-supplied values prefilled as editable fields — canonical attributes are no longer rendered as read-only confirmation.

#### Scenario: Picking a canonical + history result
- **WHEN** the user picks a Tier 1 result (both sources)
- **THEN** the form SHALL show: roast date (blank — optional), and collapse all fields already known (canonical attributes, dose from history)
- **AND** notes and the freeze toggle SHALL be visible inline (no expander — it only added a click)

#### Scenario: Picking a canonical-only result
- **WHEN** the user picks a Tier 2 Bean Base result with no history
- **THEN** the form SHALL show: roast date (blank — optional)
- **AND** canonical attributes SHALL prefill the collapsed Bean details section as editable fields (per `bag-detail-editing`), not read-only confirmation

#### Scenario: Manual entry
- **WHEN** the user selects "Enter manually"
- **THEN** all fields SHALL be shown as editable: roaster, coffee name, roast date, roast level, dose
- **AND** the text typed into the search before choosing it SHALL prefill the coffee name, not be discarded
- **AND** the collapsed Bean details section SHALL be available for optional detail entry

#### Scenario: Canonical linking available in create mode
- **WHEN** the bag form opens in create mode (history pick, inventory re-buy, or manual entry) without a canonical link
- **THEN** the Bean Base search bar SHALL be present (prefilled with the known roaster/coffee text when any) so the bag can be linked before saving — no save-then-"Find in Bean Base" round-trip

## REMOVED Requirements

### Requirement: A new bag SHALL NOT start with an empty equipment package
**Reason**: Bags no longer own equipment or grind.
**Migration**: Select equipment independently or use a recipe.
