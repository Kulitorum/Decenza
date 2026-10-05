## ADDED Requirements

### Requirement: Web Beans search and sort

The `/beans` web page SHALL offer the search field and sort controls of the web `/recipes` page, matching bags as the app does (every text value, never identifiers or links) and filtering the finished bags too.

#### Scenario: Search on the web
- **WHEN** the user types a tasting note into the `/beans` search
- **THEN** only bags carrying that note are listed, finished ones included
