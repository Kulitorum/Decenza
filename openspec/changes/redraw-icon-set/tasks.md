## 1. Decide whether to do this at all

The honest first question. The current set is not bad, and D4 says an icon that is not clearly
better should not ship — which admits the possibility that the answer is "leave them".

- [ ] 1.1 Pick 3–4 of the most-seen icons (idle screen and bottom bar) and draw ONE alternative
      treatment for each. Not the whole set — enough to see whether craft-within-monochrome
      actually reads better at 20px.
- [ ] 1.2 Render them side by side with the current icons at 20px AND at tile size, on dark and on
      light. Use the contact-sheet workflow in design.md D3.
- [ ] 1.3 Show Jeff. If the alternatives are not clearly better, STOP and archive this change with
      that finding — "we looked and the current set is close to the ceiling" is a real result.
- [ ] 1.4 If they are better, name what specifically makes them better (weight contrast? optical
      sizing? terminals?) so the remaining 64 have a brief rather than a vibe.

## 2. Explore the cheaper middle path first (design.md Open Questions)

- [ ] 2.1 Before committing to a full redraw, try keeping each icon's geometry and adjusting only
      stroke weight, terminals and optical sizing. Compare against both the current set and the
      task-1 alternatives.
- [ ] 2.2 Decide: full redraw, refinement pass, or nothing. Record the reasoning.

## 3. The redraw (only if task 2 says so)

- [ ] 3.1 Draw the set. Prioritise by visibility — the idle screen and bottom bar are seen
      constantly; AlignCenter is not.
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
- [ ] 4.4 Consider whether a test can catch this class at all. Probably not statically — but say
      so deliberately rather than leaving it unexamined.

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
