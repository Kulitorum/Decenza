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
- **AND** the text typed into the search before choosing it SHALL prefill the coffee name, not be discarded
- **AND** the collapsed Bean details section SHALL be available for optional detail entry

#### Scenario: Canonical linking available in create mode
- **WHEN** the bag form opens in create mode (history pick, inventory re-buy, or manual entry) without a canonical link
- **THEN** the Bean Base search bar SHALL be present (prefilled with the known roaster/coffee text when any) so the bag can be linked before saving — no save-then-"Find in Bean Base" round-trip

### Requirement: Tea creation mode
The Change Beans dialog SHALL support a tea mode used by the "Bag of Tea" entry point. In tea mode: the Visualizer canonical search lane SHALL be suppressed (the canonical database is coffee-only and returns coffee false-positives for tea terms); the past-bags lane SHALL search only tea bags (re-buy flow); when no tea bags exist the dialog SHALL open directly on the form. The search field SHALL carry a label that stays visible while typing, naming what is searched (past tea bags in tea mode; past bags and the Loffee Labs Bean Base otherwise). The tea form SHALL relabel roaster → "Brand" and coffee → "Tea", SHALL hide roast level, grinder setting/rpm, and all canonical-link affordances, and SHALL keep the URL field, "Get info from page", photo resolution, weight/remaining, and show-on-idle. Tea mode is subtraction over the existing form — one mode property, not a parallel form.

#### Scenario: No Visualizer results for tea
- **WHEN** the user types "earl grey" in tea mode
- **THEN** only past tea bags are searched and no canonical coffee results appear

#### Scenario: First tea goes straight to the form
- **WHEN** the user taps "Bag of Tea" with no tea bags in history
- **THEN** the form opens directly with tea labels and without roast-level or grind fields

#### Scenario: Re-buying a tea
- **WHEN** the user picks a past tea bag from the tea-mode search
- **THEN** the form prefills from it exactly as the coffee re-buy flow does
