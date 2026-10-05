## ADDED Requirements

### Requirement: Beans page search and sort

The Beans window SHALL offer the same search field and sort controls as the Recipes page. A search SHALL match every text value a bag holds (its own fields and every value in its bean-details blob), and SHALL NOT match identifiers, links or sync bookkeeping. The search SHALL apply to the finished bags too, and the "Show finished (N)" count SHALL count the matches. Sort SHALL offer Last used (the default, newest first), Roast date, Coffee and Roaster, and SHALL persist; the search SHALL reset when the page is entered.

#### Scenario: Search by a bean detail
- **WHEN** the user types a region recorded only in a bag's bean details
- **THEN** that bag is listed and bags without that text are not

#### Scenario: Search reaches finished bags
- **WHEN** a search matches only a finished bag
- **THEN** "Show finished (1)" is shown and lists it

#### Scenario: No matches
- **WHEN** a search matches no bag
- **THEN** the page says no bags match the search
