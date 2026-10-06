## ADDED Requirements

### Requirement: Web Beans search and sort

The `/beans` web page SHALL offer the search field and sort controls of the web `/recipes` page, matching bags as the app does (every text value and the kind, never identifiers, links or enum values) and filtering the finished bags too. It SHALL offer the app's sort keys in the same order, open in the device's saved sort, and save a sort chosen there as the same preference. It SHALL say nothing matches only when neither the open nor the finished bags match.

#### Scenario: Search on the web
- **WHEN** the user types a tasting note into the `/beans` search
- **THEN** only bags carrying that note are listed, finished ones included

#### Scenario: Matches only among finished bags
- **WHEN** a web search matches only finished bags
- **THEN** the page does not say nothing matches, and "Show finished (N)" counts the matches

#### Scenario: Web page shares the sort
- **WHEN** the user picks a sort on `/beans` and then opens Beans in the app
- **THEN** the app shows the bags in that order
