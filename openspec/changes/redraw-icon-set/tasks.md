## 1. Survey icon libraries the way we surveyed emoji sets

`scripts/download_emoji.py` already carries a multi-source abstraction — twemoji, openmoji, noto,
fluentui — each pinned to a tag, fetched and committed. Icons deserve the same treatment: survey
several families, measure coverage against OUR concepts, and pin whatever wins. 22 of the current
68 are already stock Feather/Lucide (`sleep.svg` is Feather's `power` verbatim), so this is not a
departure — it is doing deliberately what was already done ad hoc.

- [ ] 1.1 Survey candidate families. Record licence, icon count, available weights/styles, and
      whether they ship SVG suitable for recolouring (`fill="none"` + stroke, or a single fill —
      NOT multi-path artwork, which breaks MultiEffect colorization). Starting list:
        Phosphor (MIT) — 6 weights incl. DUOTONE, the closest thing to craft-within-monochrome
        Material Symbols (Apache 2.0) — variable axes: optical size, weight, grade, FILL
        Lucide (ISC) — the maintained Feather fork; same flat ceiling as today, but consistent
        Tabler (MIT) — very large, stroke + filled variants
        Iconoir (MIT), Remix Icon (Apache 2.0), Heroicons (MIT), Bootstrap Icons (MIT)
        Font Awesome free (CC-BY 4.0 — attribution obligation, note it)
      Add any others found; the point is breadth before choosing.
- [ ] 1.2 Measure COVERAGE against our 68 concepts, per family. Do it by rendering a contact sheet
      of each family's candidate for each of our icons and LOOKING — the emoji audit proved that
      name-matching a library's icon list produces confident nonsense. Expect roughly 50 generic
      concepts to be well covered and ~15 domain ones (decent-de1, niche-zero, espresso 8mm,
      flush 8mm, grind, coffeebeans, taste-*, body-*) to be covered by nobody.
- [ ] 1.3 Head-to-head on the FOUR most-seen icons (idle screen + bottom bar). For each: current
      icon vs Phosphor DUOTONE vs Material Symbols (a couple of axis settings) vs the other
      shortlisted families. Render at 20px AND tile size, dark and light, and on a state-coloured
      (active/orange) tile — the case that broke the emoji swap and that nobody predicted.
      A first pass on coffee/gear/drop is already done and duotone survives all three grounds
      (see design.md D1); 1.3 is the real comparison at the sizes actually used, since duotone
      depth is subtler at 20px than at 26px.
- [ ] 1.3a Prefer DUOTONE weights wherever a family offers them — that is where the depth comes
      from. Confirm the depth survives Qt's `MultiEffect { colorization: 1.0 }` specifically:
      the CSS proof used `currentColor`, and Qt's colorization path is NOT the same code. If
      colorization flattens the two opacities into one, duotone is off the table and that needs
      knowing at 1.3, not at 3.4.
- [ ] 1.4 Show Jeff. If nothing is clearly better than what we have, STOP and archive with that
      finding. "We surveyed the field and kept ours" is a real result and the one most likely to
      be re-litigated otherwise.
- [ ] 1.5 If something wins, name WHY in concrete terms (duotone depth? optical sizing? terminals?
      weight contrast?), so the remaining icons have a brief rather than a vibe.

## 2. Fill the gaps the libraries cannot

- [ ] 2.1 For the ~15 domain-specific concepts no family covers, evaluate vector-native AI tools:
      Recraft (SVG output, icon-set style consistency), Adobe Firefly Vector / Illustrator
      generative vector. Judge output on whether it is RECOLOURABLE — most AI vector output uses
      fills and multiple paths, which breaks tinting and is the single most likely failure.
- [ ] 2.2 Do NOT use raster generators (Midjourney, DALL-E, Stable Diffusion) and vectorise. The
      trace produces dozens of filled paths, colorization breaks, and cross-icon consistency is
      their known weakness. Recorded so it is not re-tried.
- [ ] 2.3 Whatever the source, the domain icons must sit in the SAME visual language as the family
      chosen in task 1 — matching stroke weight, terminals, corner radius, optical size. This is
      the hard part and the most likely reason to abandon.
- [ ] 2.4 Consider the cheaper middle path before any of this: keep the current geometry and adjust
      only stroke weight, terminals and optical sizing. It may deliver most of the perceived gain.

## 3. Adopt (only if tasks 1-2 produce a clear winner)

- [ ] 3.1 Bring in the chosen family, pinned to a tag, with a script in the shape of
      `download_emoji.py` so the set is reproducible and refreshable rather than hand-copied.
      Prioritise by visibility — the idle screen and bottom bar are seen constantly;
      AlignCenter is not.
- [ ] 3.2 Keep filenames and paths identical, so none of the ~274 QML references change.
- [ ] 3.3 Keep every icon recolourable — no baked-in fills or strokes the app cannot override.
      This is what lets a tinted icon stay legible on a state-coloured tile.
- [ ] 3.4 Verify each icon through ALL THREE rendering paths (ThemedIcon, inline
      `layer.effect: MultiEffect`, and plain `Image`). All three are in use; do not assume which.
- [ ] 3.5 Per-icon comparison against the current set at 20px and tile size, dark and light.
      Anything not clearly better keeps its existing icon.
- [ ] 3.6 Land the whole set in one release. A half-restyled set is the mixed-language failure the
      emoji swap demonstrated.

## 4. Light-mode visibility sweep (independent — do NOT wait for the redraw)

- [ ] 4.1 Walk the app in light mode and look for icons that vanish. The known shape is a plain
      `Image` + white-stroked SVG on a light surface (`Theme.surfaceColor` is #ffffff in light
      mode). One instance was found and fixed in the Settings search button; the sweep was never
      completed.
- [ ] 4.2 Do this by LOOKING, not by grepping. Two static attempts to measure which icons are
      tinted returned 17/274 and then 62/274, both wrong, because tinting happens through more
      paths than a grep finds.
- [ ] 4.3 Fix each instance by routing it through `ThemedIcon` (or the inline MultiEffect pattern
      already used nearby).
- [ ] 4.4 Consider whether a test can catch the LIGHT-MODE class. Probably not statically — tinting
      happens through three rendering paths and two grep attempts already returned wrong answers.
      But note the ADJACENT class IS catchable and the guess here was wrong once already: banned
      font glyphs (the #1537 class) are found definitively by reading DecenzaSans' cmap and checking
      every QML string literal against it. `scripts/check_font_glyph_coverage.py` does this; it found
      29 sites across 6 glyph types where hand-grepping had found 16 across 1. See
      `fix-emoji-crash-and-translation-staleness/tasks.md` 7.8/7.8b.

## 5. Documentation

- [ ] 5.1 Add a short note to `docs/CLAUDE_MD/` on what makes an icon acceptable here: depicts its
      subject, recolourable, legible at 20px, consistent with the set. Point at this spec.
- [ ] 5.2 Cross-reference the emoji decision so the two are readable together: emoji belong in
      user-authored and externally-sourced TEXT, not in chrome. The reasoning lives in
      `fix-emoji-crash-and-translation-staleness/tasks.md` 7.5.

## 6. Close-out

- [ ] 6.1 Archive with `/opsx:archive` as the last commit before merge — including the case where
      the outcome is "we looked and kept what we had". That is the result most worth recording,
      because it is the one someone will otherwise re-litigate.
