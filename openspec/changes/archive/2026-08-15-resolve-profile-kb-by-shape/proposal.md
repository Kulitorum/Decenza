## Why

The KB's per-profile facts are reached only by matching a profile **title**. A user who copies a documented
profile, nudges the temperature or the yield, and renames it to something that is not a boundary-prefix of
the original loses every one of them — even though the extraction shape the KB describes is unchanged.

What that costs is mostly **wrong lines, not missing ones**. Three of the KB's facts exist specifically to
say "this is expected here": `flow_trend_ok` (declining or rising flow is by design), `channeling_expected`
(this shape drives `dC/dt` negative naturally), `grind_check_skip`. 37 of the 47 KB entries carry
`flow_trend_ok` and 10 carry `channeling_expected`. So an unresolved lever-style profile does not merely go
quiet — it gets a "Flow dropped … (fines migration or clogging)" caution, or a channeling badge, on shots
where the matching *named* profile would correctly stay silent. The summary is actively misleading on
exactly the shapes those flags were authored for.

One live instance, from the shot review page: the profile `Best practice (light roast)_cris` is the shipped
`Best practice (light roast)` renamed with a suffix. It fails on a single character — the recipe-prefix step
matches the alias, then requires the next character to be one of `/`, `-`, space or an ASCII digit, and `_`
is not in that set. That KB entry carries **both** `flow_trend_ok` and `channeling_expected`, so every shot
on that profile is analysed with both suppressions off.

The evidence needed to recognise the relationship is already on disk: **all 100** bundled profile JSONs
resolve to a KB id (95 by exact title alias, the five `A-Flow / default-*` variants via the editor-type
default), and every shot stores its full `profileJson`. Measured through the real `Profile::fromJson` path,
those 100 form **85 distinct shapes**.

Two smaller problems ride along. The profile list's sparkle indicator and the shot pages' KB affordance are
derived from the same title-only resolution, so a user has no way to learn the app considers their profile a
Londinium with a different brew temperature. And the `QualityBadges` row on both shot pages is hidden
entirely when `profileKbId` is empty and no detector fired — which hides the "Shot Summary" chip, the only
entry point to the analysis dialog, on precisely the shots whose analysis is weakest. The dialog's contents
never depended on the KB at all.

## What Changes

- Widen the recipe-prefix step's boundary rule from an enumerated separator set (`/`, `-`, space, digit) to
  **any character that is not a letter**. The enumeration was the wrong shape for the rule: what the
  requirement actually turns on is that a following *letter* must not be a boundary (so `D-Flow / Quark`
  does not match `D-Flow / Q`), and every other character is fine. Measured on this tree: the widening
  changes the resolution of **0 of 100** shipped profiles, introduces **0** alias-vs-alias collisions, and
  leaves both letter-blocking scenarios failing correctly.
- Add a **profile-shape** resolution step to the KB resolver, consulted only after the existing title-based
  steps (exact alias → recipe-alias longest-boundary-prefix → editor-type default) have all missed. It is a
  boolean predicate over frame structure, not a distance metric: there is no threshold and no tuning
  constant to get wrong.
- Match on **structure and frame durations**, ignoring the magnitudes a dial-in changes. Two profiles are
  the same shape when frame count, `preinfuseFrameCount`, beverage type, and every frame's `pump`, `sensor`,
  `transition`, `exit` type/condition and `seconds` agree. Per-frame `temperature`, `pressure`, `flow`,
  `volume`, `exitWeight`, exit thresholds, `maxFlowOrPressure` and `popup` are ignored — those are what a
  user changes when dialling in, and none of them changes what the suppression flags assert.
  Keeping `seconds` in the key is load-bearing and was measured: dropping it puts **23 of 95** profiles into
  ambiguous buckets (including one that collapses `d-flow`, `d-flow-Q` and `d-flow-La-Pavoni` together);
  keeping it drops that to **6 of 95**.
- Resolve to a **candidate set**, not a single id, and let each fact transfer under its own rule. This is
  what makes a loose match safe without a threshold:
  - **Suppression flags** (`flow_trend_ok`, `channeling_expected`, `grind_check_skip`) transfer as the
    **union** across the candidate set. A missing suppression produces a *wrong* line; an extra suppression
    produces a *missing* one, and the flag is a claim about the shape, which by construction is shared.
    Measured: unanimous on 80 of 81 signatures, and the union rule makes it 81 of 81.
  - **Assertive facts** (`expertBand`, `ugs`) transfer only on **unanimity**, else are withheld. `d-flow`
    carries no band and `d-flow-La-Pavoni` does; guessing either invents a band line or drops one.
    Withholding is already a strict no-op in `analyzeShot`. Measured unanimous on 79 of 81.
  - `profileKbResolved` is true for any non-empty candidate set — measured 81 of 81, since every member
    answers the same structural question.
- Add `Profile::shapeSignature()` / `Profile::sameShape(a, b)` beside the existing
  `Profile::functionallyEqual`, sharing one field-by-field traversal so the two predicates cannot drift.
- Build a lazily-initialised, cached shape index over `:/profiles/*.json`, keeping only entries whose title
  resolves to a KB id through the existing resolver. Measured: 48 ms one-time on a debug+sanitizer build,
  paid only by a profile that already failed title resolution, on shot save or shot load — never on a path a
  user waits on. Warm lookups are a hash probe.
- Surface the relationship. Where a profile resolves by shape rather than by name, the profile list and the
  shot review / shot detail pages name the KB profile it matched, using the KB's canonical display name and
  worded as a derivation rather than an identity ("Based on X"). A profile that resolved by title shows what
  it shows today, with no added label.
- Make the knowledge-dialog content path resolve the same way its indicator does. `hasKnowledgeBase` and
  `profileKnowledgeContent` are separate title-only calls today, so widening only the indicator would light
  a sparkle that opens an empty dialog.
- Remove `profileKbId` from the `QualityBadges` visibility gate on both shot pages so the Shot Summary chip
  is always reachable.

Out of scope, deliberately: no profile-provenance stamp (it can do nothing for shots already saved); no
change to what the KB asserts; no new user-facing setting; no schema change or migration.

## Capabilities

### New Capabilities
- `profile-shape-equivalence`: the predicate that decides when two profiles are the same extraction shape —
  which frame fields are part of the shape, which are ignored as dial-in variables, and why the split falls
  where it does.

### Modified Capabilities
- `profile-knowledge-base`: the recipe-prefix step's closed separator set is replaced by the not-a-letter
  boundary rule. Additionally, the resolver is currently "exact-match-or-explicitly-unresolved" over titles. It
  gains a fourth step keyed on geometry rather than on a name, resolving to a candidate set with per-fact
  transfer rules. The existing prohibition on order-dependent greedy name scans is restated unchanged — the
  new step clears it by being total, deterministic, order-independent and structurally anchored, the same
  grounds that admitted the #1198 prefix step. The capability also gains the obligation that an indicator
  and the content behind it resolve identically, and that a shape-derived match is presented as a derivation
  rather than an identity.
- `shot-analysis-pipeline`: adds the requirement that the Shot Summary affordance is reachable for every
  shot regardless of KB resolution — the companion to the existing affordance-tint requirement.

## Impact

- `src/profile/profile.{h,cpp}` — shared traversal; `shapeSignature` / `sameShape`.
- `src/ai/shotsummarizer_kb.cpp:359` — the boundary predicate in `recipePrefixResolve`; plus the shape
  index, the candidate-set resolution step in `matchProfileKey`, and
  the per-fact transfer rules in `getAnalysisFlags` / `expertBandForKbId` / `ugsForKbId`.
- `src/history/shothistorystorage.cpp:2286` — the save-time `computeProfileKbId` call gains the fallback, so
  the resolved id is persisted once per shot and shot load pays nothing.
- `src/history/shothistorystorage_internal.cpp` — `prepareAnalysisInputs`, the retroactive path for shots
  already in the database (background thread).
- `src/controllers/profilemanager.cpp:1908` (`extractProfileMeta`) — `hasKnowledgeBase` is a title-only
  `computeProfileKbId`; it already holds the parsed profile object, so the frames are in hand.
  `profileKnowledgeContent` (`:1058`) and `ShotDetailPage.qml:419` take the title-only `findProfileSection`
  path and need the same widening.
- `qml/pages/ProfileSelectorPage.qml` — sparkle indicator and "Based on X" label.
- `qml/pages/PostShotReviewPage.qml:1131`, `qml/pages/ShotDetailPage.qml:367` — visibility gate and label.
- Behaviour change on existing history: previously-unresolved shots gain the suppression flags and Arm 1.
  That is the point, but a wrong match is newly possible. `tst_kb_resolution` is already a hard merge gate
  for "every corpus title resolves to exactly one id"; it grows the inverse obligation — shapes that must
  NOT match, pinned as fixtures.
- No DB schema change, no migration, no BLE or machine-facing surface touched.

## What the available evidence does and does not support

The separator widening has a confirmed live case and measures as a no-op against every shipped profile.

The shape step does not yet have one. An audit of the nine profiles in this machine's profile directory
found: two already resolving, **zero** that the separator widening would newly resolve, and **zero** that
the shape step would match. The one title that looks derivational — `Luca's Classic Italian Espresso18` —
is genuinely a different profile: two flow frames against the shipped `Classic Italian espresso`'s four
frames with a pressure rise-and-hold. Shape matching correctly declines it, and any name-based rule loose
enough to match it would have handed over the wrong knowledge. That is the shape rule behaving properly and
finding nothing to do.

Caveats, stated so the number is not over-read: the sample is nine profiles of which four are test junk;
the `_cris` profile is on a different machine and is not in it; and a profile set is not a shot history, so
it says nothing about how many *shots* sit on an unresolved profile. The shape step is retained on the
judgement that avoiding a wrong "you did something wrong" is worth machinery that may fire rarely, which is
this change's governing principle. It should be re-examined against a real device's profile set before
being counted as delivering value.

## Alternative considered and not taken

`profileKbResolved` gates grind Arm 1 on "is this flow goal a real target or a safety limiter" — a question
that is arguably answerable from the frames directly (flow-mode frames carrying a flow goal, with no
pressure-ceiling exit), for every profile, with no KB relative required. That would give Arm 1 strictly
broader coverage than shape-matching does. It is not pursued here because it addresses only that one gate
and does nothing for the suppression flags, which is where the measured harm is. It remains available as a
later change and is not foreclosed by anything in this one.
