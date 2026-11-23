#!/usr/bin/env python3
"""
Survey icon families for the redraw-icon-set change, the way download_emoji.py surveys
emoji sets: multi-source, each PINNED to a tag, fetched reproducibly.

This is the SURVEY tool (OpenSpec `redraw-icon-set`, task 3.1). It fetches candidate
icons into a STAGING directory for a contact sheet — it does NOT touch resources/icons/.
Adopting a family (copying into resources/icons/ under the app's filenames) is a separate,
owner-gated step; this script deliberately cannot do it.

Usage:
    python3 scripts/download_icons.py phosphor-duotone    # Phosphor duotone weight (MIT)
    python3 scripts/download_icons.py material-symbols    # Material Symbols outlined (Apache-2.0)
    python3 scripts/download_icons.py all                 # every source, into staging
    python3 scripts/download_icons.py <source> --check-updates

Staging dir (git-ignored, never committed):
    <repo>/.icon-survey/<source>/

GATE NOTE (task 1.3a, PROVEN): Qt's `MultiEffect { colorization: 1.0 }` maps a source's
luminance onto the tint and leaves alpha untouched (multieffect.frag:84,116). Phosphor
duotone ships `fill="currentColor"`, which Qt's QSvgRenderer resolves to BLACK with no
colour context (qsvghandler.cpp:4200) -> luminance 0 -> the tint is lost. So duotone only
survives colorization once both paths are WHITE-NORMALIZED (fill="#fff", opacity kept).
This script writes both the as-fetched file AND a white-normalized copy so the contact
sheet can show the real post-colorization outcome.
"""

from __future__ import annotations

import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

if sys.platform == "win32":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
# Survey output is staging only. NOT resources/icons/ — adoption is owner-gated.
STAGING_DIR = os.path.join(PROJECT_DIR, ".icon-survey")


# --- Sources -------------------------------------------------------------------

class IconSource:
    """Base class for an icon survey source.

    Each source is pinned to a tag for the same reason download_emoji.py pins: a
    rebuild months from now must reproduce the artwork it claims to, with a commit
    explaining any change rather than @latest drifting silently.
    """
    name = "unknown"
    license_info = ""
    repo = ""
    tag = ""

    # Map from an abstract concept -> this family's icon name. The survey only needs a
    # handful of concepts to drive the head-to-head (task 1.3): the four most-seen icons
    # plus a couple of domain probes. Name-matching a library index is exactly the mistake
    # the emoji audit made, so this map is small, explicit, and meant to be eyeballed, not
    # trusted — the contact sheet is the real judge.
    concepts: dict[str, str] = {}

    def svg_url(self, icon_name: str) -> str:
        raise NotImplementedError


class PhosphorDuotone(IconSource):
    """Phosphor icons, DUOTONE weight. MIT.

    Duotone expresses depth as two paths of one colour: a solid mass at opacity 0.2 and
    the linework at opacity 1.0 (design.md D1). That is the "craft within one tintable
    colour" this change is chasing. See the GATE NOTE at module top for what Qt requires
    of it (white-normalization).

    Pinned to phosphor-icons/core (the SVG source of record), latest release v2.0.8.
    """
    name = "phosphor-duotone"
    license_info = "Phosphor Icons (MIT) - https://github.com/phosphor-icons/core"
    repo = "phosphor-icons/core"
    tag = "v2.0.8"
    archive_svg_dir = "assets/duotone"
    # NOTE: v2.0.8 has no coffee-bean icon; 'beans' is intentionally unmapped so the
    # contact sheet shows the gap (a domain concept no family covers — design.md D1a).
    #
    # ADOPTION MAP (task 3.1). Keyed by the app's EXACT current filename (without .svg),
    # mapped to the Phosphor-duotone icon name by CONCEPT, not string-match — each was
    # eyeballed against the current artwork at 20px. Entries present here are GENERIC
    # candidates fetched for the contact sheet; DOMAIN icons (decent-de1, niche-zero,
    # the espresso/flush baskets, grind, coffeebeans, 8mm, taste-*, body-*, barista,
    # picker-wheel) are NOT here — they are drawn by hand. KEEP-CURRENT entries (where
    # no Phosphor concept beats the current icon, or a plain-Image/baked-colour consumer
    # forbids white-normalization) are also absent and documented in the report.
    concepts = {
        # --- alignment / sort / list / grid ---
        "AlignCenter": "text-align-center",
        "AlignLeft": "text-align-left",
        "AlignRight": "text-align-right",
        "SortAscending": "sort-ascending",
        "SortDescending": "sort-descending",
        "list": "list",
        "grid": "grid-four",
        # --- navigation / arrows ---
        "ArrowLeft": "arrow-left",
        "back": "caret-left",
        "quit": "sign-out",
        "portal": "door-open",
        # --- status / notification ---
        "bell": "bell",
        "bell-off": "bell-slash",
        "warning": "warning",
        "info": "info",
        "clock": "clock",
        "history": "clock-counter-clockwise",
        "sparkle": "sparkle",
        "pin": "map-pin",
        # --- editing / crud ---
        "edit": "pencil-simple",
        "trash": "trash",
        "plus": "plus",
        "minus": "minus",
        "tick": "check",
        "cross": "x",
        "more-vertical": "dots-three-vertical",
        "search": "magnifying-glass",
        "filter": "funnel",
        "discuss": "chat-circle-dots",
        # --- visibility ---
        "eye": "eye",
        "eye-off": "eye-slash",
        # --- transfer / cloud / io ---
        "download": "download-simple",
        "Upload": "upload-simple",
        "CloudUpload": "cloud-arrow-up",
        "Graph": "chart-line",
        # --- connectivity / hardware ---
        "wifi": "wifi-high",
        "keyboard": "keyboard",
        "bluetooth": "bluetooth",
        "usb": "usb",
        "scale": "scales",
        "temperature": "thermometer-simple",
        "sleep": "power",
        # --- profile / people / star ---
        "profile": "wave-sine",
        "star": "star",
        "star-outline": "star",   # same glyph; outline done by dropping the fill at adopt-time
        "hand": "hand",
        # --- drinkware / consumables ---
        "cup": "coffee",
        "mug": "coffee",
        "tea": "coffee",          # Phosphor has no teapot in v2.0.8 duotone; bespoke likely better
        "glass-full": "beer-stein",
        "pitcher": "beer-stein",
        "water": "drop",
        "steam": "drop",          # closest generic analogue; steam is domain-bespoke
        # "beans": no duotone bean in v2.0.8 -> DOMAIN
    }

    def svg_url(self, icon_name: str) -> str:
        base = f"https://raw.githubusercontent.com/{self.repo}/{self.tag}/{self.archive_svg_dir}"
        return f"{base}/{icon_name}-duotone.svg"


class MaterialSymbols(IconSource):
    """Material Symbols, outlined weight. Apache-2.0.

    A variable font with optical-size / weight / grade / FILL axes (design.md D1a). The
    SVG exports live under symbols/web/<name>/materialsymbolsoutlined/. Single-path fills,
    so recolourable; no duotone, so depth comes from weight/fill rather than two opacities.

    Pinned to a COMMIT SHA, not a tag: the repo's semantic tags (4.0.0 etc.) predate the
    symbols/web tree, which lives only on the moving master branch. A SHA is the only
    reproducible pin available, and reproducibility is the whole point of pinning.
    """
    name = "material-symbols"
    license_info = "Material Symbols by Google (Apache-2.0) - https://github.com/google/material-design-icons"
    repo = "google/material-design-icons"
    # master @ 2026-10-09; see docstring for why a SHA rather than a semantic tag.
    tag = "49d4db35df873165d6bd6ba09b063c7dafbac2f4"
    style = "materialsymbolsoutlined"
    concepts = {
        "espresso": "coffee",
        "settings": "settings",
        "steam": "water_drop",
        "beans": "nutrition",   # best generic analogue; still a substitution, judge by eye
    }

    def svg_url(self, icon_name: str) -> str:
        base = f"https://raw.githubusercontent.com/{self.repo}/{self.tag}/symbols/web"
        return f"{base}/{icon_name}/{self.style}/{icon_name}_24px.svg"


SOURCES = {
    "phosphor-duotone": PhosphorDuotone(),
    "material-symbols": MaterialSymbols(),
}


# --- Fetch (shared with download_emoji.py's shape) -----------------------------

def _urlopen_checked(req, timeout):
    """urlopen restricted to http(s). Same guard as download_emoji.py: a mis-built URL
    must not be able to read a local file via file://."""
    if urllib.parse.urlsplit(req.full_url).scheme not in ("http", "https"):
        raise ValueError(f"refusing non-http(s) URL: {req.full_url!r}")
    return urllib.request.urlopen(req, timeout=timeout)  # nosec B310 - scheme checked above


def download_svg(url: str) -> bytes | None:
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Decenza-IconSurvey/1.0"})
        with _urlopen_checked(req, 15) as resp:
            data = resp.read()
            if b"<svg" in data.lower():
                return data
            return None
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, ValueError):
        return None


def white_normalize(svg: bytes) -> bytes:
    """Rewrite fill/stroke to white while PRESERVING per-path opacity attributes.

    This is the step the GATE NOTE requires: Qt colorization needs a high-luminance
    (white) source or the tint is lost, but the two opacities are what carry the depth,
    so they must survive. Only the colour is touched, never opacity.
    """
    text = svg.decode("utf-8", errors="replace")
    text = text.replace('fill="currentColor"', 'fill="#ffffff"')
    text = text.replace("fill='currentColor'", "fill='#ffffff'")
    # Material Symbols omit fill entirely (default black); force white on the root <svg>.
    if "fill=" not in text.split(">", 1)[0]:
        text = re.sub(r"<svg\b", '<svg fill="#ffffff"', text, count=1)
    return text.encode("utf-8")


def survey(source: IconSource) -> list[str]:
    out_dir = os.path.join(STAGING_DIR, source.name)
    os.makedirs(out_dir, exist_ok=True)
    written = []
    print(f"Survey: {source.name}  (pinned {source.repo}@{source.tag})")
    print(f"License: {source.license_info}")
    for concept, icon_name in source.concepts.items():
        data = download_svg(source.svg_url(icon_name))
        if not data:
            print(f"  {concept:10s} -> {icon_name:16s} MISSING (no icon in this family at this pin)")
            continue
        raw_path = os.path.join(out_dir, f"{concept}__{icon_name}.svg")
        with open(raw_path, "wb") as f:
            f.write(data)
        wn_path = os.path.join(out_dir, f"{concept}__{icon_name}.white.svg")
        with open(wn_path, "wb") as f:
            f.write(white_normalize(data))
        written.extend([raw_path, wn_path])
        print(f"  {concept:10s} -> {icon_name:16s} OK (+ white-normalized copy)")
        time.sleep(0.2)
    # Record provenance next to the assets so the contact sheet / any reviewer can cite it.
    with open(os.path.join(out_dir, "SOURCE.txt"), "w", encoding="utf-8") as f:
        f.write(f"{source.license_info}\npinned: {source.repo}@{source.tag}\n")
    return written


def check_for_update(source: IconSource) -> int:
    """Advisory pin-rot check, same contract as download_emoji.py: 0 current, 2 newer
    exists, 3 could not determine. Never fails a build, never auto-bumps."""
    if not source.repo:
        print(f"{source.name}: no pinned repo/tag")
        return 0
    # A SHA pin (no dotted version) has no ordered "newer tag" to compare against.
    if not re.search(r"\d+\.\d+", source.tag):
        print(f"{source.name}: pinned to commit {source.tag[:12]} (no semantic tag to compare)")
        return 0
    url = f"https://api.github.com/repos/{source.repo}/tags"
    req = urllib.request.Request(url, headers={"User-Agent": "Decenza-IconSurvey/1.0",
                                               "Accept": "application/vnd.github+json"})
    try:
        with _urlopen_checked(req, 30) as resp:
            tags = [t.get("name", "") for t in json.load(resp)]
    except Exception as e:
        print(f"icons: could not check {source.repo} ({e})")
        return 3
    if not tags:
        return 3

    def parts(tag):
        return [int(x) for x in re.findall(r"\d+", tag)] or [0]

    newest = max(tags, key=parts)
    if parts(newest) > parts(source.tag):
        print(f"  {source.name}: pinned {source.tag}, newer {newest} exists ({source.repo})")
        return 2
    print(f"icons: {source.repo} {source.tag} is current (newest: {newest})")
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    flags = {a for a in sys.argv[1:] if a.startswith("-")}

    if not args or (args[0] not in SOURCES and args[0] != "all"):
        print(f"Usage: {sys.argv[0]} <source|all> [--check-updates]")
        print(f"  Sources: {', '.join(SOURCES)}")
        print(f"  Stages into: {STAGING_DIR}/<source>/  (never resources/icons/)")
        for name, src in SOURCES.items():
            print(f"    {name:18s} - {src.license_info}")
        sys.exit(1)

    chosen = list(SOURCES.values()) if args[0] == "all" else [SOURCES[args[0]]]

    if "--check-updates" in flags:
        rc = 0
        for s in chosen:
            rc = max(rc, check_for_update(s))
        sys.exit(rc)

    for s in chosen:
        survey(s)
        print()
    print(f"Staged under {STAGING_DIR}/ (git-ignored). "
          f"Adopting into resources/icons/ is a separate, owner-gated step.")


if __name__ == "__main__":
    main()
