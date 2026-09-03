## MODIFIED Requirements

### Requirement: Quality-ranked search results
Results SHALL be ranked by data quality and recency using the following tiers:
- **Tier 0**: Bags currently in inventory (`inInventory = true`) — shown first, labelled "In inventory". Selecting one is context-dependent: from the Add New Bag (inventory) entry point it opens the creation form pre-filled from that bag (a new bag of the same coffee, roast date blank, identity editable); from every other context it selects the existing bag directly (no details form, no new bag)
- **Tier 1**: Present in both shot history AND Bean Base canonical (matched on `beanBaseId` or case-insensitive roaster+name) — shown with both source labels
- **Tier 2**: Bean Base canonical only (no history match)
- **Tier 3**: Shot history with a `beanBaseId` (previously linked, not in current search results)
- **Tier 4**: Shot history with no canonical link (free text only)
- **Tier 5**: Manual entry (always last)

Within each tier, results SHALL be ordered first by link state and then by most recent use date.
Link state has three values, ordered best to worst: **live** (the entry's product URL is
reachable), **archived** (the URL is dead but the Internet Archive holds a snapshot of it), and
**none** (the entry has no URL, or its URL is dead with no snapshot). A result whose link state is
not yet known SHALL be ordered as if live, so results appear immediately and only ever move
downward as answers arrive; the list SHALL re-sort in place as each state resolves, and SHALL NOT
block on the probes. Link state SHALL be cached per URL for the session so repeating a search does
not re-probe.

A result's tier SHALL NOT change because of its link state — link state orders within a tier only.

A history or canonical result that corresponds to an existing inventory bag (matched on `beanBaseId` or case-insensitive roaster+name+roastDate) SHALL be absorbed into that bag's Tier 0 entry rather than shown separately — the dialog must never offer to re-create a coffee that is already in inventory. Within the history lane, the same coffee appearing both linked and unlinked (e.g. shots before and after canonical linking) SHALL be merged into one entry.

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

## ADDED Requirements

### Requirement: Link state is visible on each result row

Each result row that could carry a product URL SHALL indicate its link state — live, archived, or
none — alongside its existing source label, without requiring interaction. The indication SHALL
distinguish "not yet determined" from "determined to have no usable link", so a row is never
labelled worse than what is known about it.

#### Scenario: Archived-link row is distinguishable
- **WHEN** two result rows show identical roaster, coffee name and attributes, and one link is live
  while the other resolves only from the archive
- **THEN** each row SHALL show its own link state, so the two are distinguishable before selection

#### Scenario: Undetermined state is not shown as a failure
- **WHEN** a row's link state has not yet resolved
- **THEN** the row SHALL NOT claim the entry has no link
