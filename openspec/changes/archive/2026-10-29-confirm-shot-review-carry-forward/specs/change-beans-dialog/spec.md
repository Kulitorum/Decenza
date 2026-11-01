# Spec Delta

## MODIFIED Requirements

### Requirement: Selection semantics depend on opening context
What happens when a bag is selected SHALL depend on where the dialog was opened:
- **Brew settings / Beans window / idle page**: set `activeBagId` (affects the next shot)
- **Post-shot review and historical shot detail**: update only the shot's snapshot (bean fields, `bagId`, `frozenDate`, `defrostDate` and other storage dates). A latest-shot correction can be offered for the next shot separately under `shot-review-carry-forward`.

#### Scenario: Fixing the wrong bag after a shot
- **WHEN** the user opens Change Beans from the post-shot review page and selects a different bag
- **THEN** the just-saved shot's snapshot SHALL be updated to the selected bag's fields
- **AND** the active bag and next-shot settings SHALL remain unchanged until a carry-forward offer is accepted

#### Scenario: Re-linking a historical shot
- **WHEN** the user opens Change Beans from a historical shot's detail page and selects a bag
- **THEN** that shot's snapshot SHALL be updated
- **AND** `activeBagId` SHALL remain unchanged
