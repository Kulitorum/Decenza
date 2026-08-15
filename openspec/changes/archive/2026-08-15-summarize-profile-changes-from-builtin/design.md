## Context

See proposal.md — Why. Three pieces of the existing implementation shape the approach:

- `ProfileShapeIndex` already walks every `:/profiles/*.json` once per process and buckets them by
  `Profile::shapeSignature()`. Its bucket value is a `QStringList` of **KB ids**. The diff needs the bundled
  **profile** behind a match, which the bucket does not retain.
- `Profile::frameDiffReport(a, b)` already performs the frame-by-frame walk, and already carries two rules
  this change needs: it compares only the setpoint the frame's pump mode uses, and only the exit threshold
  matching the frame's exit type. Its output is developer text (`FRAME[2] pressure: A=9 B=6`), consumed by
  `profile_sync` and asserted only for emptiness by `tst_tclimport`.
- `ProfileKnowledgeDialog.openFor(title)` is documented as the only complete entry point; setting its
  properties by hand leaves `candidateNames` stale.

## Goals / Non-Goals

**Goals:**
- One traversal produces both the user-facing diff and the existing developer report.
- The nearest-candidate vote and the diff read the same field list, so they cannot disagree about what a
  dial-in field is.
- Translation and unit formatting stay in QML, where the rest of the app's do.

**Non-Goals:**
- Changing what `profile_sync` or the TCL parity gate consider "different". The developer report keeps its
  current field set and its current text.
- Any persistence. The diff is derived on dialog open and stored nowhere.
- Extending the diff to profiles that are not the same shape. A general profile-vs-profile comparison is a
  different feature with a different UI.

## Decisions

### One enumerable field list, three consumers

A single ordered list of comparable fields is defined once — profile level (brew temperature, target weight,
target volume) and per frame (temperature, active setpoint, active exit threshold, limiter value and range,
volume, display name) — each entry knowing how to read its value from a `Profile` and whether it is a shape
field, a dial-in field, or developer-only.

Three consumers read it: the user-facing diff (dial-in entries only), the nearest-candidate vote (same
entries), and `frameDiffReport` (all entries, current formatting). *Alternative rejected:* a second
independent walk for the user-facing diff. That is the copy the centralisation rule exists to prevent — two
walks are free to drift on which axis is "active", and only one of them has a test.

**Deliberate divergence between the two filters:** `frameDiffReport` compares the *inactive* axis when both
sides set it above 0.1, because a mismatch there is a TCL import defect worth catching. The user-facing diff
never compares it — the machine does not apply that value, so reporting it would show the user a difference
they cannot feel. One traversal emits both; the filters differ, and each filter's reason is recorded at its
definition.

### The shape index retains the bundled file behind each bucket

`s_index`'s value becomes a small struct carrying both the KB ids (as today, sorted, unchanged semantics) and
the resource paths of the bundled profiles that landed in that bucket. The walk already has the path in hand;
retaining it costs one `QString` per shipped profile and nothing at lookup.

*Alternative rejected:* deriving the file from the KB id at diff time. One entry can be authored against
several bundled profiles (`gentle-flat-long-preinfusion-family` has four), so an id does not name a file —
which is exactly why the spec requires selection to target a profile.

*Alternative rejected:* `Profile::titleToFilename(userTitle)`. That works only when the user kept the name,
which is the case the shape step exists to handle.

### Base selection reads the bucket, for both resolution origins

The user's profile is looked up by its own shape signature. From the resulting bucket:

- resolved by **title** to id X — candidates are the bucket's bundled profiles whose KB id is X. Empty means
  the user's profile is a different shape from the profile its own entry was written about, which is the
  spec's "no block" case and needs no special code.
- resolved by **shape** — candidates are the whole bucket.

Then the vote picks one, or abstains on a tie. Both origins converge on one code path after the candidate
list is built; the origin only chooses the list.

### Nearness is the count of differing fields, which is the block itself

Nearness is the number of dial-in fields on which the user's profile differs from a candidate — the same list
the block renders. Fewest wins; no single fewest abstains.

*Alternative rejected:* a summed normalised distance. That needs a weighting between bar, mL/s, °C and grams
that nothing in the domain supplies, and the weights would silently decide the outcome.

*Alternative rejected (and this was the design's first answer):* scoring each candidate on the fields where
the candidates differ from each other, point to whoever is closer to the user. It still compares magnitudes,
so it still needs the per-field notion of "closer", and it requires a second traversal that produces a number
nothing else uses. Counting reuses one function, and the winner's diff falls out of the same call that
selected it — the selection and the thing selected cannot disagree, because they are the same computation.

Abstaining on a tie means there is no threshold to tune — the guard is exact equality of two integers.

### C++ returns structured rows; QML formats and translates

`ProfileManager` exposes the diff as a list of maps carrying a field `kind`, a frame index, the frame's
display name, and raw old/new values. QML builds the label, applies the unit, and converts temperature
through the existing display helper.

*Alternative rejected:* preformatted strings from C++. Labels are user-visible text, and this project's
translations live in QML behind `TranslationManager`; formatting in C++ would put a second translation
mechanism next to the first.

### Two entry points on the dialog, no hand-set properties

`openFor(title)` gains a sibling `openForShot(title, profileJson)`. Both compute the diff into a plain
property inside the function — not a binding, so it evaluates once per open rather than on every dependency
change. The shot pages call the second one, satisfying the spec's per-surface source rule and respecting the
dialog's existing warning about incomplete entry.

## Risks / Trade-offs

- **Refactoring `frameDiffReport` could change what the TCL parity gate accepts.** → Its field set and text
  stay byte-identical; `tst_tclimport`'s existing assertion is the guard, and a test pins the rendered text
  for a known pair so a silent format change fails rather than passing quietly.
- **The vote can pick a base a human would not.** → It abstains on a tie unless every tied candidate carries
  the same KB id AND produces an equivalent difference list, in which case the answer is named for the entry
  and the numbers shown are true of all of them. The surface keeps disclosing
  that several bundled profiles share the shape, so the chosen base never reads as the only match. Both real
  collisions in the shipped set separate widely on dial-in values, so the tie path is expected to be rare
  rather than routine — but it is the path that prevents a confident wrong attribution.
- **A shipped profile whose dial-in values later change would silently change what a user's diff says.** →
  Acceptable: the diff is a live derivation, not a record, and it is recomputed on every open by design.
- **Dialog-open cost.** → One extra `Profile::fromJson` of a bundled file plus a linear field walk, on a
  discrete user action, after an index that is already built by the catalog scan. Deliberately NOT
  stopwatched: the read is bounded, runs once per dialog open, and is assigned to a plain property rather
  than bound, so no figure would change the decision — the call site says so rather than quoting a number
  it does not have. The one measured cost on this path is the cold shape index, and that is the same 48 ms
  already documented in `profileshapeindex.cpp`, not a new one.
