#!/usr/bin/env python3
"""
Download emoji SVGs from various open-source emoji sets.

Usage:
    python scripts/download_emoji.py twemoji      # Twitter Twemoji (flat, clean)
    python scripts/download_emoji.py openmoji      # OpenMoji (outline style)
    python scripts/download_emoji.py noto          # Google Noto Emoji
    python scripts/download_emoji.py fluentui      # Microsoft Fluent UI Emoji (flat)

Default: the ~750 emoji the app's own content references.
--all: the COMPLETE upstream set (~4,000), which is what ships.
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
    """Twemoji - flat, colorful, widely used. MIT.

    Pinned to jdecked/twemoji, NOT twitter/twemoji. The original is not archived, but
    its last release is v14.0.2 (March 2022) and it 404s on Unicode 15+ codepoints such
    as 1fae8. jdecked/twemoji is the maintained continuation and the artwork is identical
    where the two overlap (2615.svg is byte-identical across 14.0.2, 15.1.0, 16.0.1 and
    17.0.3), so moving the pin adds emoji without restyling the ones already shipped.

    Pinned rather than @latest: with no runtime fetching this is purely about build
    reproducibility, but @latest would let a rebuild months from now produce different
    artwork than the release it claims to reproduce, with no commit explaining why.
    """
    name = "twemoji"
    license_info = "Twemoji by Twitter, maintained by jdecked (MIT) - https://github.com/jdecked/twemoji"
    # Bump deliberately, and re-run with --all so bundled assets match the pin.
    repo = "jdecked/twemoji"
    tag = "v17.0.3"
    # Path within the repo archive that holds the SVGs.
    archive_svg_dir = "assets/svg"

    def svg_url(self, cps: list[str]) -> list[str]:
        base = f"https://cdn.jsdelivr.net/gh/{self.repo}@{self.tag.lstrip('v')}/assets/svg"
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
        "\U0001F311",  # 🌑 new moon
        "\U0001F312",  # 🌒 waxing crescent
        "\U0001F313",  # 🌓 first quarter
        "\U0001F314",  # 🌔 waxing gibbous
        "\U0001F315",  # 🌕 full moon
        "\U0001F316",  # 🌖 waning gibbous
        "\U0001F317",  # 🌗 last quarter
        "\U0001F318",  # 🌘 waning crescent
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
        req = urllib.request.Request(url, headers={"User-Agent": "Decenza-EmojiDownloader/1.0"})
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

def check_for_update(source) -> int:
    """Report whether upstream has released a version newer than our pin.

    The whole point of pinning is that upstream cannot change our rendering without a
    commit. The cost is that the pin silently rots — nobody notices a year has passed and
    two Unicode revisions have shipped. This closes that gap without giving up
    reproducibility: the build tells you a newer version exists, and a human decides.

    Deliberately advisory. It must never fail a build or auto-bump: a network hiccup, a
    rate limit, or an upstream retag should not be able to break someone's compile or
    silently change what their app renders.

    Exit codes are DISTINCT so the CI job cannot report a crash as "current":
      0 = pin is current
      2 = a newer release exists
      3 = could not determine (offline, rate-limited, API changed)
    1 stays reserved for usage errors, which is what an unhandled traceback also is not —
    an unexpected non-zero turns the job red rather than printing an all-clear.
    """
    if not hasattr(source, "repo"):
        print(f"{source.name}: no pinned repo/tag, nothing to check")
        return 0

    url = f"https://api.github.com/repos/{source.repo}/releases/latest"
    req = urllib.request.Request(url, headers={"User-Agent": "Decenza-EmojiDownloader/1.0",
                                               "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            latest = json.load(resp).get("tag_name", "")
    except Exception as e:
        # Offline, rate-limited, DNS down: say so and move on. Not an error.
        print(f"emoji: could not check {source.repo} for updates ({e})")
        return 3

    if not latest:
        print(f"emoji: {source.repo} reported no latest release")
        return 3

    def parts(tag):
        return [int(x) for x in re.findall(r"\d+", tag)] or [0]

    if parts(latest) > parts(source.tag):
        print()
        print(f"  emoji assets are OUT OF DATE")
        print(f"    pinned: {source.tag}    latest: {latest}  ({source.repo})")
        print(f"    to update: edit {os.path.basename(__file__)} (Twemoji.tag), then")
        print(f"      python scripts/download_emoji.py {source.name} --all")
        print(f"    and commit resources/emoji/ + resources/emoji.qrc.")
        print(f"    Emoji from a newer Unicode revision are stripped until you do.")
        print()
        return 2

    print(f"emoji: {source.repo} {source.tag} is current (latest: {latest})")
    return 0


def download_full_set(source, emoji_dir: str) -> list[str]:
    """Fetch the COMPLETE upstream emoji set and write it to emoji_dir.

    Downloads the pinned release tarball in ONE request rather than issuing ~4,000
    individual CDN requests: it is far faster, and it is the polite way to take a whole
    repository from a free CDN.

    Returns the list of written filenames.
    """
    import tarfile
    import tempfile

    if not hasattr(source, "repo"):
        print(f"ERROR: source '{source.name}' does not support --all "
              f"(no pinned repo/tag). Only twemoji does today.")
        sys.exit(1)

    url = f"https://github.com/{source.repo}/archive/refs/tags/{source.tag}.tar.gz"
    print(f"Fetching complete set: {url}")

    req = urllib.request.Request(url, headers={"User-Agent": "Decenza-EmojiDownloader/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            blob = resp.read()
    except OSError as e:
        # OSError covers URLError AND the bare TimeoutError a socket read timeout raises
        # mid-download, which URLError alone does not catch.
        print(f"ERROR: could not fetch the release archive: {e}")
        print("Existing assets left untouched.")
        sys.exit(1)

    print(f"  archive: {len(blob) / 1024 / 1024:.1f} MB")

    written = []
    staged = {}
    skipped = 0
    with tempfile.TemporaryFile() as tmp:
        tmp.write(blob)
        tmp.seek(0)
        with tarfile.open(fileobj=tmp, mode="r:gz") as tar:
            # Paths look like "twemoji-17.0.3/assets/svg/2615.svg".
            wanted = f"/{source.archive_svg_dir}/"
            for member in tar.getmembers():
                if not member.isfile() or not member.name.endswith(".svg"):
                    continue
                if wanted not in member.name:
                    continue
                # basename() IS the traversal guard — it discards any directory component,
                # so a "../../etc/x.svg" member becomes "x.svg". (An earlier version also
                # compared fname to basename(fname), which is vacuous by construction.)
                fname = os.path.basename(member.name)
                if fname.startswith("."):
                    continue
                f = tar.extractfile(member)
                if f is None:
                    skipped += 1
                    continue
                staged[fname] = f.read()
                written.append(fname)

    # "Not zero" is far too weak a check. If upstream restructures, or extractfile() returns
    # None for most members, a handful of files would regenerate emoji.qrc with a handful of
    # entries — and EmojiAssets would then find a non-empty set, keep quiet, and strip ~4,000
    # emoji as if each were individually unbundled. tst_emojiassets asserts > 4000; the
    # generator should refuse to produce something that test would reject.
    MIN_EXPECTED = 3000
    if len(written) < MIN_EXPECTED:
        print(f"ERROR: only {len(written)} SVGs found under '{source.archive_svg_dir}' "
              f"(expected at least {MIN_EXPECTED}). The upstream layout may have changed. "
              f"Existing assets left untouched.")
        sys.exit(1)
    if skipped:
        print(f"  note: skipped {skipped} unreadable member(s)")

    # Only now is it safe to replace what is already committed.
    for f in os.listdir(emoji_dir):
        if f.endswith(".svg"):
            os.remove(os.path.join(emoji_dir, f))
    for fname, data in staged.items():
        with open(os.path.join(emoji_dir, fname), "wb") as out:
            out.write(data)

    print(f"  extracted: {len(written)} SVGs")
    return written


def generate_qrc(filenames: list[str], qrc_path: str):
    """Generate a Qt resource file for the emoji SVGs."""
    lines = ['<!DOCTYPE RCC>', '<RCC version="1.0">', '    <qresource prefix="/">']
    for fn in sorted(set(filenames)):
        lines.append(f'        <file>emoji/{fn}</file>')
    lines.append('    </qresource>')
    lines.append('</RCC>')
    lines.append('')

    with open(qrc_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


# --- Main ---

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    flags = {a for a in sys.argv[1:] if a.startswith("-")}
    fetch_all = "--all" in flags
    check_only = "--check-updates" in flags

    if not args or args[0] not in SOURCES:
        print(f"Usage: {sys.argv[0]} <source> [--all]")
        print(f"  Sources: {', '.join(SOURCES.keys())}")
        print()
        print("  --all   Fetch the COMPLETE upstream set (~4,000 emoji) instead of only")
        print("          the codepoints EmojiData.js references. This is what ships: the")
        print("          app resolves emoji locally with no network fallback, so anything")
        print("          not bundled is stripped from displayed text.")
        print()
        print("  --check-updates")
        print("          Report whether upstream has a release newer than our pin, and exit.")
        print("          Advisory only: prints and returns 1, never modifies anything.")
        print()
        for name, src in SOURCES.items():
            print(f"  {name:12s} - {src.license_info}")
        sys.exit(1)

    source = SOURCES[args[0]]

    if check_only:
        sys.exit(check_for_update(source))

    print(f"Downloading emoji from: {source.name}")
    print(f"License: {source.license_info}")
    print()

    # Ensure output directory exists
    os.makedirs(EMOJI_DIR, exist_ok=True)

    if fetch_all:
        # Fetch and validate BEFORE destroying anything. Deleting first meant a flaky network
        # on a routine pin bump wiped ~4,000 committed assets and left emoji.qrc listing files
        # that no longer existed — a build failure naming individual SVGs and saying nothing
        # about the download that removed them.
        filenames = download_full_set(source, EMOJI_DIR)
        unique_filenames = sorted(set(filenames))
        generate_qrc(filenames, QRC_PATH)
        print(f"\nGenerated: {QRC_PATH} ({len(unique_filenames)} entries)")
        total_size = sum(os.path.getsize(os.path.join(EMOJI_DIR, fn)) for fn in unique_filenames)
        print(f"Total size: {total_size / 1024 / 1024:.2f} MB")
        return

    # Curated path: safe to clear first, since each emoji is fetched individually and a
    # partial result is reported rather than silently accepted.
    for f in os.listdir(EMOJI_DIR):
        if f.endswith(".svg"):
            os.remove(os.path.join(EMOJI_DIR, f))

    # Collect all unique emojis
    emojis_from_data = parse_emoji_data_js(EMOJI_DATA_JS)
    weather_emojis = get_weather_emojis()
    all_emojis = list(dict.fromkeys(emojis_from_data + weather_emojis))  # Deduplicate, preserve order
    print(f"Found {len(all_emojis)} unique emojis to download")

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
    unique_filenames = sorted(set(filenames))
    generate_qrc(filenames, QRC_PATH)
    print(f"\nGenerated: {QRC_PATH} ({len(unique_filenames)} entries)")

    # Calculate total size
    total_size = sum(os.path.getsize(os.path.join(EMOJI_DIR, fn)) for fn in unique_filenames)
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
