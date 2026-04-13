# Cup Fill View

The espresso extraction cup visualization (`qml/components/CupFillView.qml`) uses a hybrid image+procedural approach.

## Layer Stack

```
1. BackGround.png (Image)     — cup back, interior, handle
2. Coffee Canvas (masked)     — liquid fill, crema, waves, ripples
3. Effects Canvas (unmasked)  — stream, steam wisps, completion glow
4. Overlay.png (lighten blend) — rim, front wall highlights
5. Weight text overlay
```

## GPU Shaders (require Qt6 ShaderTools)

- **cup_mask.frag**: Masks coffee to cup interior using Mask.png (black = coffee visible, inverted in shader)
- **cup_lighten.frag**: MAX blend per channel with brightness-to-alpha (black areas become transparent)
- Compiled via `qt_add_shaders()` in CMakeLists.txt to `.qsb` format

## Updating Cup Images

1. Edit `resources/CoffeeCup/FullRes.7z` (contains source PSD)
2. Export three layers as 701x432 RGBA PNGs:
   - `BackGround.png` — everything behind the coffee (on transparent/black)
   - `Mask.png` — black silhouette of cup interior, white elsewhere
   - `Overlay.png` — everything in front of coffee (on black, for lighten blend)
3. Rebuild (images are in `resources.qrc`)

## Coffee Geometry

The procedural coffee rendering uses proportional coordinates relative to the cup image dimensions. Key geometry is defined in `cupGeometry()`: cup center at 44% width, rim at 6% height, bottom at 92% height. Adjust these if the cup shape changes.
