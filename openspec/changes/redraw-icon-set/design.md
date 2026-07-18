## Context

68 SVGs in `resources/icons/`, referenced ~274 times across `qml/`. All are flat line art: a 2px
stroke, `fill="none"`, hardcoded white or `#fff`. They are rendered through three different
mechanisms, all of which are in active use:

| Mechanism | Roughly | Behaviour |
|---|---|---|
| `ThemedIcon` | a small minority | `MultiEffect { colorization: 1.0; colorizationColor: Theme.iconColor }` |
| Inline `layer.effect: MultiEffect` | the bulk of tinted usage | same effect, hand-written per component |
| Plain `Image` | the remainder | no tint — renders the SVG's own white stroke |

A redraw has to survive all three. The measured split is deliberately not stated here as a number:
two attempts to count it produced 17/274 and then 62/274, both wrong, because tinting happens
through more paths than a grep finds. **Do not plan against a grep of this.**

### What the emoji experiment established

Recorded because these were expensive, and every one is a constraint on the redraw.

**1. A pictogram must depict its subject, not gesture at its category.**
☕ replacing `espresso.svg` worked because it *is* coffee. 🧰 for Equipment failed because a toolbox
is not a grinder — it is the category "tools". The same error in different clothes: 🕐 for History
(the icon draws a list of past shots), 🫘 for Beans (red kidney beans; coffee beans have a centre
crease), 📈 for Profiles (an abstract rising chart replacing a drawn pressure curve).

**2. Fixed-palette artwork cannot respond to STATE colour.** This one was not predicted by anyone
and only appeared because a tile happened to be orange when we looked. Tiles change background to
signal state — active, highlighted, warning. A tinted monochrome icon is recoloured to contrast
with whatever the tile is doing. A full-colour pictogram cannot: the red-and-grey chart emoji on
the orange active Profiles tile was unreadable. **Any redraw that bakes in colour inherits this.**

**3. A carried palette clashes with the app's.** Twemoji's toolbox is bright red; the UI is teal.
Artwork that brings its own colours will fight the theme somewhere.

**4. Mixed sets read as unfinished.** ~27 of the 68 concepts have no emoji equivalent at all, so a
partial swap is the destination, not a transition. In the final screenshot the single reverted icon
sat between two emoji and looked more native than either. **A redraw must therefore be complete or
not started** — half a set in a new style is the same failure.

**5. Classifying icons by NAME is worthless.** The whole audit's candidate list was built by mapping
filenames and tile labels to emoji, and it was wrong in both directions: it proposed swaps for
`scale` (a kitchen scale) using the justice-scales emoji while rejecting `taste-balanced`, which
*is* a balance. It rejected `Graph`, the sort arrows, and the checkbox pair, all of which have exact
matches. The classification only became useful after all 68 were rendered to a contact sheet and
looked at.

**6. Verify by looking, not by grepping.** Point 5, the tint counts, and the light-mode question
below were each answered wrongly by static analysis and correctly by rendering. This is the single
most transferable lesson in the file.

### Light-mode visibility

A real bug found by switching the running app to light mode: the Settings search button drew a
white-stroked `search.svg` as a plain `Image` on a `Theme.surfaceColor` background — `#ffffff` in
light mode — so it was invisible. Fixed at both call sites. **The sweep for other instances was
never done**, and the shape is general: plain `Image` + white-stroked SVG + light surface.

## Goals / Non-Goals

**Goals:**
- Icons with more visual craft than a uniform 2px stroke — the quality that made ☕ read better.
- Keep every property the current set has: theme tinting, state-colour response, 20px legibility,
  and coverage of concepts with no emoji.
- Encode the constraints above as requirements a future attempt cannot skip.
- No QML changes: same filenames, same paths.

**Non-Goals:**
- Emoji as chrome. Settled and reverted; `tasks.md` in `fix-emoji-crash-and-translation-staleness`
  records why. Emoji keep their place in user-authored and externally-sourced TEXT.
- Changing the rendering mechanisms. Three exist; consolidating them is separate work.
- Redesigning what the icons mean. The vocabulary is right — `grind` should still be grounds,
  `sleep` still a power symbol.
- A general light/dark visual overhaul.

## Decisions

### D1: Monochrome stays a hard constraint; craft comes from form

The temptation is to add colour, since colour is what made the emoji feel richer. **Colour is
exactly what disqualified them.** Two independent reasons:

- State: tiles recolour to signal active/warning, and only a tintable icon stays legible.
- Theme: light mode tints icons to `#1a1a2e`, dark to white, and users can set custom palettes.

So the craft has to come from everything except hue: weight contrast within a single stroke,
considered negative space, a suggestion of depth through line weight rather than shading, and
silhouettes that read at a glance. This is a harder brief than "make them colourful", and it is the
only one compatible with the constraints.

*Alternative considered:* a colour set for non-state surfaces plus a monochrome set for tiles.
Rejected — it recreates the mixed-set problem (point 4) and doubles the assets.

### D2: Redraw all 68, or none

Point 4 again. A new style landing on half the set produces exactly what the emoji swap produced:
two visual languages on one screen. If the work is staged across PRs, it must be staged so that no
release ships a partial set — e.g. draw everything, land once.

### D3: Judge every icon by rendering it, not by describing it

The contact-sheet workflow that finally worked:

```bash
# inline every SVG as a data URI into a grid, render headless, look at the PNG
python3 scripts/... > contact.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless \
  --screenshot=contact.png --window-size=1500,1250 file://.../contact.html
```

Two rounds of static analysis produced wrong answers before this. Any acceptance check on the
redraw must be visual, at real sizes, on real backgrounds, in both themes.

### D4: The bar is the current set, per icon

"Better" is only meaningful against what is there now. Each redrawn icon should be compared
side-by-side with the one it replaces, at 20px and at tile size, on dark and light. An icon that is
not clearly better should not ship — the current set is not bad, and churn is not improvement.

## Risks / Trade-offs

- **A redraw makes things worse.** → D4's per-icon comparison, and the same "render it and look"
  discipline that reverted the emoji swap. The willingness to revert is the safeguard.
- **Craft-within-monochrome is a genuinely hard brief** → It may turn out that the current icons are
  close to the ceiling for flat monochrome at 20px, and that the honest answer is "leave them".
  That is an acceptable outcome of this change; discovering it cheaply is worth more than assuming
  either way.
- **68 icons is a lot of drawing.** → Prioritise by visibility: the idle screen and bottom bar are
  seen constantly; `AlignCenter` is not.
- **Complete-or-nothing (D2) conflicts with incremental review.** → Draw incrementally, land once.

## Open Questions

- Who draws these? The constraints are specifiable; the craft is not something this change can
  assert it will produce.
- Is there a middle path — keeping the geometry and only adjusting weight, terminals and optical
  sizing? Cheaper, lower risk, and possibly most of the perceived gain.
- Does the light-mode sweep (unfinished) belong here or on its own? It is a correctness bug, not a
  craft question, and it should probably not wait for a redraw.
