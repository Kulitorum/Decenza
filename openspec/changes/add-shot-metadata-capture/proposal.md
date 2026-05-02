# Change: Capture bean-metadata corrections + close two advisor recommendation gaps

## Why

A real advisor session produced clearly bad advice and surfaced three independent gaps:

1. **The shot's recorded `roastLevel` was wrong.** It was saved as "Medium-Dark" but the user described the coffee as "really dark." The advisor reasoned from the wrong value and recommended Blooming Espresso (catalog: "not ideal for dark"). Even after the user corrected the roast level conversationally, the correction lived only in the conversation — the shot's metadata stayed wrong, so future advisor calls on the same shot would inherit the same misread.

2. **The catalog has no family graph.** The advisor recommended switching from D-Flow to "LRv2/Londinium" for a dark roast — but D-Flow, LRv2, LRv3, and Londinium are all lever-decline profiles in the Londinium family. Switching between them is a parameter tweak in disguise, not a meaningful profile change. The catalog one-liners describe each profile individually but don't surface the family relationship strongly enough for the model to avoid the recommendation.

3. **The advisor invented other-profile parameters.** The system prompt only ships the **current** shot's profile recipe in `result.profile.recipe`. For other profiles the advisor has only the catalog one-liner. But the advisor confidently quoted "Londinium runs 89-90°C vs D-Flow's 94°C" — pure hallucination. There is no rule in the system prompt forbidding the model from quoting numeric setpoints of profiles it doesn't have data on.

`shot-rating-capture` already shipped the conversational write-back path for ratings (Layer 1: when the advisor asks "how did this taste?" and the user replies with a number, the score writes back to `enjoyment0to100` + `espressoNotes`). This change extends that mechanism to bean-identity fields AND closes the two recommendation gaps with a small catalog change plus two system-prompt rules.

## What Changes

### Conversational metadata capture (gap 1)

- **NEW** `parseBeanCorrectionsFromReply` static parser on `AIManager` — scans a user reply for explicit, conservative bean-field corrections and returns a sparse `BeanCorrection` struct. Detects `roastLevel` (Light / Medium-Light / Medium / Medium-Dark / Dark), `beanBrand`, `roastDate` (ISO and a small set of natural-language date forms). Rejects compound phrases ("dark chocolate note", "light fruit") to keep false positives near zero.
- **NEW** `maybePersistBeanCorrectionFromReply(reply, priorAssistantMessage, shotId)` on `AIManager` — gates on either (a) the prior assistant turn explicitly asking about beans, or (b) an explicit corrective phrasing in the user reply ("actually it's…", "the coffee is…", "the bean is…"). Persists via the existing `ShotHistoryStorage::requestUpdateShotMetadata` background path. Wired into `AIConversation::followUp` next to the existing rating-write hook.
- **MODIFIED** `shotAnalysisSystemPrompt` — adds a "Conversational metadata corrections" subsection teaching the model to (1) acknowledge in the next reply when a bean correction has been written ("Got it — I've updated the shot's roast to Dark") and (2) trust the next-turn envelope's `currentBean.*` over what the user typed last turn.

### Profile family field (gap 2)

- **MODIFIED** `resources/ai/profile_knowledge.md` — adds a `Family:` line to each profile entry classifying its underlying mechanic. Family values: `lever-decline`, `pressure-ramp-flow`, `flow-adaptive`, `blooming`, `flat-pressure`, `turbo`, `filter`, `allonge`, `manual`, `volume-based`, `gentle-long-preinfusion`, `tea`, `maintenance`. The Londinium family (D-Flow, LRv2, LRv3, Londinium, Default, Cremina, Idan's Strega Plus, 80's Espresso, Best Overall, Traditional/Spring Lever, Advanced Spring Lever) all carry `Family: lever-decline`.
- **MODIFIED** `ShotSummarizer::buildProfileCatalog` — extracts `Family:` and appends `[family: <name>]` to each catalog one-liner so the rendered catalog surfaces family alongside category and roast guidance.
- **MODIFIED** `shotAnalysisSystemPrompt` — adds a "Profile families" subsection teaching the model: "Profiles share `Family:` when they implement the same underlying mechanic. A within-family switch (e.g., D-Flow → LRv2, both `lever-decline`) is a parameter tweak in disguise — do NOT recommend it as a profile change unless the alternative encodes a constraint the user can't replicate by adjusting parameters on the current profile (e.g., a different operating-temperature regime). When recommending a profile switch, name the family of both the current and proposed profile and explain what the family change buys the user."

### Anti-hallucination rule (gap 3)

- **MODIFIED** `shotAnalysisSystemPrompt` — adds a "Other-profile parameter discipline" subsection: "You only have the CURRENT shot's profile recipe (frame setpoints, temperatures, pressures, durations) in `result.profile.recipe`. For every other profile in the catalog, you have ONLY the one-line description (category, family, roast suitability). Do NOT quote specific numeric setpoints of other profiles. If you need to compare numerically, say so explicitly and ask the user to pull a reference shot on that profile, or recommend in qualitative terms ('lower temperature', 'higher peak pressure') without inventing specific numbers."

### Tests

- **MODIFIED** `tests/tst_aimanager.cpp` — parser unit tests covering all detected fields, compound-phrase rejection, multi-field replies, and the persistence helper's gating behavior.
- **MODIFIED** `tests/tst_shotsummarizer.cpp` (or the shotsummarizer test file if a separate one exists) — verifies the catalog string contains `[family: <name>]` tags after `buildProfileCatalog`, and that at least the Londinium-family entries (D-Flow, Londinium) carry the same family tag.

### NOT in scope

- Editing per-shot grinder/dose/yield/duration values from conversation. Those are physical recordings, not annotations — the user should pull a new shot rather than retroactively edit a curve.
- Propagating bean corrections to other shots from the same lot. Today there's no `bean` entity in the data model; each shot stores its bean fields directly.
- Detecting processing/origin/variety from conversation. No schema columns for those.
- Adding an MCP tool for `profiles_get_recipe` so the advisor could fetch full frame data for a comparison profile. This is a future option but is not the simplest fix for this round — the family rule + anti-hallucination discipline cover the immediate problem.

## Impact

- Affected specs: `shot-metadata-capture` (NEW capability).
- Affected code:
  - `src/ai/aimanager.{h,cpp}` — new parser + persistence helper.
  - `src/ai/aiconversation.cpp` — invocation site in `followUp`.
  - `src/ai/shotsummarizer.{h,cpp}` — `buildProfileCatalog` family extraction; system-prompt teaching for metadata corrections, profile families, and other-profile parameter discipline.
  - `resources/ai/profile_knowledge.md` — `Family:` line on each profile entry.
  - `tests/tst_aimanager.cpp`, `tests/tst_shotsummarizer.cpp` — parser tests, catalog rendering test.
- Behavior change: when a user types an explicit bean correction during an advisor session, the shot's bean-identity fields update silently. The next-turn envelope reflects the correction; the system prompt requires the model to acknowledge the write so the user knows it stuck. Profile catalog surface gains a family tag and the model is taught to avoid within-family recommendations and to refuse to invent setpoints for profiles it doesn't have recipes for.
