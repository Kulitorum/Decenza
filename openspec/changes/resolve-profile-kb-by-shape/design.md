## Context

See `proposal.md` — Why. The mechanics that constrain the approach:

- The resolver (`ShotSummarizer::matchProfileKey`) takes `(profileTitle, editorTypeHint)` and returns a
  single `QString` id. Every consumer — `getAnalysisFlags`, `expertBandForKbId`, `ugsForKbId`,
  `findProfileSection`, `canonicalNameForKbId` — takes an id and looks up one entry. A candidate *set*
  therefore cannot be threaded through the existing signatures unchanged.
- `resources/profiles/` holds 100 shipped profile JSONs, 494 KB total; 95 of them resolve to a KB id by
  exact title. They are reachable at runtime as `:/profiles/*.json`.
- `Profile::functionallyEqual` already performs a frame-by-frame comparison at 0.1 tolerance, with two
  subtleties that must be preserved: the inactive axis is skipped when either side is zero (de1app writes a
  default our writer omits), and only the *active* exit threshold field is compared.
- Analysis inputs are assembled in exactly two places — `ShotHistoryStorage::saveShot` (save time) and
  `prepareAnalysisInputs` (load / recompute time). Both already re-resolve the KB from live data; neither
  trusts the persisted `profileKbId` blindly.
- Measured on this tree: parsing all 100 shipped profiles costs ~2 ms of raw JSON parse (Python proxy;
  `Profile::fromJson` will be several times that). A single shape lookup against a warm index is ~0.02 ms.

## Goals / Non-Goals

**Goals:**

- One traversal of the frame list serving both the exact-equality and the shape predicate.
- Resolution that is a boolean predicate end to end — no score, no threshold, no constant a future
  maintainer has to re-tune.
- Per-fact transfer rules that are stated where the fact is read, not applied by callers.
- The shipped-profile index built once, lazily, and never on a path a user or the machine can feel.

**Non-Goals:**

- Changing what any KB entry asserts. This change moves existing facts to more profiles; it authors none.
- Any change to `Profile::functionallyEqual`'s result for any input pair.
- Deriving the Arm 1 gate from frames directly (see proposal — Alternative considered).
- Repairing `grind_check_skip`'s over-broad reach (below).

## Decisions

### D0 — The boundary rule becomes "not a letter", replacing the enumerated separator set

`recipePrefixResolve`'s predicate (`shotsummarizer_kb.cpp:359`) becomes `!sep.isLetter()`.

*Why the complement rather than a longer enumeration:* the requirement's own scenarios turn on exactly one
case — a following letter must block, so `D-Flow / Quark` does not inherit `D-Flow / Q`. Everything else was
incidental. An enumeration has to guess which punctuation users type and silently fails on the rest; `_`
was the miss that surfaced, and there is no reason to believe `.`, `,` or `(` would have fared better.

*Verified before adopting, not after:* over the 100 shipped profiles the widened rule changes **0**
resolutions; across the full alias set it introduces **0** cases where one entry's alias becomes a
boundary-prefix of another entry's alias on a newly-admitted character. Both letter-blocking scenarios
(`D-Flow / Quark`, `D-FlowX`) still resolve to nothing.

*Ordering note:* this lands in step 2, so it runs before the shape step and reduces the population the shape
step ever sees. The two are independent and each stands alone.

*Non-Latin scripts:* `QChar::isLetter()` is Unicode-aware, so a CJK or Cyrillic character following an alias
correctly blocks, exactly as an ASCII letter does. The prior enumeration admitted such characters as
boundaries by omission — a latent inconsistency this fixes rather than introduces.

### D1 — Signature string, not pairwise comparison, for the index

`Profile::shapeSignature()` returns a canonical string; the index is a
`QHash<QString, QList<QString>>` from signature to KB ids. Lookup is a hash probe, not a scan over 95
candidates.

*Why over pairwise:* the candidate-set semantics need grouping, and grouping is what a signature gives for
free — the set is simply the bucket. A pairwise scan would compute the same grouping every call. It also
makes "resolution does not depend on enumeration order" true by construction rather than by inspection.

*Cost:* the signature must be canonical (fixed field order, fixed numeric formatting for `seconds`), so a
formatting change silently changes every bucket. Mitigated by D2.

### D2 — `sameShape(a, b)` is defined as signature equality, and is the tested predicate

`Profile::sameShape(a, b)` SHALL be implemented as `shapeSignature(a) == shapeSignature(b)`, not as an
independent field walk. There is then exactly one definition of the shape, and the signature cannot drift
from the predicate the spec describes.

**Revised during implementation:** the intended shared traversal with `functionallyEqual` is NOT possible
and was dropped. `functionallyEqual`'s rules are pairwise — the inactive-axis rule reads both frames at once
(skip when EITHER side is zero, because de1app writes a default our writer omits) and the exit-threshold
rule keys off the active exit type — so they cannot be expressed as a per-frame signature. The two
predicates are kept separate, each carrying a header comment naming the other and saying why. Defining
`sameShape` AS signature equality still removes the drift risk that motivated the shared traversal: there is
one definition of "shape" and the grouping key is it.

A related finding: `functionallyEqual` had **no test coverage at all** before this change, in any file. The
shape work adds its first pin, including both subtleties above.

### D3 — Candidate set is carried in a small struct, not encoded into the id string

`matchProfileKey` keeps its signature and behaviour for the title steps. A new
`ShotSummarizer::resolveProfile(title, editorType, const Profile* frames)` returns a
`KbResolution { QStringList ids; enum Origin { Title, Shape, None }; }`. `computeProfileKbId` is retained as
the single-id façade for callers that only need identity, returning `ids.first()` when the set has exactly
one member and empty otherwise.

*Why not encode a set into the existing `QString`:* every consumer would have to learn to parse it, and a
set-shaped id would leak into the persisted `profile_kb_id` column, which is a single id by contract.

*What gets persisted:* the single-id façade's value, i.e. nothing new is written when the set is ambiguous.
The per-fact rules operate on the live resolution, not on the persisted id — the same pattern
`prepareAnalysisInputs` already uses to re-resolve the expert band from current data.

### D4 — Per-fact rules live in the KB accessors, not in callers

`getAnalysisFlags`, `expertBandForKbId` and `ugsForKbId` gain candidate-set-aware overloads that apply their
own rule (union / unanimity / unanimity respectively). Callers pass the resolution and get the same shapes
of value they get today.

*Why:* a rule applied by callers is a rule that drifts. There are already four call sites for
`getAnalysisFlags` alone; the union-vs-unanimity distinction is exactly the kind of policy that gets copied
wrong.

### D5 — `grind_check_skip` requires unanimity; the other two flags take the union

The spec states this as a general rule keyed on what the flag suppresses. Concretely, of the three flags in
the KB today:

| flag | what it suppresses | rule | why |
| --- | --- | --- | --- |
| `flow_trend_ok` | one caution line about flow trend | union | over-applying costs a missing line |
| `channeling_expected` | the channeling badge and line | union | shape-derived diagnosis; same reasoning |
| `grind_check_skip` | the **entire** grind detector, Arm 2 included | unanimity | Arm 2 (choked puck, yield shortfall, gusher) reads physics, not shape, and holds on any profile — silencing it hides a genuinely faulty shot |

Measured on the current KB: across all 81 shipped-profile shape signatures there is exactly one flag
disagreement, on `flow_trend_ok`, between `gentle-flat-long-preinfusion-family` and
`preinfuse-then-45ml-of-water`. `channeling_expected` and `grind_check_skip` never disagree within a bucket
today. The rule is written for the KB as it grows, not for a live collision.

**Noted and not fixed:** `grind_check_skip` returning early from `analyzeFlowVsGoal` disables the
physics-only Arm 2 for the six profiles that carry it (`turbo-shot`, `allonge`, `advanced-spring-lever`, …),
so a gusher on a turbo shot is currently undetectable. That is pre-existing, affects title-resolved profiles
equally, and changing it would move analysis output for named profiles — out of scope here, but it is the
reason `grind_check_skip` gets the stricter rule rather than the union.

### D5a — Identity is single-member-only; analysis facts are not

Measured across the three colliding buckets, the KB's own content answers the question directly:

| bucket | flags | expert band | UGS | prose | roast affinity |
| --- | --- | --- | --- | --- | --- |
| `d-flow` / `d-flow-la-pavoni-variant` | agree | **differ** (none vs 6–9 bar) | **differ** (0.5 vs 1.0) | differ | both absent |
| `damians-lr-v2-v3` / `londinium` | agree | **differ** (none vs 8–9 bar) | agree (0.0) | differ | **differ** |
| `gentle-flat-long-preinfusion-family` / `preinfuse-then-45ml-of-water` | **differ** | agree (none) | agree (absent) | differ | **differ** |

The flags — the thing this change exists to transfer — agree in two of three, and the third disagreement is
in the safe direction the union rule handles. Everything that disagrees was already behind unanimity, except
two facts the first draft did not classify at all: **prose body** (never identical in any bucket, and it is
what the knowledge dialog renders) and **roastAffinity** (differs in two of three; consumed by
`ProfileManager`'s roast-suitability check).

Rather than adding two more per-fact rules, these are folded into one: facts that identify *which* profile
the knowledge came from require a **single-member** set, not unanimity. Naming an arbitrary member of a
two-member set is an assertion the resolution never made. Analysis-affecting facts keep their own rules in
that case — a false positive can be suppressed without claiming to know which profile it came from, and
that separation is what lets the loose match stay useful without becoming a claim.

Any future KB field defaults to unanimity-or-withhold. The union is the narrow exception, never the default.

### D6 — The index is built lazily on first miss, on whichever thread asks

Built inside the existing `loadProfileKnowledge` mutex-guarded lazy-init pattern, but as a separate
`loadShapeIndex()` so a title-resolvable profile never pays for it. First call parses 100 JSONs; every call
after is a hash probe.

*Where it lands:* `ShotHistoryStorage::saveShot` (post-shot, background of the user's attention, machine
idle) and `prepareAnalysisInputs` (shot-load background thread). The profile-list path
(`extractProfileMeta`) runs during a profile scan, already file-I/O bound.

*What must not happen:* it must not be reachable from a binding that re-evaluates, or from any path live
during a shot. Both call sites above are one-shot per shot. The measured median and worst case go in a
comment at each site.

### D7 — Surfacing reuses the existing sparkle affordance rather than adding a new one

The profile list and both shot pages already carry a sparkle indicator gated on KB presence. Shape
resolution widens what lights it; the derivation label ("Based on X") is added next to it. `hasKnowledgeBase`
and the content path are unified onto one resolution in the same edit, since `extractProfileMeta` already
holds the parsed profile object and can pass frames.

## Risks / Trade-offs

- **A wrong shape match silently applies wrong suppression** → the candidate-set rules bound the damage to a
  *missing* line for the union flags, and to nothing at all for band/UGS. `tst_kb_resolution` gains negative
  fixtures — shapes that must NOT match — since it currently only asserts the positive direction.
- **The signature's numeric formatting is load-bearing** → D2 makes the predicate *be* signature equality, so
  a formatting change cannot desynchronise predicate from index. A test pins signature equality for a
  known derivative pair and inequality for a known structural edit.
- **Analysis output changes for existing history** → intended, and it is what the `shot_eval` corpus is for.
  Run the corpus before and after and diff the emitted lines; any change must be explainable as a
  suppression that should always have applied.
- **Widening resolution widens the blast radius of a bad KB edit** → unchanged in kind (a bad flag already
  reaches every title-matching profile), larger in degree. The build-time KB validator is unaffected.
- **Shipped profiles churn between releases** → a shipped profile's frames changing moves its bucket, so a
  user profile can gain or lose a match across an update. Acceptable: the KB tracks the current profile set
  by design, and the same is already true of the alias map.
- ~~**Proxy figures may not survive the real parse path**~~ **RESOLVED (group 2).** The C++ path over
  `Profile::fromJson` reproduces the Python proxy's grouping exactly. Confirmed by assertion, in
  `tst_shotsummarizer`: the colliding buckets are exactly the three predicted, by member name
  (`d-flow`+`d-flow-la-pavoni-variant`, `damians-lr-v2-v3`+`londinium`,
  `gentle-flat-long-preinfusion-family`+`preinfuse-then-45ml-of-water`); exactly one bucket disagrees on
  `analysisFlags` and exactly two disagree on the expert band; and dropping durations from the key more than
  doubles the profiles in colliding buckets. Normalization on load (regenerated frames on simple profiles,
  derived preinfuse counts) did not regroup anything. D5 and the `seconds`-in-the-key decision stand as
  written. Note what is NOT pinned: the absolute signature counts (81 with durations, 58 without) are
  asserted only as an inequality — an exact count would fail on any shipped-profile addition, which is churn
  rather than signal.
- **`Profile::fromJson` cost is estimated, not measured** → the ~2 ms figure is a Python proxy over raw JSON.
  The real figure is measured during implementation and written at the call sites; if it lands far above the
  10–20 ms estimate, D6 moves to a worker thread rather than lazy-on-demand.

## Migration Plan

None required. No schema change, no persisted-format change, nothing to backfill: `profile_kb_id` keeps its
existing single-id contract and stays empty for an ambiguous set, and every consumer re-resolves from live
data at analysis time already. Rollback is reverting the commit — no data written under this change needs
undoing.

## Open Questions

- Exact wording and placement of the derivation label on each of the three surfaces. Deferrable: the spec
  fixes that it must name the canonical entry and read as a derivation, which is the part that constrains
  the code; the string is a translation key decided during implementation.
