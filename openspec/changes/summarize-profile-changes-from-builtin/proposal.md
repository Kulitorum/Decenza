## Why

The knowledge dialog now opens for profiles that resolved by shape, and names the built-in they were
derived from ("Based on Damian's LR v2/v3"). But the prose it then shows describes the *original* — its
temperature, its yield, its pressure targets. A user reading it against their own re-tuned copy has no way
to tell which of those numbers still describe what they are about to brew. The one thing they need in order
to trust the rest of the dialog — *where does my copy depart from the profile this advice was written for?* —
is the one thing the app already knows and does not say.

## What Changes

- The knowledge dialog gains a **dial-in difference block above the KB prose**, listing each field where the
  user's profile departs from the built-in it matches: temperature, target weight and volume, per-frame
  pressure and flow setpoints, exit thresholds, limiter values, and frame renames.
- The block appears **only when the two profiles are the same shape**, regardless of whether the KB id was
  reached by title or by shape. Shape equality is what makes the comparison meaningful and what keeps the
  list short — frame count, pump mode, sensor, transition, exit type and durations are pinned equal by the
  match itself, so only dial-in values can differ.
- **A title-resolved profile that is the same shape as its built-in now gets the block too.** This is the
  larger population: a user who edits a bundled profile's temperature in place and keeps its name never
  reaches the shape step, yet their change is exactly as worth showing.
- When a shape matches **several** built-ins, the block targets the **nearest** one, chosen by counting the
  dial-in fields on which each candidate is closer. A tie shows no block — that is the case where naming a
  base would be a coin flip.
- The block is computed against the correct source per surface: the **live catalog profile** on the profile
  selector, and the **shot's own stored profile JSON** on the shot detail and post-shot review pages, so a
  shot never reports differences that only exist because the catalog file was edited after it was pulled.
- An identical shape with no differing dial-in values is stated as such ("no changes — a renamed copy of X")
  rather than rendering an empty block.

## Capabilities

### New Capabilities
- `profile-dial-in-diff`: When and how the app presents the dial-in differences between a user's profile and
  the bundled profile it matches — the shape gate, nearest-candidate selection and its tie rule, the field
  set that counts as dial-in, the per-surface choice of comparison source, and the empty-diff wording.

### Modified Capabilities
- `profile-knowledge-base`: the requirement that a shape-derived match be presented as a derivation currently
  ends with "a title-resolved profile gains no label". That reads as forbidding the new block on the larger
  population it is aimed at. The requirement is narrowed to the *derivation label on list and shot surfaces*,
  with the dial-in diff inside the knowledge dialog explicitly outside its scope.

## Impact

- **Code**: `src/profile/profile.{h,cpp}` — a shared frame-by-frame traversal, so the new user-facing diff and
  the existing developer-facing `Profile::frameDiffReport()` do not become two copies of one walk.
  `src/ai/profileshapeindex.{h,cpp}` — the shape index maps a signature to KB *ids*; the diff needs the
  shipped *file* behind a match, which the index does not currently retain.
  `src/controllers/profilemanager.{h,cpp}` — the QML-facing entry point.
  `qml/components/ProfileKnowledgeDialog.qml` — the block, plus a second entry point carrying a shot's stored
  profile JSON (hand-setting the dialog's properties is already documented as leaving `candidateNames` stale).
  `qml/pages/{ShotDetailPage,PostShotReviewPage}.qml` — use that entry point.
- **No persistence, no schema change, no new setting.** The diff is derived on open and stored nowhere.
- **Translations**: new keys for the block heading, the empty-diff line, and field labels.
- **Manual**: the wiki's profile section gains a short note on what the block is.
