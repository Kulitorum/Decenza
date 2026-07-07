# bean-base-search Delta

## MODIFIED Requirements

### Requirement: Cached bean attributes are user-visible on all bean surfaces

The cached bean attributes (image, origin, region, farm, producer, variety, process, elevation, bean type, quality score, place of purchase, tasting tags, tasting notes, harvest, product link) SHALL be viewable by the user — not just consumed by upload and advisor plumbing — via a shared details component mounted on the Beans page, the post-shot review page, and the shot history/detail page. This applies to canonical-supplied and user-entered values alike (per `bag-detail-editing`).

#### Scenario: Viewing details for the current bean
- **WHEN** the active bean carries bean details (canonical or user-entered) and the user opens the Beans page
- **THEN** a compact details row (bag image thumbnail + origin · variety · process summary) is shown in the Bean section
- **AND** tapping it opens a details popup rendering all stored attributes plus a "View at roaster" link when a URL exists

#### Scenario: Viewing details on a past shot
- **WHEN** the user opens the post-shot review page or a shot from history whose record carries a non-empty `beanbase_json` snapshot
- **THEN** the same details affordance is shown next to that shot's bean information
- **AND** the popup is fed from the SHOT's stored snapshot, not the live DYE state — a historical shot shows the bean it was actually pulled with

#### Scenario: Empty blobs and legacy shots have zero footprint
- **WHEN** the blob is empty (no canonical link AND no user-entered details, or a shot saved before this feature)
- **THEN** no details row, image, or popup affordance is rendered — the page looks exactly as it does today
- **AND** an unlinked bag WITH user-entered details renders the details row/popup like any linked bag
