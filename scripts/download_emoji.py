#!/usr/bin/env python3
"""
Download emoji SVGs from various open-source emoji sets.

Usage:
    python scripts/download_emoji.py twemoji      # Twitter Twemoji (flat, clean)
    python scripts/download_emoji.py openmoji      # OpenMoji (outline style)
    python scripts/download_emoji.py noto          # Google Noto Emoji
    python scripts/download_emoji.py fluentui      # Microsoft Fluent UI Emoji (flat)

Downloads ~500 emojis used by the app + weather emojis.
Outputs to resources/emoji/ and generates resources/emoji.qrc.
"""

import sys
import os
import re
import json
import urllib.request
import urllib.error
import time
import io

# Fix Windows console encoding for emoji output
if sys.platform == "win32":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
EMOJI_DIR = os.path.join(PROJECT_DIR, "resources", "emoji")
QRC_PATH = os.path.join(PROJECT_DIR, "resources", "emoji.qrc")
EMOJI_DATA_JS = os.path.join(PROJECT_DIR, "qml", "components", "layout", "EmojiData.js")


# --- Emoji sources ---

class EmojiSource:
    """Base class for emoji download sources."""
    name = "unknown"
    license_info = ""

    def svg_url(self, codepoints_hex: list[str]) -> list[str]:
        """Return list of candidate URLs to try (first match wins)."""
        raise NotImplementedError


class Twemoji(EmojiSource):
    """Twitter Twemoji - flat, colorful, widely used. CC-BY 4.0."""
    name = "twemoji"
    license_info = "Twemoji by Twitter (CC-BY 4.0) - https://github.com/twitter/twemoji"

    def svg_url(self, cps: list[str]) -> list[str]:
        base = "https://cdn.jsdelivr.net/gh/twitter/twemoji@14.0.2/assets/svg"
        joined = "-".join(cps)
        # Try with all codepoints, then without fe0f
        urls = [f"{base}/{joined}.svg"]
        without_fe0f = "-".join(c for c in cps if c != "fe0f")
        if without_fe0f != joined:
            urls.append(f"{base}/{without_fe0f}.svg")
        return urls


class OpenMoji(EmojiSource):
    """OpenMoji - outline style, colorful. CC BY-SA 4.0."""
    name = "openmoji"
    license_info = "OpenMoji (CC BY-SA 4.0) - https://openmoji.org"

    def svg_url(self, cps: list[str]) -> list[str]:
        base = "https://cdn.jsdelivr.net/gh/hfg-gmuend/openmoji@15.0/color/svg"
        # OpenMoji uses uppercase hex with hyphens
        joined = "-".join(c.upper() for c in cps)
        urls = [f"{base}/{joined}.svg"]
        without_fe0f = "-".join(c.upper() for c in cps if c != "fe0f")
        if without_fe0f != joined:
            urls.append(f"{base}/{without_fe0f}.svg")
        return urls


class NotoEmoji(EmojiSource):
    """Google Noto Emoji - Android-style, colorful. Apache 2.0."""
    name = "noto"
    license_info = "Noto Color Emoji by Google (Apache 2.0) - https://github.com/googlefonts/noto-emoji"

    def svg_url(self, cps: list[str]) -> list[str]:
        base = "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/svg"
        # Noto uses emoji_u{cp1}_cp2.svg format, lowercase
        joined = "_".join(cps)
        urls = [f"{base}/emoji_u{joined}.svg"]
        without_fe0f = "_".join(c for c in cps if c != "fe0f")
        if without_fe0f != joined:
            urls.append(f"{base}/emoji_u{without_fe0f}.svg")
        return urls


class FluentUI(EmojiSource):
    """Microsoft Fluent UI Emoji - modern flat style. MIT License."""
    name = "fluentui"
    license_info = "Fluent Emoji by Microsoft (MIT) - https://github.com/nicedoc/fluent-emoji-flat"

    def svg_url(self, cps: list[str]) -> list[str]:
        # Fluent flat emoji via CDN (codepoints with fe0f stripped, hyphen-joined)
        base = "https://cdn.jsdelivr.net/gh/nicedoc/fluent-emoji-flat@1.0/assets"
        joined = "-".join(cps)
        urls = [f"{base}/{joined}.svg"]
        without_fe0f = "-".join(c for c in cps if c != "fe0f")
        if without_fe0f != joined:
            urls.append(f"{base}/{without_fe0f}.svg")
        return urls


SOURCES = {
    "twemoji": Twemoji(),
    "openmoji": OpenMoji(),
    "noto": NotoEmoji(),
    "fluentui": FluentUI(),
}


# --- Parse emoji list from EmojiData.js ---

def parse_emoji_data_js(path: str) -> list[str]:
    """Extract all emoji strings from EmojiData.js."""
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    emojis = []
    # Find all emoji: [...] arrays and extract the string literals inside
    for array_match in re.finditer(r'emoji:\s*\[(.*?)\]', content, re.DOTALL):
        array_content = array_match.group(1)
        for str_match in re.finditer(r'"([^"]+)"', array_content):
            char = str_match.group(1)
            # Emoji chars: at least one codepoint > 0x200 (not plain ASCII text)
            if any(ord(c) > 0x200 for c in char):
                emojis.append(char)

    return emojis


def get_weather_emojis() -> list[str]:
    """Weather emojis used by WeatherItem.qml."""
    return [
        "\u2600",      # ☀ clear
        "\u26C5",      # ⛅ partly-cloudy
        "\u2601",      # ☁ overcast
        "\U0001F32B",  # 🌫 fog
        "\U0001F326",  # 🌦 drizzle/showers
        "\U0001F327",  # 🌧 rain
        "\u2744",      # ❄ snow/freezing-rain
        "\U0001F328",  # 🌨 snow-showers
        "\u26A1",      # ⚡ thunderstorm
    ]


def emoji_to_codepoints(emoji: str) -> list[str]:
    """Convert emoji string to list of hex codepoint strings."""
    cps = []
    i = 0
    while i < len(emoji):
        cp = ord(emoji[i])
        # Handle surrogate pairs (shouldn't happen in Python 3, but be safe)
        if 0xD800 <= cp <= 0xDBFF and i + 1 < len(emoji):
            lo = ord(emoji[i + 1])
            cp = 0x10000 + (cp - 0xD800) * 0x400 + (lo - 0xDC00)
            i += 2
        else:
            i += 1
        cps.append(f"{cp:x}")
    return cps


def codepoints_to_filename(cps: list[str]) -> str:
    """Generate a canonical filename from codepoints (without fe0f for shorter names)."""
    # Strip fe0f from filename for cleaner paths, but keep for download URL
    clean = [c for c in cps if c != "fe0f"]
    return "-".join(clean)


# --- Download logic ---

def download_svg(url: str) -> bytes | None:
    """Download SVG from URL, return bytes or None on failure."""
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "DecenzaDE1-EmojiDownloader/1.0"})
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = resp.read()
            if b"<svg" in data.lower():
                return data
            return None
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError):
        return None


def download_emoji(source: EmojiSource, emoji: str, cps: list[str]) -> bytes | None:
    """Try all URL candidates for an emoji, return SVG data or None."""
    urls = source.svg_url(cps)
    for url in urls:
        data = download_svg(url)
        if data:
            return data
    return None


# --- Generate QRC ---

def generate_qrc(filenames: list[str], qrc_path: str):
    """Generate a Qt resource file for the emoji SVGs."""
    lines = ['<!DOCTYPE RCC>', '<RCC version="1.0">', '    <qresource prefix="/">']
    for fn in sorted(filenames):
        lines.append(f'        <file>emoji/{fn}</file>')
    lines.append('    </qresource>')
    lines.append('</RCC>')
    lines.append('')

    with open(qrc_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


# --- Main ---

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in SOURCES:
        print(f"Usage: {sys.argv[0]} <source>")
        print(f"  Sources: {', '.join(SOURCES.keys())}")
        print()
        for name, src in SOURCES.items():
            print(f"  {name:12s} - {src.license_info}")
        sys.exit(1)

    source = SOURCES[sys.argv[1]]
    print(f"Downloading emoji from: {source.name}")
    print(f"License: {source.license_info}")
    print()

    # Collect all unique emojis
    emojis_from_data = parse_emoji_data_js(EMOJI_DATA_JS)
    weather_emojis = get_weather_emojis()
    all_emojis = list(dict.fromkeys(emojis_from_data + weather_emojis))  # Deduplicate, preserve order
    print(f"Found {len(all_emojis)} unique emojis to download")

    # Ensure output directory exists
    os.makedirs(EMOJI_DIR, exist_ok=True)

    # Clear existing SVGs
    for f in os.listdir(EMOJI_DIR):
        if f.endswith(".svg"):
            os.remove(os.path.join(EMOJI_DIR, f))

    success = 0
    failed = []
    filenames = []

    for i, emoji in enumerate(all_emojis):
        cps = emoji_to_codepoints(emoji)
        fname = codepoints_to_filename(cps) + ".svg"
        filepath = os.path.join(EMOJI_DIR, fname)

        data = download_emoji(source, emoji, cps)
        if data:
            with open(filepath, "wb") as f:
                f.write(data)
            filenames.append(fname)
            success += 1
            status = "OK"
        else:
            failed.append((emoji, cps))
            status = "FAIL"

        # Progress
        if (i + 1) % 20 == 0 or status == "FAIL":
            emoji_display = emoji.encode('unicode_escape').decode('ascii') if status == "FAIL" else emoji
            print(f"  [{i+1}/{len(all_emojis)}] {emoji_display} ({'-'.join(cps)}) ... {status}")

        # Rate limiting - be nice to CDN
        if (i + 1) % 50 == 0:
            time.sleep(0.5)

    print()
    print(f"Downloaded: {success}/{len(all_emojis)}")

    if failed:
        print(f"Failed ({len(failed)}):")
        for emoji, cps in failed:
            print(f"  {emoji} ({'-'.join(cps)})")

    # Generate QRC file
    generate_qrc(filenames, QRC_PATH)
    print(f"\nGenerated: {QRC_PATH} ({len(filenames)} entries)")

    # Calculate total size
    total_size = sum(os.path.getsize(os.path.join(EMOJI_DIR, fn)) for fn in filenames)
    print(f"Total size: {total_size / 1024:.0f} KB ({total_size / 1024 / 1024:.1f} MB)")
    print(f"\nRemember to add emoji.qrc to CMakeLists.txt and attribute:")
    print(f"  {source.license_info}")

    # Write a mapping JSON for the QML helper to use at dev-time verification
    mapping = {}
    for emoji in all_emojis:
        cps = emoji_to_codepoints(emoji)
        fname = codepoints_to_filename(cps) + ".svg"
        if fname in filenames:
            mapping[emoji] = fname
    mapping_path = os.path.join(EMOJI_DIR, "_mapping.json")
    with open(mapping_path, "w", encoding="utf-8") as f:
        json.dump(mapping, f, ensure_ascii=False, indent=2)
    print(f"Wrote mapping: {mapping_path}")


if __name__ == "__main__":
    main()
