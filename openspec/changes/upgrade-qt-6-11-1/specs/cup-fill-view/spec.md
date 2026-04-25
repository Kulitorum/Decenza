## ADDED Requirements

### Requirement: GPU-Accelerated Cup Animation
The cup fill animation SHALL render liquid fill, crema, wave, and steam layers via Qt Canvas Painter (`CanvasPainterItem`) using the platform's native GPU backend (Metal on iOS/macOS, Direct3D on Windows, Vulkan/OpenGL on Android/Linux).

#### Scenario: Live extraction on tablet
- **WHEN** an espresso extraction is in progress and `currentFlow > 0.1`
- **THEN** the liquid fill, wave, and steam animations update at the animation timer cadence with rendering executed on the GPU, freeing CPU cycles for BLE data processing and shot graph updates

#### Scenario: Visual fidelity matches Canvas baseline
- **WHEN** the app is running on any supported platform with Qt Canvas Painter available
- **THEN** the cup fill visualization is visually indistinguishable from the prior `Canvas`-based rendering — same gradients, wave geometry, crema layer, steam wisps, and completion glow
