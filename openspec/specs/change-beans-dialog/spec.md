# change-beans-dialog Specification

## Purpose
TBD - created by archiving change bean-bag-inventory. Update Purpose after archive.
## Requirements
### Requirement: Unified search across Bean Base and shot history
The Change Beans dialog SHALL search both the Visualizer canonical Bean Base autocomplete and the local shot history simultaneously as the user types. Results SHALL appear in a single ranked list.

#### Scenario: Search returns both sources
- **WHEN** the user types a query that matches entries in both Bean Base and shot history
- **THEN** results from both sources SHALL appear in one list, each labelled with its source

#### Scenario: Search returns history only
- **WHEN** the query matches shot history but not Bean Base
- **THEN** history results SHALL appear without a "no Bean Base results" message

#### Scenario: Empty query
- **WHEN** the dialog opens with no query typed
- **THEN** inventory bags (Tier 0) SHALL be shown first, followed by the user's shot history (most recent coffees) as default suggestions

### Requirement: Quality-ranked search results
Results SHALL be ranked by data quality and recency using the following tiers:
- **Tier 0**: Bags currently in inventory (`inInventory = true`) — shown first, labelled "In inventory". Selecting one is context-dependent: from the Add New Bag (inventory) entry point it opens the creation form pre-filled from that bag (a new bag of the same coffee, roast date blank, identity editable); from every other context it selects the existing bag directly (no details form, no new bag)
- **Tier 1**: Present in both shot history AND Bean Base canonical (matched on `beanBaseId` or case-insensitive roaster+name) — shown with both source labels
- **Tier 2**: Bean Base canonical only (no history match)
- **Tier 3**: Shot history with a `beanBaseId` (previously linked, not in current search results)
- **Tier 4**: Shot history with no canonical link (free text only)
- **Tier 5**: Manual entry (always last)

Within each tier, results SHALL be sorted by most recent use date. A history or canonical result that corresponds to an existing inventory bag (matched on `beanBaseId` or case-insensitive roaster+name+roastDate) SHALL be absorbed into that bag's Tier 0 entry rather than shown separately — the dialog must never offer to re-create a coffee that is already in inventory. Within the history lane, the same coffee appearing both linked and unlinked (e.g. shots before and after canonical linking) SHALL be merged into one entry.

#### Scenario: Switching to a bag already in inventory (non-inventory contexts)
- **WHEN** the dialog is opened from brew settings, the idle page, post-shot review, or a historical shot, and the user picks a Tier 0 inventory bag
- **THEN** that existing bag SHALL be selected per the context's semantics immediately, with no details form and no new bag created

#### Scenario: Another bag of the same coffee (Add New Bag)
- **WHEN** the dialog is opened from the Beans window's Add New Bag action and the user picks a Tier 0 inventory bag
- **THEN** the creation form SHALL open pre-filled from that bag (identity, canonical link, grinder/dose, notes) with the roast date blank and identity editable
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

### Requirement: Source labels on each result
Each result row SHALL display a label indicating its source(s): "Bean Base", "History", or both.

#### Scenario: Source label visibility
- **WHEN** a result row is displayed
- **THEN** its source label SHALL be visible without requiring any interaction

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

### Requirement: Roast date is always user-entered, never inferred, and optional
The system SHALL never infer or pre-fill the roast date from any source — a new bag is a new roast date. The roast date field SHALL always be blank when the form opens. It SHALL NOT be required: a bag may be created without one (supermarket beans, gifts, unknown roast dates), and the summary already silences an absent roast date rather than showing a placeholder.

#### Scenario: Roast date not pre-filled from history
- **WHEN** a history result is picked (even with a previously known roast date)
- **THEN** the roast date field SHALL be blank

#### Scenario: Creating a bag without a roast date
- **WHEN** the user confirms the bag details form with the roast date left blank
- **THEN** the bag SHALL be created with a null `roastDate`
- **AND** no roast-age display SHALL appear for it anywhere

### Requirement: Created bag added to inventory; activation follows context semantics
When the user confirms a bag in the details form (Tier 1–5 paths), the bag SHALL be created and added to inventory. Whether it becomes the active bag, or is applied to a shot, follows the context-dependent selection semantics below — creation never unconditionally changes `activeBagId`.

#### Scenario: Bag creation from brew settings
- **WHEN** the user confirms a bag details form having opened the dialog from brew settings
- **THEN** the new bag SHALL appear in the inventory view
- **AND** `activeBagId` SHALL be set to the new bag
- **AND** the Change Beans dialog SHALL close

#### Scenario: Bag creation from a historical shot
- **WHEN** the user confirms a bag details form having opened the dialog from a historical shot's detail page
- **THEN** the new bag SHALL be created and that shot's snapshot updated
- **AND** `activeBagId` SHALL remain unchanged

### Requirement: Change Beans dialog accessible from all shot contexts
The Change Beans dialog SHALL be openable from the bean summary in brew settings, post-shot review, shot detail page, the Beans window, and the idle-page bean widget.

#### Scenario: Opening from shot context
- **WHEN** the user taps "Change Beans" in any shot context
- **THEN** the Change Beans dialog SHALL open

### Requirement: Selection semantics depend on opening context
What happens when a bag is selected SHALL depend on where the dialog was opened:
- **Brew settings / Beans window / idle page**: set `activeBagId` (affects the next shot)
- **Post-shot review**: set `activeBagId` AND retroactively update the just-saved shot's snapshot (bean fields, `bagId`, `frozenDate`, `defrostDate`) — the "wrong bag selected" fix path
- **Historical shot detail**: update only that shot's snapshot; `activeBagId` SHALL NOT change

#### Scenario: Fixing the wrong bag after a shot
- **WHEN** the user opens Change Beans from the post-shot review page and selects a different bag
- **THEN** the just-saved shot's snapshot SHALL be updated to the selected bag's fields
- **AND** `activeBagId` SHALL be set to the selected bag

#### Scenario: Re-linking a historical shot
- **WHEN** the user opens Change Beans from a historical shot's detail page and selects a bag
- **THEN** that shot's snapshot SHALL be updated
- **AND** `activeBagId` SHALL remain unchanged

### Requirement: Tea creation mode
The Change Beans dialog SHALL support a tea mode used by the "Add Tea" entry point. In tea mode: the Visualizer canonical search lane SHALL be suppressed (the canonical database is coffee-only and returns coffee false-positives for tea terms); the past-bags lane SHALL search only tea bags (re-buy flow); when no tea bags exist the dialog SHALL open directly on the form. The tea form SHALL relabel roaster → "Brand" and coffee → "Tea", SHALL hide roast level, grinder setting/rpm, and all canonical-link affordances, and SHALL keep the URL field, "Get info from page", photo resolution, weight/remaining, and show-on-idle. Tea mode is subtraction over the existing form — one mode property, not a parallel form.

#### Scenario: No Visualizer results for tea
- **WHEN** the user types "earl grey" in tea mode
- **THEN** only past tea bags are searched and no canonical coffee results appear

#### Scenario: First tea goes straight to the form
- **WHEN** the user taps "Add Tea" with no tea bags in history
- **THEN** the form opens directly with tea labels and without roast-level or grind fields

#### Scenario: Re-buying a tea
- **WHEN** the user picks a past tea bag from the tea-mode search
- **THEN** the form prefills from it exactly as the coffee re-buy flow does

