# Icon System

Chrome icons are monochrome SVGs in `resources/icons/` (~76 of them), referenced by `qrc:/icons/<name>.svg`. They are tinted to `Theme.iconColor` so they follow light/dark and state colour. This is the opposite constraint from emoji, which carry fixed colour — see `EMOJI_SYSTEM.md` for where each belongs.

## What makes an icon acceptable here

1. **Depicts its subject, not its category.** A grinder, not a toolbox. ☕ worked as a swap because it *is* coffee; 🧰 for Equipment failed because a toolbox is the category "tools". The name is not the test — render it and look.
2. **Recolourable: one fill, or `fill="none"` + stroke.** NOT multi-path filled artwork. The app recolours by replacing the icon's luminance with `Theme.iconColor` (MultiEffect colorization); multi-hue or baked-colour artwork flattens or fights the theme.
3. **Survives STATE colour.** Tiles turn orange/highlighted/warning and the icon is recoloured to contrast. A fixed-palette icon cannot — the red-grey chart emoji on an orange active tile was unreadable.
4. **Legible at 20px** (real bottom-bar size), not just at tile size.
5. **Consistent with the set.** Half the set in a new style reads as unfinished; a redraw is complete or not started.

## The three render paths — an icon must work through all three

| Path | What it does |
|---|---|
| `ThemedIcon { source }` | tints to `Theme.iconColor` via `MultiEffect { colorization: 1.0 }` |
| inline `layer.effect: MultiEffect` | same effect, hand-written per component (the bulk of tinted usage) |
| plain `Image { source }` | **no tint** — renders the SVG's own stroke colour |

**The plain-`Image` path is the trap.** A white-stroked SVG shown as a plain `Image` on a light surface (`Theme.surfaceColor` is `#ffffff` in light mode) is **invisible**. Chrome icons on a surface that can be light must route through `ThemedIcon` or an inline MultiEffect, never a bare `Image`.

## Colorization is luminance × tint, alpha preserved (the duotone gate)

Qt's `MultiEffect { colorization: 1.0; colorizationColor: C }` computes `rgb = luminance(source) · C` and leaves alpha untouched (`qtdeclarative/.../multieffect.frag:84,116`). Two consequences:

- **The source must be WHITE.** A dark source has low luminance → the tint comes out dark or black regardless of `C`. Every icon here is white-stroked for this reason. An SVG using `fill="currentColor"` resolves to **black** in Qt (no colour context; `qtsvg/.../qsvghandler.cpp:4200`) → tint lost. White-normalize it (`fill="#fff"`, keep opacity) before adopting.
- **Per-path alpha survives**, so a two-opacity (duotone) icon keeps both values through colorization *once white-normalized* — depth from value, not hue. Verified empirically (OpenSpec `redraw-icon-set`, task 1.3a): a tint-prefilled duotone renders a full-opacity path at the tint and a 0.2-opacity path as a 20% blend toward the tile.

## Surveying / replacing the set

`scripts/download_icons.py` surveys candidate families (Phosphor MIT, Material Symbols Apache-2.0) pinned to a tag/SHA, staging into git-ignored `.icon-survey/` — it does **not** touch `resources/icons/`. Judge candidates by rendering a contact sheet at real sizes on real tiles (never by name-matching a library index). Adopting into `resources/icons/` is a separate, owner-gated step; keep filenames identical so the ~274 QML references are untouched. The emoji experiment that motivated all of this is recorded in `openspec/changes/.../fix-emoji-crash-and-translation-staleness` and the `redraw-icon-set` design.
