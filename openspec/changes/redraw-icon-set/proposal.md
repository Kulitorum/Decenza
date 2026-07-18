## Why

The app's 68 icons are 2px-stroke line art. They work — they theme, they respond to state colour,
they stay legible at 20px — but they are visually plain, and next to a well-drawn pictogram they
look thin.

That comparison actually happened. In July 2026 we swapped 12 of the most prominent icons for
bundled Twemoji and looked at the result together. The swap was reverted, but one substitution was
clearly better than what it replaced: **☕ for `espresso.svg`**. It prompted the reaction this
change exists to act on — *"I really like the coffee cup one and it inspires me to do better, but
these swaps did not make the app better."*

The distinction that emerged is the whole point:

> **☕ worked because it is a drawing of the thing itself. Every failed swap was a substitution** —
> 🧰 standing in for a grinder, 🕐 for past shots, 🫘 for the wrong legume.

Emoji only depict a handful of things in this domain. Grinders, portafilter baskets, puck prep,
brew curves and power states are not in anyone's emoji set, because espresso is not what emoji were
drawn for. So the thing to chase is not emoji — it is the **warmth and craft** that made ☕ read
better: depth, shading, a bit of life. Those are illustration properties, not emoji properties, and
a bespoke set can have them while keeping everything the swap cost us.

**This change exists so the reasoning is not lost.** The failure modes below were expensive to find
— several only appeared when something was rendered and looked at — and every one of them is a
constraint on any future redraw.

## What Changes

- Survey icon families (Phosphor's duotone weights, Material Symbols' variable axes, and others)
  the way `download_emoji.py` already surveys emoji sets — pinned, fetched, reproducible — and
  adopt one if it is clearly better than what we have.
- Draw the ~15 domain concepts no family covers (DE1, niche-zero, portafilter/brew-group, grind,
  taste and body attributes), in the chosen family's visual language.
- Preserve every property the current set has and the emoji swap lost: theme tinting, state-colour
  response, 20px legibility.
- Record the constraints learned from the emoji experiment as testable requirements, so a future
  attempt does not rediscover them by shipping them.
- Fix the light-mode visibility class of bug found during the audit (one instance is already
  fixed; the sweep is not done).
- **Non-breaking**: icons keep their current file names and paths, so no QML call site changes.

## Capabilities

### New Capabilities
- `icon-set-quality`: what an icon in this app must do — depict its subject, survive state and
  theme colour changes, stay legible at the sizes actually used, and cover the concepts the domain
  needs.

### Modified Capabilities
None. No existing spec states requirements about iconography.

## Impact

- `resources/icons/` — 68 SVGs replaced or refined. File names unchanged, so the ~274 QML
  references are untouched. 22 are currently stock Feather/Lucide, so a third of the set is
  already borrowed — this makes that deliberate.
- Possibly a `scripts/download_icons.py` in the shape of `download_emoji.py`, so the set is
  reproducible from a pinned upstream rather than hand-copied.
- Rendering paths stay as they are: `ThemedIcon`, inline `layer.effect: MultiEffect`, and plain
  `Image`. The redraw must work through all three, because all three are in use.
- No C++ changes. No new dependencies.
- `docs/CLAUDE_MD/` — a short note on what makes an icon acceptable here, pointing at the spec.
