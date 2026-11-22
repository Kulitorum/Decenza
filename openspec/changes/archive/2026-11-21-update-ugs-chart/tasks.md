# Tasks

## 1. Chart values and guide link

- [x] 1.1 Set Blooming Allongé to −0.75 and Extractamundo Dos to 2.5 in `resources/ai/profile_knowledge.json` with the chart's notes; verify `python3 tools/validate_kb.py` passes
- [x] 1.2 Match `docs/PROFILE_KNOWLEDGE_BASE.md`'s canonical table and profile sections to the chart's 15 rows
- [x] 1.3 Point `kUgsGuideUrl` at `https://videoblurb.com/UGS/`; verify the address returns 200 and the old one 404
- [x] 1.4 Build and run the full suite through Qt Creator, and confirm it passes (203/203; `calibrationBlock_settingsComeFromTheAnchorsOwnBasket` now checks exact values, since its ">= 7" cutoff failed on Blooming Allongé's correct 6.5)
- [ ] 1.5 Open the UGS guide button on the Equipment page and confirm the chart page loads — HELD: not checked in the app before #2229 merged; checked as part of add-genius-grind-widget task 2.6

## Workflow follow-up

- Before merge: clean `audit-code` pass on the final commit, Codex threads resolved, then `openspec archive update-ugs-chart --yes` as the last commit.
