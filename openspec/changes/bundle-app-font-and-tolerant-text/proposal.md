## Why

Text overlaps, clips, and overflows its containers for some users but not others, on both
Windows and Android (issue #1469). The root cause is that Decenza ships **no font of its own**
and sets **no application-wide font**, so every label renders in whatever face the OS supplies.
That face — and its glyph metrics — differs by platform, OEM, and OS version (e.g. an Android 12
tablet vs an Android 16 tablet render the "same" system sans at different widths), so a layout
that fits one device's font overruns another's at an identical UI scale. Uniform screen zoom
cannot fix it, because it scales text and its container together and never changes the
text-to-box ratio.

## What Changes

- Bundle a redistributable font (Roboto, SIL OFL 1.1 — Regular + Bold, ~0.5 MB) into the Qt
  resource system and register it at startup via `QFontDatabase::addApplicationFont`.
- Set it as the **application-wide default** (`QGuiApplication::setFont`) so all QML text inherits
  deterministic glyph metrics on every platform (Windows/macOS/Linux/Android/iOS), independent of
  the device's system font or a user's OEM "font style" choice. Individual QML elements can still
  override the family where needed (e.g. the existing monospace debug fields).
- Fix the container/label patterns that cannot tolerate font-metric variation, so text degrades
  gracefully for the scripts the bundled font does **not** cover (CJK, Arabic, Hebrew, Devanagari,
  Thai — which fall back to the system font) and for genuinely long strings:
  - Replace the **dead `elide` on `Text.RichText`** anti-pattern (Qt ignores `elide` on rich text,
    so those labels clip with no ellipsis — e.g. the Equipment card basket line) with wrapping or
    plain-text elide as appropriate per site.
  - Make fixed-size popups/dialogs that assume a single font's metrics content-sized and/or
    scrollable so wider text cannot overflow the screen (e.g. the Shot History search-syntax help).
  - **Standardize on `Text.StyledText`**: convert all read-only `Text.RichText` labels to StyledText
    (elide works, lighter, no `QTextDocument` per label) and reserve RichText for genuine document
    features (tables/CSS) — of which the codebase has none. Update the emoji helper to center inline
    emoji via the StyledText-honored `align="middle"`, and codify the convention in
    `QML_GOTCHAS.md`/`CLAUDE.md` so the dead-elide class cannot recur.
- Remove the diagnostic `[#582 diag]`-style geometry logging only where it is superseded; keep any
  logging still needed. (No behavioural dependency; investigation is complete.)

Not in scope: bundling multi-script (Noto CJK/Arabic/…) fonts — tens of MB, deferred; those
scripts rely on system fallback, which the container-tolerance work makes safe.

## Capabilities

### New Capabilities
- `bundled-app-font`: Decenza ships and registers its own default UI font, giving deterministic
  glyph metrics across all platforms and OS/OEM font variations, while allowing per-element family
  overrides.
- `text-overflow-tolerance`: user-visible text labels, popups, and dialogs tolerate any font
  metrics and long/translated strings without clipping mid-glyph or overflowing the screen —
  via correct eliding, wrapping, and content-driven sizing.

### Modified Capabilities
<!-- No existing spec's requirements change; this is additive. -->

## Impact

- **New dependency (bundled asset):** Roboto font files under `resources/fonts/` (SIL OFL 1.1),
  added to the Qt resource list in `CMakeLists.txt`.
- **Startup:** `src/main.cpp` — register the font and call `QGuiApplication::setFont` before the
  QML engine loads.
- **Theme:** `qml/Theme.qml` — font roles inherit the app default family (no per-role family needed
  beyond the existing `monoFontFamily`).
- **QML labels/dialogs:** targeted edits across `qml/components/` and `qml/pages/` — starting with
  `EquipmentSummary.qml` (basket line) and `ShotHistoryPage.qml` (search-syntax dialog); full set
  enumerated by grepping the `Text.RichText` + `elide` pattern and fixed-size text popups.
- **Cross-platform:** every platform's rendering shifts to the bundled face; visually matches the
  current Android look (Roboto), so no intended change in appearance for existing users.
- **Licensing:** ship the Roboto `OFL.txt` (SIL OFL 1.1) alongside the font files.
- **No API, BLE, DB, or settings-schema changes.**
