## Why

We bundled Roboto in #1487 so text metrics would stop depending on the host system's fonts, which
was meant to fix a reporter's overflowing Shot History search-syntax dialog (#1469). Five days
later the same reporter's dialog still overflows, and his UI now renders "Profile" as "Proule" and
"filter" as "ulter" (#1537) — the `fi` ligature replaced by an unrelated glyph, not dropped. The
`CurveTextRendering` fix in #1538 did not hold.

The evidence points at font *resolution*, not layout: the same build renders correctly on macOS,
Android, and a second Windows box with stock font sizes. A wrong-but-cleanly-formed glyph is the
signature of a glyph index resolved against one font and drawn from another — which is what happens
when two different fonts claim the family name `Roboto`, a name commonly already installed on
Windows by Chrome and Adobe. Wider advance widths from that same foreign font are enough to push
the dialog's grid past its fixed width budget.

Two gaps let this ship and sit broken. The `bundled-app-font` capability requires metrics that
"MUST NOT vary with the host OS", but nothing verifies the bundled font actually won the name
lookup — the startup log records only that `addApplicationFont()` returned an id, which is true and
useless. And `text-overflow-tolerance` already contains a scenario named "Search-syntax help dialog
stays on screen" forbidding clipped columns, so the dialog is a live regression against a spec we
already wrote.

## What Changes

- **Bundled font family renamed to `Decenza Sans`.** A name no other font on the host can claim
  makes the collision structurally impossible instead of depending on winning an ambiguous lookup.
- **Startup font diagnostics.** Log system families matching the bundled font *before* registration
  (the collision detector), the resolved family and `exactMatch` *after*, and a probe advance width
  that can be diffed between machines to localise a metrics problem without screenshots.
- **Startup font-size override logging.** Log only the roles the user has changed from default.
  When every size is stock, log nothing — the common case must not add noise.
- **Single source of truth for font-size defaults**, currently hardcoded twice and free to drift.
- **Fonts-only reset** in the web theme editor. `resetFontSizesToDefault()` already exists but is
  reachable only through the combined reset, which also wipes the user's colours.
- **Reset confirmation text** names both colours and font sizes, which it currently does not.
- **Search-syntax dialog layout guards** so the grid can shrink rather than clip, and the popup
  scrolls again when content exceeds its bounds.
- **`/themes` → `/theme`** in the in-app hint, which currently sends users to a 404.

## Capabilities

### New Capabilities
- `font-diagnostics`: startup logging that makes font resolution and user font-size overrides
  observable in a debug log, so a font problem on a remote machine is diagnosable from the log
  alone rather than by screenshot forensics.
- `theme-font-size-defaults`: a single authoritative set of font-size defaults, plus a fonts-only
  reset and accurate destructive-action wording in the web theme editor.

### Modified Capabilities
- `bundled-app-font`: the family name must be unique enough that no host-installed font can claim
  it. The existing "Deterministic metrics across platforms and OS versions" requirement is
  unsatisfiable while the bundled family shares a name with a commonly-installed system font, and
  registration success is not evidence the bundled font is the one being used.

## Impact

- `src/main.cpp` — font registration block; family name, diagnostics, probe metric.
- `resources/fonts/*.ttf` — internal family name rewritten to `Decenza Sans` (OFL permits renaming;
  `Decenza Sans` does not infringe the Reserved Font Name). `OFL.txt` stays as-is.
- `qml/Theme.qml` — font-size defaults move to the shared source; explicit family reference.
- `src/core/settings_theme.{h,cpp}` — canonical defaults, override query for logging.
- `src/network/shotserver_theme.cpp` — defaults sourced centrally; new fonts-only reset endpoint.
- `src/network/webtemplates/theme_{html,js}.h` — reset button, corrected confirm text.
- `qml/pages/ShotHistoryPage.qml` — search-syntax dialog sizing and scroll.
- `qml/pages/settings/SettingsThemesTab.qml` — `/themes` → `/theme`.
- Wiki manual — the fonts-only reset is user-visible and needs an entry.
- Closes #1537; reopens and properly closes #1469.
- No BLE, profile, or shot-data surfaces touched. No migration: existing `customFontSizes` values
  are role-keyed and unaffected by the family rename.
