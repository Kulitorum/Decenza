## 1. Decide the translation mechanism (gates everything in group 3)

- [x] 1.1 Spike D1 in isolation: add `Q_PROPERTY(QJSValue translate READ translateFn NOTIFY
      translationsChanged)` to a scratch object, bind a `Text.text` to
      `Obj.translate("k", "f")`, emit the notify, and confirm the binding re-evaluates. Do this in
      a throwaway QML file or a unit test — NOT by sweeping the codebase first.
- [x] 1.2 Measure the cost of returning the callable: confirm the function object can be cached
      across evaluations and that constructing it does not require a live binding context.
- [x] 1.3 Confirm behaviour when `translate` is read but not called, and when it is called from a
      non-binding context (a signal handler, `Component.onCompleted`).
- [x] 1.4 Confirm the same mechanism covers the emoji resolver's async case (D4): a function whose
      return value changes when a fetch completes must re-render. If D1's mechanism handles
      translations but not this, say so — they may need different answers.
- [x] 1.5 Record the outcome in design.md — either D1 confirmed, or D1 killed and the codemod
      fallback adopted. State what was actually observed, not what was expected.

## 2. Emoji crash and escaping (independent of group 1; already written, needs verification)

- [x] 2.1 Review the working-tree changes to `Theme.replaceEmojiWithImg()` for the `allowMarkup`
      parameter and default-escaping behaviour.
- [x] 2.2 Audit all 26 `replaceEmojiWithImg()` call sites and confirm each is correctly classified.
      5 opt-ins: `ExpandableTextArea`, `CustomEditorPopup`, `CustomItem`, and the two
      <font>-highlight sites (`ShotDetailPage`, `PostShotReviewPage`); the remaining 21 are escaped — pay particular attention to the sites that
      build a `result` string (`ShotDetailPage`, `PostShotReviewPage`, `ShotHistoryPage`) and to
      `ConversationOverlay`, whose text comes from `AIConversation::getConversationText()`.
- [x] 2.3 Confirm `ShotDetailPage:357` still renders its `<font color=...>` highlight — it builds
      markup deliberately and calls `escapeHtml()` on the name itself, so it may need the opt-in.
- [x] 2.4 Verify the release-notes field in the running app. DONE, and it found a real bug I
      had hedged about instead of confirming: an inline `<img>` truncates the rest of a
      Markdown document in Qt, so the notes rendered only up to the first emoji. Fixed by
      converting markdown -> HTML BEFORE injecting emoji (`MarkdownRenderer`, see D8).
      Verified in the running app: emoji render at inline size, headings/bold/bullets intact,
      scrolls to the end with no truncation.
- [x] 2.5 Correct the `CurveTextRendering` comment in `src/main.cpp` (already in the working tree —
      re-read it after the #1549 merge and confirm it reads coherently in its new surroundings).
- [x] 2.6 Add a test that asserts `replaceEmojiWithImg()` escapes by default and preserves markup
      when `allowMarkup` is true.

## 3. Emoji asset set (CDN design REVERSED — see design.md D4)

Originally a CDN resolver with a disk cache. Measuring the install-size cost it was avoiding
(+2.8 MB compressed, on a 137 MB bundle) reversed the decision: ship all 4,009 assets, delete the
runtime network path entirely. Former tasks 3.2-3.5 and 3.9-3.10 (resolver, caches, async
re-render, offline behaviour, eviction) no longer exist.

- [x] 3.1 Settle the upstream source and filename pattern. DONE: `jdecked/twemoji@17.0.3` via
      jsDelivr, now a BUILD-TIME source only. `twitter/twemoji` is NOT archived (an earlier note
      here said it was — wrong), but its last release is v14.0.2 from March 2022 and it 404s on
      Unicode 15+. Byte-identical for overlapping assets, so regenerating does not restyle the 744
      already shipping. Skin-tone, ZWJ, flag, keycap and `©` sequences verified by real requests;
      U+FE0F must be stripped from the key (`31-20e3` 200, `31-fe0f-20e3` 404).
- [x] 3.2 Close the variation-selector crash path: keycaps and `©️ ®️ ™️` matched no codepoint range
      and reached the platform colour renderer. Both new tests verified to fail without the fix.
- [x] 3.3 Point `scripts/download_emoji.py` at `jdecked/twemoji@17.0.3` and give it a mode that
      fetches the COMPLETE upstream set rather than only the codepoints `EmojiData.js` lists.
- [x] 3.4 Run it: ~4,009 assets into `resources/emoji/`, regenerate `resources/emoji.qrc`, commit.
      Sanity-check the count and total size against the measured figures (4,009 files, 9.65 MB raw)
      rather than assuming the run was complete.
- [x] 3.5 Add the existence check so an emoji with no asset is STRIPPED rather than emitted as an
      unresolvable `qrc:` path. This is the broken-image bug and it is not fixed by bundling more —
      a Unicode 18 emoji still misses. Applies to both `emojiToImage()` and `replaceEmojiWithImg()`.
- [x] 3.6 Verify the strip path with a test: an emoji known to be outside the set produces no
      `<img>` and leaves surrounding text intact.
- [x] 3.7 Confirm what the regeneration changed. RESULT: 3,265 added, 0 deleted, and **8 of 744
      modified** (🌁 🍉 🏥 🔒 🔓 🚑 🤡 🥺) — my prediction of "no modifications" was wrong, having
      generalised a byte-identity check on one asset to all 744. Mostly upstream path optimisation
      (arcs for béziers; 🥺 2277→1428 bytes, same shape); 🔒 is a genuine redraw, now spanning the
      full viewBox height. Fine to take, but it is a visual change and is recorded as one.

## 4. Translation reactivity (blocked on 1.5)

- [x] 4.1 Implement whichever mechanism task 1 selected.
- [x] 4.2 Rename the C++ entry point (`translateString()`) and update the 10 C++ callers (I said
      11 earlier — miscounted a comment line as a call):
      `main.cpp`, `updatechecker`, `databasebackupmanager`, `blemanager`, `visualizerimporter`,
      `visualizeruploader`, `aiprovider`, `aimanager`, `aiconversation`, `livesteamcoach`.
- [x] 4.3 Add the regression guard test per D2 — under D1, assert the QML-facing `translate` is a
      notifying property; under the codemod fallback, assert no bare `translate(` survives in
      `qml/`.
- [x] 4.4 Verify the guard actually fails: deliberately revert the mechanism, watch the test go
      red, then restore. Do not claim the guard works without having seen it fail.
- [x] 4.5 Confirm `Tr.qml` still behaves correctly and decide whether its `translationVersion`
      touch stays (design leans yes, as documentation).

## 5. Documentation

- [x] 5.1 Rewrite the i18n bullet in `CLAUDE.md` (currently around line 81) so the documented
      pattern is the reactive one. The current text recommends the bare call that this change
      exists to fix.
- [x] 5.2 Rework the glyph rule in `CLAUDE.md` per D6. The current bullet bans Unicode glyphs as
      icons outright and lists a "safe" set. Rewrite it to say: pictographic emoji are fine and
      resolve to images (state the resolution order and that unresolvable ones are stripped);
      non-emoji text symbols (`→`, `←`, `▶`, `☰`) remain unsafe because they are rendered by the
      bundled font, are absent from its cmap, and vary per machine. Do NOT delete the section —
      the two concerns are different and only one is fixed here.
- [x] 5.3 Document the build rule in `CLAUDE.md` alongside the existing "new QML files must be
      added to CMakeLists.txt" convention: using a new emoji requires no manual asset step, the
      build fetches and commits it.
- [x] 5.4 Update `docs/CLAUDE_MD/EMOJI_SYSTEM.md` for the new resolution order, the cache, and the
      build step. Its "Adding New Emojis" section currently prescribes a manual 3-step process that
      this change makes obsolete.
- [x] 5.5 Check `docs/CLAUDE_MD/QML_GOTCHAS.md` for the invokable-in-binding trap and add it if
      absent — it has now been hit three times in one session (`effectiveFontSizes`,
      `canAutoTranslate`, `translate`), and D4 would have been a fourth.
- [ ] 5.6 Update the wiki manual only if user-visible behaviour changed. A language switch taking
      effect without a restart is user-visible; check whether the manual currently tells users to
      restart.

## 6. Verification

- [x] 6.1 Build via Qt Creator; zero errors and zero new warnings. Clean throughout.
- [x] 6.2 Run the full test suite; 81/81 suites pass, no warnings.
- [x] 6.3 Language switch verified live via MCP + screenshots: English -> German retranslated
      the whole UI with NO restart (Rezepte/Bohnen/Dampf/Bereit/Spülen/Verlauf/…), and German ->
      English left no residue. RESIDUAL: page titles are set by imperative assignment in
      StackView.onActivated (35 sites), not bindings, so they keep the language active when the
      page was last activated and self-heal on navigation. See 6.9.
- [x] 6.4 Update tab opened against v2.0.0 notes (which contain emoji): no crash, emoji render
      as bundled SVGs at inline size, full content present.
- [ ] 6.9 DECIDE: fix the 35 imperative `currentPageTitle` assignments so page titles retranslate
      live. Needs each page to expose a `pageTitle` property and main.qml to bind it — a
      one-line change per page plus a small refactor. Not done: it is a separate shape of bug
      from the binding fix, and this change is already large.
- [ ] 6.5 Check pages that display externally-sourced text after the escaping change — bean names,
      recipe names, shot history, screensaver author, AI conversation — and confirm no raw tags
      appear.
- [ ] 6.6 Exercise an emoji outside the bundled 745 in a bean name or recipe name: confirm it
      fetches and renders, survives a restart, and strips cleanly with the network off.
- [x] 6.7 Android verification NOT REQUIRED for this change — Jeff's call, and correct.
      The rule is that PLATFORM/BLE/JNI code needs an Android CI build because a macOS compile
      skips `#ifdef Q_OS_ANDROID` branches. This change adds none: it is QML, three plain-Qt C++
      classes (TranslationManager, EmojiAssets, MarkdownRenderer) and resources. Verified the
      merged commit touches no platform-conditional block. I had over-applied the rule and was
      treating this as the branch's largest risk; it is not. Android gets exercised by the next
      beta build like anything else.
- [x] 6.8 Windows likewise — no platform-conditional code, nothing Windows-specific to verify
      ahead of the normal build. (The FONT work in #1549 genuinely was Windows-specific; this
      change is not, and I conflated the two.)

## 6b. Observed in testing — NOT this change's code, decide separately

- [x] 6.10 FIXED (shipped in #1550). The Language tab contradicted itself on ONE card: it shows German as `2974 / 2977`
      and, directly below, "Translation complete!". Two code paths round the same 99.899%
      differently:
        - `translationmanager.cpp:1149` `(translated * 100) / total` — integer division,
          truncates to **99%** (the language list)
        - `SettingsLanguageTab.qml:278` `Math.round((translated/total) * 100)` — rounds to
          **100**, which then trips `if (percent === 100) return "Translation complete!"`
      Truncation is the safer of the two (it cannot claim 100 while anything is missing), so
      the QML side is the one to change. Two lines. `SettingsLanguageTab.qml` is NOT touched
      by this change, so this is a deliberate scope call, not an oversight.
- [ ] 6.11 The 3 untranslated German strings are all long, multi-line MCP help text
      (`settings.ai.mcp.help.capabilities` / `.platformNote` / `.steps`). Every other string in
      the batch translated. Check whether the AI translation path skips or silently fails on
      strings past some length — if so, that is a real gap, not a coverage statistic.
- [ ] 6.12 The percentage's DENOMINATOR may be wrong: it counts the string registry (2,977),
      but `machineStatus.idle` is absent from the registry entirely — so it is invisible to
      both the percentage AND to AI translation, and can never be translated. That is why
      "Idle" stayed English while the rest of the UI switched to German (see 6.3). 450 keys
      also have German translations but are not in the registry, so the two have drifted in
      both directions. CAVEAT: `string_registry.json` on disk was last written 12:46 while the
      app started 15:01, so re-check against a freshly saved registry before treating the
      450-key drift as established.

## 7. Custom icon audit (investigation only — no swaps in this change)

Motivation: 69 bespoke SVGs in `resources/icons/` are a maintenance burden, and standard emoji are
more widely recognised. The blocker is that these icons are monochrome and theme-tinted via
`ThemedIcon.qml` (`MultiEffect { colorization: 1.0; colorizationColor: Theme.iconColor }`, used
across 41 QML files), while emoji are fixed multi-colour. Colorising an emoji at 1.0 flattens it to
a silhouette; not colorising drops it out of the theme entirely. This group produces the data to
decide with, rather than guessing.

- [x] 7.1 Classify all 68 icons in `resources/icons/` into (I said 69 — the 69th file is
      decenza.ico, not an SVG): (a) a standard emoji is a genuine
      equivalent, (b) no emoji equivalent exists, (c) an equivalent exists but the icon is
      theming-critical. Record the proposed emoji codepoint for every (a).
- [x] 7.2 For each (a), note where it is used and whether that context tints it. CORRECTED
      TWICE, both times by looking instead of grepping:
        1st count: 17/274 tinted — wrong, only matched ThemedIcon blocks.
        2nd count: 62/274 (23%) — also counts inline `layer.effect: MultiEffect`, which is
          how most components actually tint. Still an undercount; there may be further
          mechanisms.
      The measurement that matters came from the running app in light mode: icons DO adapt
      (idle buttons render blue, bottom bar near-black). My "94% untinted, probably invisible
      in light mode" claim was wrong. Static analysis of this kept misleading me.
- [x] 7.3 Side-by-side comparison built from the real assets (artifact published): every icon on
      the dark surface AND on light mode's #eef0f6, beside the exact bundled emoji.
- [x] 7.4 BOTH of my original objections to the icon swap are now dead, so 7.1-7.3 decide it on
      merit rather than on my reservations:
      (a) Offline/CDN — gone. Everything is bundled; nothing fetches at runtime.
      (b) Theming — OpenMoji ships a `black/` tree of MONOCHROME line-art built exactly like our
      icons: `fill="none"` + a single `stroke`, same as `resources/icons/*.svg`. So a swapped
      icon CAN follow `Theme.iconColor` through ThemedIcon after all.
      One thing to actually test rather than assume: our icons stroke WHITE and ThemedIcon's
      comment says white is what makes `MultiEffect { colorization: 1.0 }` work; OpenMoji black
      strokes `#000`. Colorising a black source may not produce a light tint. Test it before
      concluding the swap is viable — this is the whole question now.
      Also note OpenMoji is CC-BY-SA (share-alike) where Twemoji is MIT, and it has no
      equivalent for `flush`, `espresso 8mm`, `niche-zero`, `decent-de1`, `body-*`, `taste-*`.
- [x] 7.5 DECIDED: do NOT swap. Tried it on the 12 most prominent icons and looked at the
      result together. Reverted.

      What decided it, none of which came from the classification — all of it from rendering
      the thing and looking:
      - Concepts with NO honest emoji kept appearing: sleep (IEC power symbol; U+23FB..23FE
        exist in Unicode but no emoji set draws them), equipment (grinder + basket + puck
        prep), history (a list of past shots), beans (the emoji is red KIDNEY beans; coffee
        beans have a centre crease). That is 4 of 12, plus profiles as a semantic downgrade —
        an abstract "chart increasing" replacing a drawn pressure curve.
      - Emoji cannot respond to STATE colour. The Profiles tile turns orange when active;
        a red-and-grey chart emoji sits on it unreadably. Monochrome icons are tinted to
        contrast with whatever the tile is doing. This is a category problem, not a bad pick.
      - The mix reads as unfinished. With ~27 icons unable to convert, mixed is the
        destination, not a transition. Side by side, the one reverted icon (History) looked
        more native than the emoji flanking it.
      - The icons already theme correctly (verified in light mode), so the swap trades a
        working mechanism for a fixed-colour one.

      Emoji keep earning their place where #1550 put them: release notes, AI replies, bean and
      recipe names, widget labels — user-authored and externally-sourced TEXT. Functional
      chrome is the opposite case.
- [x] 7.6 Light-mode check in the running app. Most icons tint correctly. ONE genuine bug found
      and fixed: the Settings search button (SettingsPage.qml + SettingsSearchDialog.qml) drew
      a white-stroked search.svg as a plain Image on a `Theme.surfaceColor` background — #ffffff
      in light mode — so it was completely INVISIBLE. Now ThemedIcon. Needs a relaunch to
      confirm; the running app still had the old QML.
- [x] 7.7 Light-mode sweep DONE by walking the app (idle, settings tabs, shot history) in light
      mode. Result: NO further invisible icons found. The search-button fix is confirmed visible.
      Icons tint correctly across the surfaces walked — the "94% untinted" fear was wrong, as the
      corrected 7.2 already records. Not exhaustive (every page was not visited), but the
      hypothesis that this was widespread is not supported.
- [x] 7.8 Banned font-glyph sweep — RESOLVED BY ADDING A FONT, not by editing 29 sites.
      Jeff's call, and the right one: "is there a standard font for these, let's make an easy
      problem easy." The 29 sites are now correct with zero QML changes.

      What actually happened. Hand-grepping for `→` had found 16 sites of one glyph type.
      `scripts/check_font_glyph_coverage.py` reads DecenzaSans' cmap and checks every QML string
      literal against it: 29 sites, six glyph types (`→ ↗ ↕ ← ▶ ◀ ⧉`). Decenza Sans has 927 glyphs
      and no symbols at all, so all of them came from a per-machine host fallback.

      Fix: bundle Noto Sans Math (SIL OFL, same licence as the UI face) purely as a symbol
      fallback. Measured three candidates by cmap before choosing — no Noto Symbols face covers
      the set (Symbols misses ▶ ◀ ⧉, Symbols2 misses every arrow); Math covers all seven in use.
      Chained after the UI family in `Theme.fontFamilies` and on the application font in main.cpp
      (needed separately: sites setting only `font.pixelSize`, like ValueInput's gear hint,
      inherit the app font and would otherwise still hit the host). Qt consults a later family
      only for missing codepoints, so letterforms are untouched. Build clean, 0 warnings.
      Scanner now reports zero uncovered symbols; it exits non-zero for CI, ignoring
      AddLanguagePage's native language names, which are SUPPOSED to use a platform fallback.

      Two things this replaced, both worse:
        - Rewriting 25 sites to `›`. Churns translated strings for a cosmetic gain.
        - Turning them into emoji. Only four of the seven have Twemoji assets at all, and I
          started down this path before catching that it would have made things WORSE: adding
          U+FE0F to `AccessibleButton.text` / a plain `Text` — neither routes through
          `replaceEmojiWithImg` — is an explicit colour-emoji request, i.e. the exact macOS
          render-thread crash this change exists to prevent. Reverted before it went anywhere.
          Emoji also carry fixed colours and would not follow Theme, which is what sank the icon
          swap. A text font keeps symbols monochrome and correctly coloured.

      The premise was also wrong. CLAUDE.md justified the ban with #1537; #1537 was a Windows
      distance-field re-caching bug that dropped the "fi" ligature from "Profile" — a word
      entirely inside the bundled font, nothing to do with fallbacks. Nothing in this app has ever
      been traced to a missing glyph. CLAUDE.md and QML_GOTCHAS.md now say symbols are fine, name
      the script, and record the bad citation so the ban is not reinstated from memory.

      NEEDS JEFF'S EYES: the seven symbols now draw in Noto Sans Math rather than the host font,
      so they will look slightly different — weight and arrowhead shape most likely. Check the
      dose→yield line, Espresso's pressure/flow goal, and FlowCalibration's prev/next buttons.


- [ ] 7.8a Translation debt found while measuring 7.8 — INDEPENDENT of the glyph fix.
      Translations are keyed by string key ONLY; there is no source-text hash. Changing an English
      fallback therefore does NOT invalidate the existing translation. Proof: `settings.ai.remoteMcp.setupGuidance`
      has already been rewritten in QML to drop its arrows, yet the registry and all of ar/de/fr
      still carry the old arrow-bearing text. So fixing 7.8's English leaves ~17-20 arrows per
      language on disk indefinitely — worst in Arabic, where an LTR arrow is also directionally wrong.
      Also found: `settings.ai.remoteMcp.adminFunnel` / `.adminHttps` are referenced NOWHERE in the
      tree but still sit in the registry and all three catalogs — the registry accumulates dead keys.

- [ ] 7.8b The glyph class IS statically catchable — `redraw-icon-set` task 4.4 guessed it was not.
      `scripts/check_font_glyph_coverage.py` already does it. Worth landing as a test, but it cannot
      be green until 7.8's 29 sites are fixed, so it lands WITH the fix (no allowlist — an allowlist
      is how this becomes permanent debt). Update redraw-icon-set 4.4, which currently says
      "Probably not statically".

- [ ] 7.9 A faint cloud indicator in Shot History rows is very low-contrast in light mode. Could
      not locate its source by grepping ShotHistoryPage; may be deliberate de-emphasis for an
      "uploaded" state. Look at it directly before assuming either way.

## 8. Close-out

- [ ] 8.1 Archive this change. NOT DONE, deliberately — attempted 2026-07-18 and stopped.
      The code is merged and live (#1550), but Android and Windows have had ZERO exercise and
      this change touches text rendering on every platform plus 4,009 new assets. Archiving
      would file the paperwork on a change whose riskiest claim is still unverified.
      GATE UPDATED: 6.7/6.8 are closed — no platform-specific code, so no Android/Windows
      build is owed ahead of the next beta. The remaining open items (5.6 wiki, 6.5/6.6 spot
      checks, 6.11/6.12 translation findings, 7.8 arrow glyphs) are follow-ups rather than
      verification of what shipped. The three specs are ADDED-only new capabilities, so
      promotion is clean whenever the archive happens.
