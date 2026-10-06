## ADDED Requirements

### Requirement: Beans page search and sort

The Beans window SHALL offer the same search field and sort controls as the Recipes page. A search SHALL match every text value a bag holds (its own fields and every value in its bean-details blob) and the bag's kind ("Tea" or "Coffee", localized), and SHALL NOT match identifiers, links, stored enum values or sync bookkeeping. The search SHALL update on every edit and SHALL apply to the finished bags too, and the "Show finished (N)" count SHALL count the matches. Sort SHALL offer Last used (the default, newest first), Roast date, Coffee and Roaster, and SHALL persist; a saved sort the page does not offer SHALL show and sort as Last used. The search SHALL reset when the page is entered.

#### Scenario: Search by a bean detail
- **WHEN** the user types a region recorded only in a bag's bean details
- **THEN** that bag is listed and bags without that text are not

#### Scenario: Search by kind
- **WHEN** the user types "tea"
- **THEN** every bag of tea is listed

#### Scenario: Search reaches finished bags
- **WHEN** a search matches only a finished bag
- **THEN** "Show finished (1)" is shown and lists it, and the page does not say nothing matches

#### Scenario: Finished bags change during a search
- **WHEN** the user finishes a bag that matches the current search
- **THEN** it moves from the open results to the finished matches

#### Scenario: No matches
- **WHEN** a search matches no bag, open or finished
- **THEN** the page says no bags match the search

#### Scenario: Finished bags cannot be read
- **WHEN** reading the finished bags fails while a search is on or the finished bags are shown
- **THEN** the page says the finished bags could not be read, and does not say nothing matches
