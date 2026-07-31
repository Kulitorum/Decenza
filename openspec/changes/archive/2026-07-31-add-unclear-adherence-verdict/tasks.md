# Tasks

## 1. Classification

- [x] 1.1 `RecommendationKind` tri-state (`None` / `Scoreable` / `Unscoreable`) in `dialing_blocks.cpp`.
- [x] 1.2 `classifyGrinderRecommendation()` — JSON type check, trim, prose check.
- [x] 1.3 `classifyPositiveNumberField()` for `rpm` and `doseG` — rejects wrong JSON type and non-positive values, closing the `rpm` fail-open path.
- [x] 1.4 `classifyStringField()` for `profileTitle`.
- [x] 1.5 Fold every axis through one shared lambda in `computeAdherence()` so a fifth field cannot be added without a classify step.

## 2. Comparison

- [x] 2.1 `GrinderAliases::looksLikeSetting()` — every accepted form must begin with a dial number; single-token strings are no longer trusted unvalidated.
- [x] 2.2 `GrinderAliases::compoundKey()` and `leadingDialNumber()`, sharing the notation regexes with `parseGrinderSetting()`.
- [x] 2.3 `grinderMatches()` compares through those helpers so the gate and comparator agree.

## 3. Prompt

- [x] 3.1 Teach `"unclear"` in `ShotSummarizer`'s `recentAdvice` section.

## 4. Tests

- [x] 4.1 Prose (multi-word and single-word) → `"unclear"`, with the verdict independent of what the user actually did.
- [x] 4.2 Wrong JSON type on `grinderSetting` and on `rpm` → `"unclear"`.
- [x] 4.3 `rpm: 0` → `"unclear"`, not a free match.
- [x] 4.4 Compound spacing and RPM-annotated settings → `"followed"`.
- [x] 4.5 Every scoring test asserts BOTH verdicts, so none can pass by never scoring.

## 5. Docs

- [x] 5.1 `docs/CLAUDE_MD/AI_ADVISOR.md` — the verdict, the uniform classification, and why `"followed"` was the worse failure.
- [x] 5.2 Wiki manual — not required; `adherence` is internal and has no user-visible surface.

## 6. Archive

- [x] 6.1 Archive this change and sync the delta into `openspec/specs/advisor-user-prompt/spec.md` as the final commit on the PR.
