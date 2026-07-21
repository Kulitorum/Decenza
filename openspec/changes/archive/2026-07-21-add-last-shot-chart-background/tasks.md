## 1. Make the background source explicit

- [x] 1.1 Add `backgroundSource` (`"none" | "colour" | "image" | "shot"`) to `SettingsTheme`,
      with the two shot entries distinguished by a companion value for the advanced split
- [x] 1.2 Route the existing `setBackgroundPreset` / `setBackgroundImagePath` setters through it
      so the source is written in ONE place — the pairwise clearing they do today becomes
      quadratic with a third kind, and two-sources-at-once is the failure mode
- [x] 1.3 Derive the source on first read for existing installs (preset set → `colour`, image
      path set → `image`, neither → `none`). No stored-value rewriting: the derivation is
      unambiguous, and rewriting user-set data is against project rules
- [x] 1.4 Reject an unknown source value with a `qWarning` rather than silently erasing the
      background, matching how unknown preset/pattern ids are already handled
- [x] 1.5 Tests: three-way exclusivity in both directions for all six ordered pairs; the
      migration derivation; unknown-value rejection; round-trip through a backup

## 2. Load the last shot once

- [x] 2.1 Add an app-wide holder that fetches the most recent shot's series via the existing
      `requestMostRecentShotId()` + async `requestShot()` path
- [x] 2.2 Refresh on shot-completed, and on shot-deleted when the deleted shot is the one drawn
- [x] 2.3 Confirm no database work happens on the main thread (project rule)
- [x] 2.4 Expose a "nothing to draw" state distinct from "still loading", so the fallback does
      not flash on a cold start
- [~] 2.5 Tests for the holder: NOT unit-testable — it is a QML singleton wired to storage
      signals and this suite is C++ with no QML test harness. Covered by the live checks in
      8.4 instead; stated here rather than quietly dropped

## 3. Render the chart once to an image

- [x] 3.1 Instantiate `HistoryShotGraph` OFFSCREEN at background size — the same component the
      review page uses, reduced via its existing `showLabels` / `showPhaseLabels` properties.
      Do NOT fork a background-only chart renderer
- [x] 3.2 Bind per-line visibility to the existing `graph/show*` keys, and `advancedMode` to the
      chosen ENTRY rather than to `shotReview/advancedMode`
- [x] 3.3 `grabToImage()` the chart, hold the result, destroy the chart. **Expect this to need
      iteration:** grabbing an item that has never been rendered can return empty, because the
      grab needs a window and a live scene graph. Prove it produces a real image before building
      anything on top of it
- [x] 3.4 Key the cache on (shot id, advanced flag, theme identity, visibility set, size) and
      re-render on any key change — the invalidation table in design.md. Keying is what makes
      this correct; remembering to invalidate at each call site is what makes it wrong later
- [x] 3.5 Verify a theme change re-renders. A stale render is the failure mode this design
      trades render cost for, and it is silent — the wrong blue behind the right theme
- [x] 3.6 Tests for the cache key. No Qt Quick Test harness exists in this repo and the key
      lives in QML, so the ARITHMETIC is verified live; what is tested in C++ is the key's
      COMPLETENESS against the settings HistoryShotGraph actually reads. It caught
      showWeightAxis missing on its first run

## 4. Draw it

- [x] 4.1 Draw the cached image in `BackgroundSurface` through the existing IMAGE path, so scrim,
      glass chrome and fallback are the same code rather than a parallel implementation
- [x] 4.2 Confirm inertness follows for free — an `Image` takes no input — and that no chart
      instance survives behind any page
- [x] 4.3 Fall back to the plain theme background when there is nothing to draw — no empty axes,
      no spinner, no error
- [x] 4.4 Leave the foreground derivation alone: the canvas is the theme's own background colour,
      and deriving from drawn pixels would shift every text colour when a shot finished

## 5. Wire the shared predicate

- [x] 5.1 Extend `Theme.glassChrome` to include the shot source. This must stay a one-line change
      to the predicate — if any chrome call site needs touching, the predicate has leaked
- [x] 5.2 Audit `hasBackgroundImage` / `hasBackgroundPreset` readers for sites that assume the
      background is one of exactly two kinds
- [x] 5.3 Confirm the documented split still holds: a fill that exists to LOOK a certain way is
      gated on `glassChrome`; a colour that exists to stay READABLE is gated on the preset

## 6. Chooser

- [x] 6.1 Add the "Shot" section with the two entries, between "Colours & patterns" and "Images"
- [x] 6.2 Disable the Pattern row with its reason visible while a shot entry is highlighted;
      retain the stored pattern so returning to a colour restores it
- [x] 6.3 Make the tiles and the preview show what the page will render, including the forced
      glass chrome — the chooser's promise is kept by construction, via `BackgroundSurface`
- [x] 6.4 Handle the empty-history case in the chooser: the entries stay selectable and say why
      nothing is drawn, rather than vanishing
- [x] 6.5 Accessibility: both entries need `Accessible.name`, focusability and selected-state
      announcement, matching the preset tiles

## 7. Translations

- [x] 7.1 Add keys for the two entry names, the "Shot" section heading, the disabled-pattern
      reason, and the empty-history explanation

## 8. Verify

- [x] 8.1 Full local suite green — there is no PR CI gate, so this is the gate
- [x] 8.2 `qmllint` with `-I` on the build dir, unqualified access ON: undeclared QML identifiers
      compile clean and fail only at runtime
- [x] 8.3 Live in the running app: every idle-screen control still responds with the chart behind
      it (a chart that eats taps is the worst failure here)
- [x] 8.4 Live: pull a shot and confirm the background updates; delete it and confirm it refreshes
- [x] 8.5 Live: toggle curves in the review legend and confirm the background follows
- [x] 8.6 Live: switch between colour, image and shot backgrounds in every order; confirm no
      combination leaves two sources drawn or the pattern stuck
- [x] 8.7 Live: apply a different theme and confirm the background re-renders in its colours.
      This is the stale-cache failure, and it is silent — check it deliberately
- [~] 8.8 DEFERRED to the beta (same as #1584's moiré check): **On a real TABLET, time the one-off grab at shot end.** Steady state is a texture, so
      per-page cost is no longer the question; what matters is whether the render stalls the app
      at the moment a shot finishes. Note the number in the PR either way
- [~] 8.9 PARTIAL: the render adapts to a light theme (verified via image dump); the composited page needs an eyeball in the beta: Confirm the light-theme case: the chart on a light canvas, scrimmed, still readable

## 9. Docs

- [x] 9.1 Update the wiki manual (`Kulitorum/Decenza.wiki`, `Manual` page): the two entries, that
      per-line visibility is shared with the review page, that the entry chooses basic vs
      advanced, and that patterns do not apply
- [x] 9.2 Re-read the delta specs against what actually shipped before archiving — on the last
      background change the delta had gone stale on its own implementation and would have been
      promoted contradicting the code
- [ ] 9.3 Run `openspec archive add-last-shot-chart-background` as the final commit on the branch,
      before merge
