## 1. Baseline & inventory

- [ ] 1.1 Capture the drift-check baseline for `resources/ai/profile_knowledge.md`: the exact list of `## ` headings and every `Also matches:` line (these MUST be byte-identical after this change).
- [ ] 1.2 List the real built-in profile titles from `resources/profiles/a_flow_*.json` and `d_flow_*.json` / `damian_s_*.json` (`title` field) to use as the authoritative name set. Confirm the stale names in the shipped KB (`A-Flow / medium`, `-dark`, `-very dark`, `-like D-Flow`) have no backing file.
- [ ] 1.3 Grep the shipped KB D-Flow/A-Flow sections for profile-implying phrasings ("base D-Flow", "D-Flow / Damian family", "D-Flow Q variant" *in prose* (not the header), "standard D-Flow variants", "A D-Flow variant", "A-Flow variants").

## 2. Fix the shipped KB (`resources/ai/profile_knowledge.md`)

- [ ] 2.1 Rewrite the `## D-Flow` section body prose to the editor model: D-Flow is an editor type; the profile is the name past the `/`; the pressurized-soak → flow-pour-with-pressure-limit → lever-decline structure and the per-profile pressure-limit clamp are editor-level behavior; "base D-Flow" → "the `D-Flow / default` profile" (the starter/example); "the whole D-Flow / Damian family" → "profiles built with the D-Flow editor". Do NOT change the heading or `Also matches:`.
- [ ] 2.2 Rewrite `## D-Flow Q variant` and `## Damian's LRv2 / LRv3` body prose the same way (each is a *profile built with the D-Flow editor*; cross-reference shared editor behavior rather than calling them "variants of D-Flow"). Headers/aliases unchanged.
- [ ] 2.3 Rewrite the `## A-Flow` section: A-Flow is an editor type (by Janek, `[SRC:aflow-repo]`); replace the stale profile list with the real built-ins `A-Flow / default-light/-medium/-dark/-very-dark/-like-dflow`; describe per-profile differences as roast targeting of distinct profiles, not "A-Flow variants". Header/aliases unchanged.
- [ ] 2.4 Run the §1.1 drift-check: heading list and every `Also matches:` line byte-identical; only prose/in-section names changed. Confirm `## ` heading count unchanged.

## 3. Consistency sweep of other docs

- [ ] 3.1 Edit AI/user-facing docs where wording implies D-Flow/A-Flow is a profile: `docs/CLAUDE_MD/AI_ADVISOR.md`, `docs/ESPRESSO_DIAL_IN_REFERENCE.md`, `docs/SIMPLE_PROFILE_EDITOR.md`, `docs/UNIVERSAL_GRIND_SETTING.md`. Fix only profile-implying phrasing; do not churn incidental mentions.
- [ ] 3.2 Verification-only: confirm `docs/PROFILE_KNOWLEDGE_BASE.md` (already cleaned) and `docs/CLAUDE_MD/RECIPE_PROFILES.md` (authoritative-correct) are consistent with the shipped KB after §2; reconcile any divergence toward the editor model.
- [ ] 3.3 Lower-traffic docs (`AUTO_FLOW_CALIBRATION.md`, `BLE_PROTOCOL.md`, `SAW_LEARNING.md`, `MCP_SERVER.md`, `MCP_TEST_PLAN.md`, `SHOT_REVIEW.md`): fix only where wording actively asserts D-Flow/A-Flow is a profile. Skip `docs/plans/*` and frozen test-plan artifacts.

## 4. Guard & verify

- [ ] 4.1 Add a regression guard (test or KB-lint) asserting `resources/ai/profile_knowledge.md` contains none of the stale A-Flow names and that every D-Flow/A-Flow profile name referenced has a backing `resources/profiles/*.json` title.
- [ ] 4.2 Build via Qt Creator MCP (0 errors, 0 warnings) and run the full test suite — must stay green (no functional change expected) plus the new guard.
- [ ] 4.3 Spot-read the rendered `dialing_get_context` KB section and the in-app advisor prompt: D-Flow/A-Flow read as editor types, profiles are the slash-names, A-Flow names are the real built-ins, no "variant/family/base D-Flow" profile framing; headers/aliases visibly unchanged.
