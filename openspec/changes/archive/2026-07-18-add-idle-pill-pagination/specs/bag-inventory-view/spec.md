## MODIFIED Requirements

### Requirement: Idle-page bean widget shows inventory bags
The idle-page bean layout widget (`BeansItem.qml`) SHALL display inventory bags (`inInventory = true`, MRU order) as selectable pills, replacing the showOnIdle-filtered preset pills. Tapping a pill SHALL set `activeBagId`. The full inventory remains on the Beans page.

The pill row SHALL show at most five bags at a time and SHALL paginate the full MRU inventory in pages of five via prev/next arrows. The previous arrow SHALL be shown only when a previous page exists (not on the first page) and the next arrow only when a further page exists (not on the last page); with five or fewer inventory bags neither arrow SHALL appear and the row SHALL be visually identical to the non-paginated row. Paging SHALL change only which five bags are visible — it SHALL NOT change `activeBagId` or reorder the inventory. Opening the widget SHALL start on the first page (the most-recent five). When the inventory changes, the current page SHALL be clamped to remain within range.

#### Scenario: Selecting a bag from the idle page
- **WHEN** the user taps a bag pill on the idle page
- **THEN** `activeBagId` SHALL be set to that bag
- **AND** the pill SHALL show a selected state

#### Scenario: Bag marked empty disappears from idle widget
- **WHEN** a bag is marked empty
- **THEN** its pill SHALL be removed from the idle widget (visibility criterion is `inInventory`, the dropped `showOnIdle` flag has no successor)

#### Scenario: Paging to reach older bags
- **WHEN** more than five inventory bags exist and the user taps the next arrow
- **THEN** the row shows the next five bags in MRU order without changing `activeBagId`

#### Scenario: Few bags need no arrows
- **WHEN** five or fewer inventory bags exist
- **THEN** the pill row SHALL show no pagination arrows and appear exactly as the non-paginated row did
