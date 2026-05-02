# Tasks

## 1. Parser

- [ ] 1.1 Add `BeanCorrection` struct to `aimanager.h` (sparse: `std::optional<QString> roastLevel/beanBrand/roastDate`).
- [ ] 1.2 Implement `static std::optional<BeanCorrection> AIManager::parseBeanCorrectionsFromReply(const QString &reply)` with conservative regex matching.
- [ ] 1.3 Implement `roastLevel` extraction with canonical normalization ("medium dark"/"medium-dark"/"mediumdark" → `"Medium-Dark"`).
- [ ] 1.4 Reject compound phrases ("dark chocolate", "light fruit", "medium body") via require-context-word patterns ("the coffee/bean/roast is X", "actually X").
- [ ] 1.5 Implement `beanBrand` extraction from "actually (it's|this is) from X", "the (bean|coffee|brand|roaster) is X".
- [ ] 1.6 Implement `roastDate` extraction (ISO `YYYY-MM-DD`, "Month D, YYYY", "Month D" defaulting to current year).

## 2. Persistence

- [ ] 2.1 Implement `bool AIManager::maybePersistBeanCorrectionFromReply(const QString &userReply, const QString &priorAssistantMessage, qint64 shotId)`.
- [ ] 2.2 Gate on either (a) prior assistant message contains bean-asking markers ("roast level", "what kind of bean", "describe the bean", "is it a light/medium/dark roast") or (b) reply starts with explicit corrective phrasing.
- [ ] 2.3 Build the `QVariantMap` and call `ShotHistoryStorage::requestUpdateShotMetadata(shotId, fields)` (background-thread path already in place).
- [ ] 2.4 Return `true` when a write was persisted, `false` otherwise.

## 3. Wire into followUp

- [ ] 3.1 In `AIConversation::followUp`, after the existing rating-write call, also call `maybePersistBeanCorrectionFromReply`. Both can fire on the same reply.
- [ ] 3.2 Use the same `m_pendingShotId`/`shotIdForTurn(turnIndex)` resolution as the rating-write hook.

## 4. System prompt

- [ ] 4.1 Add "Conversational metadata corrections" subsection to `shotAnalysisSystemPrompt` after the existing "Conversational rating capture" / structured-fields teaching.
- [ ] 4.2 Teach: (a) when a bean correction is written, acknowledge in the next reply ("Got it — I've updated the shot's roast to Dark"); (b) on subsequent turns, trust `currentBean.*` over the user's last-turn phrasing; (c) bean fields persist across the conversation.
- [ ] 4.3 Verify the addition does not break the cache-prefix byte stability (the new block sits in a stable position relative to other prompt sections).

## 5. Tests

- [ ] 5.1 `parseBeanCorrectionsFromReply_extractsRoastLevel` — "actually it's really dark" → `roastLevel: "Dark"`.
- [ ] 5.2 `parseBeanCorrectionsFromReply_canonicalizesRoastValues` — "medium-dark" / "medium dark" / "MediumDark" all → `"Medium-Dark"`.
- [ ] 5.3 `parseBeanCorrectionsFromReply_rejectsCompoundPhrases` — "dark chocolate notes", "light citrus", "medium body" → `std::nullopt`.
- [ ] 5.4 `parseBeanCorrectionsFromReply_extractsRoastDate` — ISO and natural-language forms.
- [ ] 5.5 `parseBeanCorrectionsFromReply_extractsBeanBrand` — "actually it's from Sey" → `beanBrand: "Sey"`.
- [ ] 5.6 `parseBeanCorrectionsFromReply_emptyOnUnrelated` — "really good shot, balanced" → `std::nullopt`.
- [ ] 5.7 `parseBeanCorrectionsFromReply_handlesMultipleFields` — "actually it's from Sey, dark roast" → both fields set.
- [ ] 5.8 `maybePersistBeanCorrectionFromReply_persistsWhenShotIdSet` — verifies `requestUpdateShotMetadata` is invoked with the parsed fields.
- [ ] 5.9 `maybePersistBeanCorrectionFromReply_noOpWhenShotIdAbsent` — no shotId → no write.
- [ ] 5.10 `maybePersistBeanCorrectionFromReply_gatesOnBeanContext` — neither corrective phrasing nor prior bean question → no write.
- [ ] 5.11 Integration test: simulated conversation corrects roast level → next-turn envelope's `currentBean.roastLevel` reflects the correction.
- [ ] 5.12 All test runs end with `qWarning` calls wrapped in `QTest::ignoreMessage` per `docs/CLAUDE_MD/TESTING.md`.

## 6. Profile family field (gap 2)

- [ ] 6.1 Add `Family: <name>` line to every profile entry in `resources/ai/profile_knowledge.md`. Use the fixed set: `lever-decline`, `pressure-ramp-flow`, `flow-adaptive`, `blooming`, `flat-pressure`, `turbo`, `filter`, `allonge`, `manual`, `volume-based`, `gentle-long-preinfusion`, `tea`, `maintenance`. Confirm Londinium-family profiles (D-Flow, LRv2, LRv3, Londinium, Default, Cremina, Idan's Strega Plus, 80's Espresso, Best Overall, Traditional/Spring Lever, Advanced Spring Lever) all carry `lever-decline`.
- [ ] 6.2 Update `ShotSummarizer::buildProfileCatalog` to extract `Family:` and append `[family: <name>]` to each catalog one-liner.
- [ ] 6.3 Add a "Profile families" subsection to `shotAnalysisSystemPrompt` instructing the model not to recommend within-family switches as meaningful profile changes unless the alternative encodes a constraint that cannot be replicated by parameter tweaks.
- [ ] 6.4 Add a `tst_shotsummarizer.cpp` test verifying the catalog string contains `[family: <name>]` tags after `buildProfileCatalog`, and that the Londinium-family entries (D-Flow, Londinium) carry `lever-decline`.

## 7. Other-profile parameter discipline (gap 3)

- [ ] 7.1 Add an "Other-profile parameter discipline" subsection to `shotAnalysisSystemPrompt` teaching the model that it has full recipe data only for the current shot's profile and must NOT invent numeric setpoints of other profiles. Recommendations on other profiles MUST be qualitative ("lower temperature", "higher peak pressure"), not numeric.

## 8. Validate and PR

- [ ] 8.1 `openspec validate add-shot-metadata-capture --strict --no-interactive`.
- [ ] 8.2 Build via Qt Creator MCP, run `tst_aimanager` and `tst_shotsummarizer`, confirm 0 failures and 0 warnings.
- [ ] 8.3 Open PR; run `/review`; address any 75+-confidence issues.
