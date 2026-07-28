# Emoji System

Emojis are rendered as pre-rendered SVG images (Twemoji), not via a color font. This avoids D3D12/GPU crashes caused by CBDT/CBLC bitmap fonts (NotoColorEmoji.ttf) being incompatible with Qt's scene graph glyph cache across all platforms.

## How It Works

- Emoji characters are stored as Unicode strings in settings/layout data (e.g., `"☕"`, `"😀"`)
- Decenza SVG icons are stored as `qrc:/icons/...` paths (e.g., `"qrc:/icons/espresso.svg"`)
- At display time, `Theme.emojiToImage(emoji)` converts to an image path:
  - `qrc:/icons/*` paths pass through unchanged
  - Unicode emoji → codepoints → `qrc:/emoji/<hex>.svg` (e.g., `"☕"` → `"qrc:/emoji/2615.svg"`)
  - **U+FE0F is stripped from the key.** Upstream ships `31-20e3.svg`, not `31-fe0f-20e3.svg`.
- All QML components use `Image { source: Theme.emojiToImage(value) }` — never Text for emojis

## Resolution: bundled or stripped, never fetched

The **complete** upstream set ships (~4,000 SVGs, ~3 MB compressed — measured at v2.0, July 2026). Resolution is local and
synchronous — there is no CDN, no cache, and no network path. An earlier design fetched unbundled
emoji at runtime; measuring the install-size cost it was avoiding (+2.8 MB on a 137 MB bundle)
killed it, and with it a disk cache, a negative cache, and an async-rerender problem.
(Both figures measured July 2026; re-measure before reusing them as an argument.)

`EmojiAssets` (`src/core/emojiassets.h`) answers "is this bundled?" from the Qt resource system.
`Theme._emojiAssetPath()` asks before emitting a path, so an emoji with no asset is **stripped**
rather than becoming an image reference nothing can resolve. This matters even with 4,000 assets:
over a thousand codepoints inside the emoji ranges have no upstream artwork.

Two things that reach the platform colour renderer without help, and are handled explicitly:
- **Keycaps** (`1️⃣` = `U+0031 U+FE0F U+20E3`) — the base is an ASCII digit, matched by no range.
- **`©️ ®️ ™️`** — ordinary symbols wearing a variation selector.

`Theme._isEmojiPresentation()` treats "followed by U+FE0F" as the signal, bounded to keycap bases
and `cp >= 0xA9` so a stray selector cannot turn a letter into an image.

## Using emoji well — where they earn their place

Emoji are cheap, render identically on every platform, and are the one visual element that
survives translation unchanged. Reach for them where they make a screen easier to read or more
pleasant to use — an interface that is all grey text is not more professional, it is just
harder to scan. CLAUDE.md carries the short version and the one hard rule (never a plain
`Text`); this is the full guidance.

**Where they earn their place:**
- Category and section markers, where a glyph makes a list scannable at a glance.
- Status and outcome, alongside the words rather than instead of them — `☕ Espresso`,
  `⚠️ Tank low`, `✅ Uploaded`.
- User-authored content: bean names, recipe names, widget labels, notes. Users already type
  emoji here and the picker offers the full set.
- Empty states and first-run screens, where a little warmth reads as care rather than noise.

**Where they do not:**
- **Never as the only carrier of meaning.** A screen reader announces the name, not the picture,
  and a stripped context (plain-text fields, exports, MCP responses) drops it entirely. Always
  pair with a word.
- **Not on destructive or error actions** in place of clear wording. `🗑️ Delete all shots` is
  fine; `🗑️` alone is not.
- **Not more than one per label,** and not decorating every row of a list — repetition turns a
  useful signal into visual noise, and a page where everything is marked marks nothing.
- **Not in place of a themed icon** for chrome — toolbar and navigation icons are monochrome
  SVGs that follow `Theme.iconColor` through `ThemedIcon`. Emoji carry fixed colours and will
  not adapt to light/dark or a custom palette.

**Mechanics — these are not optional:**
- Render through `Theme.emojiToImage()` (for an `Image`) or `Theme.replaceEmojiWithImg()` (for
  text with emoji inline). Putting an emoji in a plain `Text` lets a colour glyph reach the
  platform renderer, which **crashes the render thread on macOS**.
- No manual asset step. Using a new emoji needs no download and no `.qrc` edit — the full set
  already ships. `.github/workflows/emoji-pin-check.yml` reports when upstream has a newer
  release worth pulling in.
- An emoji with no bundled asset is silently stripped, so a sequence from a Unicode revision
  newer than the pin simply disappears. Don't build a layout that only makes sense if the emoji
  renders.
- For accessibility, give the element an `Accessible.name` with the word, not the picture.
  `Theme.toAccessibleText()` strips emoji and tags from a rendered string for exactly this.

## Switching Emoji Sets

```bash
# twemoji, openmoji, noto, fluentui — regenerates resources/emoji/ + resources/emoji.qrc
python scripts/download_emoji.py openmoji
```

**Careful: without `--all` this shrinks the set to the ~750 codepoints `EmojiData.js` lists**,
which silently breaks the guarantee that every emoji resolves locally — everything else gets
stripped from user text. Only `twemoji` supports `--all` today (it is the only source with a
pinned `repo`/`tag`); adding it for another source means giving that class the same attributes
and a release archive layout.

OpenMoji also ships a `black/` tree of monochrome line-art (`fill="none"` + a single stroke, the
same structure as `resources/icons/*.svg`), which is the variant to look at if emoji are ever
considered as replacements for themed monochrome icons. Note it is CC-BY-SA where Twemoji is MIT.

## Adding New Emojis

**Nothing to do.** The full set already ships, so using a new emoji needs no download and no
`.qrc` edit. Add it to `qml/components/layout/EmojiData.js` only if it should appear in the
picker's categories.

## Updating to a newer upstream release

Pinned to `jdecked/twemoji@17.0.3` (the maintained fork; `twitter/twemoji`'s last release is
v14.0.2 from March 2022 and 404s on Unicode 15+). Pinning keeps a rebuild reproducible — `@latest`
would let upstream change rendering with no commit on our side.

```bash
# edit Twemoji.tag in scripts/download_emoji.py, then:
python scripts/download_emoji.py twemoji --all      # one tarball, not ~4,000 requests
git add resources/emoji resources/emoji.qrc
```

`.github/workflows/emoji-pin-check.yml` runs monthly and reports when a newer release exists;
`python scripts/download_emoji.py twemoji --check-updates` does the same locally.

**Check the diff for MODIFIED files, not just additions.** Upstream revises existing artwork — 8 of
744 changed between 14.0.2 and 17.0.3 (mostly path optimisation, but 🔒 was genuinely redrawn).

## Attribution

Twemoji, maintained by jdecked (MIT): https://github.com/jdecked/twemoji
