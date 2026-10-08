# Tasks

## 1. Teach the harness Haiku 5.5

- [x] 1.1 Verify Haiku 5.5's price on Anthropic's pricing page, add `"claude-haiku-5-5"` to `PRICES` in `tools/ai_model_eval/replay.py` with the check date in the comment, and confirm `python3 replay.py capture-help` still runs
- [x] 1.2 Make `anthropic_thinking()` in `replay.py` and the thinking shape in `probe_request_shape.py` send `disabled` for `claude-haiku-5-5`, mirroring `AIRequestShape::disableAnthropicThinking`; add it to `ANTHROPIC_MODELS`. Verify with a diff of the body each script prints or builds for Haiku against the app's rule *(Both scripts already send `disabled` to Haiku 5.5. The probe's list mirrors the shipped catalogs, so Haiku is probed with `--anthropic`; it joins `ANTHROPIC_MODELS` in 4.1 if admitted.)*
- [x] 1.3 If OpenRouter lists `anthropic/claude-haiku-5.5`, add it to the probe's `OPENROUTER_MODELS`; otherwise record "not served on OpenRouter as of <date>" in 6.x's hold note *(Served as of 2026-10-07 at $0.10/$0.50; probed with `--openrouter` and added to the list in 4.4 if admitted.)*

## 2. Probe (needs the maintainer's go-ahead to spend, a few cents)

- [x] 2.1 Ask the maintainer to approve the evaluation budget (about $0.80 with reference arms, about $0.10 without), and record the approval *(Approved by the maintainer on 2026-10-07.)*
- [x] 2.2 Run `python3 probe_request_shape.py` and confirm Haiku 5.5 accepts `thinking: disabled`, returns a `text` block, and reports zero thinking tokens on the advisor, translator and web-tool bodies. If it fails, stop here: go to group 5 (fail path) with the probe output as the reason

## 3. Replay and judge

- [x] 3.1 Dry-run one tablet shot (`ai_advisor_invoke dryRun` on the de1 MCP) and diff its system prompt against `captured_v3`. If they are identical, skip the reference arms. If not, recapture all six scenarios into a new `captured_*` directory
- [x] 3.2 Write down the pass bar from design.md decision 3 in the run notes before running anything
- [x] 3.3 Run `replay.py blind` with `claude-haiku-5-5` (thinking off) plus, if 3.1 requires them, `claude-sonnet-5-5` and `gpt-6-luna`, two runs per scenario; record judgments per sample before `replay.py reveal`
- [x] 3.4 Run `replay.py emission` on the same set and record usable-block counts on tasted scenarios against Luna's *(Audited from the blind samples with the harness's own parser instead of a second paid emission run: Sonnet 8/8, Luna 6/8, Haiku 5/8.)*
- [x] 3.5 Only if Haiku fails on grind direction: run the diagnostic arm (adaptive thinking, `effort: low`) on the failing scenarios, record the result, and bring it to the maintainer as a decision. Do not adopt it without their agreement
- [x] 3.7 Run the full replay with Haiku 5.5 at adaptive thinking + `effort: low`, direct and through OpenRouter (`@low`), plus gpt-6-luna as the blind's third arm, two runs over all six scenarios. Judge against the same pass bar before reveal. Then send one real two-turn call (first reply resent as text only) and confirm the follow-up returns text with no error
- [x] 3.6 Add a dated findings entry to `tools/ai_model_eval/README.md`: arms, scenarios, per-scenario verdicts, block counts, measured tokens and cost per shot, outcome, and the sample-size caveat. Verify that every number in it traces to a file under `runs/`

## 4. Pass path: admit Haiku 5.5 (skip if 3.x failed)

- [x] 4.1 Add `{ "claude-haiku-5-5", "Haiku 5.5" }` as the second entry in `AnthropicProvider::availableModels()` and update its comment and `modelHint()` to describe both models. Add `AIRequestShape::setAnthropicAdvisorThinking()` (adaptive + `output_config.effort: low` for Haiku 5.5, otherwise what `disableAnthropicThinking` sends), use it in `analyze()` and `analyzeConversation()` only, and mirror it in `replay.py` and `probe_request_shape.py` (the probe checks the advisor body at the 4,096 cap for a text block)
- [x] 4.2 Add the Haiku 5.5 rows to `tst_aiproviders`' verified-shape table (advisor body: adaptive + low; Test Connection and translator: disabled) and catalog expectations (Sonnet 5.5 still first). Break each shape (advisor sends `disabled`, Test Connection sends adaptive) and confirm the test goes red, then restore it *(Both breaks went red: advisor sending `disabled` failed the verified table and the new test; Test Connection sending adaptive failed only the new test.)*
- [x] 4.3 Add a `kCostLines` entry (`ai.cost.anthropic.haiku55`) from the measured replay tokens, including the 5-minute cache write; confirm it appears under the Anthropic picker in Settings → AI and on the ShotServer settings page *(Both read `costHintFor()`; confirmed in the app in 4.7. OpenRouter Haiku has no line: its price was not measured separately, and the table never borrows one.)*
- [x] 4.4 If OpenRouter serves it and the probe passed (1.3/2.2): add `anthropic/claude-haiku-5.5` to the OpenRouter catalog with the reasoning setting 3.7 validated, plus `setOpenRouterReasoning`, the `tst_aiproviders` table and `modelHint()`; otherwise record the hold in tasks
- [x] 4.5 Build and run the full suite through the Qt Creator MCP (`build`, then `run_tests` scope `all`, after asking first); confirm green *(119/119 on 2026-10-07; the text-invariants scripts pass locally. Adding the OpenRouter Haiku cost line, measured at $0.0032 with reasoning `low`, fixed `everyCataloguedModelHasACostHint`.)*
- [x] 4.6 Update `docs/CLAUDE_MD/AI_ADVISOR.md`: providers table, Anthropic rationale (replace the "announced for the coming weeks" line), and cost table row. Verify each figure matches the findings entry
- [x] 4.7 Ask the maintainer to restart Decenza, select Anthropic → Haiku 5.5, run one advisor analysis on a tasted shot, and confirm the reply and the `nextShot` block arrive *(Done over the dev build's MCP, 2026-10-07: the picker listed Sonnet 5.5 and Haiku 5.5, and the bitter 47 s shot returned "one step coarser, 6.75" with a parsed structuredNext in 10.7 s. Follow-up turns were covered by the two-turn API call in 3.7. Settings restored to OpenAI / Luna.)*

## 5. Fail path: record and stop (skip if group 4 ran)

- [x] 5.1 *(N/A: pass path taken.)* Update `docs/CLAUDE_MD/AI_ADVISOR.md`'s Anthropic rationale with Haiku 5.5's measured failure in one or two sentences, replacing the "announced for the coming weeks" line; verify it cites the findings entry date
- [x] 5.2 *(N/A: pass path taken.)* Revise this change's spec delta per design.md decision 6: Anthropic scenarios name Sonnet 5.5 alone, the picker example moves to OpenAI, and the admission requirement stays. Verify with `openspec validate evaluate-haiku-5-5-advisor --strict`

## 6. Spec and manual

- [x] 6.1 Edit the Purpose line of `openspec/specs/advisor-model-selection/spec.md` to drop the retired model examples (Sonnet 4.6, Sonnet 5, GPT-5.4); verify `openspec validate --specs --strict` passes *(This spec passes strict. Repo-wide, all 169 specs pass non-strict, while 137 other specs fail strict on long descriptions, which predates this change.)*
- [x] 6.2 Pass path only: update the wiki Manual §13 model table's Anthropic row ("Sonnet 5.5, Haiku 5.5 (…cost note…)"). Keep it one table cell. Ask the maintainer whether to push now or hold for the release *(Pushed 2026-10-07 at the maintainer's request: wiki b2fc20b, Anthropic and OpenRouter rows.)*

## 7. Review

- [x] 7.1 Open the PR and have it reviewed (maintainer, 2026-10-07: by GitHub agents, not `/pr-review-toolkit:review-pr`); address findings and confirm the `text-invariants` run for the final commit is green before merge *(Codex's one finding, the model-hint key bump, was fixed in 51c9f68. Codex's second finding, that bag extraction via `analyze()` got Haiku's thinking, was fixed after the archive: `AIProvider::extract()` keeps thinking off, and `tst_aimanager` asserts the routing (break-checked). Checks are read on the final commit before merge.)*

## Workflow follow-up

- Archive with `openspec archive evaluate-haiku-5-5-advisor --yes` as the PR's final commit, then push and read that commit's checks before merging.
