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

- [ ] 6.1 Build via Qt Creator; zero errors and zero new warnings.
- [ ] 6.2 Run the full test suite; all suites pass.
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
- [ ] 6.7 Verify on Android. The escaping change affects every platform, and the font work that
      preceded this was verified on macOS only.
- [ ] 6.8 Verify on Windows.

## 7. Custom icon audit (investigation only — no swaps in this change)

Motivation: 69 bespoke SVGs in `resources/icons/` are a maintenance burden, and standard emoji are
more widely recognised. The blocker is that these icons are monochrome and theme-tinted via
`ThemedIcon.qml` (`MultiEffect { colorization: 1.0; colorizationColor: Theme.iconColor }`, used
across 41 QML files), while emoji are fixed multi-colour. Colorising an emoji at 1.0 flattens it to
a silhouette; not colorising drops it out of the theme entirely. This group produces the data to
decide with, rather than guessing.

- [ ] 7.1 Classify all 69 icons in `resources/icons/` into: (a) a standard emoji is a genuine
      equivalent, (b) no emoji equivalent exists, (c) an equivalent exists but the icon is
      theming-critical. Record the proposed emoji codepoint for every (a).
- [ ] 7.2 For each (a), note where it is used and whether that context tints it. An icon that is
      never tinted is a much cheaper swap than one in themed chrome.
- [ ] 7.3 Build a side-by-side visual comparison of the candidates — themed SVG vs Twemoji — in
      both light and dark mode. The theming loss has to be seen, not described.
- [ ] 7.4 BOTH of my original objections to the icon swap are now dead, so 7.1-7.3 decide it on
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
- [ ] 7.5 Present the classification and comparison to Jeff and get a decision. Do NOT swap any
      icon in this change — a follow-up change carries whatever is agreed.

## 8. Close-out

- [ ] 8.1 Archive this change with `/opsx:archive` as the last commit on the branch, before merge.
