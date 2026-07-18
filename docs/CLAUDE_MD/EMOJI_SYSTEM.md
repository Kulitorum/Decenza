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
