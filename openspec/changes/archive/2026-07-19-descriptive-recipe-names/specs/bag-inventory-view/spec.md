## MODIFIED Requirements

### Requirement: Idle-page bean widget shows inventory bags
The idle-page bean layout widget (`BeansItem.qml`) SHALL display inventory bags (`inInventory = true`, MRU order) as selectable pills, replacing the showOnIdle-filtered preset pills. Tapping a pill SHALL set `activeBagId`. The full inventory remains on the Beans page.

The pill row SHALL show as many bags as comfortably fit within **at most two rows** at the row's current available width, and SHALL paginate the remainder of the MRU inventory via prev/next arrows. The number of bags per page SHALL be computed **live** from the actual (measured) pill widths — so longer bag names mean fewer pills per page — and MAY differ from one page to the next. The previous arrow SHALL be shown only when a previous page exists (not on the first page) and the next arrow only when a further page exists (not on the last page); when every bag fits within two rows neither arrow SHALL appear and the row SHALL be visually identical to the non-paginated row. Paging SHALL change only which bags are visible — it SHALL NOT change `activeBagId` or reorder the inventory. Opening the widget SHALL start on the first page (the most-recent bags that fit). When the inventory changes, the current page SHALL be clamped to remain within range.

This mirrors the Recipes idle widget's fit-based pagination (recipe-quick-switch) so the two widgets behave identically.

#### Scenario: Selecting a bag from the idle page
- **WHEN** the user taps a bag pill on the idle page
- **THEN** `activeBagId` SHALL be set to that bag
- **AND** the pill SHALL show a selected state

#### Scenario: Bag marked empty disappears from idle widget
- **WHEN** a bag is marked empty
- **THEN** its pill SHALL be removed from the idle widget (visibility criterion is `inInventory`, the dropped `showOnIdle` flag has no successor)

#### Scenario: Page size follows name length
- **WHEN** bags have long names such that fewer fit within two rows
- **THEN** the first page shows only as many as fit, the rest paginate, and different pages MAY show different numbers of bags

#### Scenario: Paging to reach older bags
- **WHEN** more bags exist than fit within two rows and the user taps the next arrow
- **THEN** the row shows the next group of bags (however many fit) in MRU order without changing `activeBagId`

#### Scenario: Everything fits needs no arrows
- **WHEN** every bag fits within two rows at the current width
- **THEN** the pill row SHALL show no pagination arrows and appear exactly as the non-paginated row did

#### Scenario: Never more than two rows
- **WHEN** any page of bag pills is shown
- **THEN** the pills SHALL occupy at most two rows
