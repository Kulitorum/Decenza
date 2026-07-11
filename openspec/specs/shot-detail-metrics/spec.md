# shot-detail-metrics Specification

## Purpose
Defines the shot detail page's top-level Grind metric cell — its position in the metrics row, its RPM-suffix formatting, and its combined accessible name — and clarifies that it supplements rather than replaces the Equipment card's existing grind dial-in line.
## Requirements
### Requirement: Shot detail metrics row shows a clean five-metric row
The Shot Detail metrics row SHALL show Duration, Dose, Output, Ratio, and Rating, and SHALL NOT include a dedicated Grind cell. Grind is a per-shot dial-in surfaced in the Shot Plan snapshot line and on its owning card (recipe or bean), not a standalone metric.

#### Scenario: Metrics row omits grind
- **WHEN** the user opens a shot and views the metrics row
- **THEN** it shows Duration, Dose, Output, Ratio, and Rating, with no dedicated Grind cell

