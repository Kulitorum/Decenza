## ADDED Requirements

### Requirement: Shot detail metrics row shows a clean five-metric row
The Shot Detail metrics row SHALL show Duration, Dose, Output, Ratio, and Rating, and SHALL NOT include a dedicated Grind cell. Grind is a per-shot dial-in surfaced in the Shot Plan snapshot line and on its owning card (recipe or bean), not a standalone metric.

#### Scenario: Metrics row omits grind
- **WHEN** the user opens a shot and views the metrics row
- **THEN** it shows Duration, Dose, Output, Ratio, and Rating, with no dedicated Grind cell

## REMOVED Requirements

### Requirement: Shot detail metrics row shows grind setting
**Reason**: Grind is a per-shot dial-in, not a standalone metric. It now appears in the top Shot Plan snapshot line (glanceable) and on its owning card (recipe or bean), so the dedicated metrics-row Grind cell is redundant and is removed to keep a clean five-metric row.
**Migration**: Read the shot's grind in the Shot Plan snapshot line at the top of the page, or on the recipe card (when a recipe was used) or the bean card (otherwise). See the `shot-plan-snapshot-line` and `shot-recipe-card` capabilities.

### Requirement: Grind cell shows RPM as a caption suffix
**Reason**: The metrics-row Grind cell is removed; its RPM suffix goes with it. RPM now renders in the snapshot line and the owning card via the shared Shot Plan grind format.
**Migration**: Read grind/RPM from the snapshot line or the owning card.

### Requirement: Grind cell is accessible
**Reason**: The metrics-row Grind cell is removed, so its accessibility contract no longer applies. The snapshot line and the owning card carry their own accessibility requirements.
**Migration**: Accessibility for grind/RPM is defined by the `shot-plan-snapshot-line` and `shot-recipe-card` capabilities.

### Requirement: Equipment card keeps its grind dial-in line
**Reason**: Cards must show only the data they keep. Grind/RPM is a dial-in that belongs to the bean bag or the recipe, never to equipment, so the Equipment card no longer shows a grind dial-in line.
**Migration**: Grind/RPM now renders on the recipe card (when the shot used a recipe) or the bean card (otherwise), per the `shot-recipe-card` capability. The Equipment card shows grinder, burrs, basket, and puck prep only.
