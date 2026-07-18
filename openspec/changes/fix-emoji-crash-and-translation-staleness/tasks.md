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
- [ ] 2.4 Verify the release-notes field in the running app. Changed from `RichText` to
      `MarkdownText` — the source is GitHub markdown, so RichText showed `##`, `-` and `**`
      as literal characters. Confirm headings/bullets/bold render AND that the emoji `<img>`
      survives the markdown importer: a scratch check with no qrc resources showed the img
      and its trailing text dropped, which may be an artefact of the missing resource or may
      be real. `ConversationOverlay` is the in-app precedent claiming it works. NEEDS JEFF.
- [x] 2.5 Correct the `CurveTextRendering` comment in `src/main.cpp` (already in the working tree —
      re-read it after the #1549 merge and confirm it reads coherently in its new surroundings).
- [x] 2.6 Add a test that asserts `replaceEmojiWithImg()` escapes by default and preserves markup
      when `allowMarkup` is true.

## 3. Emoji asset resolution (blocked on 1.4)

- [ ] 3.1 Settle the CDN source and URL template. `twitter/twemoji` is archived; check
      `jdecked/twemoji` via jsDelivr, confirm the SVG path pattern for a real multi-codepoint
      sequence (e.g. a skin-tone or ZWJ emoji), and pin a tag rather than tracking `latest`.
      Verify by actually fetching two or three, not by assuming the pattern.
- [ ] 3.2 Implement the resolver: bundled → disk cache → fetch → strip, with a negative cache so a
      failed emoji is not refetched on every re-render.
- [ ] 3.3 Give the resolver a notifying property per D4 and route `emojiToImage()` and
      `replaceEmojiWithImg()` through it. Confirm an emoji that arrives after first paint actually
      re-renders — this is the requirement most likely to be silently unmet.
- [ ] 3.4 Confirm the strip path: with the network unreachable, an unbundled emoji is removed and
      the surrounding text renders normally, with no broken-image artefact.
- [ ] 3.5 Confirm the cache survives a restart, and that a restart with no network still renders
      previously-fetched emoji.
- [ ] 3.6 Add the build step that bundles referenced-but-missing emoji, extending
      `scripts/download_emoji.py`. It must scan every app-authored source of emoji — `qml/`,
      `EmojiData.js`, translated strings — not just the picker's category lists.
- [ ] 3.7 Add a check that enforces D5's invariant: every emoji referenced by app-authored content
      has a bundled asset, failing the build otherwise. Verify it fails by deleting one bundled
      asset that the app references and watching the build go red. The point is that an
      app-authored emoji can never silently reach the network.
- [ ] 3.8 Make the build step commit newly fetched assets and the regenerated `emoji.qrc` back to
      the repository, so a release is reproducible from a clean checkout with no CDN access.
      Decide where this runs — a local/dev step, or CI — and make sure it cannot fire on a
      release-tag build and mutate the tagged tree. Read `docs/CLAUDE_MD/CI_CD.md` first; the
      Android workflow already commits `versioncode.txt` back to `main` and is the precedent to
      follow.
- [ ] 3.9 Verify offline behaviour end to end on a real device with networking disabled: every
      emoji in the app's own UI renders, every emoji in the picker renders and can be selected,
      and only externally-sourced text shows stripping.
- [ ] 3.10 Decide and record whether the cache needs eviction. Small assets and codepoint keying
      suggest not, but make it a decision rather than an omission.

## 4. Translation reactivity (blocked on 1.5)

- [ ] 4.1 Implement whichever mechanism task 1 selected.
- [ ] 4.2 Rename the C++ entry point (`translateString()`) and update the 11 C++ callers:
      `main.cpp`, `updatechecker`, `databasebackupmanager`, `blemanager`, `visualizerimporter`,
      `visualizeruploader`, `aiprovider`, `aimanager`, `aiconversation`, `livesteamcoach`.
- [ ] 4.3 Add the regression guard test per D2 — under D1, assert the QML-facing `translate` is a
      notifying property; under the codemod fallback, assert no bare `translate(` survives in
      `qml/`.
- [ ] 4.4 Verify the guard actually fails: deliberately revert the mechanism, watch the test go
      red, then restore. Do not claim the guard works without having seen it fail.
- [ ] 4.5 Confirm `Tr.qml` still behaves correctly and decide whether its `translationVersion`
      touch stays (design leans yes, as documentation).

## 5. Documentation

- [ ] 5.1 Rewrite the i18n bullet in `CLAUDE.md` (currently around line 81) so the documented
      pattern is the reactive one. The current text recommends the bare call that this change
      exists to fix.
- [ ] 5.2 Rework the glyph rule in `CLAUDE.md` per D6. The current bullet bans Unicode glyphs as
      icons outright and lists a "safe" set. Rewrite it to say: pictographic emoji are fine and
      resolve to images (state the resolution order and that unresolvable ones are stripped);
      non-emoji text symbols (`→`, `←`, `▶`, `☰`) remain unsafe because they are rendered by the
      bundled font, are absent from its cmap, and vary per machine. Do NOT delete the section —
      the two concerns are different and only one is fixed here.
- [ ] 5.3 Document the build rule in `CLAUDE.md` alongside the existing "new QML files must be
      added to CMakeLists.txt" convention: using a new emoji requires no manual asset step, the
      build fetches and commits it.
- [ ] 5.4 Update `docs/CLAUDE_MD/EMOJI_SYSTEM.md` for the new resolution order, the cache, and the
      build step. Its "Adding New Emojis" section currently prescribes a manual 3-step process that
      this change makes obsolete.
- [ ] 5.5 Check `docs/CLAUDE_MD/QML_GOTCHAS.md` for the invokable-in-binding trap and add it if
      absent — it has now been hit three times in one session (`effectiveFontSizes`,
      `canAutoTranslate`, `translate`), and D4 would have been a fourth.
- [ ] 5.6 Update the wiki manual only if user-visible behaviour changed. A language switch taking
      effect without a restart is user-visible; check whether the manual currently tells users to
      restart.

## 6. Verification

- [ ] 6.1 Build via Qt Creator; zero errors and zero new warnings.
- [ ] 6.2 Run the full test suite; all suites pass.
- [ ] 6.3 Have Jeff launch the app and switch language to German, navigate several pages, switch
      back to English, and confirm no German text remains anywhere without a restart.
- [ ] 6.4 Have Jeff open the update tab against a release whose notes contain emoji and confirm no
      crash.
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
- [ ] 7.4 Note that D5 does NOT block an icon swap: icons are app-authored, so any emoji replacing
      one would be bundled by the build step and never fetched. The offline concern dissolves. The
      remaining objection is theming alone — a swapped icon stops following `Theme.iconColor` —
      so the decision rests entirely on 7.3's visual comparison, not on connectivity.
- [ ] 7.5 Present the classification and comparison to Jeff and get a decision. Do NOT swap any
      icon in this change — a follow-up change carries whatever is agreed.

## 8. Close-out

- [ ] 8.1 Archive this change with `/opsx:archive` as the last commit on the branch, before merge.
