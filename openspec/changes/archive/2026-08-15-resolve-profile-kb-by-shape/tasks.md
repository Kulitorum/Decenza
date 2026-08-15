## 0. Boundary rule widening (`profile-knowledge-base`)

- [x] 0.1 Replace the enumerated separator predicate in `ShotSummarizer::recipePrefixResolve`
      (`shotsummarizer_kb.cpp:359`) with `!sep.isLetter()`, and update the comment above it to state the rule
      as the complement (a letter blocks) rather than as a list.
- [x] 0.2 Remove the second copy of the old separator set from the `recipePrefixResolve` doc comment in
      `src/ai/shotsummarizer.h:462` — one definition, per the centralize rule.
- [x] 0.3 **Outcome gate (primary).** Add a corpus fixture: copy `tests/data/shots/blooming_choker.json`
      with its profile titled `Blooming Espresso_cris`, and a manifest entry carrying the SAME expectations
      (`channeling: "None"`, `grindIssue: true`, `pourTruncated: false`). Those expectations hold only while
      the title resolves and `channeling_expected` transfers — so the fixture fails before the boundary fix
      and passes after. Verify it fails on the pre-fix predicate before keeping it (a test that cannot fail
      is a comment that compiles).
- [x] 0.4 **Identity assertions (secondary).** Add slots to `tests/tst_shotsummarizer.cpp` — an existing
      file, so milliseconds rather than a new ~1.4 s TU: `"Best practice (light roast)_cris"` resolves to
      `best-practice-light-roast`; `"Londinium.v2"` and `"Londinium, decaf"` resolve to `londinium`;
      `"D-Flow / Quark"` and `"D-FlowX"` still resolve to nothing; a non-Latin letter following an alias
      blocks.
- [x] 0.5 Assert the no-op property the widening was adopted on: every shipped profile resolves identically
      under the widened rule, and no entry's alias becomes a boundary-prefix of another entry's alias on a
      newly-admitted character.

## 1. Shape predicate (`profile-shape-equivalence`)

- [x] 1.1 ~~Extract the frame-by-frame comparison in `Profile::functionallyEqual` into a private traversal
      parameterised by which fields it consults.~~ **Not done — not possible as specified, and the reason
      is worth keeping.** `functionallyEqual`'s rules are inherently PAIRWISE: the inactive-axis rule reads
      both frames at once (skip when EITHER side is zero, because de1app writes a default our writer omits),
      and the exit-threshold rule keys off the active exit type. A per-frame signature cannot express
      either. Forcing a shared traversal would have meant passing both frames into a "visitor" that is then
      not a visitor. Instead the two are kept separate with a header comment at each explaining why, and
      `sameShape` is defined AS signature equality (D2) so the predicate and the grouping key cannot drift
      from one another — which was the actual risk the shared traversal was meant to remove.
- [x] 1.2 Add `Profile::shapeSignature()` returning a canonical string over frame count, preinfuse frame
      count, beverage type, and per frame: pump, sensor, transition, exit type, exit condition, seconds
      (fixed formatting, 0.1 s granularity). Document at the definition that the formatting is load-bearing.
- [x] 1.3 Add `Profile::sameShape(a, b)` defined as signature equality (D2), not an independent field walk.
- [x] 1.4 Add test slots to an existing `tests/tst_profile*.cpp` (new slots, not a new file — build cost):
      a temperature/yield-only derivative is the same shape; a frame added, a pump-mode change, a transition
      change and an exit-type change are each a different shape; a >0.1 s duration change is a different
      shape and a ≤0.1 s one is not; differing beverage types are different shapes; argument order does not
      matter; exact equality implies same shape.
- [x] 1.5 Assert `functionallyEqual` is unchanged: pin its result on a set of pairs that exercise both
      subtleties from 1.1, and confirm the import de-duplication path is unaffected.

## 2. Measure the real predicate against the shipped set — gate before wiring anything

Every figure in `design.md` came from a Python proxy over raw JSON fields. `Profile::fromJson` normalizes
(regenerated frames on simple profiles, derived vs stored `preinfuseFrameCount`, the zero-inactive-axis
asymmetry), so the real grouping may differ. This group re-derives the numbers from the shipped C++ path and
is a **stop point**: if they disagree materially with the proxy, revisit D5 and the `seconds`-in-the-key
decision before continuing.

- [x] 2.1 Add a test slot that loads all 100 shipped profiles through `Profile::fromJson`, computes
      `shapeSignature()` for each, and groups them. **Landed in `tst_shotsummarizer.cpp`, not
      `tst_builtinprofileformat.cpp`** — the latter is the semantically obvious home but does not link
      `ai.qrc`, so the KB could not load there and all four slots returned vacuous zeros. Profile files are
      read from the source tree via `DECENZA_SOURCE_DIR` either way, so no resource was added to any target.
- [x] 2.2 Assert every shipped profile's bucket contains its own KB id (no profile fails to match itself
      through the real parse path).
- [x] 2.3 Pin the collision structure by name, not just by count: assert the exact set of buckets holding
      more than one KB id. Proxy expectation is 3 buckets — {`d-flow`, `d-flow-la-pavoni-variant`},
      {`damians-lr-v2-v3`, `londinium`}, {`gentle-flat-long-preinfusion-family`,
      `preinfuse-then-45ml-of-water`} — 6 profiles of 95. Pinning members means a future shipped-profile edit
      that collapses `d-flow-q-variant` into `d-flow` fails the suite instead of silently widening a bucket.
- [x] 2.4 Re-derive the per-fact figures from the real grouping: presence-of-KB-context, flag unanimity, band
      unanimity, UGS unanimity. Proxy expectation 81/81, 80/81, 79/81, 79/81 over 81 signatures.
- [x] 2.5 Re-run 2.3/2.4 with `seconds` dropped from the signature and confirm the widening the proxy
      measured (58 signatures, 9 colliding buckets, 23 profiles). This is the evidence for keeping `seconds`
      in the key; if the real path does not reproduce it, that decision is not supported.
- [x] 2.6 **Compare against `design.md` and stop if they disagree.** Update the design's figures to the
      measured ones either way, and record in the PR that they came from the C++ path rather than the proxy.

## 3. Shipped-profile shape index

- [x] 3.1 ~~Add `loadShapeIndex()` to `shotsummarizer_kb.cpp`~~ — **wrong home.** That TU exists to stay a
      lean KB-data layer so `shot_eval` and `tst_shotrecord_cache` link a small closure, and it says so;
      shape resolution needs `Profile`, which reaches Qt Bluetooth through `profileframe.cpp`'s use of
      `DE1::FrameFlag`. Landed instead as its own TU, `src/ai/profileshapeindex.{h,cpp}`, with the same
      mutex-guarded lazy-init pattern. Consequence recorded at the top of that header: `shot_eval` does not
      do shape resolution, so the corpus gates the title steps but not this one.
- [x] 3.2 Build the index over `:/profiles/*.json`: parse each, resolve its title through the existing title
      steps, and keep only those that resolve — as `QHash<signature, QList<kbId>>`.
- [x] 3.3 Measure the real one-time build cost (`Profile::fromJson`, not raw JSON parse) and the warm lookup
      cost; record median and worst case in comments at both call sites added in section 4.
- [x] 3.4 Test: every shipped profile's own signature resolves to a bucket containing its own KB id, and the
      index is byte-identical across two builds (order independence).

## 4. Candidate-set resolution and per-fact transfer (`profile-knowledge-base`)

- [x] 4.1 Add `KbResolution { ids, origin }` and `resolveProfileKb(const Profile&)`. **Not on
      `ShotSummarizer`** as originally written — that would push `profile.h` into a header every KB
      consumer includes, the same closure problem group 3 hit. Both live in `profileshapeindex.h`, which
      already forward-declares `Profile`. Title steps run first and unchanged; the shape step is reached
      only on a total title miss.
- [x] 4.2 Keep `computeProfileKbId` as the single-id façade — returns the sole id when the set has exactly
      one member, empty otherwise. Confirm no existing caller changes behaviour for a title-resolvable
      profile.
- [x] 4.3 Candidate-set overloads landed in `shotsummarizer_kb.cpp` — they take id LISTS, not profiles, so
      the lean TU keeps its closure. `flagTransfersAsUnion()` is the single classification table;
      unlisted flags default to unanimity so a newly authored flag cannot inherit the union by omission.
- [x] 4.4 Test the rules directly: a disputed shape-silencing flag applies; a disputed `grind_check_skip`
      does not and the grind detector runs; a disputed band yields no band and is a strict no-op; a disputed
      UGS yields nothing; a single-member set is byte-identical to a title match.
- [x] 4.5 Add the inverse obligation the identity assertions lack: negative fixtures in
      `tests/tst_shotsummarizer.cpp` — structurally-distinct profiles that must NOT match — plus a
      shape-resolved positive fixture, plus the assertion that disabling the shape step leaves every shipped
      profile's resolution unchanged.
- [x] 4.6 ~~Add a corpus fixture for the shape path.~~ **Dropped.** `shot_eval` resolves the KB itself and
      does not link `Profile`, so it cannot evaluate a shape-resolved fixture; giving it that closure would
      pull ~4,900 lines and Qt Bluetooth into a curve-eval CLI. Nothing else depended on this fixture — the
      corpus still gates the boundary rule (0.3), which is the half with a confirmed live case.

## 5. Wire into the analysis paths

- [x] 5.1 `ShotHistoryStorage::saveShot`: resolves via `resolveProfileKb(*profile)`; an unambiguous
      shape-resolved id is persisted, an ambiguous one stores nothing (single-id column by contract, and
      every read path re-resolves anyway).
- [x] 5.2 `prepareAnalysisInputs` reuses the existing parse (`ProfileFrameInfo` now carries the parsed
      `Profile`) and feeds the resolution through the per-fact accessors. **Beyond the stated scope, and
      deliberate:** `analysisFlags` previously came from the PERSISTED id while the expert band came from a
      fresh re-resolve — a split the old comment called "out of this change's scope". Both now come from the
      fresh resolution. A shot with a stale persisted id therefore gets flags from the entry the band
      already used, instead of flags and band disagreeing about which entry the shot belongs to. Flagged in
      the PR as a behaviour change to existing history.
- [x] 5.3 `AnalysisInputs::profileKbResolved` is now the single source for that gate. **Three** call sites
      derived it by hand from the persisted id, not the two the task named — the third was
      `shothistorystorage_serialize.cpp:144`. All three read the field now, so the next change to what
      "resolved" means cannot reach two and miss one.
- [x] 5.4 Corpus green (22 shots, incl. the renamed-profile fixture). **Note the limit:** `shot_eval` does
      its own KB resolution and does not call `prepareAnalysisInputs`, so the corpus cannot measure the
      app-side change — it confirms the title path is unmoved, nothing more. The app path is covered instead
      by four direct `prepareAnalysisInputs` tests: a renamed profile gains its flags and Arm 1; a
      title-resolved profile is unchanged; an unrecognisable profile stays unresolved; and a profile-less
      shot is not attributed to the shipped "Default" profile.

## 6. Surfacing (`profile-knowledge-base`, presentation requirements)

- [x] 6.1 `extractProfileMeta` (`profilemanager.cpp:1908`): it already holds the parsed profile object —
      resolve with frames so `hasKnowledgeBase` reflects shape resolution, and carry the matched canonical
      display name and the resolution origin onto `ProfileInfo`.
- [x] 6.2 Unify the content path: `profileKnowledgeContent` / `findProfileSection` must resolve the same way
      the indicator does, so a lit sparkle can never open an empty dialog.
- [x] 6.3 `ProfileSelectorPage.qml`: sparkle now lights for shape-resolved profiles; add the derivation
      label naming the matched entry, shown only when the origin is Shape.
- [x] 6.4 `ShotDetailPage.qml` and `PostShotReviewPage.qml`: same label beside the existing header sparkle;
      fix `ShotDetailPage.qml:419`'s title-only `profileKnowledgeContent` call.
- [x] 6.5 Add the translation keys via `TranslationManager.translate` / `Tr` per project convention.

## 7. Shot Summary affordance visibility (`shot-analysis-pipeline`)

- [x] 7.1 Remove `profileKbId` from the `QualityBadges` `visible` gate in `PostShotReviewPage.qml:1131` and
      `ShotDetailPage.qml:367` so the row — and the Shot Summary chip — is always present.
- [x] 7.2 Confirm the flag chips and the clean-extraction chip keep their own conditions, and the
      affordance tint still binds to the recomputed `verdictCategory`.
- [x] 7.3 Open both pages in the running app on a clean shot with an unresolved profile — the case the gate
      was hiding. `pragma ComponentBehavior` / delegate role hazards are not in play here, but neither page
      is covered by a test that would catch a QML runtime error.

## 7b. Indicator follows the candidate set, and the KB stops saying one thing twice

Both moves came out of auditing the three shipped shape collisions with the user. The first is a KB
data fix the shape matcher exposed; the second is the surfacing rule the first one clarified.

- [x] 7b.1 Merge the `damians-lr-v2-v3` / `londinium` split. `londonium.json` and `damian_s_lrv2.json`
      are byte-identical across all seven frames — Londonium's own notes say "This is identical to the
      LRv2 profile, but renamed to be easier to understand" — so they are one profile, and the KB's
      claim that LRv2 had "different fill/infuse behavior and higher frame temperatures" is disproved
      by the files (both run 89/89/88.5/88.5/88/88/88). LRv2 now resolves to `londinium` and carries
      the cited pressure-peak band it was always entitled to.
- [x] 7b.2 Split LRv3 into its own `damians-lrv3` entry: eight frames, 90 °C, a 9-bar hold before the
      decline, no flow-control step. It keeps no band, per the existing rationale. The id rename is
      deliberate — a legacy persisted `damians-lr-v2-v3` on an LRv2 shot would otherwise resolve to an
      LRv3-only entry and inherit wrong facts; unresolvable-then-healed by re-resolution beats
      silently-wrong.
- [x] 7b.3 Update the six tests that pinned the old split, each of which asserted some form of
      "LRv2 ≠ Londinium". Shipped shape collisions drop 3 → 2, band disagreements 2 → 1.
- [x] 7b.4 `ProfileInfo` carries the whole candidate set (`kbIds`), not just the identity. The knowledge
      indicator gates on the set: an ambiguous match still shapes the badges through union suppression
      and unanimous facts, so a dark sparkle over KB-influenced badges told the user less than the
      analysis knew.
- [x] 7b.5 `profileKnowledgeContent` composes every candidate's body, each preceded by its canonical
      name, so a lit sparkle can never open an arbitrary member's prose. No English connective in C++ —
      `profileKbCandidateNames()` feeds the dialog a translated line instead.
- [x] 7b.6 Route all three sparkle call sites through `ProfileKnowledgeDialog.openFor()`, which was
      already the shared entry point and was being bypassed by three hand-written copies of the same
      two assignments.
- [x] 7b.7 Link `ai.qrc` into `tst_profilemanager`. Without it every KB lookup in ProfileManager's own
      test binary silently returned empty — the same trap that cost a debugging round in group 2.

## 8. Gates and documentation

- [x] 8.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) and the qmllint gate; both green.
- [x] 8.2 Note in the PR that `tst_kb_resolution` — named as a hard gate by the pre-change spec — never
      existed; the delta replaces that requirement with the corpus-outcome gate plus identity assertions in
      an existing file.
- [x] 8.3 Update `docs/SHOT_REVIEW.md`: the badge row is no longer KB-gated, and the "Shot Summary chip
      always sits at the end of the row" sentence is now true unconditionally.
- [x] 8.4 Re-run the profile audit and record the result in the PR, so the shape step's value is measured
      rather than assumed. **Done for this Mac's set; the tablet was NOT reachable** (its MCP host does not
      resolve from this network), so the device-side audit is still outstanding and the PR says so rather
      than implying wider evidence than exists. Result on 9 non-built-in profiles: 1 shape match, and that
      one is a same-titled copy of a shipped profile which resolves at step 1 and never reaches the shape
      step. So the honest local figure is still **zero profiles that the shape step actually rescues** —
      recorded in proposal.md as weak evidence, unchanged.
- [x] 8.5 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` (or the KB doc it points at) with the shape step and the
      per-fact transfer rules.
- [x] 8.6 Update the wiki manual: a short entry (3–5 sentences) on why a custom profile may show "Based on
      X", and that the app uses the matched profile's knowledge to avoid flagging expected behaviour.
      Added as `### Profile Knowledge` under section 6, committed in the wiki clone; the push is a public
      publication and is left for the maintainer to trigger.
- [x] 8.7 Archive this change with `openspec archive resolve-profile-kb-by-shape` as the last commit on the
      branch, before merge.
