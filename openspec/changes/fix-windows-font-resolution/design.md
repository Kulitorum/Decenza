## Context

#1487 bundled Roboto and registered it as the application font so text metrics would stop depending
on host fonts. On one reporter's Windows box the fix did not take: his search-syntax dialog still
overflows (#1469) and `fi` renders as an unrelated glyph — "Profile" → "Proule", "filter" → "ulter"
(#1537). The same build is correct on macOS, Android, and a second Windows box whose font sizes are
all stock.

A dropped ligature and a *substituted* one are different failures. A fallback font renders "filter"
correctly, merely wider; only a glyph index resolved against one font and drawn from another
produces a wrong-but-cleanly-formed glyph. That points at two fonts claiming the family name
`Roboto` — a name Chrome, Adobe, and various installers commonly drop into `C:\Windows\Fonts`.
The same foreign font's wider advance widths are enough to push the dialog's grid past its fixed
width budget, which explains both symptoms from one cause.

The reporter's log confirms the environment is otherwise unremarkable (devicePixelRatio 1, 96 DPI,
`[Font] Bundled application font set: "Roboto"` with no failures) — which is precisely the problem:
that log line records that `addApplicationFont()` returned an id, not which font is rendering.

We cannot reproduce this locally and cannot inspect the reporter's machine. The design therefore
has to both fix the likely cause and make the outcome verifiable from a submitted log.

## Goals / Non-Goals

**Goals:**
- Make it impossible for a host-installed font to be selected in place of the bundled one.
- Make font resolution and user font-size overrides readable from a debug log, so the next report
  is diagnosable without screenshot forensics.
- Give the search-syntax dialog the ability to fit its content instead of clipping it.
- Remove the duplicated font-size default tables before they drift.

**Non-Goals:**
- Reproducing the reporter's fault locally. We are fixing a mechanism the evidence implicates, and
  shipping the instrumentation that confirms or refutes it on his next log.
- Changing the visual design. `Decenza Sans` is byte-identical to the current Roboto apart from the
  name table; rendering should be unchanged everywhere it is already correct.
- Auditing every dialog for overflow. Only the search-syntax dialog is in scope; the general rule
  already exists in `text-overflow-tolerance`.
- Adding a user-facing font *family* picker.

## Decisions

### Rename in the font files, committed pre-renamed

Rewrite name IDs 1 (Family), 4 (Full name), 6 (PostScript name) and 16 (Typographic Family, where
present) to `Decenza Sans`, leaving ID 2 (Subfamily) intact so the four weights still map to
Light/Regular/Medium/Bold. Commit the renamed `.ttf` files.

*Alternative — rename at build time* via a fonttools step: rejected. It adds a Python dependency to
every platform build for a one-time transformation, and makes the shipped artifact something no
reviewer can inspect directly.

*Alternative — `QFont::insertSubstitution`*: rejected. Substitution maps one family name to another;
it does not disambiguate two fonts that share a name, which is the actual failure.

`OFL.txt` line 1 carries no Reserved Font Name clause, so renaming is permitted outright (had there
been one, renaming would have been *required* for a modified font). `OFL.txt` stays as shipped.

### Effective sizes as a bindable property, not a method

Font-size defaults move to a single declaration in `SettingsTheme`, exposed as a
`Q_PROPERTY QVariantMap effectiveFontSizes` that merges defaults with overrides and notifies on
`customFontSizesChanged`.

QML must read a *property* for a binding to re-evaluate — property reads inside `Q_INVOKABLE`
methods are invisible to the binding engine, the gotcha CLAUDE.md documents against `Theme.qml`'s
temperature helpers. So `scaled(Settings.theme.effectiveFontSizes.labelSize)` is correct where a
`fontSizeFor("labelSize")` call would silently fail to update when the user drags a slider.

The web editor and startup override logging read the same declaration, satisfying the
single-source-of-truth requirement without a second table.

### Set the family explicitly on theme roles, and keep `setFont`

Keep `QGuiApplication::setFont` so unstyled `Text` inherits the bundled family, and additionally
give `Theme.qml`'s eight font roles an explicit `family: Theme.fontFamily`.

`Theme.fontFamily` resolves to the registered family, or empty string if registration failed —
`Qt.font({})` with an empty family falls back to the application default, preserving today's
graceful-degradation behaviour. Being explicit removes reliance on inheritance surviving Quick
Controls' style defaults, and makes the family assertable in a test.

Per-glyph fallback for scripts the font does not cover (CJK, Arabic, …) is unaffected — Qt falls
back per missing glyph regardless of an explicit family.

### Probe metric fixed, not user-relative

The probe logs `horizontalAdvance("Extraction yield (%)")` at a hardcoded 14px, deliberately not at
the user's effective `labelSize`. A probe that moved with user settings would not be comparable
between two machines, which is the entire point of logging it. The string is one of the actual
grid cells that overflows.

### Log overrides only when they exist

Nothing is logged when every role is stock — the overwhelmingly common case, and noise in a log
people read by eye. An override is defined by *value differing from default*, not by presence of a
stored key: dragging a slider and returning it writes an entry equal to the default, which is not
an override and must not be reported as one.

Ordering constraint: the existing `[Font]` block runs before `Settings` is constructed (0.023s vs
0.025s in the reporter's log), so override logging cannot live there. It goes immediately after
Settings initialisation.

### Layout tolerance is the primary fix, not a secondary cleanup

Parsing the shipped `Roboto-Regular.ttf` cmap directly: the bundled font covers Latin (including
Extended), Greek, Cyrillic and the `fi` ligature, and does **not** cover Hebrew, Arabic, Devanagari,
Thai, CJK, Hiragana or Hangul. `TranslationManager` explicitly handles RTL for `ar`/`he`/`fa`/`ur`,
so those users exist.

For every non-Latin locale, therefore, essentially all UI text renders in a platform fallback font
and the metric-determinism guarantee simply does not apply. The rename fixes nothing for them; the
layout guards are their *only* protection against a clipped dialog. That inverts the priority these
two pieces of work looked like they had — the rename has the narrower reach, not the wider one.

A practical consequence: a CJK or Arabic user can report exactly this overflow with entirely clean
font resolution, and the new diagnostics will correctly show no collision. That is not a diagnostic
failure and should not be chased as one.

### Symbol coverage: fall back, do not bundle more

Seven glyphs used in QML UI string literals fall outside the bundled font — `→` (21 uses across 13
files), `↗`, `←`, `↕`, `◀`, `▶`, `⧉` — about 28 occurrences total. Adding a symbols font to cover
them would roughly increase the bundled font payload by half on every platform, for glyphs that
already render correctly through fallback.

The CJK/Arabic/Hebrew/Devanagari/Thai characters that also fall outside coverage are all native
language names in `AddLanguagePage.qml`. Those must fall back — a Latin font cannot render a
language picker's own names — and are correct as they stand.

*Alternative — adopt a symbols font as an icon source*, replacing hand-authored SVGs: rejected on
measurement. Of 68 icons in `resources/icons/`, roughly 18 have solid monochrome Unicode
equivalents and 6 more are marginal; the remaining ~44 (the DE1, Niche grinder, portafilter, taste
axes, body weights, six battery states, Bluetooth/WiFi/USB) have no Unicode equivalent at all. A
font would replace the cheapest icons to source and none of the expensive ones. Notably `edit` and
`list` are `✎` and `☰` — the two glyphs CLAUDE.md explicitly prohibits — which suggests this
codebase already ran the experiment and reverted it.

What does change: CLAUDE.md lists `→` among glyphs safe to use as literals. The other four on that
list (`°` `·` `—` `×`) are genuinely covered; `→` is not, and it appears in shipped UI strings such
as the shot history's `18.0g → 41.6g`. The guidance is corrected rather than the font extended.

### Dialog fits by shrinking, not by growing

Give the grid's description and example cells `Layout.fillWidth` with `elide: Text.ElideRight` so
the grid has a legal narrow layout, rather than only widening the dialog. A `GridLayout` whose cells
have no elide has an unshrinkable implicit width; once that exceeds the dialog, the content column
outgrows the viewport and `clip: true` silently eats the right-hand column — which is what the
reporter sees, and why his wrapping intro line renders unwrapped and cut mid-word.

The reporter also notes the popup no longer scrolls, so the `ScrollView` viewport/content width
relationship is implicated in the same mechanism and needs verifying against real content during
implementation rather than being assumed fixed by the elide change alone.

## Risks / Trade-offs

- **The rename is not the actual cause** (his font sizes are inflated, or something else entirely)
  → The instrumentation ships in the same build, so his next log distinguishes the cases regardless
  of whether the fix worked. This is why both land together rather than sequentially.
- **A botched name-table rewrite ships a broken font** → Verify post-rename by asserting the
  registered family in a unit test, and confirm all four weights still resolve to distinct styles.
  A failed registration already degrades gracefully to the platform font.
- **Explicit family regresses a script relying on inheritance** → Per-glyph fallback is unchanged;
  covered by the existing `bundled-app-font` non-covered-scripts requirement. Worth a CJK spot check.
- **`Decenza Sans` collides with something anyway** → Vanishingly unlikely, and now detectable: the
  pre-registration collision log names any competing family.
- **Elide hides content the user needs** → The example column is the least load-bearing of the
  three, and elide is strictly better than the current silent clipping. The dialog stays scrollable.
- **Cannot verify the fix before shipping.** The reporter is the only known reproducer. Accepted:
  the change is safe on machines that already work, and the instrumentation is the verification.

## Migration Plan

No data migration. `customFontSizes` is keyed by role (`labelSize`, …), not by family, so stored
overrides survive the rename untouched. Users who saved theme colours are unaffected.

Rollback is reverting the branch; the font files are the only binary artifact and carry no state.

## Open Questions

- Does the reporter's machine in fact have a system-installed Roboto? Unconfirmed at authoring time.
  Jeff has taken an action with the reporter whose content is not yet known here; if it produced a
  font list or a Font Sizes screenshot, it may settle the cause before implementation starts and
  should be folded in.
- Should the fonts-only reset also appear in-app? The app has no font-size UI at all today, so the
  editor is the only surface that needs it. Adding in-app controls is deliberately not proposed —
  it would be a new settings surface, against the project's prefer-fewer-settings stance.
- A bundled icon font was considered and rejected here on coverage grounds (see Decisions), but the
  underlying cost is real: 20 of the 68 icons were added in the last 90 days. If it is revisited, it
  should be its own proposal, and it should wait for this change's evidence — a uniquely-named
  bundled font resolving cleanly on a machine that currently mis-resolves one is exactly the
  precondition an icon font would depend on. A wrong glyph in text is recoverable by the reader; a
  wrong glyph on a button is not.
