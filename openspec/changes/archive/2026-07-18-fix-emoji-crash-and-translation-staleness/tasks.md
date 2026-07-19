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
- [x] 5.6 Wiki manual — checked and edited. It never told users to restart after a language
      change, so there was nothing to correct; the edit states the behaviour positively instead,
      for anyone who restarts out of habit, and documents the new drift rule (a translation of
      superseded English steps aside; your own edits are kept). Two small changes in Manual.md,
      section 19 and the Language summary. HELD LOCALLY at /tmp/decenza-wiki — pushing publishes
      to the public wiki, so it waits for Jeff.

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
- [x] 6.9 DONE (commit d4be5c28): 29 pages now declare `readonly property string pageTitle`
      and main.qml derives currentPageTitle from the current item, so titles retranslate live.
      Marker was left stale. Original wording follows.
      DECIDE: fix the 35 imperative `currentPageTitle` assignments so page titles retranslate
      live. Needs each page to expose a `pageTitle` property and main.qml to bind it — a
      one-line change per page plus a small refactor. Not done: it is a separate shape of bug
      from the binding fix, and this change is already large.
- [x] 6.5 Check pages that display externally-sourced text after the escaping change — bean names,
      recipe names, shot history, screensaver author, AI conversation — and confirm no raw tags
      appear. PASS. Audited every surface that renders external text as StyledText/RichText:
      each one routes through an escaping helper before display — `Theme.joinWithBullet`
      (BagCard's attrLineRich/metaLineRich, BeanBaseDetailsRow), `replaceEmojiWithImg` with no
      `allowMarkup` (ShotHistoryPage, AutoFavoriteInfoPage), an explicit `Theme.escapeHtml`
      (ShotDetailPage), and `MarkdownNoHTML` for the AI conversation. No unescaped path found.
- [x] 6.6 Exercise an emoji outside the bundled set in a bean name or recipe name: confirm it
      fetches and renders, survives a restart, and strips cleanly with the network off.
      **This found a real bug.** 399 assigned codepoints fall inside the ranges `_isEmoji`
      matches with no bundled asset behind them, so this is reachable by typing, not a
      hypothetical. Set a bean name to `Milk U+1F322 Blend U+2615 Espresso` and it rendered
      **`Milk ▯ Blend`** — a tofu box, not the silent strip CLAUDE.md documents.
      Root cause was not the emoji pipeline but a gap ahead of it: `ShotPlanText._rich` escaped
      user text into `Text.StyledText` and never called `replaceEmojiWithImg`, so emoji reached
      the renderer as raw codepoints. Two siblings had the same shape — `BeanSummary.qml` and
      `SteamPlanText.qml` — found by scanning for "escapes into StyledText, no emoji pipeline".
      Worth stating plainly: the tofu was the visible half. The same gap meant a *bundled*
      colour emoji also reached the platform text renderer, which is the macOS render-thread
      crash path this whole change exists to close. A bean name with ☕ in it was a live crash
      route on the shot-plan line.
      Fixed all three; a re-scan reports no remaining files of that shape. Pinned by
      `tst_textescaping::unbundledEmojiIsDroppedNotTofu`, which asserts both halves against the
      real shipped asset set: bundled becomes `<img>` with no raw codepoint surviving, unbundled
      disappears entirely.
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
- [x] 6.11 RESOLVED — no length gap. German now has ZERO untranslated strings; the three MCP
      help texts translated fine once a working provider ran. They were outstanding because
      Anthropic was 404ing (see 7.8p), not because the AI path skips long strings. Hypothesis
      tested and falsified rather than carried forward.
      Original note: The 3 untranslated German strings are all long, multi-line MCP help text
      (`settings.ai.mcp.help.capabilities` / `.platformNote` / `.steps`). Every other string in
      the batch translated. Check whether the AI translation path skips or silently fails on
      strings past some length — if so, that is a real gap, not a coverage statistic.
- [x] 6.12 CONFIRMED, and the cause is far worse than a wrong denominator: **scanAllStrings()
      has never scanned anything**. It iterated `:/qml`, which does not exist in a Qt 6
      qt_add_qml_module build — main.cpp loads `qrc:/qt/qml/Decenza/qml/main.qml`. The log says
      it outright: "Scanning 0 QML files ... Found 0 new strings. Total: 3042".

      So the registry has never been the app's string set. It is "strings this device happened
      to RENDER", accumulated over months and persisted. Everything 6.12 observed follows:
      machineStatus.idle and four siblings (cleaning, descaling, refill, unknown) are missing
      while their rendered neighbours are present; 446 keys have German translations the
      registry has never heard of; and the percentage's denominator is a per-device artifact.
      The real cost is not the statistic — AI translation and community upload have never been
      offered the complete string list, only what this machine displayed.

      Fixed: scan the correct root, and warn loudly when a scan finds zero files (a scan
      finding nothing is always a bug — the QML is compiled into the binary).

      Found by the "keys not in any QML file" report added in 7.8a: it printed all 3042 keys,
      which is absurd on its face. The diagnostic caught what the code review did not — again.
      Original note: The percentage's DENOMINATOR may be wrong: it counts the string registry (2,977),
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

      VERIFIED in the running macOS build (2026-07-18 16:52 session, binary built 16:51:49):
        - `[Font] Symbol fallback registered: "Noto Sans Math"` — the face loads.
        - Startup log clean: no QML TypeErrors, no font warnings. (MQTT connection failures in
          that session are the Home Assistant broker being unreachable, unrelated.)
        - FlowCalibration's ◀/▶ buttons draw as clean monochrome triangles, centred, in the
          text colour. That was the case worth seeing: U+25B6 HAS a Twemoji asset, so it was the
          one codepoint with a plausible colour-font resolution path. It resolved to the text font.
      NOT visually confirmed: a `→` on screen, which is 22 of the 29 sites. Same font and same
      chain, and U+2192 has no emoji form so it is strictly the safer case, but it has not been
      looked at. Cheapest place to catch one: History → any shot, detail line "18.0g → 36.0g".


- [x] 7.8a Source-text drift — FIXED for the registry, MITIGATED for translations, with one
      limitation that is architectural rather than a bug.

      Root cause was narrower than "no source hash": every registry write was guarded by
      `if (!m_stringRegistry.contains(key))`, at all five sites INCLUDING the full rescan. A key's
      English was captured once and never revisited, so no amount of rescanning could repair it.
      Note the contrast that made this invisible: `m_aiTranslations` is keyed by FALLBACK TEXT and
      so self-invalidates, while `m_translations` is keyed by KEY and silently did not.

      Fixed by `noteSourceString()`, which inserts or updates and reports whether anything changed.
      The registry now follows the source text, which matters beyond display — it is what the AI
      translator is prompted with and what a community upload publishes, so the drift was
      propagating outward from here.

      On a rewrite, a DOWNLOADED translation is dropped: it rendered a different sentence and can
      be flatly wrong in the confident way ("Delete" reworded to "Archive" leaves the old verb
      standing everywhere else). A USER'S OWN override is kept, matching setTranslation()'s
      existing contract that overrides survive updates. The propagate step in translateString()
      now also runs for reworded keys, so a rewrite landing on wording already translated
      elsewhere inherits it for free rather than going blank.

      A defect found by testing, not by reading: the drop was NON-DETERMINISTIC. translateString()
      only marks the registry dirty, so whether the new English survived the process decided
      whether the next launch re-detected the same drift. noteSourceString() now saves
      immediately on the drift path — drift is rare by construction, so the write is cheap.

      LIMITATION, asserted in the test rather than described so it cannot rot: the drop is
      one-shot. A published translation carries no record of which English it was made from, so a
      re-download of the same stale text is indistinguishable from a fresh translation and it
      returns. Closing that needs provenance in the published format — a SERVER-side change, and
      the real remaining half of this problem.

      Dead keys: the scan now reports registry keys it did not find in any QML file, and
      deliberately does not delete them. Live strings are registered from C++ too (blemanager,
      aimanager, updatechecker, visualizer*, main.cpp's model hints), and those look identical
      from here — deleting one silently untranslates it in every language. So the report names
      candidates for a human, with adminFunnel/.adminHttps as the known-dead examples.

      tests/tst_translationsourcedrift.cpp — 8 cases, seeding via a real downloaded language file
      rather than setTranslation(), which marks everything a user override and would have tested
      nothing. Full suite 82/82.


- [x] 7.8c Should the existing translations be marked stale wholesale? NO — measured, not argued.
      The on-disk registry is itself a snapshot of the OLD English (captured at first sight,
      never updated), so comparing it to today's QML identifies exactly which entries drifted.
      Against Jeff's real registry: 2978 keys, 2457 still match, 469 absent from QML
      (C++-registered or dead), and 52 drifted — 1.7%. Each language carries a translation for
      all 52, i.e. 1.5% of ~3425. Marking everything stale would discard three complete
      languages to fix fifty-two strings.
      Nothing to do, either: those 52 are cleaned automatically on the first run after this
      change, because the stale registry value is still there to be compared against. The
      history handles itself once, then the mechanism keeps it honest.
      Of the 52 — 37 real wording changes (bagcard.deleteRefused still tells German users to
      press "Mark as Empty", a button since renamed to "Bag finished"), 1 with a changed
      placeholder set (an accessibility string that gained "%1 of %2", so the translation was
      already broken), and 14 punctuation-only ("Elevation:" losing its colon). The 14 are a
      small real loss — a good translation dropped over a colon — and are the price of not
      keeping a heuristic that guesses which rewrites matter.

- [x] 7.8d A bug the 7.8c measurement caught in 7.8a's own fix, before it shipped.
      The scanner reads QML as TEXT and sees escape sequences; the runtime sees the characters
      they denote. Both write the registry. The scanner's unescape — duplicated inline three
      times — handled only \", \n and \t, so six fallbacks using \uXXXX (GraphLegend's
      superscript two, customeditor's degree sign) decoded differently on the two paths.
      Harmless while the !contains guard meant first-writer-wins. With 7.8a it becomes a
      permanent oscillation: every scan and every render sees the other's value as a rewrite,
      drops the translation, and rewrites the registry.
      Fixed by a single unescapeQmlLiteral() used at all three sites, handling \uXXXX plus the
      escapes that already worked, leaving malformed input exactly as written rather than
      eating the backslash. Two tests pin it, including the oscillation itself.
      Worth noting how this was found: not by reading the diff, but by looking at what the
      change would actually do to real data. The categorisation was for Jeff's question; the
      bug fell out of the "wording changed" bucket looking wrong.

- [x] 7.8e Ran 7.8a against Jeff's real app and it disproved the design. The drop is REVERTED.
      Method: back up his translations, switch to German (3425 translations), open Lang & Access
      so scanAllStrings() runs over the whole tree, diff the files.

      Predicted 52 drops. Got 11 — and all 11 were WRONG. Their registry entry was unchanged
      before and after, which cannot happen from drift. Cause: `beanbase.details.elevation` is
      written as `labelFallback: "Elevation:"` on ChangeBeansDialog.qml:1638 and as
      `translate(..., "Elevation")` on line 1639 and in BeanBaseDetailsPopup.qml:194. One key,
      two English strings, in the SAME build.

      26 keys are like this — every `beanbase.details.*`, `common.accessibility.dismissDialog`
      across eleven files ("Close dialog" vs "Dismiss dialog"), several profileEditor.* labels.
      They do not drift, they OSCILLATE: whichever site is scanned or rendered last wins, so
      dropping destroys their translation on every launch, silently, forever. It ate 11 German
      strings on the first real run. Restored from the backup.

      So the numbers say don't do it: 52 keys of genuine drift, mostly cosmetic and partly
      self-healing through the propagate step, against 26 keys of permanent damage. The registry
      update is kept — that was the actual documented harm, since the registry is what the AI
      translator is prompted with and what a community upload publishes. The translation is now
      REPORTED as rendering superseded English and otherwise left alone, with the warning text
      saying that repeats for one key mean a source conflict rather than a rewrite.

      Also observed, working: the language switch retranslated the whole UI live with no restart
      (6.3/6.9 still good), and the propagate step refilled two keys by inheritance
      (`ratio.edit.button`, `settings.search.placeholder`) instead of leaving them blank.

      Two design assumptions died here, both of which read as obviously true beforehand: that a
      key has one English string, and that a mismatch means the English was rewritten. Neither
      survives contact with the codebase. Only running it found that.

- [x] 7.8f Fixed all 27 keys used with two different English strings (26 by the first count;
      widening the pattern to `translationKey`/`translationFallback` found one more).

      Almost all were one shape: a VISIBLE LABEL and an `Accessible.name` sharing a key, worded
      differently on purpose — "Volume" on screen, "Max volume" for a screen reader that has no
      column colour to go by. That is good accessibility practice and simply needs two keys.

      Which variant keeps the key was decided by the DATA, not by preference: whichever the
      existing translations were made from, so nothing already translated is invalidated. That
      mattered — `settings.options.showInMl` had 'Show in milliliters' in the registry while the
      German read 'In Millilitern (ml) anzeigen', i.e. translated from the OTHER variant. The
      German is the evidence of intent; the registry value was just whoever wrote last.

      Unified, where the difference was noise (20 keys): the 11 `beanbase.details.*` labels lose
      a trailing colon — German already renders them bare ('Herkunft'), so English users saw
      "Origin:" while German users saw "Herkunft", and the colon was never part of the key's
      meaning. Note the contrast with `changebeans.form.roaster`, whose German IS 'Röster:' —
      that colon is genuinely part of that string and was left alone. Plus casing
      (Frame Name/Frame name), ellipses (Search settings...), 'Close dialog' → 'Dismiss dialog'
      across two of twelve files, and 'Purge steam wand' → 'Purge the steam wand'.

      Split, where the accessible name genuinely differs (7 keys): `.accessible` suffix, the
      convention already set by `changebeans.form.url.accessible` — maxVolume, maxWeight,
      exitValue, settings.data.settingsai, ratio.edit.button; plus
      `settings.preferences.autoCalibration.description` for the long visible text (the SHORT
      form is what German holds, so the switch keeps the key) and `idle.button.hotwater.short`
      for MiniGHC's cramped "Water" tile.

      `scripts/check_translation_key_conflicts.py` now fails on a reintroduction, and documents
      both repair strategies so the next person does not guess. 2587 keys checked, 0 conflicts.
      Build clean, 82/82 via Qt Creator.

      NOTE: the 7 new keys are untranslated until a re-translate, so they render English in
      ar/de/fr. All 7 are accessible names or a short tile label; none is body text.

- [x] 7.8g Replaced the launch-time `QTimer::singleShot(3000, ...)` that triggered the
      community-translation merge. It was a fixed delay standing in for "wait until the network
      is up" — the timer-as-a-guard pattern CLAUDE.md rules out, and wrong in both directions:
      too long on a warm desktop, too short on a tablet still associating with Wi-Fi, and when
      it fired too early the check simply failed with nothing retrying until the next launch.

      QNetworkInformation already reports reachability and main.cpp loads the backend at line
      842, before TranslationManager is constructed at 854, so the condition can be waited on
      directly. Online at startup -> check immediately; offline -> check on the transition to
      Online, which is a case the old timer could never handle (a device that gets a network
      minutes in now picks translations up without a restart). No backend on the platform ->
      check immediately, since being offline just fails a request that is already handled.

      Guarded by `m_launchUpdateCheckDone`, set before any early return: reachabilityChanged can
      fire repeatedly on a flapping link, which a one-shot timer never had to survive.

      The three remaining `QTimer::singleShot(RETRY_DELAY_MS, ...)` in this file are 429 retry
      backoffs and the one repeating QTimer is the registry batch-save — both are genuinely
      periodic/backoff work, which the rule allows. Left alone.

- [x] 7.8h Reviewed the backend (Kulitorum/decenza-shotmap) that the translation sync talks to,
      since 7.8a concluded the remaining fix was server-side. Findings and Jeff's call, recorded
      so this does not get re-raised as an open action.

      Sensible already: language codes are regex-validated before the S3 key is built, so no path
      traversal; pre-signed URLs expire in 300s with ContentType pinned; rate limiting is a proper
      atomic DynamoDB counter with TTL (10 per IP per hour); NoSuchKey returns 404; reads carry
      Cache-Control. The client is also defensive at the parse boundary — isObject(), non-empty,
      values toString()'d — so malformed JSON is rejected.

      Weaknesses: upload is unauthenticated and the payload is never validated by anything,
      because the pre-signed URL bypasses the Lambda entirely (no JSON check, no size cap, no key
      allowlist, whole-file replace). No versioning on the translations bucket — the only
      aws_s3_bucket_versioning in the Terraform is for the website bucket, and the translations
      bucket is not declared in that repo at all, only referenced as var.translations_bucket.

      JEFF'S CALL: acceptable. Recovery is "upload another translation", which is a fair answer
      for a community-curated file in a project this size. Not pursuing server-side auth,
      validation, versioning, or a promotion step. No issue filed.

- [x] 7.8i Client-side hardening done, since re-uploading does not undo a fetch that already
      happened. Measured first, and the exposure was far smaller than feared — the codebase was
      already defending this almost everywhere:
        * `Tr.qml` sets no textFormat, so the ~3,200 ordinary call sites are plain text.
        * `replaceEmojiWithImg` escapes every non-emoji chunk unless a caller opts into
          `allowMarkup`, and the five callers that do pass HTML they assembled from
          already-escaped pieces, or MarkdownRenderer output (hardened earlier in this change).
        * `joinWithBullet` escapes each part, so BeanBaseDetailsRow's rich branch was fine.
        * `ShotDetailPage.qml:344` already wraps its translated value in `Theme.escapeHtml`.
      Exactly ONE site was unescaped: the else-branch at BeanBaseDetailsRow.qml:65, where
      `beanbase.row.linked` went straight into a StyledText. Wrapped in Theme.escapeHtml.

      `scripts/check_translated_richtext.py` fails on a reintroduction and lists the four safe
      patterns so the fix is obvious. `tests/tst_textescaping.cpp` gains the actual payload —
      a remote `<img>` — asserting the tag cannot survive. That test deliberately also asserts
      the URL is STILL PRESENT as inert text: checking for its absence would pass for the wrong
      reason, which is a mistake a sibling test in this branch made earlier and had to be fixed.

      Note what this does and does not buy. It stops markup in a translation being executed as
      markup. It does not stop a hostile translation from displaying wrong or abusive WORDS —
      nothing client-side can, and per 7.8h the answer to that is a human uploading a correction.

- [x] 7.8j Caught a regression in 7.8g's own fix, by Jeff asking whether German had been
      retested after all the later changes. It had not, and looking at the running app's log
      showed `[Network] initial reachability: Unknown` with NO reachabilityChanged following in
      the whole session.

      7.8g waited for `Online` specifically. Unknown is not a synonym for offline — it is what
      the backend reports when it cannot tell, and it is what macOS reports at startup on this
      machine. So the launch check would have sat waiting for a transition that never came,
      never running a merge the old 3s timer always ran. Strictly worse than what it replaced,
      on the very machine it was written on.

      Now waits only on `Disconnected`, the one state that positively means no network.
      Everything else attempts the request, since failing is already handled and attempting is
      what the timer did. Local/Site will likely fail against an internet host, and that is
      fine and matches the previous behaviour.

      The lesson is the one this whole change keeps re-teaching: the code read correctly and
      the tests passed. Only the log from a real run showed the state it actually gets.

- [x] 7.8k Retested German in the app after all of 7.8e-7.8j (build 17:48, reachability fix in).
      NOTHING LOST — de.json still 3425 translations, zero missing against the pre-test backup,
      `Loaded 3425 translations for: "de"`, TTS switched to de_DE, no QML errors, and no drift
      warnings (the registry was already brought current by the earlier session's scan, so there
      is nothing left to report). The only WARNs are MQTT failing to reach Home Assistant.

      The conflict fixes behaved as designed: `beanbase.details.origin` now holds 'Origin' with
      the colon gone, and its German 'Herkunft' is untouched. Same for dismissDialog
      ('Dismiss dialog' / 'Dialog schließen') and search.placeholder. Picking the variant the
      translations were made from is what preserved them.

      HONEST READING OF THE COUNTER. The page shows German 2975/2979, 4 untranslated, against
      2961/2978 before. That is NOT a 13-string improvement from this work — 11 of it is simply
      the translations I dropped during 7.8e being restored. And the 4 will grow to about 10:
      only 1 of the 7 new `.accessible` keys (ratio.edit.button.accessible) has been registered,
      because the rest are on screens not yet opened this session. The other 3 untranslated are
      the long multi-line MCP help strings already known from 6.11, unchanged.

      Also confirmed incidentally: this launch reported reachability Online where the previous
      one reported Unknown, on the same machine minutes apart — which is exactly why 7.8j's fix
      was needed and why waiting on Online specifically was the wrong condition.

- [x] 7.8l Jeff asked why the Update button still read "Update" in German. Because it was never
      translated: `text: TranslationManager.downloading ? "..." : "Update"` — a bare literal, no
      translate() call. Its two siblings in the same row ARE translated ("Hinzufügen...",
      "Löschen"), which is exactly why it stood out. A string like this is invisible twice over:
      it stays English in every language, and it never enters the registry, so no translator is
      ever offered it.

      Scanning for the class found 9 candidates, 7 of them false positives (platform checks like
      `=== "ios"`, and `!== "undefined"` guards). Real ones, both in SettingsLanguageTab:
        line 177  "Update"
        line 332  "Uploading..." / "Submit to Community"
      Also ShotHistoryPage:499, whose `|| "Date"` fallback was a literal while every value in
      sortFieldLabels is translated. RecipesPage:600 looked similar but already falls back to a
      translated label, so it was left alone.

      Deliberately NOT adding a guard script for this one: 7 of 9 hits were noise, and a check
      that cries wolf gets ignored. Distinguishing a user-visible literal from a comparison
      needs real QML parsing, not a regex.

      CONSEQUENCE worth stating: this makes the buttons translatable, it does not make them
      German. The three new keys have no translation yet, so German goes from the 2979/2979 it
      just reached back to 3 short until someone re-translates. Fixing an untranslated string
      always looks like a small regression in the counter first.

- [x] 7.8m The Update button was DESTRUCTIVE. Found by Jeff pressing it, twice, in front of me.
      `onLanguageFileFetched` opened the language file WriteOnly and wrote the downloaded bytes
      straight over it, then reloaded. The server's copy is whatever someone last submitted, and
      NOTHING uploads automatically — Submit to Community is a separate button, itself gated
      behind `Settings.app.developerTranslationUpload` — so a machine that has AI-translated its
      own gaps normally holds far more than the server does. A richer local set is the normal
      state, not an edge case.

      Observed: German went 3429 -> 1515 on one click, discarding 1910 strings including an AI
      pass Jeff had just paid for. Recovered fully: 1910 from the backup taken before testing,
      4 more from de_ai.json, which survives because it is a separate file keyed by English text.
      The server's copy was kept at de.json.server-copy.

      The automatic launch check has always merged and preserved overrides. That the manual
      button did the OPPOSITE, for the same job, is what made this a trap rather than a
      preference — and it silently defeats the overrides file, which stores only KEY NAMES while
      the text lives in the replaced file. A user's own wording was thrown away while the key
      went on being marked as customised.

      Fixed: the download now merges. Current language goes through mergeLanguageUpdate (in
      memory, preserving overrides, saving once); other languages merge on the file, keeping
      any local translation the download does not carry. The reload after the merge is gone —
      re-reading the file is how the replaced content became live in the first place.
      mergeLanguageUpdate promoted to public: it is the meaning of "apply a downloaded
      language", both paths use it, and the regression test pins it.

      This is the sharpest instance of the pattern this whole change keeps hitting: the code was
      readable and the behaviour was wrong, and only using it showed that. It also cost real
      money before it was caught, which the earlier findings did not.

- [x] 7.8n Jeff pressed "Alle übersetzen & hochladen" and asked what happened. Three findings,
      two of them defects, one of them mine.

      WHAT THE BATCH DID: uploaded ar and fr (both now 3431 on the server, up from ~1225) and
      SKIPPED de, which is the one he wanted. Cause: a language with nothing left to translate
      was skipped entirely — including its upload. The comment above that branch has always
      read "Check if translation is needed or just upload"; the "just upload" half was never
      written. Being fully translated locally is precisely the state a language is in AFTER
      someone runs the AI pass, so the languages most worth publishing were the ones passed
      over. Three sites had the same shape (the first-language block, processNext, and the
      "already translated" result inside autoTranslateFinished); all three now upload.

      LANGUAGE LEFT CHANGED: the batch switches currentLanguage per language and restored only
      the AI PROVIDER at the end. Now saves and restores m_originalLanguage too.

      MY CHECKER WAS WRONG. scripts/check_translation_key_conflicts.py reported a clean tree
      while the app's log was full of oscillation warnings for settings.tab.languageAccess and
      others. It only matched labelKey/translationKey pairs on ONE line; those keys use the
      plain key:/fallback: form spread across lines, which scanAllStrings has always handled
      with a nearest-fallback-within-200-characters pairing. The checker was narrower than the
      thing it checked: it found 0 where there were 20. Widened to mirror all three scanner
      patterns — 3221 keys.

      So the earlier claim in 7.8f that all conflicts were fixed was WRONG, and the guard I
      added to prove it was the thing that hid them. What actually caught it was the warning
      text from 7.8e, which says repeats for one key mean two QML sites disagree — written
      before I knew it would be needed for this.

      All 20 now fixed on the same evidence rule as 7.8f — the German decides which variant
      keeps the key. 11 unified (colons on settings.data.*, casing on visualizer/debug), 8 split
      to `.accessible` (Mode/Announcement mode, Type/Screensaver type, Temperature/Steam
      Temperature, ...), and settings.tab.languageAccess now uses the `.full` key that already
      existed for the long form. Unifying changed only the ENGLISH: one key already meant one
      German string shown in both places, so no translation was invalidated.

- [x] 7.8o MERGE FIX VERIFIED against the exact action that destroyed German twice. Jeff pressed
      Update on the fixed build:
        before the fix:  3429 -> 1515   (1910 lost)
        after the fix:   3431 -> 3474   (0 lost)
      It GREW, absorbing the 43 strings the server had that he did not, and discarded nothing.
      Log agrees: `Loaded 3432 translations for: "de"` and `Propagated translations to 2 keys`.
      A follow-up AI pass then filled the 40 new `.accessible` keys from 7.8n (openai, 2 batches,
      40/40). Coverage 3028/3028.

      Server still shows de=1227 because Update DOWNLOADS. Uploading needs the button, which is
      hidden behind `Settings.app.developerTranslationUpload` — confirmed absent from the plist,
      so it defaults to false and the button never renders. That is why it could not be found:
      Debug -> Übersetzungs-Entwicklertools -> "Übersetzungsupload aktivieren", then it appears
      in Sprache & Zugriff under the string browser. ar and fr are already up at 3431 each.

      Incidental, not a defect: `OpenType support missing for "Decenza Sans", script 11` (and for
      .AppleIndicFont) is Devanagari in the language picker rendering through a platform
      fallback, exactly as the bundled-font note in CLAUDE.md describes.

- [x] 7.8p "My API key is set to OpenAI, why was it using Claude?" — two separate defects, both
      of which had been quietly overriding the user's configuration.

      PROVIDER: getConfiguredProviders() hard-ordered "Claude first (best quality), then OpenAI"
      and never read `aiProvider`. Anyone with an Anthropic key got Anthropic for translation no
      matter what Settings said. Now the SELECTED provider goes first and the others stay as
      fallbacks — the fallback list is what let a batch finish while Anthropic was 404ing, so
      removing it would turn one dead provider into a dead feature.

      MODEL: every cloud provider hard-coded its own model and ignored `providerModel()`.
      That is how Anthropic sat on `claude-3-5-haiku-20241022` — RETIRED 2026-02-19, confirmed
      against the claude-api skill's retired-models table — for months: nothing in Settings
      could correct it, because Settings was never consulted. OpenAI was pinned to `gpt-4o-mini`
      and Gemini to `gemini-2.0-flash`, NEITHER of which is in the app's own model catalog
      (`gpt-5.4`/`gpt-5.4-mini`, `gemini-2.5-flash`/`gemini-3.5-flash`) — so all three cloud
      providers had drifted, not just the one that happened to 404. Ollama read its setting and
      is the only one that never went stale, which is the whole argument.

      Now `translationModelFor(provider, fallback)` returns the configured model, using a cheap
      default only when unset. The fallbacks are a floor, not a preference: if the user has
      picked an expensive model, that is what runs. Translation is bulk (2400+ strings, 25 per
      request), so that can cost real money — but respecting the choice beats second-guessing
      it, and a surprising bill is more visible than a silently wrong model.

      Worth noting how this surfaced: the 404 was in a log Jeff asked me to read for a DIFFERENT
      reason. The failure was invisible by design — the batch falls through to the next provider,
      so anyone with a second key configured saw translations succeed and never learned the
      first provider was dead. Only a user with Anthropic alone would have noticed, by getting
      nothing at all.

- [x] 7.8q "Are there specific translate models?" — NO, at any provider the app talks to.
      Anthropic has no translation-specific model. OpenAI's /v1/audio/translations is SPEECH
      (Whisper), not text. Google Cloud Translation IS a dedicated service but a separate
      product from Gemini, with its own key and billing; the app talks to generativelanguage.
      DeepL is the notable dedicated text engine and is not integrated. So the choice is only
      ever WHICH GENERAL MODEL — which is exactly why it drifted unnoticed.

      Jeff's call: best model is fine, but honour the selected provider. Done in 7.8p. What
      remained was the fallback for an unconfigured provider, and the root cause of the drift:
      the translator kept its own model strings. aiprovider.cpp already states the principle —
      "Keeps the default a single source of truth (no parallel DEFAULT_MODEL constant to keep
      in sync with the list order)" — and the translator WAS that parallel constant.

      Attempted the obvious fix first (AIProvider::recommendedModelFor(), read the catalog at
      runtime) and REVERTED it: decenza_testlib compiles translationmanager.cpp but not the AI
      stack, so it failed to link, and satisfying it would drag the provider classes into
      forty-odd test targets to read one string. The link error was a layering signal, not an
      obstacle to route around.

      Instead the fallback stays a local literal and `tst_aiproviders` asserts each equals its
      provider's FIRST catalog entry — the codebase's own definition of "recommended". The
      coupling lives in a test rather than the link line, and drift now fails CI instead of
      silently 404ing for months.

      Note the fallbacks are the CATALOG's recommendation, not the flashiest model: openai
      gpt-5.4, anthropic claude-sonnet-4-6, gemini gemini-2.5-flash. A user wanting Sonnet 5
      selects it, and 7.8p's change means that selection is now actually used.

- [x] 7.8r Selected provider ONLY, and a failure is loud. Jeff's call, and the right one:
      silent substitution is what hid the retired model for months.

      getConfiguredProviders() now returns just the selected provider (or nothing, if it has no
      key), for all four including Gemini and Ollama — nothing is auto-discovered any more, so
      the old "excluded from auto-discovery" carve-outs are gone with it.

      A translation failure now STOPS the batch: it clears the queues, restores the provider and
      language, sets lastError to "Translation failed on <Provider> for <lang>: <message>",
      warns, and emits batchTranslateUploadFinished(false, ...). Previously it walked to the next
      provider and, once those were exhausted, moved to the next LANGUAGE — so a run could report
      "complete" having translated nothing, using a provider the user never chose.
      "No AI providers configured" likewise became "<Provider> is selected as the AI provider but
      is not configured", which names the thing to fix.

      The dead fallback branch was deleted rather than left behind an `if (false)` — I wrote that
      shim first and it is exactly the sort of thing that reads as live code six months later.

      TRADE-OFF, accepted deliberately: a transient rate-limit on the selected provider now ends
      the run instead of quietly finishing on another. That is the point — the previous behaviour
      was indistinguishable from success, and only a user with a single key could ever have
      noticed the provider was dead.

- [x] 7.8b DONE: `scripts/check_font_glyph_coverage.py` ships and exits non-zero for CI;
      redraw-icon-set 4.4 was corrected to say so. Marker was left stale.
      The glyph class IS statically catchable — `redraw-icon-set` task 4.4 guessed it was not.
      `scripts/check_font_glyph_coverage.py` already does it. Worth landing as a test, but it cannot
      be green until 7.8's 29 sites are fixed, so it lands WITH the fix (no allowlist — an allowlist
      is how this becomes permanent debt). Update redraw-icon-set 4.4, which currently says
      "Probably not statically".

- [x] 6.12a VERIFIED after the scan fix: registry 3042 -> 3667 (+625 keys) and
      `machineStatus.idle` is now present. The scan reads the QML for the first time.

- [x] 7.10 "Server busy, retrying upload 2/100" — a real limit AND a real bug behind it.
      The limit is genuine: the backend allows 10 translation upload-url requests per IP per
      hour (`checkRateLimitCustom("TRANSLATION#"+ip, 10)`, 3600s window), and the batch had
      already spent them on ar+fr+de.

      The bug is the response to it. MAX_RETRIES was 100 at 10s = ~17 minutes of hammering a
      server we do not own, against a window of a FULL HOUR — it could not succeed by
      construction, because the window cannot reset inside the retry span. And 429 was the
      ONLY status it retried, i.e. the one case where retrying is futile; genuine transient
      failures got no retry at all.

      Now 3 attempts (enough for a real burst), and on exhaustion the message names the cause:
      "Upload limit reached. The server allows a limited number of translation uploads per hour
      — try again in up to 60 minutes." Previously it said "Server busy", which points the user
      at the wrong thing entirely.

      Same principle as 7.8r: hammering someone else's server 100 times is the infrastructure
      version of spending tokens people are not expecting.

- [x] 7.9 A faint cloud indicator in Shot History rows is very low-contrast in light mode.
      FOUND AND FIXED — not deliberate de-emphasis. It was a plain `Image` on the Twemoji
      asset `qrc:/emoji/2601.svg`, whose two fills are baked in at `#CCD6DD` and `#E1E8ED`.
      Both are near-white, so on a light row it was nearly invisible, and being fills inside
      the SVG it could not follow the theme at any brightness.
      This is the case CLAUDE.md already names: "Not in place of a themed icon for chrome —
      emoji carry fixed colours and will not adapt to light/dark or a custom palette." The
      monochrome, tintable `qrc:/icons/CloudUpload.svg` already existed and was already used
      for this exact indicator on ShotDetailPage and PostShotReviewPage; only the history row
      reached for the emoji. Now a `ThemedIcon` with `Theme.successColor`, matching those.
      I missed it last time by grepping ShotHistoryPage for "cloud" — the string never
      appears, since the asset is named by codepoint.
      Fixed alongside it, in the same expression: the row's `Accessible.name` never mentioned
      upload state, and the icon is `Accessible.ignored`, so "uploaded" was carried by the
      picture alone — unreachable by screen reader, the exact failure mode behind the "never
      the only carrier of meaning" rule. It now appends the same
      `shotdetail.uploadedtovisualizer` string ShotDetailPage uses. The four quality-issue
      labels in that expression were also hardcoded English ("puck failed", "channeling", …);
      they now reuse the `badges.*` keys the visible badges render from, so the spoken row and
      the badges cannot disagree in a translated locale.
      Left alone deliberately: three other Twemoji-as-chrome sites (`2705` in DescalingPage
      and CustomItem, `1f4c5` in ChangeBeansDialog). They are the same style deviation but not
      the same bug — both assets are strongly coloured and legible on either theme — and they
      sit in files this change does not otherwise touch.

## 8. Close-out

- [x] 8.1 Archive this change. Done 2026-07-18, on the second attempt.
      The first attempt was stopped deliberately and the reasoning is kept here rather than
      overwritten, because the gate moving is the useful part of the record.
      ORIGINAL HOLD: the code was merged and live (#1550), but Android and Windows had had zero
      exercise and this change touches text rendering on every platform plus 4,009 new assets.
      Archiving then would have filed paperwork on a change whose riskiest claim was unverified.
      WHY THE GATE MOVED: 6.7/6.8 established the change contains no platform-conditional code —
      it is QML, three plain-Qt C++ classes and resources, so a macOS build exercises the same
      lines Android and Windows would. That removed the reason for the hold rather than merely
      waiting it out.
      GATE ITEMS AT ARCHIVE TIME, all closed: 5.6 (wiki), 6.5 (escaping audit — passed, no code
      change), 6.6 (unbundled emoji — found and fixed a live macOS crash route), 6.11/6.12
      (translation findings), 7.8 (symbol glyphs — fixed by bundling Noto Sans Math), 7.9 (the
      low-contrast cloud — found and fixed, plus the screen-reader gap beside it).
      70 of 71 tasks complete; this one is the 71st. Full suite green at 82/82, branch rebased
      onto origin/main with no conflicts.
      SPEC PROMOTION: the three capabilities (emoji-asset-resolution, translation-reactivity,
      untrusted-text-rendering) were ADDED-only and none existed in `openspec/specs/`, so
      promotion was a clean copy with a `## Purpose` written for each — no merge, nothing
      overwritten. `openspec validate --specs` passes at 98/98 after promotion.
