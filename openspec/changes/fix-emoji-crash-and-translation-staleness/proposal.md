## Why

Two defects that share a root shape: a value the UI depends on changes, and the UI does not
find out.

**The app crashes when a colour emoji reaches the text renderer.** On macOS, CoreText shaping a
colour emoji leads to `QSGTextMaskMaterial` → `CopyEmojiImage` → an ImageIO PNG decode on
`QSGRenderThread`, which faults (SIGBUS at `0xbad4007`). The app already avoids this everywhere it
remembers to, by rewriting emoji to bundled SVG `<img>` tags via `Theme.replaceEmojiWithImg()`.
The update tab's release-notes field did not, so opening it against a GitHub release whose notes
contain emoji crashes the app. This is live on `main` today.

A comment in `main.cpp` asserted that `QQuickWindow::CurveTextRendering` had eliminated this crash
path. A crash on 2026-07-18 disproved it: curves cannot represent colour bitmaps, so Qt still falls
back to the texture-mask path for colour glyphs specifically — exactly the case the setting was
believed to cover.

**Switching language does not retranslate the UI.** `TranslationManager.translate(key, fallback)`
is a `Q_INVOKABLE`. A QML binding re-evaluates when a NOTIFY fires for a property it *read* during
its last evaluation; calling an invokable registers no such dependency. So a binding over
`translate()` computes once and then freezes. `Tr.qml` works around this by reading
`TranslationManager.translationVersion` before calling `translate()` — but **3,248 call sites in
`qml/` call `translate()` bare** and therefore never update. Switching to German and back leaves
German text scattered across the UI until the app is restarted, which is what the user observed.

`CLAUDE.md` currently teaches the broken pattern as the recommended one for property bindings, so
every new string written to spec is stale by construction.

## What Changes

- Fix the release-notes crash by routing release notes through the SVG emoji path.
- **BREAKING (internal)**: `Theme.replaceEmojiWithImg()` escapes HTML in its input by default. An
  `allowMarkup` opt-in is added for the five call sites that deliberately supply markup. Untrusted
  text — bean names, AI replies, community screensaver authors, GitHub release notes — can no
  longer inject tags into the `RichText`/`StyledText` renderer. Getting this wrong now fails
  visibly (raw tags on screen) rather than silently.
- Make a language switch retranslate every binding, without restarting and without editing 3,248
  call sites. The intended mechanism is to convert `translate` from a `Q_INVOKABLE` into a
  `Q_PROPERTY` that returns a callable, so that reading `TranslationManager.translate` is itself a
  property read and registers the dependency. **Confirmed by experiment** against a real
  `QQmlEngine` before any sweep — see design.md D1 and `tests/tst_translationreactivity.cpp`.
- Add a regression guard so a newly written bare `translate()` call cannot silently reintroduce
  staleness.
- Rewrite the `CLAUDE.md` i18n guidance so the documented pattern is the correct one.
- Ship the complete emoji set. Today the app bundles 744 Twemoji SVGs and emits
  `qrc:/emoji/<hex>.svg` **without checking that the file exists**, so any emoji outside that set
  becomes an unresolvable image reference — neither rendered nor stripped. Bundle all 4,009 assets
  from a pinned upstream (+2.8 MB compressed, measured, on a 137 MB bundle) and add the missing
  existence check so an unavailable emoji is stripped. No runtime network path: no CDN, no cache,
  no asynchronous resolution.
- Fix a crash path the change would otherwise have left open: keycap sequences (`1️⃣`) and symbols
  with emoji presentation (`©️ ®️ ™️`) are matched by no codepoint range, so they reach the platform
  colour-emoji renderer today.
- Narrow the `CLAUDE.md` glyph guidance: pictographic emoji become safe to use because they resolve
  to images, while non-emoji text symbols (arrows and similar) remain a bundled-font coverage
  question and keep their existing warning.

## Capabilities

### New Capabilities
- `translation-reactivity`: a language change is reflected in all visible text without an app
  restart, and the correct call pattern is enforceable rather than remembered.
- `untrusted-text-rendering`: externally-sourced strings are escaped before reaching a markup-aware
  text renderer, and colour emoji are rendered as bundled SVGs rather than platform colour glyphs.
- `emoji-asset-resolution`: every emoji the app can display ships with it, resolution is local and
  synchronous, an unavailable emoji is stripped rather than left as a broken image reference, and
  the asset set is reproducible from a pinned upstream.

### Modified Capabilities
None. No existing spec states requirements about translation reactivity or emoji rendering.

## Impact

- `src/core/translationmanager.{h,cpp}` — `translate` changes shape; `translationVersion` may
  become redundant at call sites but is retained for `Tr.qml` and existing consumers.
- `qml/` — 3,248 `translate()` call sites. Under the property mechanism they are untouched; under
  the codemod fallback they are all rewritten.
- `qml/Theme.qml` — `replaceEmojiWithImg()` gains `allowMarkup`; of 26 call sites, 21 gain
  escaping and 5 opt out. `escapeHtml()` stops escaping `>` so one policy serves StyledText,
  RichText and MarkdownText alike.
- `qml/pages/settings/SettingsUpdateTab.qml` — release notes rendered as MarkdownText through the
  emoji path (the source is GitHub markdown).
- `src/main.cpp` — corrects the CurveTextRendering comment that claimed the crash was gone.
- `resources/emoji/` grows from 744 to ~4,009 committed assets; `resources/emoji.qrc` regenerated.
- `qml/Theme.qml` — existence check before emitting an image reference; variation-selector
  handling. Both functions stay pure and synchronous, so no binding-dependency work is needed here.
- `scripts/download_emoji.py` — pinned to `jdecked/twemoji@17.0.3` and able to fetch the full set.
- **No** new runtime network traffic. An earlier version of this proposal disclosed per-emoji CDN
  requests; that design was reversed after measuring the install-size cost it was avoiding.
- `CLAUDE.md` — i18n guidance rewritten; glyph guidance narrowed rather than removed.
- Any C++ caller of `TranslationManager::translate()` must keep working; the C++ entry point is
  preserved regardless of the QML-facing shape.
