## Context

Both defects are instances of "the UI does not learn that a value it depends on changed", but they
need different mechanisms, so they are designed separately below.

**Translation staleness.** `TranslationManager::translate(key, fallback)` is a `Q_INVOKABLE`. QML
bindings track dependencies by recording which *properties* were read during evaluation; a method
call records nothing. A binding of the form

```qml
text: TranslationManager.translate("settings.title", "Settings")
```

therefore evaluates once, at construction, and never again — no matter how many times
`translationsChanged` fires. `Tr.qml` already knows this and works around it by touching
`TranslationManager.translationVersion` (a real `Q_PROPERTY`, `NOTIFY translationsChanged`) before
calling `translate()`. That single read is what makes `Tr` reactive.

Counted on this branch: **3,248** bare `translate()` call sites in `qml/`. All of them are stale
after a language change. `CLAUDE.md` recommends exactly this bare form for property bindings, so
the population grows with every feature.

Constraint: **11 C++ call sites** (`updatechecker`, `blemanager`, `aimanager`, `aiconversation`,
`livesteamcoach`, `visualizerimporter`, `visualizeruploader`, `databasebackupmanager`,
`aiprovider`, `main.cpp`) call `translate()` directly and must keep compiling and working.

**Emoji crash.** Apple Color Emoji glyphs are colour bitmaps. When one reaches Qt's scene graph,
Qt uses `QSGTextMaskMaterial`, whose glyph upload path calls into ImageIO to decode the embedded
PNG on `QSGRenderThread`, and that decode faults. The app's existing defence is to never let a
colour glyph reach the shaper: `Theme.replaceEmojiWithImg()` rewrites emoji codepoints to `<img>`
tags pointing at bundled Twemoji SVGs. 22 call sites use it; the release-notes field did not, and
crashed.

`QQuickWindow::CurveTextRendering` was previously believed to remove this path. It does not:
curves cannot represent colour bitmaps, so Qt falls back to the texture-mask path precisely for
colour glyphs. The crash on 2026-07-18 had Curve rendering active.

## Goals / Non-Goals

**Goals:**
- A language switch updates all visible text with no restart.
- The correct i18n call pattern is enforced by something that fails, not by documentation.
- No externally-sourced string can reach a markup-aware renderer unescaped.
- Release notes containing emoji do not crash the app.
- Preserve the 11 C++ `translate()` callers unchanged.

**Non-Goals:**
- Migrating to Qt's native `qsTr()` / `.ts` translation system. That is a far larger change and
  would discard the community/AI translation pipeline built around `TranslationManager`.
- Retranslating text held in C++-side model data (e.g. strings already baked into a `QVariantList`
  a view is showing). Those refresh when their model refreshes; widening that is out of scope.
- Changing which emoji *set* is used (Twemoji vs OpenMoji vs Noto). `scripts/download_emoji.py`
  already supports switching; that stays as-is.
- Rendering emoji in contexts that take plain text only. `stripEmoji()` remains correct there.

## Decisions

### D1: Make `translate` a property that returns a callable

Reading a property registers a dependency; calling a method does not. So expose `translate` as a
`Q_PROPERTY` whose value is a callable JS function, with `NOTIFY translationsChanged`:

```cpp
Q_PROPERTY(QJSValue translate READ translateFn NOTIFY translationsChanged)
```

At a call site, `TranslationManager.translate("k", "f")` first *reads* the `translate` property —
registering a dependency on `translationsChanged` — and then invokes the returned function. The
call-site syntax is byte-for-byte identical to today's, so **all 3,248 sites are fixed without
being edited**, and any new code written in the natural way is correct by default.

The C++ method is preserved under a distinct name (`translateString()`), with the 11 C++ callers
updated to it; the QML-facing property delegates to it.

*Alternatives considered:*
- **Codemod all 3,248 sites** to touch `translationVersion` first. Mechanical and verifiable, but
  it churns 3,248 lines, makes every future call site a chance to reintroduce the bug, and leaves
  the codebase teaching a workaround.
- **Re-create the UI on language change.** Simple and certainly correct, but it discards navigation
  state and in-progress input, and is a heavy response to a text refresh.
- **Expose translations through a `QQmlPropertyMap`** (`TranslationManager.s["key"]`). Property
  reads track correctly, but there is nowhere to put the English fallback, which is the safety net
  when a key is missing from the registry.

**D1 CONFIRMED by experiment** (2026-07-18, Qt 6.11.1, macOS). `tests/tst_translationreactivity.cpp`
demonstrates it against a real `QQmlEngine`:

| Test | Result |
|---|---|
| `invokableInBindingIsFrozen` | Binding over a `Q_INVOKABLE` does **not** re-run after notify — the bug, reproduced |
| `propertyReturningCallableReEvaluates` | Binding over the `QJSValue` property **does** re-run, with identical call syntax; switching away and back leaves no residue |
| `callableIsCachedAcrossEvaluations` | Same function object across 20 notify cycles (`strictlyEquals`); no per-evaluation wrapper churn |
| `callableWorksOutsideBindings` | Works in `Component.onCompleted`, i.e. with no binding context |
| `asyncResolutionPatternReRenders` | The same mechanism covers D4's async emoji case |

The first row is the load-bearing one: it is a negative control proving the suite can distinguish
the two mechanisms rather than passing regardless. Without it, the other four would be worthless.

Two implementation details the spike settled, both of which would have been silent bugs:
- The callable **must** be cached. Rebuilding the `QJSValue` per evaluation would allocate on every
  re-render.
- Ownership must be pinned with `QJSEngine::setObjectOwnership(this, CppOwnership)` before
  `newQObject()`, or the engine may collect the object out from under the callable.

Not separately tested: reading the property without calling it. That path returns the cached
function object and does nothing else, so it is harmless by construction.

The codemod alternative is therefore **not needed** and is retained here only as a record of what
was considered.

### D2: Guard the pattern with a test, not a convention

Whichever mechanism D1 lands on, add a test that fails when a QML file uses a stale-by-construction
translation pattern. Under D1 this asserts that `translate` remains a notifying property (a future
refactor back to `Q_INVOKABLE` silently refreezes 3,248 bindings and would otherwise be caught by
nobody); under the codemod fallback it asserts no bare `translate(` survives in `qml/`.

This follows the precedent set in #1549, where a test asserts the QML font-role names match the C++
table — a seam that is invisible at compile time and silently wrong at runtime.

### D3: `replaceEmojiWithImg()` escapes by default

The function's output is fed to `RichText`/`StyledText` renderers. Its inputs include bean names
from Bean Base, AI replies, community screensaver author names, and GitHub release notes. Default
to escaping, and require callers that genuinely supply markup to opt in with `allowMarkup: true`.

Three call sites opt in: `ExpandableTextArea` (its `formatTextWithLinks()` already escapes and then
emits `<a>` tags), `CustomEditorPopup`, and `CustomItem` (both render user-authored widget
templates that may deliberately contain formatting).

Default-safe is chosen over default-permissive because the failure modes are asymmetric: a missing
opt-in shows raw tags on screen and gets reported immediately, whereas a missing escape is
invisible until it is exploited or until a bean name containing `<` mangles a page.

### D4: Ship the complete emoji set; there is no runtime resolution

Today `emojiToImage()` and `replaceEmojiWithImg()` are pure string functions: codepoints in,
`qrc:/emoji/<hex>.svg` out, with **no check that the file exists**. 744 assets ship. Anything
outside that set becomes an image reference nothing can resolve — the emoji is neither drawn nor
removed. This was not a designed fallback; it is an unhandled case.

**Bundle all 4,009 upstream assets and keep the functions pure.** Resolution is
bundled-or-strip, decided locally and synchronously.

*This reverses an earlier decision in this document, on a measurement I should have taken first.*
The original design specified bundled → disk cache → CDN fetch → strip, and rejected shipping the
full set with the parenthetical "~3,700 SVGs, roughly 15 MB… rejected on install size for a mobile
app." That figure was estimated, never measured, and it was the sole argument against. Measured:

| | Files | Raw | Compressed (3.06x, measured on the existing set) |
|---|---|---|---|
| Bundled today | 744 | 0.99 MB | 0.32 MB |
| Full twemoji v17.0.3 | 4,009 | 9.65 MB | ~3.15 MB |

**+2.8 MB** against a 137 MB application bundle. The cost that justified the entire runtime
architecture is about 2%.

What shipping everything deletes, rather than merely simplifies:

- The CDN fetch, and the new outbound network traffic the proposal had to disclose.
- The disk cache, the negative cache, and the cache-eviction question.
- **The asynchronous-resolution problem entirely.** The previous D4 argued the resolver had to
  expose a notifying property, because a function whose value arrives later is the same trap as
  the 3,248 stale `translate()` bindings — and that fixing one while introducing the other would
  be absurd. With a local synchronous lookup the hazard does not exist to be guarded against.
  (The spike's `asyncResolutionPatternReRenders` case is now unused by this design; it is kept
  because the mechanism it proves is still what makes `translate` reactive.)
- Offline behaviour as a design concern, and the app-authored vs uncontrolled boundary that
  existed only to decide what was allowed to hit the network.

What remains: an **existence check** before emitting an image reference. Even at 4,009 assets a
newer Unicode revision or an odd sequence can miss, and emitting an unresolvable path is the
broken-image bug above. "Bundled or strip" is a set lookup, not a resolver.

*Alternatives considered:*
- **CDN with disk cache** — the original design. Rejected once measured: it buys a ~2.8 MB install
  saving in exchange for a network dependency, two caches, and a class of stale binding.
- **Let QML `Image` load a CDN URL directly.** Rejected: inside `<img>` in `StyledText`, Qt's
  rich-text engine resolves resources through a document resource handler rather than the network
  stack, so remote images are not reliably fetched at all.

### D5: Nothing the application displays depends on the network

**No part of the app depends on the network to render text.** This was previously a boundary to
police — app-authored content bundled, uncontrolled content allowed to fetch — enforced by a build
step and a test. Shipping the full set makes the boundary unnecessary: there is no fetch path for
anything to fall down, so there is nothing to enforce.

Emoji rendering is not an enhancement that may degrade. A colour glyph reaching the platform text
renderer crashes the app on macOS, so resolution must be unconditional rather than best-effort.

The pinned upstream version is what keeps this reproducible: assets are committed, and a clean
checkout with no network builds a complete application. See the source note below.

**Upstream source settled (verified 2026-07-18 by fetching, not by assumption).** This is now a
BUILD-TIME source only — nothing fetches at runtime:

```
https://cdn.jsdelivr.net/gh/jdecked/twemoji@17.0.3/assets/svg/<hex>[-<hex>...].svg
```

`scripts/download_emoji.py` pins `twitter/twemoji@14.0.2`, which is where the bundled 744 came
from. Upstream status, checked against the GitHub API rather than assumed (an earlier draft of this
document claimed `twitter/twemoji` was archived — **it is not**):

| | `twitter/twemoji` | `jdecked/twemoji` |
|---|---|---|
| Latest release | v14.0.2, **March 2022** | v17.0.3, **June 2026** |
| Since then | a README edit; a v14.0.3 released and reverted | v16.0.1, v17.0.0–17.0.3 |

Not archived, but dormant where it matters: no emoji release in four years. The functional
consequence is decisive — `twitter/twemoji@14.0.2` **404s on `1fae8`** (🫨, Unicode 15). Staying on
it would mean shipping a "complete" set that is three Unicode revisions stale.
`jdecked/twemoji` is the maintained continuation and serves it.

The fork is a continuation rather than a redesign. `2615.svg` is **byte-identical** (SHA-256
`8b8afd8f…31ff`) across `twitter@14.0.2`, `jdecked@15.1.0`, `16.0.1` and `17.0.3`.

**Corrected after actually regenerating:** an earlier version of this paragraph generalised that
one sample to the whole set and claimed re-generating "does not restyle the 744 assets already
shipping". It does, slightly. Measured against the committed set:

- 736 of 744 byte-identical
- **8 changed (1.1%)**: 🌁 🍉 🏥 🔒 🔓 🚑 🤡 🥺
- 3,265 added, 0 deleted

Most of the eight are path optimisation from upstream's "optimize v17.0 SVGs" work — arcs
replacing cubic béziers, e.g. 🥺 drops 2,277 → 1,428 bytes with identical shape. At least one is a
genuine redraw: 🔒 previously spanned y=3–36 within the 36-unit viewBox and now spans y=0–36, so
the lock is drawn slightly larger.

This is upstream improving its artwork across three Unicode revisions and is fine to take, but it
is a visual change to shipped assets rather than the pure addition claimed above.

Alternatives rejected: **OpenMoji** is actively maintained but CC-BY-SA, adding a share-alike
obligation Twemoji's MIT does not; **Noto Emoji** is OFL and Google-maintained. Either would mean
redrawing every bundled asset in a different visual language for no functional gain.

Filename rules confirmed against real requests:
- Codepoints are lowercase hex, hyphen-joined: `1f44d-1f3fd` (skin tone), `1f469-200d-1f4bb` (ZWJ),
  `1f1fa-1f1f8` (flag) — all 200.
- **U+FE0F must be stripped.** `31-20e3` → 200, `31-fe0f-20e3` → 404. `emojiToImage()` and
  `replaceEmojiWithImg()` already strip it, so this is compatible — but it is a hard requirement on
  key derivation, not an incidental detail.

Pin the tag rather than tracking `latest`. With no runtime fetch this is purely a build-
reproducibility concern, but it is a real one: `@latest` means a rebuild months from now can
produce different artwork than the release it claims to reproduce, with no commit explaining why,
and bug reports about rendering become unreproducible. Bumping a pin is a one-line reviewable
change. The cost is that emoji from a Unicode revision newer than the pin strip until someone
bumps it — a narrow and self-correcting gap.

### D4a: `_isEmoji()` has coverage gaps that leave the crash path open

Found while verifying the URL patterns. `_isEmoji()` matches by codepoint range, and several
sequences that macOS renders with Apple Color Emoji fall outside every range it lists:

- **Keycaps** — `1️⃣` is `U+0031 U+FE0F U+20E3`. The base is ASCII `1`; neither it nor U+20E3 is in
  any range, so the sequence is never rewritten and reaches CoreText as a colour glyph.
- **`©️ ®️ ™️`** — U+00A9, U+00AE, U+2122 followed by U+FE0F. Same story.

These are the crash this change exists to prevent, reached by a path the change does not currently
close. The general rule that catches them: **a codepoint followed by U+FE0F is being requested in
emoji presentation**, so treat it as emoji regardless of its range — plus recognising U+20E3 as a
sequence continuation.

Scope note: this is a pre-existing defect, not one introduced here, but `Theme.qml` is a file this
change already edits and the gap defeats the change's own purpose, so it is fixed here.

### D6: Narrow the CLAUDE.md glyph guidance — do not delete it

The existing guidance bans Unicode glyphs used as icons and lists `→` and other arrows as unsafe.
It is tempting to remove it wholesale now that emoji resolve to images, but that conflates two
different things:

- **Pictographic emoji** (`☕`, `😀`) go through `emojiToImage()`/`replaceEmojiWithImg()` and become
  images. After this change they are safe, and the ban on them should lift.
- **Non-emoji text symbols** (`→`, `←`, `▶`, `☰`) are ordinary glyphs rendered by the bundled font.
  They are absent from Decenza Sans's cmap, fall back to a platform font, and vary in metrics per
  machine. The CDN does nothing for them. Their warning must stay.

Deleting the section wholesale would let someone ship `→` in a label and reintroduce exactly the
metric non-determinism that #1549 was about.

### D7: Fix the `main.cpp` comment, keep the setting

`CurveTextRendering` stays — it is there for a separate ligature-glitch reason on desktop resize —
but the comment claiming it eliminates the emoji crash is corrected to state what the 2026-07-18
crash showed. The real defence is D3's routing, and the comment says so.

## Risks / Trade-offs

- **D1's mechanism does not work as reasoned** → Task 1 spikes it in isolation before any sweep;
  the codemod fallback is fully specified and known-good. Nothing else in the change depends on
  which one wins.
- **Returning a `QJSValue` per binding evaluation costs more than a method call** → Measure in the
  spike. The function object can be constructed once and cached; bindings re-evaluate only when
  `translationsChanged` fires, which is rare (language switch, translation edit), not per frame.
- **Escaping by default breaks a call site that silently relied on markup** → 19 sites change
  behaviour. The failure is visible (raw tags rendered), not silent, and all 22 were read while
  making the change. Still, this is the item most in need of a real launch across pages that show
  bean names, recipe names, shot history and the AI conversation.
- **`translationVersion` becomes redundant at call sites but stays public** → Retained: `Tr.qml`
  and any external consumer still use it, and removing it is churn with no benefit.
- **Verification has been macOS-only for the font work that preceded this** → The emoji crash is
  macOS-specific by nature, but the escaping change affects every platform. Android and Windows
  need a real launch before this merges.
- **~4,000 new files in the repository** → A single directory of small generated assets, added and
  refreshed by one documented script. Reviewing the diff by eye is impractical, so the guard is
  that they are generated from a pinned upstream at a verified SHA rather than hand-edited.
- **+2.8 MB install size** → Measured, not estimated (0.32 MB → ~3.15 MB compressed, against a
  137 MB bundle). Stated here because the previous version of this design rejected the whole
  approach on an unmeasured size estimate that was wrong by roughly 5x.
- **Emoji newer than the pinned upstream strip rather than render** → Accepted, with a monthly CI
  job (`.github/workflows/emoji-pin-check.yml`) that reports when upstream has a newer release.
  Pinning without that check just moves the failure from "upstream changed under us" to "the pin
  rotted and nobody noticed" — and with no runtime fallback, a stale pin is the only thing between
  a user and a missing emoji. The job is advisory: it opens nothing, changes nothing, gates no
  merge, and runs on a schedule so no compile depends on a third-party API being reachable.
- **The existence check is justified by TODAY's gaps, not a future Unicode revision** → 1,075
  codepoints inside `_isEmoji`'s ranges currently have no bundled asset (unassigned or undrawn
  upstream), so the broken-image path is reachable now. Unicode 18 (~September 2026) is not the
  argument; an earlier draft leaned on it, and a test written against that reasoning used a
  codepoint `_isEmoji` does not even match, so it passed without exercising the check at all.
- **`_isEmojiPresentation` is a heuristic, not the Unicode emoji property** → It treats "followed
  by U+FE0F" as the signal, bounded to keycap bases and cp >= 0xA9. A character with emoji
  presentation outside that bound would still be missed. Enumerating the real Unicode property
  would be exhaustive but is a larger change; the bound is chosen to be safe rather than complete.

## Migration Plan

No data migration. Behavioural rollout only:
1. Spike D1 in isolation; decide mechanism.
2. Land the emoji/escaping fix (already written) — it is independent of the D1 outcome.
3. Land the translation mechanism plus its guard test.
4. Rewrite `CLAUDE.md` guidance to match what shipped.

Rollback is a revert; nothing persists state that would outlive it.

## Open Questions

- Does D1 hold? Task 1 answers it. Everything else is settled.
- Should `Tr.qml` drop its now-redundant `translationVersion` touch if D1 lands? Leaning no — it is
  harmless, and it documents the dependency explicitly for readers.
