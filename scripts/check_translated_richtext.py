#!/usr/bin/env python3
"""Fail if a translated string reaches a rich-text renderer unescaped.

Community translations are downloaded from the backend and merged automatically at launch, and
uploading one is unauthenticated -- so a translated string is attacker-influenceable in a way
an ordinary literal is not. `Text.StyledText` parses its input as markup and renders <img>
(which is exactly how Theme.replaceEmojiWithImg draws emoji), so an unescaped remote <img> in a
translation would be fetched by every user of that language when the element renders.

Safe patterns, all already used in the tree:
  * plain Text (no textFormat) -- the default, and what Tr.qml uses, so the ~3,200 ordinary
    call sites are not affected
  * Theme.escapeHtml(TranslationManager.translate(...))  -- ShotDetailPage.qml:344
  * Theme.joinWithBullet(parts)                          -- escapes each part
  * Theme.replaceEmojiWithImg(text, size)                -- escapes non-emoji chunks unless
    the third argument allowMarkup is true, which callers pass only for HTML they built
    themselves out of already-escaped pieces
"""
import glob, io, re, sys

RICH = re.compile(r'textFormat\s*:\s*Text\.(StyledText|RichText)')
TEXT_BINDING = re.compile(r'\btext\s*:\s*(.+?)(?:\n\s*[a-zA-Z_.]+\s*:|\n\s*\})', re.S)

def main() -> int:
    findings = []
    for path in sorted(glob.glob("qml/**/*.qml", recursive=True)):
        lines = io.open(path, encoding="utf-8").read().split("\n")
        for i, line in enumerate(lines):
            if not RICH.search(line):
                continue
            block = "\n".join(lines[max(0, i - 14):min(len(lines), i + 14)])
            m = TEXT_BINDING.search(block)
            if not m:
                continue
            expr = " ".join(m.group(1).split())
            if "TranslationManager.translate" not in expr:
                continue
            # Escaped, or routed through a helper that escapes.
            if ("escapeHtml" in expr or "joinWithBullet" in expr
                    or re.search(r'replaceEmojiWithImg\([^)]*\)(?!\s*,\s*true)', expr)):
                continue
            findings.append((path, i + 1, expr[:160]))

    print(f"Checked QML for translated strings bound to StyledText/RichText.")
    if not findings:
        print("None reach a rich-text renderer unescaped.")
        return 0
    print(f"\n{len(findings)} unescaped:\n")
    for path, lineno, expr in findings:
        print(f"  {path}:{lineno}\n      text: {expr}\n")
    print("Wrap the translated value in Theme.escapeHtml(), or drop textFormat if the element")
    print("does not actually need markup.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
