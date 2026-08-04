# history-bag-filter Specification

## Purpose
TBD - created by archiving change add-history-filter-widget-actions. Update Purpose after archive.
## Requirements
### Requirement: Shot History can be filtered to an exact coffee bag

Shot History SHALL support filtering to a single coffee bag by the bag's id, matching
`shots.bag_id` exactly. The filter SHALL be reachable through the same `initialFilter`
channel the recipe tap-through and the Auto-Favorites "Show" button use, so the filter
banner and its Clear control apply unchanged.

A bag filter SHALL be narrower than a bean filter: shots from a different bag of the same
coffee SHALL be excluded.

#### Scenario: Only that bag's shots are listed

- **WHEN** Shot History is filtered to a bag id
- **THEN** only shots whose `bag_id` equals that id are listed

#### Scenario: Same coffee, different bag, excluded

- **WHEN** two bags share a roaster and coffee name and Shot History is filtered to one of them
- **THEN** shots recorded against the other bag are absent from the results

#### Scenario: Pre-bag shots are excluded

- **WHEN** the history contains shots recorded before bags existed, carrying no bag id, and a bag filter is active
- **THEN** those shots are absent rather than matching every bag

#### Scenario: Filter banner names the bag

- **WHEN** a bag filter is active
- **THEN** the filter banner shows the bag's label — its coffee name, or its roaster name when the coffee name is empty — with the Clear control that removes the filter

### Requirement: A `bag:` search keyword scopes a search to bags

The Shot History search field SHALL accept a `bag:` keyword that restricts results to
shots whose bag matches the term, in the same two forms the `recipe:` keyword accepts:

- `bag:ethiopia` — single unquoted token
- `bag:"blue bottle"` — quoted, spaces allowed, unterminated quote running to end of string

The match SHALL be a case-insensitive SUBSTRING against the bag's coffee name, roaster
name and roast date, so a user can narrow by any of the three without knowing which field
carries the words they remember. An incomplete `bag:` with no term SHALL be treated as
not-a-keyword and left to free-text search. An explicitly empty quoted term (`bag:""`)
SHALL match nothing.

The keyword SHALL be strippable from the free-text remainder exactly as `recipe:` is, so
its term never leaks into the FTS search.

#### Scenario: Keyword narrows by coffee name

- **WHEN** a user searches `bag:ethiopia`
- **THEN** only shots whose bag's coffee name, roaster name or roast date contains "ethiopia" are listed

#### Scenario: Quoted multi-word term

- **WHEN** a user searches `bag:"blue bottle"`
- **THEN** the whole quoted phrase is the search term, not just the first word

#### Scenario: Roast date is matchable

- **WHEN** a user searches `bag:2026-07`
- **THEN** shots from bags whose roast date contains "2026-07" are listed

#### Scenario: Bare keyword falls through to free text

- **WHEN** a user searches `bag: ethiopia` with a space after the colon
- **THEN** the search behaves as a free-text search for "ethiopia" and returns results, rather than matching nothing

#### Scenario: Explicitly empty term matches nothing

- **WHEN** a user searches `bag:""`
- **THEN** no shots are listed

#### Scenario: Keyword term does not leak into free text

- **WHEN** a user searches `bag:ethiopia channeling:yes`
- **THEN** the bag term is consumed by the keyword and the remaining free text is empty, so results are the channeling-flagged shots from matching bags

### Requirement: Exact bag id and the `bag:` keyword are distinct scopes

The exact `bagId` filter and the `bag:` keyword SHALL be separate filter inputs with
separate semantics, mirroring the existing `recipeId` / `recipeName` split: the id is the
rename-proof exact selection used by tap-through and by the Custom widget's bag action,
the keyword is the typed substring. A bag's label SHALL travel with an exact filter for
the banner's benefit only, and SHALL NOT be used as a query term.

#### Scenario: Renamed bag still matches by id

- **WHEN** a bag's coffee name is edited after its shots were pulled and an exact bag filter for that bag is applied
- **THEN** the shots are still listed, and the banner shows the bag's current label

#### Scenario: Two bags with the same name stay distinct under an exact filter

- **WHEN** two bags share a coffee name and an exact filter selects one of them
- **THEN** only that bag's shots are listed

### Requirement: Bag filtering adds no schema change

Bag filtering SHALL use the existing populated `shots.bag_id` column. No new column,
index, table or schema-version bump SHALL be introduced for it.

#### Scenario: No migration runs

- **WHEN** the app starts on a database created before this change
- **THEN** bag filtering works with no migration having run and no schema version change

