# Change: Tap-only taste intake before the first AI advisor turn

## Why

The AI advisor already receives dose, yield, ratio, duration, the pressure/flow
curves, and the anomaly flags for free from every shot. The one thing it most
needs and **cannot** derive from the curve is how the shot *tasted* — and today
it only gets that if the user types it. On the common path (tap **AI Advice**,
don't type), the advisor is blind on the single input that most changes its
recommendation, and — since the `#1501` work that gates "success" language on
taste feedback — it has to hedge or spend a turn asking "how did it taste?".

The existing conversational capture (`shot-rating-capture` Layer 1) is purely
**reactive**: the advisor asks, the user types a number, the reply is parsed. It
works, but it costs a round-trip and still requires typing.

This change front-loads the missing signal with a **fully text-free, tap-only
intake** that opens the first time the user opens the advisor for a shot. The
user taps a couple of chips describing taste; those ride along with an automatic
"What do you think?" and are **persisted to the shot as real metadata** — so the
same taps also improve dial-in history, SAW context, and future advisor calls,
not just this one conversation. Tapping nothing (Skip) drops the user straight
into today's normal text conversation, unchanged.

The taste axes come directly from the app's dial-in reference tables
(`resources/ai/espresso_dial_in_reference.md`): the sour↔bitter axis drives
grind/temperature/time; the thin↔strong axis drives ratio/dose.

## What Changes

### New tap-only intake dialog (fully text-free)

- **NEW** `qml/components/TastePicker.qml` — a single shared, tap-only picker with
  up to three rows, **every row optional**:
  - **Extraction**: `Sour` · `Balanced` · `Bitter` (three chips) — the universal
    espresso under/over-extraction language.
  - **Body**: `Thin` · `Medium` · `Heavy` (three chips) — body/mouthfeel
    intensity; these are the SCA terms and map 1:1 to Visualizer's `mouthfeel`.
  - **Overall**: the existing `RatingInput` component (25/50/75/100), reused —
    **not** a new rating widget.
  - The picker contains **no text field** — there is no free-text/note affordance.
    Users who want to type use the normal compose box after the first turn.
- **NEW** the intake is presented on opening the advisor (from the AI Advice
  button on `PostShotReviewPage` and `ShotDetailPage`) **unless there is already
  something to return to**: a saved conversation for the shot's context
  (`conversation.hasHistory`) or taste feedback already saved on the shot (any of
  `taste_balance` / `taste_body` / `enjoyment0to100`). So a new / cleared /
  backed-out-without-asking conversation re-shows the intake; asking the AI or
  recording any taste sends the user straight to the text conversation
  thereafter. When shown, all three rows are offered (the gate guarantees no
  saved feedback yet).
- **NEW** an "Ask" action composes a natural-language sentence from the tapped
  values (e.g. "It tasted sour and a bit thin, I'd rate it 50. What do you think
  and how should I adjust the next shot?") and sends it with the shot attached,
  exactly as the current AI Advice flow attaches shot data. Tapping "Ask" with
  nothing selected sends the plain "What do you think?".
- **NEW** a **Skip** affordance closes the picker and drops the user into the
  existing text conversation with the shot attached — identical to today's
  behavior.

### Structured taste storage (own columns, migration 33)

- **NEW** `shots.taste_balance` (TEXT, values `sour` / `balanced` / `bitter`,
  empty = unset) and `shots.taste_body` (TEXT, values `thin` / `medium` /
  `heavy`, empty = unset), added by **shots migration 33**. Empty-string sentinel
  for "unset", matching the `enjoyment0to100 == 0` convention (not NULL).
  Overall continues to use the existing `enjoyment0to100`. Local storage holds the
  **coarse chip enum** — the honest record of a single tap — not the derived
  0–15 CVA integers (those are computed only at Visualizer-upload time, below).
- **MODIFIED** `ShotProjection` — add `tasteBalance` / `tasteBody` fields and their
  serialization (read projection, insert binding, metadata column map).
- **MODIFIED** `ShotHistoryStorage::requestUpdateShotMetadata` accepts
  `tasteBalance` / `tasteBody` keys alongside the existing `enjoyment` /
  `espressoNotes` keys, so the picker persists on the same background write path
  Layer 1 already uses.
- **NEW** the same `TastePicker` appears on `PostShotReviewPage` alongside the
  rating slider, reading/writing the same columns — so there is exactly **one**
  taste UI and **one** storage location (no parallel interface).

### Visualizer CVA mapping (upload)

Visualizer's `shots` resource carries the SCA **Coffee Value Assessment (CVA)
Descriptive Assessment** attributes — `acidity`, `bitterness`, `sweetness`,
`mouthfeel`, `aftertaste`, `aroma`, `flavor`, `fragrance` — each an integer
intensity `0–15` (`openapi.yaml`, added March 2026). The tap enums map to a
coarse subset on upload:

| Local tap | Visualizer CVA field(s), 0–15 |
|-----------|-------------------------------|
| Extraction `sour` | `acidity` 12, `bitterness` 4 |
| Extraction `balanced` | `acidity` 8, `bitterness` 8 |
| Extraction `bitter` | `acidity` 4, `bitterness` 12 |
| Body `thin` / `medium` / `heavy` | `mouthfeel` 4 / 8 / 12 |
| Overall (RatingInput) | `espresso_enjoyment` 0–100 (already mapped) |

- **MODIFIED** `visualizeruploader.cpp` — on CREATE and PATCH, translate the tap
  enums to the CVA fields above.
- The five CVA attributes the taps do not speak to (`sweetness`, `aftertaste`,
  `aroma`, `flavor`, `fragrance`) SHALL NOT be set — a single tap must not
  fabricate a descriptive score for an attribute the user never rated.
- A mapped value SHALL NOT overwrite a CVA value the user set by hand on
  Visualizer (same class of bug as the removed `enjoyment_source` overwrite of
  the user's default-rating setting).

### Advisor prompt + gate wiring

- **MODIFIED** `ShotSummarizer::buildUserPrompt` — render `taste_balance` /
  `taste_body` into the per-shot prompt as prose when set (alongside the existing
  enjoyment/notes rendering), so the advisor sees structured taste in its first
  turn.
- **MODIFIED** the "has taste feedback" signal (`tastingFeedback` /
  `hasEnjoymentScore` in the dialing blocks) — treat a set `taste_balance` /
  `taste_body` as taste feedback too, not only a non-zero enjoyment score. This
  is what keeps the advisor from re-asking "how did it taste?" when the user
  tapped a taste chip but left Overall unrated.
- **No change to Layer 1.** Suppression of the reactive "how did it taste?" ask is
  emergent: when the picker supplies taste, the advisor's first prompt already
  contains it (via the composed sentence and the rendered fields), so the model
  has no reason to ask, so `maybePersistRatingFromReply` never fires. When the
  user skips, the flow is byte-for-byte today's behavior.

### Setting (default ON)

- **NEW** `SettingsAI` property `tasteIntakeOnAsk` (bool, default `true`). When
  ON, the AI Advice button opens the taste picker first; when OFF, it opens the
  conversation/compose directly as today. Registered for QML access as
  `Settings.ai.tasteIntakeOnAsk`.

### Docs & tests

- **MODIFIED** wiki Manual (AI advisor section) — document the taste picker, the
  first-open behavior, Skip, and the setting.
- **MODIFIED** `docs/CLAUDE_MD/AI_ADVISOR.md` — note the intake surface and its
  relationship to Layer 1.
- **NEW/MODIFIED** tests — migration 33 round-trip, `ShotProjection`
  serialization of the two fields, `requestUpdateShotMetadata` accepting the new
  keys, and the "has taste feedback" gate recognizing structured taste.

## NOT in scope

- Making the whole conversation tap-driven. The picker accelerates only the
  **first** turn; follow-ups remain the normal text compose box.
- Any free-text entry in the picker (explicitly excluded — the surface is tap-only).
- New taste axes beyond taste/body/overall (clarity, aftertaste, astringency).
- Milk-drink or other unsupported beverage types (advisor already gates these).
- A full CVA instrument. Decenza captures a coarse single-tap impression and maps
  it *up* to CVA on upload; it does not present the full 8-attribute 0–15 CVA
  descriptive form. Users wanting precise CVA scores enter them on Visualizer,
  and those hand-entered values are never overwritten by the mapping.
- Reverse-mapping CVA → tap enum on download. The upload mapping is one-directional;
  the local chip enum is the source of truth for the AI and dial-in.
- Auto-inferring taste from the curve. (This is the `remove-inferred-shot-ratings`
  failure mode — a synthesized signal the model is taught to ignore. Out of scope
  by design.)

## Impact

- Affected specs: `taste-intake` (NEW capability).
- Affected code:
  - `qml/components/TastePicker.qml` (new); `CMakeLists.txt` (register).
  - `qml/components/ConversationOverlay.qml` — first-open gate, compose-from-taps,
    Skip.
  - `qml/pages/PostShotReviewPage.qml`, `qml/pages/ShotDetailPage.qml` — present the
    picker on the AI Advice path; embed picker on the review page next to the rating.
  - `src/history/shothistorystorage.cpp` — migration 33, insert/select/metadata
    plumbing.
  - `src/history/shotprojection.{h,cpp}` — two new fields + serialization.
  - `src/ai/shotsummarizer.{h,cpp}` — render taste in the user prompt.
  - `src/ai/dialing_blocks.h` — "has taste feedback" recognizes structured taste.
  - `src/network/visualizeruploader.cpp` — tap-enum → CVA field mapping on
    CREATE + PATCH, with the no-overwrite / no-fabrication guardrails.
  - `src/settings/settings_ai.{h,cpp}`, `main.cpp` (uncreatable-type registration
    only if a new sub-object were added — it is not; `SettingsAI` already exists).
  - `tests/` — migration, projection, metadata-write, and gate tests.
