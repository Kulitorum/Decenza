# Design — Tap-only taste intake

## Problem framing

The advisor's advice keys on taste, but taste is the one shot attribute the curve
can't reveal. The dial-in reference tables (`espresso_dial_in_reference.md`) map
taste directly onto machine changes:

- **Sour ↔ Bitter** → extraction axis → grind / temperature / time.
- **Thin ↔ Strong** → concentration axis → ratio / dose / preinfusion.

So the highest-value-per-tap signals are exactly those two axes plus an overall
score. We harvest what the AI can't measure; we do **not** ask for anything the
curve already tells it (duration, ratio, channeling, etc.).

## Key decisions

### 1. Tap-only, first-open gate — no text in the picker

The picker is presented the first time the advisor opens for a shot and is
**entirely tap-driven**. There is deliberately no note field. Rationale:

- The value proposition is "get better advice without typing." A text box
  reintroduces the keyboard — and the keyboard is exactly what has caused this
  overlay's recurring render/scroll bugs (#920, #608, #470, #767). Keeping the
  picker text-free keeps it clear of that entire bug class.
- Users who want to type already have the normal compose box; Skip drops them
  there in one tap.

### 2. Adaptive display — show only unfilled axes

Each row appears only when its value is unset (`enjoyment0to100 == 0`,
`taste_balance` empty, `taste_body` empty). This means the user is only ever asked
for signals the AI is actually missing, and a fully-rated shot (common when
revisiting an old shot on the detail page) collapses the dialog to a single
"Ask → What do you think?" button — which is precisely the one-tap question from
issue #914. The same surface is both the guided intake and the plain one-tap ask.

### 3. Structured columns, not free-text — forced by the adaptive UX

Taste could have been encoded into the existing `espressoNotes` free-text field
(cheaper, no migration). It is stored as structured columns instead because the
adaptive display **requires** it: to show a row "only if unset" and to
pre-select a prior value, the code must reliably answer "is taste set?" and read
the value back — which is trivial against a column with an empty sentinel and
unreliable against prose. Free-text would also be a **second, informal**
representation of a concept that already has a structured widget (the rating
slider), i.e. the parallel-interface trap. The double-win (dial-in history, SAW
context, "last time you said sour you went finer") also needs queryable columns.

Bar applied (drawn from why `remove-inferred-shot-ratings` failed): *add a shots
column only if the signal can't be gotten another way AND changes the AI's
advice.*

- **Taste (sour/bitter): passes cleanly** — uninferable from the curve, decision-
  changing, only otherwise captured reactively-if-typed.
- **Body: passes on the AI axis weakly** (partly inferable from ratio/flow), but
  earns its place on a second ground: **body/mouthfeel is a real SCA CVA attribute
  and a real Visualizer column**, so it maps 1:1 on upload (see decision 7). That
  clean round-trip is why the middle row is **Body (Thin/Medium/Heavy)** and not
  "Strength (Weak/Strong)" — strength has no CVA field and would not round-trip.

### 4. Overall reuses `RatingInput` + `enjoyment0to100`

No new rating widget. `RatingInput.qml` is already shared (review page +
Visualizer settings). This is the survivor of `shot-rating-capture`: Layer 3
(inferred auto-rating) was removed for overwriting the user's Visualizer default
and giving the model nothing; the `QuickRatingRow` (Layer 2) was a *parallel*
rating widget that drifted and was consolidated back into the slider (#1243/#1245).
Both outcomes argue for reusing the one canonical rating control — which we do.

### 5. Layer-1 handoff is emergent, not coded

`maybePersistRatingFromReply` fires only when the prior assistant turn asked about
taste (marker list in `aimanager.cpp`) and the reply parses as a rating. The
advisor asks only because the system prompt tells it to when taste feedback is
absent. Therefore:

```
 fill  → composed "…tasted sour…" sentence + rendered fields in the first prompt
         → advisor has taste → never asks → Layer 1 stays dormant
 skip  → no taste in prompt → advisor asks (today) → user types → Layer 1 catches
```

No suppression flag is needed. The only wiring required is extending the
"has taste feedback" signal so a set `taste_balance`/`taste_body` counts as
feedback even when Overall is left unrated — otherwise the advisor could still ask
despite a tapped taste chip.

### 6. Setting defaults ON, in `SettingsAI`

`Settings.ai.tasteIntakeOnAsk`, default `true`. OFF restores today's
open-straight-to-compose behavior, so no existing workflow regresses. It lives in
the `SettingsAI` domain sub-object per the settings-split rules; `SettingsAI`
already exists, so no new `qmlRegisterUncreatableType` is required.

### 7. Visualizer CVA mapping — coarse but honest

Visualizer adopted the SCA **CVA Descriptive Assessment** (March 2026): 8
attributes, each `0–15` intensity, writable via the same `PATCH /shots/{id}` /
`POST /shots/upload` body `visualizeruploader.cpp` already sends, and editable in
Visualizer's `_tasting_assessment` web form. The tap enums map up:

```
  sour     → acidity 12, bitterness 4      thin   → mouthfeel 4
  balanced → acidity 8,  bitterness 8      medium → mouthfeel 8
  bitter   → acidity 4,  bitterness 12     heavy  → mouthfeel 12
  overall  → espresso_enjoyment (0–100, already mapped)
```

Two honesty guardrails keep this clear of the Layer-3 synthesized-signal failure:

- **No fabrication.** A tap sets only the attributes it speaks to. `sweetness`,
  `aftertaste`, `aroma`, `flavor`, `fragrance` are never set from a tap — a coarse
  impression must not invent a descriptive score.
- **Never clear.** A mapped CVA field is written ONLY when the local tap is set,
  and never sent as null. So a shot the user never taps in Decenza never has its
  CVA fields written or cleared — anything scored by hand in Visualizer's
  assessment form survives. (The mapping does not read remote state, so for a
  shot the user *does* tap, the tap is authoritative for the mapped fields.) This
  is the same class of caution as the removed `enjoyment_source`, which silently
  overwrote the user's Visualizer default-rating setting.

The mapping is one-directional (Decenza → Visualizer). The local chip enum is the
source of truth; we do not reverse-map CVA scores back into a chip.

**Upload happens at capture time via the existing auto-sync gates — no new
trigger.** A taste tap persists through the same `saveEditedShot` /
`requestUpdateShotMetadata` path the review page already uses, and that path
already PATCHes Visualizer when `Settings.visualizer.visualizerAutoUpdate` is on
(`PostShotReviewPage.qml:832`) and CREATE-uploads when
`Settings.visualizer.visualizerAutoUpload` is on. So, with the user's Visualizer
auto-sync enabled, tapping a taste chip pushes the mapped CVA fields to Visualizer
immediately — the only wiring required is adding `taste_balance` / `taste_body` to
`touchesVisualizerFields()` so a taste-only edit counts as a syncable change. With
auto-sync off, the taps persist locally and upload on the next manual push, exactly
like every other shot field.

## Risks / open questions

- **Body's weak justification** (see decision 3). Ship with it; revisit if it
  proves noisy in advice quality.
- **Migration cost of being wrong.** `enjoyment_source` was added (migration 14)
  and dropped (migration 16) within 11 days, requiring a data reset + deferred
  Visualizer back-sync. Two new columns carry the same unwind cost if taste turns
  out unused. The adaptive-UX requirement and the double-win are what justify
  accepting that risk now.
- **Compose wording.** The tap→sentence mapping should read naturally and not
  over-claim (e.g. don't assert "sour" as fact to the model beyond "I tasted…").
  Kept qualitative and first-person.
