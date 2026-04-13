# Emoji System

Emojis are rendered as pre-rendered SVG images (Twemoji), not via a color font. This avoids D3D12/GPU crashes caused by CBDT/CBLC bitmap fonts (NotoColorEmoji.ttf) being incompatible with Qt's scene graph glyph cache across all platforms.

## How It Works

- Emoji characters are stored as Unicode strings in settings/layout data (e.g., `"☕"`, `"😀"`)
- Decenza SVG icons are stored as `qrc:/icons/...` paths (e.g., `"qrc:/icons/espresso.svg"`)
- At display time, `Theme.emojiToImage(emoji)` converts to an image path:
  - `qrc:/icons/*` paths pass through unchanged
  - Unicode emoji → codepoints → `qrc:/emoji/<hex>.svg` (e.g., `"☕"` → `"qrc:/emoji/2615.svg"`)
- All QML components use `Image { source: Theme.emojiToImage(value) }` — never Text for emojis

## Switching Emoji Sets

```bash
# Download from a different source (twemoji, openmoji, noto, fluentui)
python scripts/download_emoji.py openmoji
# Regenerates resources/emoji/ and resources/emoji.qrc
```

## Adding New Emojis

1. Add the emoji character to the relevant category in `qml/components/layout/EmojiData.js`
2. Re-run `python scripts/download_emoji.py twemoji` to download the new SVG
3. Rebuild (the script regenerates `emoji.qrc`)

## Attribution

Twemoji by Twitter/X (CC-BY 4.0): https://github.com/twitter/twemoji
