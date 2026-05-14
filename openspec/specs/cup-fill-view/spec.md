# cup-fill-view Specification

## Purpose
Specifies the rendering of the espresso-page cup fill animation — the liquid body, crema, wave/ripple effects, completion glow, and steam wisps. Documents the GPU-backed rendering pipeline (Qt Canvas Painter via a `JsCanvasPainterItem` wrapper around `QCanvasPainterItem`) and the visual-fidelity contract against the prior `Canvas`-based implementation, including the documented radial-gradient single-center approximation.

## Requirements
### Requirement: GPU-Accelerated Cup Animation
The cup fill animation SHALL render liquid fill, crema, wave, and steam layers via Qt Canvas Painter — specifically a `JsCanvasPainterItem` QML element backed by a `QCanvasPainterItem` subclass — using the platform's native GPU backend (Metal on iOS/macOS, Direct3D on Windows, Vulkan/OpenGL on Android/Linux).

#### Scenario: Live extraction on tablet
- **WHEN** an espresso extraction is in progress and `currentFlow > 0.1`
- **THEN** the liquid fill, wave, and steam animations update at the animation timer cadence with rendering executed on the GPU, freeing CPU cycles for BLE data processing and shot graph updates

#### Scenario: Visual fidelity matches Canvas baseline
- **WHEN** the app is running on any supported platform with Qt Canvas Painter available
- **THEN** the cup fill visualization is visually equivalent to the prior `Canvas`-based rendering — same wave geometry, crema layer, steam wisps, and completion glow. Linear gradients are pixel-equivalent; radial gradients whose Canvas 2D inner and outer circle centers differ are rendered with the gradient anchored at the inner circle's focal point (the Canvas 2D start-color origin) since `QCanvasRadialGradient` is single-centered. This preserves the "lit-from-this-side" asymmetric highlight CupFillView uses for the crema and is the documented design trade-off in `src/ui/jscanvaspainteritem.cpp`.

