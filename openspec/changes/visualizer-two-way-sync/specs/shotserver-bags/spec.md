## ADDED Requirements

### Requirement: Finished bags on the web Beans page

`GET /api/bags/finished` SHALL return the finished bags in the same shape as `GET /api/bags`. `POST /api/bag/<id>/restore` SHALL return a finished bag to inventory. The `/beans` page SHALL show a "Show finished (N)" toggle listing them as dimmed cards with Restock, Restore, Edit and Info, and Restock SHALL open the new-bag editor prefilled from the finished bag as the app does. A failed bag read SHALL answer 500, never an empty list or "Bag not found".

#### Scenario: Restock from the web
- **WHEN** the user taps Restock on a finished bag on the web Beans page
- **THEN** the editor opens as a new bag with that bag's identity and details, its dates and notes blank
