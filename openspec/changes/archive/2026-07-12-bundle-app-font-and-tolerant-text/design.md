## Context

Issue #1469 reports text overlapping, clipping, and overflowing on Windows and Android for some
users but not others. Investigation established:

- Decenza sets **no application font** and bundles **none** (`grep` finds no
  `QGuiApplication::setFont`, no `addApplicationFont`, no `.ttf`/`.otf`). Every `Theme.*Font` role
  is `Qt.font({ pixelSize: … })` with no `family`, so text renders in the OS's default face.
- The app's master UI scale is `min(width/960, height/600)` × multipliers (`qml/main.qml`
  `updateScale()`), computed in **logical pixels**. Two reporting Android tablets both resolve to
  an identical `1.333` scale and identical `800`-px logical height — so scale, DPR, and resolution
  are ruled out.
- What differs is the **system font**: a SM-T503 on Android 12 vs a SM-X210 on Android 16 render
  the "same" system sans at different glyph widths. Same `pixelSize`, wider glyphs → text that fits
  its box on one device overruns it on the other. Uniform zoom cannot fix this because it scales
  text and box together (ratio-invariant).
- A separate, compounding bug: several labels set `elide` on `textFormat: Text.RichText`. Qt
  **ignores `elide` on rich text**, so those labels hard-clip with no ellipsis (e.g.
  `qml/components/EquipmentSummary.qml` basket line uses RichText for emoji-as-`<img>`).

Constraints: cross-platform (Windows/macOS/Linux/Android/iOS); Qt 6.11.1; new QML files and
resources must be registered in `CMakeLists.txt`; app-size sensitivity on mobile.

## Goals / Non-Goals

**Goals:**
- Deterministic glyph metrics for the Latin/Cyrillic/Greek UI on every platform and OS/OEM/version,
  by bundling and defaulting to our own font.
- Preserve the current appearance (bundle Roboto, the face Android already uses).
- Make labels/dialogs tolerant of any font metrics and long/translated strings, so the scripts the
  bundled font does not cover (CJK, Arabic, Hebrew, Devanagari, Thai — system fallback) are also
  safe from clipping.
- Fix the concrete reported sites (Equipment basket line, Shot History search-syntax dialog).

**Non-Goals:**
- Bundling multi-script coverage (Noto CJK/Arabic/…): tens of MB, deferred. Those scripts rely on
  system fallback made safe by the tolerance work.
- Runtime font-metric measurement / auto-shrink ("compensation"): rejected — it shrinks text
  (accessibility cost), is approximate, and treats the symptom. Bundling removes the variance
  outright.
- Any new user-facing setting. (Consistent with "prefer smarter defaults over new settings".)
- Changing the UI scale math, DPR handling, or per-page multipliers.

## Decisions

### D1: Bundle Roboto and set it as the application default (not reference a system font)

Register `resources/fonts/Roboto-Regular.ttf` + `Roboto-Bold.ttf` via
`QFontDatabase::addApplicationFont` in `src/main.cpp` before `QGuiApplication` font assignment and
before the QML engine loads, then `app.setFont(QFont("Roboto", …))`.

- **Why not reference the device's Roboto / `"sans-serif"` by name?** The OS's copy is the moving
  target — its metrics drift by OS version and OEM, which is exactly the reported failure. Only a
  version-pinned copy we carry is deterministic. Explicitly naming `sans-serif` would at most
  neutralize a user's OEM "font style" override; it does not fix OS-version drift.
- **Why Roboto?** It is the face Android already uses, so bundling it keeps the current look
  (no intended visual change for existing users), and it is SIL OFL 1.1, freely redistributable.
- **Why app-wide `setFont` rather than editing every `Theme.*Font`?** One insertion point; QML text
  inherits it; the two existing `monoFontFamily` overrides continue to win locally. Avoids touching
  every font role and avoids the recompile-blast rules around `Theme`.
- Keep the pixel size on `setFont` neutral; `Theme.scaled()` continues to drive sizes. `setFont`'s
  role here is family, not size.

### D2: Preserve override points and mono fields

`Theme.monoFontFamily` (used by two debug/mono fields in `main.qml`) stays as an explicit override.
Confirm the bundled default does not regress those. No change to `Theme.scaled()` or the font-role
pixel sizes.

### D3: Container tolerance — classify each site, don't blanket-wrap

Enumerate offending labels by grepping the `textFormat: Text.RichText` (or `StyledText`) + `elide`
pattern, plus fixed-size text popups. For each site decide by its container:

| Container height | Fix |
|---|---|
| Content-driven (Flow/Column/`implicitHeight`) | switch dead `elide` → `wrapMode` |
| Plain text, single line wanted | `Text.PlainText` + `elide: Text.ElideRight` |
| Fixed height / grid cell that cannot grow | elide, or enlarge the container |
| Fixed-size popup/dialog | content-sized within a screen-bounded max + `ScrollView` |

Confirmed-safe example: the Equipment card is a content-sized `Rectangle`
(`implicitHeight: cardColumn.implicitHeight + …`) inside a `Flow` that pins only width — so the
basket line can wrap and the card grows (worst case: ragged row heights, no overflow).

### D3b: Standardize on StyledText, retire explicit RichText

The dead-elide bug is a symptom of using `Text.RichText` where `Text.StyledText` suffices. Rather
than only fixing the elide sites, **eliminate explicit `Text.RichText` from read-only labels
entirely** and make StyledText the default. Rationale: `elide` works with StyledText, StyledText is
lighter (no `QTextDocument` per label), and the codebase uses no RichText-only features (grep of
label content shows only `<b>/<i>/<font>/<a>/<img>/<br>` — no tables/CSS). This prevents the whole
bug class from recurring, not just the current instances.

- The fix at each dead-elide site is therefore `RichText → StyledText` (not `→ wrap`), which
  **preserves the original single-line elided layout** and avoids any height change — important
  because `EquipmentSummary` is shared by `EquipmentCard`, `ShotDetailPage`, and
  `PostShotReviewPage`, where wrapping would have altered shot-page heights.
- Inline emoji: `Theme.replaceEmojiWithImg` switches from CSS `style="vertical-align:middle"` (which
  StyledText ignores) to the HTML `align="middle"` attribute (which StyledText honors, confirmed in
  `qquickstyledtext.cpp`); the `style=` is kept so any RichText caller still centers.
- `TextEdit.RichText` (editable rich-text fields) is out of scope — elide does not apply there.
- Codified as a convention in `QML_GOTCHAS.md` + `CLAUDE.md` so new labels default to StyledText.

### D4: Retire superseded diagnostics

The `[#582 diag]` geometry logging and any probe scaffolding added during investigation are removed
where no longer needed; keep logging that remains useful. Purely additive/removable; no behavior
depends on it.

## Risks / Trade-offs

- **[Bundled font subtly shifts rendering on desktop/iOS where the prior system font was not
  Roboto]** → Acceptable and intended (determinism is the goal); Roboto matches the dominant
  Android look. Verify key screens on macOS/Windows and a physical Android device before release.
- **[Bundled Latin font does not cover CJK/Arabic/etc.; those still vary per device]** → The
  `text-overflow-tolerance` work makes fallback-font text safe from clipping; full multi-script
  bundling is an explicit non-goal for now.
- **[App-size growth]** → ~0.5 MB for Regular + Bold; negligible relative to app size. If a heavier
  weight set is ever needed, add lazily.
- **[Blanket-wrapping could overflow a fixed-height container]** → Mitigated by D3's per-site
  classification (wrap only where height is content-driven; elide otherwise).
- **[Missed sites]** → The grep-driven enumeration is the source of truth; the two reported sites
  are explicit acceptance checks, and the requirement is font-independent so fallback scripts are
  covered by construction.
- **[Verification needs the reporter's device]** → Root-cause fixes are deterministic and testable
  locally (measure identical metrics; confirm no dead-elide remains); final confirmation is one
  build handed to the #1469 reporter, who has the reproducing hardware.

## Migration Plan

1. Add font assets + license under `resources/fonts/`; register in `CMakeLists.txt` resources.
2. Wire `addApplicationFont` + `setFont` in `src/main.cpp`; verify startup logs the registered
   family and that mono overrides still resolve.
3. Land the two reported-site fixes (Equipment basket line, search-syntax dialog) + the full
   grep-enumerated set, classified per D3.
4. Remove superseded diagnostics.
5. Build locally (Qt Creator) for a compile/visual check on desktop; produce a GitHub Android CI
   build for a physical-device check (platform `#ifdef`/font paths differ from macOS).
6. Hand the Android build to the #1469 reporter for confirmation; close on confirmation.

Rollback: revert the branch; the font is an additive asset and the default-font call is a single
site, so removal is clean.

## Open Questions

- Bundle Roboto **Light** (300)? It is used only in two decorative screensaver spots
  (`StrangeAttractorScreensaver.qml`, `ScreensaverPage.qml`); skipping it falls back to Regular
  there. Recommendation: include it (~170 KB) so nothing is substituted. (Resolved for
  Regular/Medium/Bold: all three are required — `Font.Medium` is used on the live Espresso screen,
  Bold + Regular are the Theme roles.)
- Any screen deliberately relying on the platform font's look (e.g. native-feeling iOS text)? If
  so, exclude via an explicit family override rather than opting out of the app default.
