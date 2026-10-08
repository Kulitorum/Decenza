# Spec Delta

## REMOVED Requirements

### Requirement: Equipment card appears last in the review field grid
**Reason**: The review page's field grid is replaced by the shot page's single-column layout.
**Migration**: See the `shot-page` requirement "The shot page is one column with the graph at full width". The equipment card remains the last card.

### Requirement: Review field grid reorder preserves behavior
**Reason**: The field grid this requirement protected no longer exists.
**Migration**: See the `shot-page` requirement "Merging the pages preserves editing behaviour", which carries the same autosave, undo and reading-order guarantees.
