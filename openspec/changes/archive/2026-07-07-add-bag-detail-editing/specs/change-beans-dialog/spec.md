# change-beans-dialog Delta

## MODIFIED Requirements

### Requirement: Bag details form after picking a result
After selecting any result, the dialog SHALL show a Bag Details form pre-filled with available data. The form SHALL only show fields for which the system does not already have a value, except the Bean details section, which SHALL always be present (collapsed) with canonical-supplied values prefilled as editable fields — canonical attributes are no longer rendered as read-only confirmation.

#### Scenario: Picking a canonical + history result
- **WHEN** the user picks a Tier 1 result (both sources)
- **THEN** the form SHALL show: roast date (blank — optional), and collapse all fields already known (canonical attributes, grinder from history, dose from history)
- **AND** notes, grinder hardware, and the freeze toggle SHALL be visible inline (no expander — it only added a click)

#### Scenario: Picking a canonical-only result
- **WHEN** the user picks a Tier 2 Bean Base result with no history
- **THEN** the form SHALL show: roast date (blank — optional), grinder setting (blank)
- **AND** canonical attributes SHALL prefill the collapsed Bean details section as editable fields (per `bag-detail-editing`), not read-only confirmation

#### Scenario: Manual entry
- **WHEN** the user selects "Enter manually"
- **THEN** all fields SHALL be shown as editable: roaster, coffee name, roast date, roast level, grinder setting, dose
- **AND** the collapsed Bean details section SHALL be available for optional detail entry

#### Scenario: Canonical linking available in create mode
- **WHEN** the bag form opens in create mode (history pick, inventory re-buy, or manual entry) without a canonical link
- **THEN** the Bean Base search bar SHALL be present (prefilled with the known roaster/coffee text when any) so the bag can be linked before saving — no save-then-"Find in Bean Base" round-trip
